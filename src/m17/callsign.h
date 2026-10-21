#pragma once
// M17 base-40 address encoding (spec, "Address Encoding" appendix).
// Alphabet by value: ' ' A-Z 0-9 - / .  The first character lands in the
// least significant digit; the 48-bit result is stored big-endian.

#include <string>

#include "m17/m17_defs.h"

namespace m17 {

// Strict encoder: upper-cases a-z, rejects characters outside the alphabet
// (the spec's example encoder maps them to space; we refuse instead), more
// than 9 characters, and an all-space/empty string (address 0 is reserved).
// "@ALL" encodes to broadcast; a leading '#' selects the hash range (40^9 +
// value of the rest), as in libm17. Returns false on error.
bool encodeCallsign(const std::string &callsign, Address &out);

// Decoder. Standard addresses decode to their text (trailing spaces vanish by
// construction), broadcast to "@ALL", the hash range [40^9, 40^9+40^8) to
// "#" + text, and reserved addresses (0, and above the hash range) to "".
std::string decodeCallsign(const uint8_t *addr);
inline std::string decodeCallsign(const Address &a) { return decodeCallsign(a.data()); }

uint64_t addressValue(const uint8_t *addr);

// DST used when talking through a reflector: the 7-character reflector name,
// space padded to 8, then the module, e.g. "M17-M17 C" (mrefd callsign.cpp
// SetModule; the spec's example "M17-M17 C"). mrefd rewrites such a DST to
// @ALL before forwarding.
std::string reflectorDestination(const std::string &reflector, char module);

}  // namespace m17
