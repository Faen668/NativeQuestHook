#pragma once

// NativeQuestHook.log, beside the .asi. Started afresh each launch.
void LogInit(void* module);
void Log(const char* fmt, ...);
