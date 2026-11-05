// The engine's quest listener (REDkit's IQuestSystemListener): the game still calls it for every quest block that
// activates or deactivates, and nothing listens in a normal game. We add one.
//
// The engine calls it in the middle of its quest tick, so it only copies what happened into a ring. We take over
// the quest system's own tick in its vtable: after the real tick, outside any of the engine's loops, the ring is
// sent on to script, filtered to the blocks mods asked for.
#include "listener.h"
#include <windows.h>
#include <intrin.h>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <vector>
#include "engine.h"
#include "log.h"

namespace
{
	// Offsets, from the game's own class registration and code
	constexpr size_t kSysListeners = 0xE0, kSysThreads = 0xEC;   // arrays: a pointer, then a u32 count at +8
	constexpr size_t kThreadScope = 0x58, kThreadInstance = 0x60, kThreadListeners = 0x84;
	constexpr size_t kBlockName = 0x68, kBlockGuid = 0x80, kBlockConnections = 0x90, kBlockOutputVar = 0xA0;

	using AttachListener_t = void(__fastcall*)(void* thread, void* listener);
	using PushBackUnique_t = bool(__fastcall*)(void* array, void* const* item);
	using QuestTick_t = void(__fastcall*)(void* system, float dt);

	enum EventKind : uint8_t { kQuestStarted, kQuestStopped, kSystemPaused, kThreadPaused, kThreadStarted, kThreadKilled,
		kActivated, kDeactivated, kInputAgain };
	const char* const kKindNames[] = { "quest started", "quest stopped", "system paused", "thread paused", "thread started",
		"thread killed", "activated", "deactivated", "input again" };

	// What script hears, in NQH_Event's kind
	enum ScriptKind { kScriptActivated = 1, kScriptFinished = 2, kScriptAlreadyActive = 3, kScriptStopped = 4 };

	constexpr int kMaxNext = 4, kNameChars = 48;
	struct Event
	{
		EventKind kind;
		bool replay;        // AttachListener telling us about a block that was already active
		uint8_t nextCount;
		uint32_t output;    // the CName the block fired, 0 for None
		uint32_t caller;    // RVA of what called us, to tell the paths apart
		const void* thread;
		const void* vtable; // the block's class
		uint8_t guid[16];
		uint8_t scope[16];  // the scope block that owns the thread, zero for a quest's own
		uint8_t next[kMaxNext][16];
		char name[kNameChars];
	};

	constexpr uint32_t kRingSize = 32768;   // a load or a fast travel sends about 8,000 at once
	Event g_ring[kRingSize];
	std::atomic<uint32_t> g_head{ 0 }, g_tail{ 0 }, g_lost{ 0 };

	AttachListener_t g_attach = nullptr;
	PushBackUnique_t g_push = nullptr;
	QuestTick_t g_originalTick = nullptr;
	bool g_logEverything = false;

	template <class T> T At(const void* p, size_t offset) { return *reinterpret_cast<const T*>(static_cast<const uint8_t*>(p) + offset); }

