#include "config/reflectors.h"

namespace cfg {

bool findReflector(const std::string &name, const std::vector<HostEntry> &extra,
                   HostEntry &out) {
    for (const auto &e : extra) {
        if (e.name == name) {
            out = e;
            return true;
        }
    }
    for (size_t i = 0; i < BUILTIN_REFLECTOR_COUNT; ++i) {
        const ReflectorEntry &r = BUILTIN_REFLECTORS[i];
        if (name == r.name) {
            out.name = r.name;
            out.host = r.host;
            out.port = r.port;
            return true;
        }
    }
    return false;
}

}  // namespace cfg
