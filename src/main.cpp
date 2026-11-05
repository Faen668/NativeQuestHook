#include <windows.h>
#include <string>
#include "engine.h"
#include "listener.h"
#include "log.h"

namespace
{
	double Ms(LARGE_INTEGER from)
	{
		LARGE_INTEGER now, frequency;
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		return static_cast<double>(now.QuadPart - from.QuadPart) * 1000.0 / static_cast<double>(frequency.QuadPart);
	}

	// Off the game's loading path. The scan runs first, before anything is hooked, as hooking changes what it checks.
	// On the build it was worked out on the table is used, and the log says whether the scan agrees. On any other
	// build the scan decides: everything found, or nothing is hooked.
	DWORD WINAPI Start(LPVOID)
	{
		const DWORD timestamp = ExeTimestamp();
		LARGE_INTEGER began;
		QueryPerformanceCounter(&began);
		Engine scanned;
		std::string scanProblem;
		const bool found = EngineFromScan(scanned, scanProblem);
		const double ms = Ms(began);

		std::string problem;
		Engine table;
		if (EngineFromTable(table, problem))
		{
			if (!found)
				Log("exe 0x%08lx: using the table; the scan failed in %.0f ms: %s", timestamp, ms, scanProblem.c_str());
			else if (EngineSame(table, scanned))
				Log("exe 0x%08lx: using the table, and the scan agrees (%.0f ms)", timestamp, ms);
			else
				Log("exe 0x%08lx: using the table, but the scan found something else (%.0f ms): %s", timestamp, ms, EngineDescribe(scanned).c_str());
			EngineUse(table);
		}
		else if (found)
		{
			Log("exe 0x%08lx: found everything by scan in %.0f ms: %s", timestamp, ms, EngineDescribe(scanned).c_str());
			EngineUse(scanned);
		}
		else
		{
			Log("off: exe 0x%08lx isn't a build this knows, and the scan failed in %.0f ms: %s", timestamp, ms, scanProblem.c_str());
			return 0;
		}
		if (!ListenerInstall(problem))
			Log("off: %s", problem.c_str());
		return 0;
	}
}

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID)
{
	if (reason != DLL_PROCESS_ATTACH)
		return TRUE;
	DisableThreadLibraryCalls(module);
	LogInit(module);
	Log("Native Quest Hook 1.0.0");
	if (HANDLE thread = CreateThread(nullptr, 0, Start, nullptr, 0, nullptr))
		CloseHandle(thread);
	return TRUE;
}