	void ReadBlock(Event& e, const void* thread, const void* block, bool withOutput)
	{
		__try
		{
			e.vtable = *static_cast<void* const*>(block);
			memcpy(e.guid, static_cast<const uint8_t*>(block) + kBlockGuid, 16);
			const char* text = At<const char*>(block, kBlockName);
			const uint32_t length = At<uint32_t>(block, kBlockName + 8);
			if (text && length > 1)
			{
				const uint32_t n = length - 1 < kNameChars - 1 ? length - 1 : kNameChars - 1;
				memcpy(e.name, text, n);
				e.name[n] = 0;
			}
			const void* scope = thread ? At<const void*>(thread, kThreadScope) : nullptr;
			if (scope)
				memcpy(e.scope, static_cast<const uint8_t*>(scope) + kBlockGuid, 16);
			if (!withOutput || !thread)
				return;
			// The output it fired is still in its instance data, and its links say where that goes
			const void* instance = At<const void*>(thread, kThreadInstance);
			const void* buffer = instance ? At<const void*>(instance, 0x70) : nullptr;
			const uint8_t* data = buffer ? At<const uint8_t*>(buffer, 0x28) : nullptr;
			if (!data)
				return;
			e.output = *reinterpret_cast<const uint32_t*>(data + At<uint32_t>(block, kBlockOutputVar));
			const uint8_t* sockets = At<const uint8_t*>(block, kBlockConnections);
			const uint32_t socketCount = At<uint32_t>(block, kBlockConnections + 8);
			for (uint32_t s = 0; sockets && s < socketCount; ++s)
			{
				const uint8_t* socket = sockets + s * 0x10;
				if (*reinterpret_cast<const uint32_t*>(socket) != e.output)
					continue;
				const uint8_t* links = *reinterpret_cast<const uint8_t* const*>(socket + 4);
				const uint32_t linkCount = *reinterpret_cast<const uint32_t*>(socket + 0xC);
				for (uint32_t l = 0; links && l < linkCount && e.nextCount < kMaxNext; ++l)
				{
					const uint8_t* next = *reinterpret_cast<const uint8_t* const*>(links + l * 0x10);
					if (next)
						memcpy(e.next[e.nextCount++], next + kBlockGuid, 16);
				}
				break;
			}
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			e.name[0] = 0;
		}
	}

	void Push(EventKind kind, const void* thread, const void* block, void* returnAddress)
	{
		const uint32_t head = g_head.load(std::memory_order_relaxed);
		if (head - g_tail.load(std::memory_order_acquire) >= kRingSize)
		{
			g_lost.fetch_add(1, std::memory_order_relaxed);
			return;
		}
		const Engine& engine = GetEngine();
		Event& e = g_ring[head % kRingSize];
		memset(&e, 0, sizeof(e));
		e.kind = kind;
		e.thread = thread;
		const uintptr_t ret = reinterpret_cast<uintptr_t>(returnAddress);
		e.caller = ret > engine.base ? static_cast<uint32_t>(ret - engine.base) : 0;
		e.replay = e.caller >= engine.attachListener && e.caller < engine.attachListenerEnd;
		if (block)
			ReadBlock(e, thread, block, kind == kDeactivated);
		g_head.store(head + 1, std::memory_order_release);
	}

	// The engine's listener interface, in its order. The first slot is its destructor, which the game never calls.
	struct QuestListener
	{
		virtual void Slot0(unsigned) {}
		virtual void OnQuestStarted(void*, void*) {}
		virtual void OnQuestStopped(void*) {}
		virtual void OnSystemPaused(bool) {}
		virtual void OnThreadPaused(void*, bool) {}
		virtual void OnThreadStarted(void*, void*) {}
		virtual void OnThreadKilled(void*, void*) {}
		virtual void OnBlockActivated(void* thread, void* block) { Push(kActivated, thread, block, _ReturnAddress()); }
		virtual void OnBlockDeactivated(void* thread, void* block) { Push(kDeactivated, thread, block, _ReturnAddress()); }
		virtual void OnBlockInputActivated(void* thread, void* block) { Push(kInputAgain, thread, block, _ReturnAddress()); }
	};
	QuestListener g_listener;   // static: the engine keeps a raw pointer to it for good

	void FormatGuid(const uint8_t* g, char* out)
	{
		uint32_t a; uint16_t b, c;
		memcpy(&a, g, 4); memcpy(&b, g + 4, 2); memcpy(&c, g + 6, 2);
		snprintf(out, 40, "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", a, b, c, g[8], g[9], g[10], g[11], g[12], g[13], g[14], g[15]);
	}

	const char* ClassName(const void* vtable, char* out, size_t size)
	{
		out[0] = 0;
		__try
		{
			const uint8_t* locator = static_cast<const uint8_t* const*>(vtable)[-1];
			const uint32_t typeRva = *reinterpret_cast<const uint32_t*>(locator + 0x0C);
			const char* name = reinterpret_cast<const char*>(GetEngine().base + typeRva + 0x10);
			if (strncmp(name, ".?AV", 4) == 0)
				name += 4;
			size_t i = 0;
			for (; name[i] && name[i] != '@' && i + 1 < size; ++i)
				out[i] = name[i];
			out[i] = 0;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			snprintf(out, size, "?");
		}
		return out;
	}

