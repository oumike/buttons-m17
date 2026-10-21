#include "m17/callsign.h"

namespace m17 {

namespace {

constexpr const char *ALPHABET = " ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789-/.";

int charValue(char c) {
    if (c == ' ') return 0;
    if (c >= 'A' && c <= 'Z') return c - 'A' + 1;
    if (c >= 'a' && c <= 'z') return c - 'a' + 1;
    if (c >= '0' && c <= '9') return c - '0' + 27;
    if (c == '-') return 37;
    if (c == '/') return 38;
    if (c == '.') return 39;
    return -1;
}

void store(uint64_t v, Address &out) {
    for (int i = 5; i >= 0; --i) {
        out[i] = static_cast<uint8_t>(v & 0xFF);
        v >>= 8;
    }
}

std::string base40(uint64_t v) {
    std::string s;
    while (v) {
        s += ALPHABET[v % 40];
        v /= 40;
    }
    return s;
}

}  // namespace

bool encodeCallsign(const std::string &callsign, Address &out) {
    if (callsign == "@ALL") {
        store(ADDR_BROADCAST, out);
        return true;
    }
    if (callsign.empty() || callsign.size() > CALLSIGN_CHARS) return false;
    const bool hash = callsign[0] == '#';
    const std::string body = hash ? callsign.substr(1) : callsign;
    uint64_t v = 0;
    for (auto it = body.rbegin(); it != body.rend(); ++it) {
        const int cv = charValue(*it);
        if (cv < 0) return false;
        v = v * 40 + static_cast<uint64_t>(cv);
    }
    if (v == 0) return false;   // all spaces: reserved address
    if (hash) v += ADDR_HASH_BASE;
    store(v, out);
    return true;
}

uint64_t addressValue(const uint8_t *addr) {
    uint64_t v = 0;
    for (size_t i = 0; i < ADDR_LEN; ++i) v = (v << 8) | addr[i];
    return v;
}

std::string decodeCallsign(const uint8_t *addr) {
    const uint64_t v = addressValue(addr);
    if (v == ADDR_BROADCAST) return "@ALL";
    if (v >= ADDR_HASH_BASE + ADDR_HASH_SPAN) return "";   // reserved
    // mrefd callsign.cpp prints the '#' range without subtracting the base;
    // libm17 call.c (decode_callsign_value) subtracts it. We follow libm17,
    // which round-trips with its encoder.
    if (v >= ADDR_HASH_BASE) return "#" + base40(v - ADDR_HASH_BASE);
    return base40(v);
}

std::string reflectorDestination(const std::string &reflector, char module) {
    std::string d = reflector.substr(0, 8);
    d.resize(8, ' ');
    d += module;
    return d;
}

}  // namespace m17
