#pragma once
// mrefd UDP packets: control (CONN/LSTN/ACKN/NACK/DISC/PING/PONG) and the
// 54-byte "M17 " stream packet. Pure byte handling, no sockets.

#include <cstddef>
#include <cstdint>

#include "m17/m17_defs.h"

namespace m17 {

enum class Control : uint8_t {
    None,   // not a control packet (or malformed)
    Conn,   // client -> reflector, link request
    Lstn,   // client -> reflector, listen-only link request
    Ackn,   // reflector -> client, link accepted
    Nack,   // reflector -> client, link refused (no module / blacklisted)
    Disc,   // either way; 4 bytes = ack of our DISC, 10 bytes = carries callsign
    Ping,   // keepalive (mrefd sends PING; PING and PONG are equivalent)
    Pong,
};

struct ControlPacket {
    Control kind = Control::None;
    bool hasCallsign = false;
    Address callsign{};       // sender's address, when present
    char module = 0;          // CONN/LSTN only
};

// Builders return the number of bytes written. `out` must hold
// CTRL_CONN_LEN (11) bytes for connect, CTRL_CALL_LEN (10) for the others.
size_t buildConnect(uint8_t *out, const Address &self, char module, bool listenOnly);
size_t buildDisconnect(uint8_t *out, const Address &self);
// Reply to the reflector's keepalive; the callsign is ours (mrefd
// protocol.cpp IsValidKeepAlive matches it against the client regex).
size_t buildPong(uint8_t *out, const Address &self);

// Recognises control packets by magic and exact length. Anything else,
// including stream packets, yields kind == None.
ControlPacket parseControl(const uint8_t *buf, size_t len);

struct StreamPacket {
    uint16_t streamId = 0;
    Address dst{};
    Address src{};
    uint16_t type = voiceStreamType();
    Meta meta{};
    uint16_t frameNumber = 0;   // 0..0x7FFF, without the last-frame bit
    bool last = false;
    Payload payload{};
};

// Writes STREAM_PACKET_LEN bytes and the CRC.
void buildStream(const StreamPacket &p, uint8_t *out);

enum class StreamParse : uint8_t { Ok, NotStream, BadLength, BadCrc, NotStreamMode };

// Accepts 54 bytes (and 55, ignoring the extra trailing byte, as mrefd does).
StreamParse parseStream(const uint8_t *buf, size_t len, StreamPacket &out);

const char *controlName(Control c);

}  // namespace m17
