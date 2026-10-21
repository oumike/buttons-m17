#pragma once
// Settings backup / restore as a small YAML document. Pure C++, host-tested.
//
// Format: flat "key: value" lines, every string double-quoted with JSON-style
// escapes (so the multi-line host list survives), numbers and booleans bare.
// That is valid YAML, readable and hand-editable; the parser accepts only that
// subset. Unknown keys are ignored so newer backups load on older firmware.
//
// The admin password is never written: a backup file is something people copy
// around, and restoring onto a fresh device happens after first-boot setup has
// already set a password. Wi-Fi credentials ARE included, so a backup is a
// secret; the web UI says so next to the download button.

#include <string>

#include "config/settings.h"

namespace cfg {

constexpr const char *BACKUP_FORMAT = "buttons-m17-settings";
constexpr int BACKUP_VERSION = 1;
constexpr size_t BACKUP_MAX_BYTES = 8192;

std::string settingsToYaml(const Settings &s);

// Parses `text` over a copy of `base` (so absent keys keep their current
// value), keeps base.adminPass, validates the result, and writes it to `out`
// only on success. Returns "" or an error message.
std::string settingsFromYaml(const std::string &text, const Settings &base, Settings &out);

}  // namespace cfg
