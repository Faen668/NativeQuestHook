#include "log.h"
#include <windows.h>
#include <cstdarg>
#include <cstdio>
#include <share.h>
#include <mutex>

namespace
{
	FILE* g_file = nullptr;
	std::mutex g_mutex;
}

void LogInit(void* module)
{
	wchar_t path[MAX_PATH];
	GetModuleFileNameW(static_cast<HMODULE>(module), path, MAX_PATH);
	wchar_t* dot = wcsrchr(path, L'.');
	if (dot)
		wcscpy_s(dot, MAX_PATH - (dot - path), L".log");
	// The last session's log is kept as NativeQuestHook.prev.log: after a crash, the next launch would write over it
	wchar_t previous[MAX_PATH];
	wcscpy_s(previous, path);
	if (wchar_t* ext = wcsrchr(previous, L'.'))
		wcscpy_s(ext, MAX_PATH - (ext - previous), L".prev.log");
	MoveFileExW(path, previous, MOVEFILE_REPLACE_EXISTING);
	// Shared, so the log can be read while the game runs
	g_file = _wfsopen(path, L"w", _SH_DENYNO);
}

void Log(const char* fmt, ...)
{
	std::lock_guard lock(g_mutex);
	if (!g_file)
		return;
	SYSTEMTIME t;
	GetLocalTime(&t);
	fprintf(g_file, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
	va_list args;
	va_start(args, fmt);
	vfprintf(g_file, fmt, args);
	va_end(args);
	fputc('\n', g_file);
	fflush(g_file);
}
