#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

// The engine's own functions and objects this uses. Worked out from witcher3.exe build 0x6ADFBD7A:
// see research/quest-hook-findings.md in Progress on the Path for where each comes from.
struct Engine
{
	uintptr_t base = 0;
	// Quest system
	uintptr_t questSystemVtable = 0, questSystemTick = 0, attachListener = 0, attachListenerEnd = 0, pushBackUnique = 0;
	uint32_t normalFinish = 0;   // return address of the listener call when a block finishes in the tick
	// Names and script calls
	uintptr_t getRtti = 0, getNames = 0, addName = 0, findGlobalFunction = 0, callDirect = 0, free = 0, scriptingPtr = 0;
	uint32_t nameTable = 0;      // where the name pool keeps its table
	bool names = false;          // whether a CName's text can be read
};

DWORD ExeTimestamp();
// The build it was worked out on: its table, checked against the code at each address
bool EngineFromTable(Engine& e, std::string& problem);
// Any build: by pattern, each matching exactly once. False, with why, if anything doesn't.
bool EngineFromScan(Engine& e, std::string& problem, uintptr_t loaded = 0);   // loaded: another loaded copy, for testing
void EngineUse(const Engine& e);
std::string EngineDescribe(const Engine& e);
bool EngineSame(const Engine& a, const Engine& b);
const Engine& GetEngine();

bool ScriptsReady();
uint32_t NameId(const char* name);                  // 0 if it can't be made
bool NameText(uint32_t id, char* out, size_t size); // false if it has none, or there's no lookup

// A global script function with no parameters, by name. Looked up every time: a recompile replaces them.
void* ScriptFunction(const char* name);
bool CallScriptInt(const char* name, int& out);
bool CallScriptString(const char* name, std::string& out);
// NQH_Event(version : int, index : int, kind : int, output : name), checked against the function's own parameters
bool CallScriptEvent(int version, int index, int kind, uint32_t output);
