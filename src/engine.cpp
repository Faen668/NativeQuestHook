#include "engine.h"
#include <cstring>
#include "log.h"

namespace
{
	Engine g_engine;
	bool g_crashed = false;   // a script call went wrong: no more calls this session

	constexpr DWORD kBuild = 0x6ADFBD7A;
	constexpr size_t kScriptingReady = 0x174;
	constexpr size_t kFunctionFlags = 0x18, kFunctionParams = 0x28, kFunctionFrame = 0x58, kFunctionCodeSize = 0x90;
	constexpr uint32_t kFuncNative = 0x1, kFuncLatent = 0x40;
	constexpr size_t kPropertyName = 0x10, kPropertyOffset = 0x20;

#pragma pack(push, 4)
	struct EngineString
	{
		char* data;
		uint32_t size;   // counts the terminating NUL
	};
#pragma pack(pop)
	static_assert(sizeof(EngineString) == 12);

	using GetRTTI_t = void*(__fastcall*)();
	using GetNames_t = void*(__fastcall*)();
	using AddName_t = uint32_t(__fastcall*)(void*, const char*);
	using FindGlobalFunction_t = void*(__fastcall*)(void*, const uint32_t*);
	using CallDirect_t = bool(__fastcall*)(void*, void*, void*, void*);
	using Free_t = void(__fastcall*)(void*);

	// What the hook needs, found by the code around it. On the build it was worked out on the table below is used,
	// and the scan still runs, to show in the log whether it holds up. On any other build the scan is all there is,
	// and every pattern must match exactly once (NameSite may match more often, as long as every match agrees).
	enum Kind
	{
		kFunction,     // the function's own opening
		kCallTarget,   // a call to it: a rel32 at `at`, the next instruction at at + 4
		kGlobal,       // code reading a global: a rel32 at `at`, the next instruction at `end`
		kFinishSite,   // the tick's deactivation: its listener call is the first `call [rax+40h]` after it
		kNameSite,     // NameToString: GetNames called at +1, the name table's offset at +25
	};
	struct Pattern { const char* name; Kind kind; const char* bytes; int at; int end; uintptr_t known; };
	const Pattern kPatterns[] = {
		{ "QuestSystemTick", kFunction, "40 53 48 83 EC 20 80 79 68 00 48 8B D9 C6 41 6A 01 0F 84 ?? ?? ?? ?? 83 79 6C 00 0F 8F ?? ?? ?? ??", 0, 0, 0x1FF8ED0 },
		{ "QuestThreadAttachListener", kFunction, "48 89 5C 24 10 48 89 6C 24 18 48 89 74 24 20 57 41 56 41 57 48 83 EC 20 4C 8B F2 48 89 54 24 40 48 8B D9 48 8D 54 24 40 48 81 C1 84 00 00 00 E8", 0, 0, 0x2079330 },
		{ "QuestThreadDetachListener", kFunction, "48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 48 83 EC 20 48 8B F1 48 8B FA 48 8B 89 84 00 00 00", 0, 0, 0x2079410 },
		{ "DynArrayPushBackUniquePtr", kFunction, "48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 4C 8B 11 48 8B FA 8B 51 08 48 8B D9 49 8B C2 48 8D 34 D5 00 00 00 00", 0, 0, 0x388D70 },
		{ "QuestThreadRunBlocks_Finish", kFinishSite, "80 A3 C0 00 00 00 FD 48 8B B3 84 00 00 00 8B 83 8C 00 00 00 48 8D 0C C6 48 3B F1 74 ?? 66 0F 1F 44 00 00", 0, 0, 0x207827F },
		{ "GetRTTI", kFunction, "48 83 EC 28 48 8B 05 ?? ?? ?? ?? 48 85 C0 0F 85 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 8D 48 08 FF 15 ?? ?? ?? ?? 48 8B 05 ?? ?? ?? ?? 48 85 C0 0F 85 ?? ?? ?? ?? 38 05 ?? ?? ?? ?? 74 ?? 88 05 ?? ?? ?? ?? 33 C0 48 8D 0D ?? ?? ?? ?? 48 89 05 ?? ?? ?? ?? 89 05 ?? ?? ?? ?? 48 89 05", 0, 0, 0x31F8D0 },
		{ "AddName", kFunction, "48 85 D2 74 ?? 80 3A 00 0F 85 ?? ?? ?? ?? 33 C0 C3", 0, 0, 0x26B7A90 },
		{ "FindGlobalFunction", kFunction, "83 79 44 00 74 ?? 44 8B 0A 33 D2 41 8B C1 F7 71 40", 0, 0, 0x26CB380 },
		{ "CallDirect", kFunction, "40 55 41 54 41 55 41 56 41 57 48 81 EC 70 01 00 00 48 8D 6C 24 20 8B 81 90 00 00 00 4D 8B F9", 0, 0, 0x26FBD80 },
		{ "Free", kCallTarget, "4D 85 F6 74 ?? 49 8B CE E8 ?? ?? ?? ?? 32 DB EB ?? CC", 9, 0, 0xBCB6F0 },
		// Past the script tick's opening, which other mods hook: PotP's overlay writes a jump over its first 15 bytes
		{ "ScriptingPtr", kGlobal, "48 8B 3D ?? ?? ?? ?? C5 F8 29 74 24 20 C5 F8 28 F1 80 BF 79 01 00 00 00", 3, 7, 0x5A7BA70 },
		{ "NameToString", kNameSite, "E8 ?? ?? ?? ?? 48 63 5C 24 68 48 8B C8 48 8B F8 FF 15 ?? ?? ?? ?? 48 8B 8F ?? ?? ?? ?? 48 8B 1C D9", 0, 0, 0x31D510 },
	};
	constexpr uintptr_t kKnownQuestSystemVtable = 0x37C32F0;
	constexpr uint32_t kKnownNameTable = 0x11830;

