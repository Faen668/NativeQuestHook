#pragma once
#include <string>

extern "C" char __ImageBase;

// Takes over the quest system's tick, which attaches our listener and sends events to script. False, with why, if not.
bool ListenerInstall(std::string& problem);
