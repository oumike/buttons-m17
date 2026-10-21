#pragma once
// Settings model and validation. Pure C++: no Arduino or IDF headers, so the
// rules are unit-tested on the host (test/native/test_config).
// Persistence to NVS is in settings_store.{h,cpp}.

#include <cstdint>
#include <string>
#include <vector>

#include "config/reflectors.h"

namespace cfg {

enum class PttMode : uint8_t { Momentary = 0, Toggle = 1 };

// TODO(reflector): no target reflector chosen yet. M17-M17 is the M17
// Project's own reflector; change it in the web UI.
constexpr const char *DEFAULT_REFLECTOR = "M17-M17";
extern const char *const DEFAULT_EXTRA_HOSTS;

struct Settings {
    // Network / admin
    std::string wifiSsid;
    std::string wifiPass;
    std::string adminPass;      // web UI basic-auth password (user "admin")

    // Station identity. Empty callsign = transmit inhibited.
    std::string callsign;       // M17 source address, 3..9 chars

    // Reflector
    std::string reflector = DEFAULT_REFLECTOR;
    char reflectorModule = 'A';
    std::string extraHosts;     // user entries, see parseHostList()
    bool autoLink = false;
    bool listenOnly = false;    // link with LSTN: receive only, never transmit

    // Audio / UI
    PttMode pttMode = PttMode::Momentary;
    uint8_t volume = 70;        // 0..100 %
    // Mic gain in dB, as in scheff-zello: the ES7243E's analogue PGA up to its
    // 37.5 dB maximum, with anything above that added digitally.
    int8_t micGainDb = 36;      // MIC_GAIN_MIN_DB..MIC_GAIN_MAX_DB
};

constexpr int MIC_GAIN_MIN_DB = 0;
constexpr int MIC_GAIN_MAX_DB = 42;
constexpr size_t ADMIN_PASS_MIN = 8;
constexpr size_t CALLSIGN_MAX = 9;   // M17 address: 9 base-40 characters

// Each validator normalises `in` (trim, upper-case where relevant) into `out`
// and returns an empty string on success, or a human-readable error.
std::string normalizeCallsign(const std::string &in, std::string &out);
std::string normalizeModule(const std::string &in, char &out);
std::string normalizeReflector(const std::string &in, std::string &out);

// Extra hosts: one "M17-XXX host [port]" per line; blank lines and lines
// starting with '#' are ignored. Port defaults to 17000. Returns "" on
// success, else the first error with its line number.
std::string parseHostList(const std::string &text, std::vector<HostEntry> &out);

// Validate and normalise every user-editable field (Wi-Fi/admin excluded),
// including that the selected reflector is in the built-in or extra list.
// On error, `s` may be partially normalised; callers should discard it.
std::string validate(Settings &s);

// The firmware refuses to transmit until a valid callsign is configured, and
// never transmits in listen-only mode.
bool canTransmit(const Settings &s);

}  // namespace cfg