	struct Compiled { uint8_t bytes[128]; bool any[128]; size_t length; };

	Compiled Compile(const char* pattern)
	{
		Compiled c{};
		for (const char* p = pattern; *p && c.length < sizeof(c.bytes);)
		{
			if (*p == ' ')
			{
				++p;
				continue;
			}
			if (p[0] == '?')
				c.any[c.length] = true;
			else
			{
				const char hex[3] = { p[0], p[1], 0 };
				c.bytes[c.length] = static_cast<uint8_t>(strtoul(hex, nullptr, 16));
			}
			++c.length;
			p += 2;
		}
		return c;
	}

	bool MatchesAt(const uint8_t* at, const Compiled& c)
	{
		for (size_t i = 0; i < c.length; ++i)
			if (!c.any[i] && at[i] != c.bytes[i])
				return false;
		return true;
	}

	// Every place in [from, to) the pattern matches, up to `most`
	int FindAll(const uint8_t* from, const uint8_t* to, const Compiled& c, const uint8_t** found, int most)
	{
		int n = 0;
		for (const uint8_t* p = from; p + c.length <= to;)
		{
			p = static_cast<const uint8_t*>(memchr(p, c.bytes[0], static_cast<size_t>(to - c.length - p) + 1));
			if (!p)
				break;
			if (MatchesAt(p, c))
			{
				if (n < most)
					found[n] = p;
				++n;
			}
			++p;
		}
		return n;
	}

	int32_t Rel32(const uint8_t* at) { int32_t v; memcpy(&v, at, 4); return v; }

	struct Section { const uint8_t* begin; const uint8_t* end; };

