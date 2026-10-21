#pragma once
// M17 constants. Sources:
//   M17 specification (M17-Project/M17_spec): LSF, TYPE field, META text,
//     CRC, Address Encoding appendix.
//   n7tae/mrefd Packet-Description.md, packet.cpp, protocol.cpp: the UDP
//     reflector protocol (no longer part of the M17 spec since v1.5.0).

#include <array>
#include <cstddef>
#include <cstdint>

namespace m17 {

constexpr size_t ADDR_LEN = 6;
constexpr size_t META_LEN = 14;
constexpr size_t PAYLOAD_LEN = 16;          // two 8-byte Codec2 3200 frames
constexpr size_t CODEC2_FRAME_LEN = 8;      // 20 ms of speech at 3200 bps
constexpr uint32_t PACKET_INTERVAL_MS = 40; // one stream packet = 2 x 20 ms

using Address = std::array<uint8_t, ADDR_LEN>;
using Meta = std::array<uint8_t, META_LEN>;
using Payload = std::array<uint8_t, PAYLOAD_LEN>;

// Address space (spec, "Encoded Addresses").
constexpr uint64_t ADDR_MAX_STANDARD = 0xEE6B27FFFFFFull;   // "........."
constexpr uint64_t ADDR_HASH_BASE = 0xEE6B28000000ull;      // 40^9: "#" range
constexpr uint64_t ADDR_HASH_SPAN = 0x05F5E1000000ull;      // 40^8 (libm17 U40_9_8)
constexpr uint64_t ADDR_BROADCAST = 0xFFFFFFFFFFFFull;      // "@ALL"
constexpr size_t CALLSIGN_CHARS = 9;

// Stream packet over UDP (mrefd packet.cpp offsets).
//   0  magic "M17 "     4  stream id (BE)   6  DST      12 SRC
//   18 TYPE (BE)        20 META (14)        34 frame number (BE, bit15=last)
//   36 payload (16)     52 CRC-16 over bytes 0..51 (BE)
constexpr size_t STREAM_PACKET_LEN = 54;
// mrefd also accepts 55 bytes (one trailing byte) from clients; we accept it
// on receive and ignore the extra byte.
constexpr size_t STREAM_PACKET_LEN_ALT = 55;
constexpr size_t OFF_SID = 4, OFF_DST = 6, OFF_SRC = 12, OFF_TYPE = 18, OFF_META = 20,
                 OFF_FN = 34, OFF_PAYLOAD = 36, OFF_CRC = 52;
constexpr uint16_t FN_LAST = 0x8000;
constexpr uint16_t FN_MASK = 0x7FFF;

// Control packets (mrefd Packet-Description.md).
constexpr size_t CTRL_MAGIC_LEN = 4;
constexpr size_t CTRL_CONN_LEN = 11;   // "CONN"/"LSTN" + callsign + module
constexpr size_t CTRL_CALL_LEN = 10;   // "PING"/"PONG"/"DISC" + callsign
constexpr size_t CTRL_BARE_LEN = 4;    // "ACKN"/"NACK"/"DISC"

constexpr uint16_t DEFAULT_PORT = 17000;
// mrefd sends a keepalive about every 3 s; after 30 s of silence a node may
// assume the other end has gone.
constexpr uint32_t KEEPALIVE_TIMEOUT_MS = 30000;

// LSF TYPE field, stream mode (spec, "Stream Mode / TYPE Field"):
//   bit 0      packet(0)/stream(1)
//   bits 1-2   data type: 01 data, 10 voice, 11 voice+data
//   bits 3-4   encryption type: 00 none, 01 scrambler, 10 AES
//   bits 5-6   encryption subtype (null encryption: 00 text, 01 GNSS,
//              10 extended callsign)
//   bits 7-10  channel access number (CAN)
//   bit 11     signed stream
constexpr uint16_t TYPE_STREAM = 0x0001;
constexpr uint16_t TYPE_DATATYPE_MASK = 0x0006;
constexpr uint16_t TYPE_DATATYPE_VOICE = 0x0004;
constexpr uint16_t TYPE_DATATYPE_VOICE_DATA = 0x0006;
constexpr uint16_t TYPE_ENC_MASK = 0x0018;
constexpr uint16_t TYPE_ENC_SUBTYPE_MASK = 0x0060;
constexpr uint16_t TYPE_ENC_SUBTYPE_TEXT = 0x0000;
constexpr uint16_t TYPE_CAN_SHIFT = 7;
constexpr uint16_t TYPE_CAN_MASK = 0x0780;

// Voice-only Codec2 3200, no encryption, META carries text, given CAN.
constexpr uint16_t voiceStreamType(uint8_t can = 0) {
    return static_cast<uint16_t>(TYPE_STREAM | TYPE_DATATYPE_VOICE | TYPE_ENC_SUBTYPE_TEXT |
                                 ((can & 0x0F) << TYPE_CAN_SHIFT));
}

// META text (spec, "Text Data"): up to 4 blocks of 13 bytes.
constexpr size_t TEXT_BLOCK_LEN = 13;
constexpr size_t TEXT_MAX_BLOCKS = 4;
constexpr size_t TEXT_MAX_LEN = TEXT_BLOCK_LEN * TEXT_MAX_BLOCKS;   // 52

// mrefd's "quiet" Codec2 3200 payload (packet.cpp), used to fill gaps.
constexpr Payload SILENCE_PAYLOAD = {0x01, 0x00, 0x09, 0x43, 0x9c, 0xe4, 0x21, 0x08,
                                     0x01, 0x00, 0x09, 0x43, 0x9c, 0xe4, 0x21, 0x08};

}  // namespace m17