	std::string OutputName(uint32_t id)
	{
		if (id == 0)
			return "None";
		char text[128];
		if (NameText(id, text, sizeof(text)))
			return text;
		return "#" + std::to_string(id);
	}

	// Attaching: onto the system, so new quests get us, and onto each quest already running
	bool Listening(void* system)
	{
		__try
		{
			void* const* list = At<void* const*>(system, kSysListeners);
			const uint32_t count = At<uint32_t>(system, kSysListeners + 8);
			for (uint32_t i = 0; i < count; ++i)
				if (list[i] == &g_listener)
					return true;
			return false;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}

	bool Attach(void* system)
	{
		__try
		{
			uint8_t* sys = static_cast<uint8_t*>(system);
			Log("quest system at %p: %u listeners before ours, %u quests running", system, At<uint32_t>(sys, kSysListeners + 8),
				At<uint32_t>(sys, kSysThreads + 8));
			void* const item = &g_listener;
			g_push(sys + kSysListeners, &item);
			for (uint32_t i = 0; i < At<uint32_t>(sys, kSysThreads + 8); ++i)
				g_attach(At<void* const*>(sys, kSysThreads)[i], &g_listener);
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Log("attaching crashed: the hook is off");
			return false;
		}
	}

	// What script watches: everyone's lists together, from NQH_WatchList as "version|guid,guid,...".
	// Events name a block by its place in this list, so no text crosses over.
	std::vector<std::string> g_watch;
	int g_watchVersion = -1;
	std::unordered_map<std::string, int> g_watchIndex;   // a GUID's first place in the list

	// Every block running now, by GUID, with how many threads it runs in
	std::unordered_map<std::string, int> g_active;

	void SendToScript(int kind, int index, uint32_t output)
	{
		if (!CallScriptEvent(g_watchVersion, index, kind, output))
			Log("couldn't send kind %d for %s to script", kind, g_watch[index].c_str());
	}

	void RefreshWatchList()
	{
		std::string text;
		if (!CallScriptString("NQH_WatchList", text))
			return;
		const size_t bar = text.find('|');
		const int version = atoi(text.c_str());
		if (bar == std::string::npos || version == g_watchVersion)
			return;
		std::unordered_map<std::string, int> before;
		before.swap(g_watchIndex);
		g_watchVersion = version;
		g_watch.clear();
		size_t at = bar + 1;
		while (at < text.size())
		{
			size_t end = text.find(',', at);
			if (end == std::string::npos)
				end = text.size();
			std::string guid = text.substr(at, end - at);
			for (char& c : guid)
				c = static_cast<char>(tolower(static_cast<unsigned char>(c)));
			if (!g_watchIndex.count(guid))
				g_watchIndex[guid] = static_cast<int>(g_watch.size());
			g_watch.push_back(guid);
			at = end + 1;
		}
		// Blocks newly watched that are running already: a mod that registers after a load, say
		int already = 0;
		for (const auto& [guid, index] : g_watchIndex)
		{
			const auto running = g_active.find(guid);
			if (!before.count(guid) && running != g_active.end() && running->second > 0)
			{
				SendToScript(kScriptAlreadyActive, index, 0);
				++already;
			}
		}
		Log("watching %zu blocks (list %d), %zu blocks running, %d of ours already", g_watch.size(), version, g_active.size(), already);
	}

	uint64_t g_seen = 0, g_sent = 0;

	void Drain()
	{
		const uint32_t lost = g_lost.exchange(0);
		if (lost)
			Log("%u events lost: the ring was full", lost);
		const uint32_t normalFinish = GetEngine().normalFinish;
		uint32_t tail = g_tail.load(std::memory_order_relaxed);
		const uint32_t head = g_head.load(std::memory_order_acquire);
		for (; tail != head; ++tail)
		{
			const Event& e = g_ring[tail % kRingSize];
			char guid[40];
			FormatGuid(e.guid, guid);
			++g_seen;
			if (e.kind == kActivated)
				++g_active[guid];
			else if (e.kind == kDeactivated)
			{
				const auto it = g_active.find(guid);
				if (it != g_active.end() && --it->second <= 0)
					g_active.erase(it);
			}
			int kind = 0;
			if (e.kind == kActivated)
				kind = e.replay ? kScriptAlreadyActive : kScriptActivated;
			else if (e.kind == kDeactivated)
				kind = e.caller == normalFinish ? kScriptFinished : kScriptStopped;
			const auto watched = g_watchIndex.find(guid);
			if (watched == g_watchIndex.end() && !g_logEverything)
				continue;
			char cls[64], scope[40] = "-", next[kMaxNext * 40 + 8] = "";
			ClassName(e.vtable, cls, sizeof(cls));
			static const uint8_t zero[16] = {};
			if (memcmp(e.scope, zero, 16) != 0)
				FormatGuid(e.scope, scope);
			for (int i = 0; i < e.nextCount; ++i)
			{
				char one[40];
				FormatGuid(e.next[i], one);
				strcat_s(next, one);
				strcat_s(next, i + 1 < e.nextCount ? " " : "");
			}
			Log("%s%s %s \"%s\" %s | scope %s | out %s -> [%s] | from +0x%x%s", kKindNames[e.kind], e.replay ? " (replay)" : "", cls,
				e.name, guid, scope, OutputName(e.output).c_str(), next, e.caller, watched == g_watchIndex.end() ? "" : " | watched");
			if (watched != g_watchIndex.end() && kind)
			{
				SendToScript(kind, watched->second, kind == kScriptFinished ? e.output : 0);
				++g_sent;
			}
		}
		g_tail.store(tail, std::memory_order_release);
	}

	void* g_system = nullptr;
	uint64_t g_ticks = 0;
	bool g_off = false;

	void __fastcall HookedTick(void* system, float dt)
	{
		g_originalTick(system, dt);
		if (g_off)
			return;
		++g_ticks;
		if (system != g_system || (g_ticks % 30 == 0 && !Listening(system)))
		{
			Log("%s quest system", g_system == nullptr ? "first" : system != g_system ? "a new" : "our listener has gone from the");
			g_system = system;
			if (!Attach(system))
			{
				g_off = true;
				return;
			}
		}
		if (!ScriptsReady())
			return;
		static uint64_t lastHello = 0, lastList = 0;
		if (g_ticks - lastHello >= 60)
		{
			lastHello = g_ticks;
			int ignored = 0;
			CallScriptInt("NQH_Hello", ignored);
		}
		if (lastList == 0 || g_ticks - lastList >= 120)
		{
			lastList = g_ticks;
			RefreshWatchList();
		}
		Drain();
		if (g_ticks % 3600 == 0)
			Log("%llu block events seen, %llu sent to script", g_seen, g_sent);
	}
}

bool ListenerInstall(std::string& problem)
{
	const Engine& engine = GetEngine();
	g_attach = reinterpret_cast<AttachListener_t>(engine.base + engine.attachListener);
	g_push = reinterpret_cast<PushBackUnique_t>(engine.base + engine.pushBackUnique);
	wchar_t ini[MAX_PATH];
	GetModuleFileNameW(reinterpret_cast<HMODULE>(&__ImageBase), ini, MAX_PATH);
	if (wchar_t* dot = wcsrchr(ini, L'.'))
		wcscpy_s(dot, MAX_PATH - (dot - ini), L".ini");
	g_logEverything = GetPrivateProfileIntW(L"Debug", L"LogEveryBlock", 0, ini) != 0;

	// The quest system's tick, slot 48 of CR4QuestSystem's vtable
	auto* slot = reinterpret_cast<uintptr_t*>(engine.base + engine.questSystemVtable + 48 * 8);
	DWORD old = 0;
	if (!VirtualProtect(slot, sizeof(*slot), PAGE_READWRITE, &old))
	{
		problem = "the quest system's vtable can't be written";
		return false;
	}
	g_originalTick = reinterpret_cast<QuestTick_t>(*slot);
	*slot = reinterpret_cast<uintptr_t>(&HookedTick);
	VirtualProtect(slot, sizeof(*slot), old, &old);
	Log("quest tick hooked%s", g_logEverything ? ", logging every block" : "");
	return true;
}