	bool FindSection(uintptr_t base, const char* name, Section& out)
	{
		const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
		const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
		const IMAGE_SECTION_HEADER* s = IMAGE_FIRST_SECTION(nt);
		for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++s)
			if (strncmp(reinterpret_cast<const char*>(s->Name), name, 8) == 0)
			{
				out.begin = reinterpret_cast<const uint8_t*>(base + s->VirtualAddress);
				out.end = out.begin + s->Misc.VirtualSize;
				return true;
			}
		return false;
	}

	// The quest system's vtable, through its MSVC type information: the type descriptor named .?AVCR4QuestSystem@@,
	// the complete object locator for its main vtable that points at it, and the vtable just after the pointer to that
	uintptr_t FindVtableByRtti(uintptr_t base, const char* typeName)
	{
		Section data{}, rdata{};
		if (!FindSection(base, ".data", data) || !FindSection(base, ".rdata", rdata))
			return 0;
		const size_t nameLength = strlen(typeName) + 1;
		const uint8_t* name = nullptr;
		for (const uint8_t* p = data.begin; p + nameLength <= data.end; ++p)
		{
			p = static_cast<const uint8_t*>(memchr(p, typeName[0], static_cast<size_t>(data.end - nameLength - p) + 1));
			if (!p)
				break;
			if (memcmp(p, typeName, nameLength) == 0)
			{
				name = p;
				break;
			}
		}
		if (!name)
			return 0;
		const uint32_t typeRva = static_cast<uint32_t>(name - 0x10 - reinterpret_cast<const uint8_t*>(base));
		for (const uint8_t* p = rdata.begin; p + 24 <= rdata.end; p += 4)
		{
			uint32_t col[6];
			memcpy(col, p, sizeof(col));
			// signature 1, the main vtable (offset 0), this type, and the locator's own address
			if (col[0] != 1 || col[1] != 0 || col[3] != typeRva || col[5] != static_cast<uint32_t>(p - reinterpret_cast<const uint8_t*>(base)))
				continue;
			const uint64_t locator = static_cast<uint64_t>(base) + col[5];
			for (const uint8_t* q = rdata.begin; q + 8 <= rdata.end; q += 8)
			{
				uint64_t v;
				memcpy(&v, q, 8);
				if (v == locator)
					return static_cast<uintptr_t>(q + 8 - reinterpret_cast<const uint8_t*>(base));
			}
		}
		return 0;
	}

	void Assign(Engine& e, const char* name, uintptr_t value)
	{
		if (!strcmp(name, "QuestSystemTick")) e.questSystemTick = value;
		else if (!strcmp(name, "QuestThreadAttachListener")) e.attachListener = value;
		else if (!strcmp(name, "QuestThreadDetachListener")) e.attachListenerEnd = value;
		else if (!strcmp(name, "DynArrayPushBackUniquePtr")) e.pushBackUnique = value;
		else if (!strcmp(name, "QuestThreadRunBlocks_Finish")) e.normalFinish = static_cast<uint32_t>(value);
		else if (!strcmp(name, "GetRTTI")) e.getRtti = value;
		else if (!strcmp(name, "AddName")) e.addName = value;
		else if (!strcmp(name, "FindGlobalFunction")) e.findGlobalFunction = value;
		else if (!strcmp(name, "CallDirect")) e.callDirect = value;
		else if (!strcmp(name, "Free")) e.free = value;
		else if (!strcmp(name, "ScriptingPtr")) e.scriptingPtr = value;
		else if (!strcmp(name, "NameToString")) e.getNames = value;
	}

	GetRTTI_t GetRTTI() { return reinterpret_cast<GetRTTI_t>(g_engine.base + g_engine.getRtti); }
	GetNames_t GetNames() { return reinterpret_cast<GetNames_t>(g_engine.base + g_engine.getNames); }

	bool GuardedCall(void* fn, void* params, void* result)
	{
		__try
		{
			return reinterpret_cast<CallDirect_t>(g_engine.base + g_engine.callDirect)(fn, nullptr, params, result);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			Log("script call crashed: exception 0x%08lx. No more script calls this session.", GetExceptionCode());
			g_crashed = true;
			return false;
		}
	}

	void EngineFree(void* p)
	{
		__try
		{
			reinterpret_cast<Free_t>(g_engine.base + g_engine.free)(p);
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
		}
	}
}

DWORD ExeTimestamp()
{
	const uintptr_t base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
	return reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew)->FileHeader.TimeDateStamp;
}

bool EngineFromTable(Engine& e, std::string& problem)
{
	e = Engine{};
	e.base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	if (ExeTimestamp() != kBuild)
	{
		problem = "not the build the table is for";
		return false;
	}
	for (const Pattern& p : kPatterns)
	{
		// The code at each address must be what it was
		const uintptr_t at = p.kind == kFunction ? p.known : 0;
		if (at && !MatchesAt(reinterpret_cast<const uint8_t*>(e.base + at), Compile(p.bytes)))
		{
			problem = std::string(p.name) + " isn't what it should be";
			return false;
		}
		Assign(e, p.name, p.known);
	}
	e.questSystemVtable = kKnownQuestSystemVtable;
	e.nameTable = kKnownNameTable;
	e.names = true;
	if (*reinterpret_cast<uintptr_t*>(e.base + e.questSystemVtable + 48 * 8) != e.base + e.questSystemTick)
	{
		problem = "the quest system's vtable isn't laid out as expected";
		return false;
	}
	return true;
}

