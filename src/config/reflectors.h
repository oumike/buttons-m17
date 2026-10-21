#pragma once
// M17 reflector directory: a built-in list (generated, see
// tools/gen_reflectors.py) plus the user's own entries, which take priority.
// Pure C++, unit-tested on the host.

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cfg {

constexpr uint16_t M17_DEFAULT_PORT = 17000;  // mrefd example.cfg

struct ReflectorEntry {
    const char *name;   // "M17-XXX"
    const char *host;   // hostname or dotted IPv4
    uint16_t port;
};

extern const ReflectorEntry BUILTIN_REFLECTORS[];
extern const size_t BUILTIN_REFLECTOR_COUNT;
extern const char *const BUILTIN_REFLECTORS_DATE;

struct HostEntry {
    std::string name;
    std::string host;
    uint16_t port = M17_DEFAULT_PORT;
};

// User entries first, then built-in. Returns false if the name is unknown.
bool findReflector(const std::string &name, const std::vector<HostEntry> &extra,
                   HostEntry &out);

}  // namespace cfg
