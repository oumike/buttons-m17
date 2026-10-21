#pragma once
// NVS persistence for cfg::Settings (namespace "m17"), plus a mutex-guarded
// in-RAM copy that every task reads through settingsGet().

#include "config/settings.h"

void settingsBegin();                         // load from NVS (defaults if empty)
cfg::Settings settingsGet();                  // thread-safe copy
bool settingsSave(const cfg::Settings &s);    // persist + update RAM copy
void settingsFactoryReset();                  // wipe namespace