bool EngineFromScan(Engine& e, std::string& problem, uintptr_t loaded)
{
	e = Engine{};
	e.base = loaded ? loaded : reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
	Section text{};
	if (!FindSection(e.base, ".text", text))
	{
		problem = "no .text section";
		return false;
	}
	const auto* image = reinterpret_cast<const uint8_t*>(e.base);
	for (const Pattern& p : kPatterns)
	{
		const Compiled c = Compile(p.bytes);
		const uint8_t* found[8];
		const int n = FindAll(text.begin, text.end, c, found, 8);
		// Every problem is listed, not just the first, so one log line says all that changed
		if (n == 0 || (n > 1 && p.kind != kNameSite) || n > 8)
		{
			problem += (problem.empty() ? "" : ", ") + std::string(p.name) + (n == 0 ? " not found" : " found more than once");
			continue;
		}
		uintptr_t value = 0;
		for (int i = 0; i < n; ++i)
		{
			const uint8_t* m = found[i];
			uintptr_t v = 0;
			switch (p.kind)
			{
			case kFunction: v = static_cast<uintptr_t>(m - image); break;
			case kCallTarget: v = static_cast<uintptr_t>(m + p.at + 4 + Rel32(m + p.at) - image); break;
			case kGlobal: v = static_cast<uintptr_t>(m + p.end + Rel32(m + p.at) - image); break;
			case kFinishSite:
			{
				static const uint8_t call[] = { 0xFF, 0x50, 0x40 };
				for (const uint8_t* q = m; q < m + 0x80; ++q)
					if (memcmp(q, call, 3) == 0)
					{
						v = static_cast<uintptr_t>(q + 3 - image);
						break;
					}
				break;
			}
			case kNameSite:
			{
				v = static_cast<uintptr_t>(m + 5 + Rel32(m + 1) - image);
				const uint32_t table = static_cast<uint32_t>(Rel32(m + 25));
				if (i > 0 && table != e.nameTable)
				{
					problem = "the NameToString sites don't agree";
					return false;
				}
				e.nameTable = table;
				break;
			}
			}
			if (v == 0 || (i > 0 && v != value))
			{
				problem = std::string(p.name) + (v == 0 ? " can't be read" : " sites don't agree");
				return false;
			}
			value = v;
		}
		Assign(e, p.name, value);
	}
	if (!problem.empty())
		return false;
	e.names = true;
	e.questSystemVtable = FindVtableByRtti(e.base, ".?AVCR4QuestSystem@@");
	if (!e.questSystemVtable)
	{
		problem = "CR4QuestSystem's vtable not found";
		return false;
	}
	if (*reinterpret_cast<uintptr_t*>(e.base + e.questSystemVtable + 48 * 8) != e.base + e.questSystemTick)
	{
		problem = "the quest system's vtable doesn't hold its tick where expected";
		return false;
	}
	return true;
}

void EngineUse(const Engine& e)
{
	g_engine = e;
}

std::string EngineDescribe(const Engine& e)
{
	char text[512];
	snprintf(text, sizeof(text), "tick +0x%llx, attach +0x%llx..+0x%llx, push +0x%llx, finish +0x%x, vtable +0x%llx, rtti +0x%llx, names +0x%llx/+0x%x, add name +0x%llx, find +0x%llx, call +0x%llx, free +0x%llx, scripting +0x%llx",
		(unsigned long long)e.questSystemTick, (unsigned long long)e.attachListener, (unsigned long long)e.attachListenerEnd,
		(unsigned long long)e.pushBackUnique, e.normalFinish, (unsigned long long)e.questSystemVtable, (unsigned long long)e.getRtti,
		(unsigned long long)e.getNames, e.nameTable, (unsigned long long)e.addName, (unsigned long long)e.findGlobalFunction,
		(unsigned long long)e.callDirect, (unsigned long long)e.free, (unsigned long long)e.scriptingPtr);
	return text;
}

bool EngineSame(const Engine& a, const Engine& b)
{
	return a.questSystemTick == b.questSystemTick && a.attachListener == b.attachListener && a.attachListenerEnd == b.attachListenerEnd
		&& a.pushBackUnique == b.pushBackUnique && a.normalFinish == b.normalFinish && a.questSystemVtable == b.questSystemVtable
		&& a.getRtti == b.getRtti && a.getNames == b.getNames && a.nameTable == b.nameTable && a.addName == b.addName
		&& a.findGlobalFunction == b.findGlobalFunction && a.callDirect == b.callDirect && a.free == b.free && a.scriptingPtr == b.scriptingPtr;
}

const Engine& GetEngine()
{
	return g_engine;
}

bool ScriptsReady()
{
	if (g_crashed)
		return false;
	__try
	{
		const uint8_t* scripting = *reinterpret_cast<uint8_t* const*>(g_engine.base + g_engine.scriptingPtr);
		return scripting && scripting[kScriptingReady] != 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		return false;
	}
}

uint32_t NameId(const char* name)
{
	return reinterpret_cast<AddName_t>(g_engine.base + g_engine.addName)(GetNames()(), name);
}

