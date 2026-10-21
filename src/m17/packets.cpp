#include "m17/packets.h"

#include <cstring>

#include "m17/crc.h"

namespace m17 {

namespace {

void put16(uint8_t *p, uint16_t v) {
    p[0] = static_cast<uint8_t>(v >> 8);
    p[1] = static_cast<uint8_t>(v & 0xFF);
}

uint16_t get16(const uint8_t *p) { return static_cast<uint16_t>((p[0] << 8) | p[1]); }

size_t magicAndCall(uint8_t *out, const char *magic, const Address &a) {
    std::memcpy(out, magic, CTRL_MAGIC_LEN);
    std::memcpy(out + CTRL_MAGIC_LEN, a.data(), ADDR_LEN);
    return CTRL_CALL_LEN;
}

bool magicIs(const uint8_t *buf, const char *m) { return std::memcmp(buf, m, CTRL_MAGIC_LEN) == 0; }

}  // namespace

size_t buildConnect(uint8_t *out, const Address &self, char module, bool listenOnly) {
    magicAndCall(out, listenOnly ? "LSTN" : "CONN", self);
    out[CTRL_CALL_LEN] = static_cast<uint8_t>(module);
    return CTRL_CONN_LEN;
}

size_t buildDisconnect(uint8_t *out, const Address &self) { return magicAndCall(out, "DISC", self); }

size_t buildPong(uint8_t *out, const Address &self) { return magicAndCall(out, "PONG", self); }

ControlPacket parseControl(const uint8_t *buf, size_t len) {
    ControlPacket c;
    if (!buf || len < CTRL_MAGIC_LEN) return c;

    if (len == CTRL_BARE_LEN) {
        if (magicIs(buf, "ACKN")) c.kind = Control::Ackn;
        else if (magicIs(buf, "NACK")) c.kind = Control::Nack;
        else if (magicIs(buf, "DISC")) c.kind = Control::Disc;
        return c;
    }
    if (len == CTRL_CALL_LEN) {
        if (magicIs(buf, "PING")) c.kind = Control::Ping;
        else if (magicIs(buf, "PONG")) c.kind = Control::Pong;
        else if (magicIs(buf, "DISC")) c.kind = Control::Disc;
        else if (magicIs(buf, "NACK")) c.kind = Control::Nack;   // mrefd parses a 10-byte NACK too
        else return c;
        c.hasCallsign = true;
        std::memcpy(c.callsign.data(), buf + CTRL_MAGIC_LEN, ADDR_LEN);
        return c;
    }
    if (len == CTRL_CONN_LEN && (magicIs(buf, "CONN") || magicIs(buf, "LSTN"))) {
        c.kind = magicIs(buf, "CONN") ? Control::Conn : Control::Lstn;
        c.hasCallsign = true;
        std::memcpy(c.callsign.data(), buf + CTRL_MAGIC_LEN, ADDR_LEN);
        c.module = static_cast<char>(buf[CTRL_CALL_LEN]);
    }
    return c;
}

void buildStream(const StreamPacket &p, uint8_t *out) {
    std::memcpy(out, "M17 ", 4);
    put16(out + OFF_SID, p.streamId);
    std::memcpy(out + OFF_DST, p.dst.data(), ADDR_LEN);
    std::memcpy(out + OFF_SRC, p.src.data(), ADDR_LEN);
    put16(out + OFF_TYPE, p.type);
    std::memcpy(out + OFF_META, p.meta.data(), META_LEN);
    put16(out + OFF_FN, static_cast<uint16_t>((p.frameNumber & FN_MASK) | (p.last ? FN_LAST : 0)));
    std::memcpy(out + OFF_PAYLOAD, p.payload.data(), PAYLOAD_LEN);
    put16(out + OFF_CRC, crc16(out, OFF_CRC));
}

StreamParse parseStream(const uint8_t *buf, size_t len, StreamPacket &out) {
    if (!buf || len < 4 || std::memcmp(buf, "M17 ", 4) != 0) return StreamParse::NotStream;
    if (len != STREAM_PACKET_LEN && len != STREAM_PACKET_LEN_ALT) return StreamParse::BadLength;
    if (crc16(buf, OFF_CRC) != get16(buf + OFF_CRC)) return StreamParse::BadCrc;
    const uint16_t type = get16(buf + OFF_TYPE);
    if (!(type & TYPE_STREAM)) return StreamParse::NotStreamMode;

    out.streamId = get16(buf + OFF_SID);
    std::memcpy(out.dst.data(), buf + OFF_DST, ADDR_LEN);
    std::memcpy(out.src.data(), buf + OFF_SRC, ADDR_LEN);
    out.type = type;
    std::memcpy(out.meta.data(), buf + OFF_META, META_LEN);
    const uint16_t fn = get16(buf + OFF_FN);
    out.frameNumber = fn & FN_MASK;
    out.last = (fn & FN_LAST) != 0;
    std::memcpy(out.payload.data(), buf + OFF_PAYLOAD, PAYLOAD_LEN);
    return StreamParse::Ok;
}

const char *controlName(Control c) {
    switch (c) {
        case Control::None: return "none";
        case Control::Conn: return "CONN";
        case Control::Lstn: return "LSTN";
        case Control::Ackn: return "ACKN";
        case Control::Nack: return "NACK";
        case Control::Disc: return "DISC";
        case Control::Ping: return "PING";
        case Control::Pong: return "PONG";
    }
    return "?";
}

}  // namespace m17