bool NameText(uint32_t id, char* out, size_t size)
{
	out[0] = 0;
	if (!g_engine.names || id == 0)
		return false;
	uint8_t* pool = static_cast<uint8_t*>(GetNames()());
	if (!pool)
		return false;
	// The pool's critical section is its first member, as NameToString uses it
	auto* lock = reinterpret_cast<CRITICAL_SECTION*>(pool);
	EnterCriticalSection(lock);
	__try
	{
		const char* const* entry = (*reinterpret_cast<const char* const* const* const*>(pool + g_engine.nameTable))[id];
		const char* text = entry ? *entry : nullptr;
		size_t i = 0;
		for (; text && text[i] && i + 1 < size; ++i)
			out[i] = text[i];
		out[i] = 0;
	}
	__except (EXCEPTION_EXECUTE_HANDLER)
	{
		out[0] = 0;
	}
	LeaveCriticalSection(lock);
	return out[0] != 0;
}

void* ScriptFunction(const char* name)
{
	if (g_crashed)
		return nullptr;
	const uint32_t id = NameId(name);
	void* fn = id ? reinterpret_cast<FindGlobalFunction_t>(g_engine.base + g_engine.findGlobalFunction)(GetRTTI()(), &id) : nullptr;
	if (!fn)
		return nullptr;
	const auto* bytes = static_cast<const uint8_t*>(fn);
	const uint32_t flags = *reinterpret_cast<const uint32_t*>(bytes + kFunctionFlags);
	const uint32_t codeSize = *reinterpret_cast<const uint32_t*>(bytes + kFunctionCodeSize);
	if ((flags & (kFuncNative | kFuncLatent)) || codeSize == 0)
	{
		Log("%s can't be called: flags 0x%x, code size %u", name, flags, codeSize);
		return nullptr;
	}
	return fn;
}

bool CallScriptInt(const char* name, int& out)
{
	void* fn = ScriptFunction(name);
	int32_t value = 0;
	if (!fn || !GuardedCall(fn, nullptr, &value))
		return false;
	out = value;
	return true;
}

bool CallScriptString(const char* name, std::string& out)
{
	void* fn = ScriptFunction(name);
	if (!fn)
		return false;
	EngineString text{};
	const bool ok = GuardedCall(fn, nullptr, &text);
	if (g_crashed)
		return false;
	out.clear();
	if (text.data && text.size > 1)
		out.assign(text.data, text.size - 1);
	if (text.data)
		EngineFree(text.data);
	return ok;
}

namespace
{
	// The event function's parameters, checked by name and laid out where the engine says
	struct EventLayout { uint32_t frame; uint32_t offset[4]; };

	bool ReadEventLayout(void* fn, EventLayout& layout)
	{
		static const char* const kNames[4] = { "version", "index", "kind", "output" };
		__try
		{
			const auto* bytes = static_cast<const uint8_t*>(fn);
			const auto* const* params = *reinterpret_cast<const uint8_t* const* const*>(bytes + kFunctionParams);
			const uint32_t count = *reinterpret_cast<const uint32_t*>(bytes + kFunctionParams + 8);
			layout.frame = *reinterpret_cast<const uint32_t*>(bytes + kFunctionFrame);
			if (count != 4 || layout.frame == 0 || layout.frame > 512)
				return false;
			for (int i = 0; i < 4; ++i)
			{
				const uint8_t* prop = params[i];
				if (*reinterpret_cast<const uint32_t*>(prop + kPropertyName) != NameId(kNames[i]))
					return false;
				layout.offset[i] = *reinterpret_cast<const uint32_t*>(prop + kPropertyOffset);
				if (layout.offset[i] + 4 > layout.frame)
					return false;
			}
			return true;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			return false;
		}
	}
}

bool CallScriptEvent(int version, int index, int kind, uint32_t output)
{
	void* fn = ScriptFunction("NQH_Event");
	if (!fn)
		return false;
	EventLayout layout{};
	if (!ReadEventLayout(fn, layout))
	{
		static bool told = false;
		if (!told)
			Log("NQH_Event's parameters aren't (version : int, index : int, kind : int, output : name): not calling it");
		told = true;
		return false;
	}
	alignas(16) uint8_t frame[512] = {};
	const int32_t values[3] = { version, index, kind };
	for (int i = 0; i < 3; ++i)
		memcpy(frame + layout.offset[i], &values[i], 4);
	memcpy(frame + layout.offset[3], &output, 4);
	return GuardedCall(fn, frame, nullptr);
}
