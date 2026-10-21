// Host tests for the M17 core library. Vectors come from the M17 spec (CRC
// test vectors, the AB1CD address example) and mrefd (packet layout, quiet
// payload). Run: pio test -e native
#include <unity.h>

#include <cstring>
#include <string>

#include "m17/callsign.h"
#include "m17/crc.h"
#include "m17/meta_text.h"
#include "m17/packets.h"
#include "m17/stream.h"

using namespace m17;

void setUp() {}
void tearDown() {}

namespace {
Address addr(const char *cs) {
    Address a{};
    TEST_ASSERT_TRUE_MESSAGE(encodeCallsign(cs, a), cs);
    return a;
}
}  // namespace

// ── CRC (spec Table "CRC Test Vectors") ──────────────────────────────────────

void test_crc_spec_vectors() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, crc16(nullptr, 0));
    TEST_ASSERT_EQUAL_HEX16(0x206E, crc16(reinterpret_cast<const uint8_t *>("A"), 1));
    TEST_ASSERT_EQUAL_HEX16(0x772B, crc16(reinterpret_cast<const uint8_t *>("123456789"), 9));
    uint8_t all[256];
    for (int i = 0; i < 256; ++i) all[i] = static_cast<uint8_t>(i);
    TEST_ASSERT_EQUAL_HEX16(0x1C31, crc16(all, sizeof(all)));
}

void test_crc_over_data_and_crc_is_zero() {
    // Spec: a CRC over the whole LSF including a valid CRC field equals zero.
    uint8_t lsf[30] = {};
    for (int i = 0; i < 28; ++i) lsf[i] = static_cast<uint8_t>(i * 7 + 3);
    const uint16_t c = crc16(lsf, 28);
    lsf[28] = c >> 8;
    lsf[29] = c & 0xFF;
    TEST_ASSERT_EQUAL_HEX16(0x0000, crc16(lsf, 30));
}

// ── Address encoding (spec appendix) ─────────────────────────────────────────

void test_callsign_spec_example() {
    const Address a = addr("AB1CD");
    TEST_ASSERT_EQUAL_HEX64(0x9FDD51, addressValue(a.data()));   // 10476881
    TEST_ASSERT_EQUAL_STRING("AB1CD", decodeCallsign(a).c_str());
}

void test_callsign_edges() {
    TEST_ASSERT_EQUAL_HEX64(0x1, addressValue(addr("A").data()));
    TEST_ASSERT_EQUAL_HEX64(0xEE6B27FFFFFF, addressValue(addr(".........").data()));
    // Trailing spaces do not change the address; lower case is upper-cased.
    TEST_ASSERT_EQUAL_HEX64(addressValue(addr("ABC").data()), addressValue(addr("abc      ").data()));
    TEST_ASSERT_EQUAL_STRING("K8UNS/P", decodeCallsign(addr("K8UNS/P")).c_str());
    TEST_ASSERT_EQUAL_STRING("M17-M17 C", decodeCallsign(addr("M17-M17 C")).c_str());
}

void test_callsign_rejects() {
    Address a{};
    TEST_ASSERT_FALSE(encodeCallsign("", a));
    TEST_ASSERT_FALSE(encodeCallsign("   ", a));          // reserved address 0
    TEST_ASSERT_FALSE(encodeCallsign("ABCDEFGHIJ", a));   // 10 chars
    TEST_ASSERT_FALSE(encodeCallsign("K8UNS!", a));
    TEST_ASSERT_FALSE(encodeCallsign("K8_UNS", a));
}

void test_callsign_special_ranges() {
    const Address b = addr("@ALL");
    TEST_ASSERT_EQUAL_HEX64(0xFFFFFFFFFFFF, addressValue(b.data()));
    TEST_ASSERT_EQUAL_STRING("@ALL", decodeCallsign(b).c_str());

    const Address h = addr("#ECHO");
    TEST_ASSERT_EQUAL_HEX64(ADDR_HASH_BASE + addressValue(addr("ECHO").data()), addressValue(h.data()));
    TEST_ASSERT_EQUAL_STRING("#ECHO", decodeCallsign(h).c_str());

    const Address zero{};
    TEST_ASSERT_EQUAL_STRING("", decodeCallsign(zero).c_str());
    const Address reserved = {0xF4, 0x61, 0x09, 0x00, 0x00, 0x00};   // first past the # range
    TEST_ASSERT_EQUAL_STRING("", decodeCallsign(reserved).c_str());
}

void test_reflector_destination() {
    TEST_ASSERT_EQUAL_STRING("M17-M17 C", reflectorDestination("M17-M17", 'C').c_str());
    TEST_ASSERT_EQUAL_STRING("M17-USA A", reflectorDestination("M17-USA", 'A').c_str());
}

// ── Control packets (mrefd Packet-Description.md) ───────────────────────────

void test_build_control_packets() {
    const Address me = addr("N0CALL");
    uint8_t buf[16];

    TEST_ASSERT_EQUAL(11, buildConnect(buf, me, 'C', false));
    TEST_ASSERT_EQUAL_MEMORY("CONN", buf, 4);
    TEST_ASSERT_EQUAL_MEMORY(me.data(), buf + 4, 6);
    TEST_ASSERT_EQUAL('C', buf[10]);

    TEST_ASSERT_EQUAL(11, buildConnect(buf, me, 'A', true));
    TEST_ASSERT_EQUAL_MEMORY("LSTN", buf, 4);

    TEST_ASSERT_EQUAL(10, buildPong(buf, me));
    TEST_ASSERT_EQUAL_MEMORY("PONG", buf, 4);
    TEST_ASSERT_EQUAL_MEMORY(me.data(), buf + 4, 6);

    TEST_ASSERT_EQUAL(10, buildDisconnect(buf, me));
    TEST_ASSERT_EQUAL_MEMORY("DISC", buf, 4);
}

void test_parse_control_packets() {
    TEST_ASSERT_TRUE(parseControl(reinterpret_cast<const uint8_t *>("ACKN"), 4).kind == Control::Ackn);
    TEST_ASSERT_TRUE(parseControl(reinterpret_cast<const uint8_t *>("NACK"), 4).kind == Control::Nack);
    const ControlPacket d4 = parseControl(reinterpret_cast<const uint8_t *>("DISC"), 4);
    TEST_ASSERT_TRUE(d4.kind == Control::Disc);
    TEST_ASSERT_FALSE(d4.hasCallsign);

    const Address refl = addr("M17-M17");
    uint8_t ping[10];
    std::memcpy(ping, "PING", 4);
    std::memcpy(ping + 4, refl.data(), 6);
    const ControlPacket p = parseControl(ping, 10);
    TEST_ASSERT_TRUE(p.kind == Control::Ping);
    TEST_ASSERT_TRUE(p.hasCallsign);
    TEST_ASSERT_EQUAL_STRING("M17-M17", decodeCallsign(p.callsign).c_str());

    uint8_t conn[11];
    buildConnect(conn, addr("N0CALL"), 'B', false);
    const ControlPacket c = parseControl(conn, 11);
    TEST_ASSERT_TRUE(c.kind == Control::Conn);
    TEST_ASSERT_EQUAL('B', c.module);

    // Wrong lengths and unknown magic are not control packets.
    TEST_ASSERT_TRUE(parseControl(ping, 9).kind == Control::None);
    TEST_ASSERT_TRUE(parseControl(reinterpret_cast<const uint8_t *>("ACKNx"), 5).kind == Control::None);
    TEST_ASSERT_TRUE(parseControl(reinterpret_cast<const uint8_t *>("HELO"), 4).kind == Control::None);
    TEST_ASSERT_TRUE(parseControl(nullptr, 0).kind == Control::None);
}

// ── Stream packets ───────────────────────────────────────────────────────────

void test_voice_stream_type() {
    // Stream bit + data type 10 (voice), no encryption, text subtype, CAN 0.
    TEST_ASSERT_EQUAL_HEX16(0x0005, voiceStreamType());
    TEST_ASSERT_EQUAL_HEX16(0x0785, voiceStreamType(15));
}

StreamPacket samplePacket() {
    StreamPacket p;
    p.streamId = 0xBEEF;
    p.dst = addr("M17-M17 C");
    p.src = addr("N0CALL");
    p.frameNumber = 0x1234;
    p.payload = SILENCE_PAYLOAD;
    p.meta[0] = 0x11;
    return p;
}

void test_stream_build_layout() {
    uint8_t b[STREAM_PACKET_LEN];
    buildStream(samplePacket(), b);
    TEST_ASSERT_EQUAL_MEMORY("M17 ", b, 4);
    TEST_ASSERT_EQUAL_HEX8(0xBE, b[4]);
    TEST_ASSERT_EQUAL_HEX8(0xEF, b[5]);
    TEST_ASSERT_EQUAL_MEMORY(addr("M17-M17 C").data(), b + 6, 6);
    TEST_ASSERT_EQUAL_MEMORY(addr("N0CALL").data(), b + 12, 6);
    TEST_ASSERT_EQUAL_HEX8(0x00, b[18]);
    TEST_ASSERT_EQUAL_HEX8(0x05, b[19]);   // mrefd checks bit 0 of byte 19
    TEST_ASSERT_EQUAL_HEX8(0x11, b[20]);
    TEST_ASSERT_EQUAL_HEX8(0x12, b[34]);
    TEST_ASSERT_EQUAL_HEX8(0x34, b[35]);
    TEST_ASSERT_EQUAL_MEMORY(SILENCE_PAYLOAD.data(), b + 36, 16);
    const uint16_t crc = crc16(b, 52);
    TEST_ASSERT_EQUAL_HEX8(crc >> 8, b[52]);
    TEST_ASSERT_EQUAL_HEX8(crc & 0xFF, b[53]);
}

void test_stream_round_trip_and_last_bit() {
    StreamPacket in = samplePacket();
    in.last = true;
    in.frameNumber = 0x7FFF;
    uint8_t b[STREAM_PACKET_LEN_ALT] = {};
    buildStream(in, b);
    TEST_ASSERT_EQUAL_HEX8(0xFF, b[34]);   // 0x8000 | 0x7FFF

    StreamPacket out;
    TEST_ASSERT_TRUE(parseStream(b, STREAM_PACKET_LEN, out) == StreamParse::Ok);
    TEST_ASSERT_EQUAL_HEX16(0xBEEF, out.streamId);
    TEST_ASSERT_EQUAL_HEX16(0x7FFF, out.frameNumber);
    TEST_ASSERT_TRUE(out.last);
    TEST_ASSERT_EQUAL_STRING("N0CALL", decodeCallsign(out.src).c_str());
    TEST_ASSERT_EQUAL_MEMORY(in.payload.data(), out.payload.data(), 16);
    // 55-byte variant accepted.
    TEST_ASSERT_TRUE(parseStream(b, STREAM_PACKET_LEN_ALT, out) == StreamParse::Ok);
}

void test_stream_parse_rejects() {
    uint8_t b[STREAM_PACKET_LEN];
    buildStream(samplePacket(), b);
    StreamPacket out;
    TEST_ASSERT_TRUE(parseStream(b, 53, out) == StreamParse::BadLength);
    TEST_ASSERT_TRUE(parseStream(reinterpret_cast<const uint8_t *>("PING"), 4, out) == StreamParse::NotStream);
    b[40] ^= 0x01;
    TEST_ASSERT_TRUE(parseStream(b, STREAM_PACKET_LEN, out) == StreamParse::BadCrc);
    b[40] ^= 0x01;
    StreamPacket pk = samplePacket();
    pk.type = 0x0004;   // packet-mode bit clear
    buildStream(pk, b);
    TEST_ASSERT_TRUE(parseStream(b, STREAM_PACKET_LEN, out) == StreamParse::NotStreamMode);
}

// ── META text ────────────────────────────────────────────────────────────────

void test_meta_text_encode_blocks() {
    MetaTextEncoder e;
    e.set("");
    TEST_ASSERT_EQUAL(0, e.blockCount());
    TEST_ASSERT_EQUAL_HEX8(0x00, e.block(0)[0]);

    e.set("Hello M17");                     // one block
    TEST_ASSERT_EQUAL(1, e.blockCount());
    Meta m = e.block(0);
    TEST_ASSERT_EQUAL_HEX8(0x11, m[0]);
    TEST_ASSERT_EQUAL_MEMORY("Hello M17    ", m.data() + 1, 13);   // space padded

    e.set("0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ");   // 36 chars = 3 blocks
    TEST_ASSERT_EQUAL(3, e.blockCount());
    TEST_ASSERT_EQUAL_HEX8(0x71, e.block(0)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x72, e.block(1)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x74, e.block(2)[0]);
    TEST_ASSERT_EQUAL_HEX8(0x71, e.block(3)[0]);     // wraps
    TEST_ASSERT_EQUAL_MEMORY("QRSTUVWXYZ   ", e.block(2).data() + 1, 13);
}

void test_meta_text_truncates_on_utf8_boundary() {
    MetaTextEncoder e;
    std::string s(51, 'x');
    s += "\xC3\xA9";   // 'é' straddles byte 52
    e.set(s);
    TEST_ASSERT_EQUAL(4, e.blockCount());
    TEST_ASSERT_EQUAL_HEX8(0xF8, e.block(3)[0]);
    TEST_ASSERT_EQUAL_HEX8(' ', e.block(3)[13]);   // lead byte dropped, padded

    MetaTextDecoder d;
    for (int i = 0; i < 4; ++i) d.feed(voiceStreamType(), e.block(i));
    TEST_ASSERT_EQUAL_STRING(std::string(51, 'x').c_str(), d.text().c_str());
}

void test_meta_text_decode_any_order() {
    MetaTextEncoder e;
    e.set("Livonia ARC via M17 internet");   // 28 chars = 3 blocks
    MetaTextDecoder d;
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), e.block(2)));
    TEST_ASSERT_FALSE(d.complete());
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), e.block(0)));
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), e.block(0)));   // repeat
    TEST_ASSERT_TRUE(d.feed(voiceStreamType(), e.block(1)));    // completes
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), e.block(2)));   // already complete
    TEST_ASSERT_TRUE(d.complete());
    TEST_ASSERT_EQUAL_STRING("Livonia ARC via M17 internet", d.text().c_str());
}

void test_meta_text_decode_ignores_other_meta() {
    MetaTextEncoder e;
    e.set("hi");
    MetaTextDecoder d;
    const uint16_t gnss = voiceStreamType() | 0x0020;    // subtype 01
    TEST_ASSERT_FALSE(d.feed(gnss, e.block(0)));
    const uint16_t aes = voiceStreamType() | 0x0010;     // encrypted
    TEST_ASSERT_FALSE(d.feed(aes, e.block(0)));
    Meta bad{};
    bad[0] = 0x12;                                       // block 2 of a 1-block message
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), bad));
    bad[0] = 0x00;
    TEST_ASSERT_FALSE(d.feed(voiceStreamType(), bad));
    TEST_ASSERT_FALSE(d.complete());
    TEST_ASSERT_TRUE(d.feed(voiceStreamType(), e.block(0)));
    TEST_ASSERT_EQUAL_STRING("hi", d.text().c_str());
}

void test_meta_text_new_message_resets() {
    MetaTextEncoder a, b;
    a.set(std::string(20, 'a'));   // 2 blocks
    b.set("short");                // 1 block
    MetaTextDecoder d;
    d.feed(voiceStreamType(), a.block(0));
    TEST_ASSERT_TRUE(d.feed(voiceStreamType(), b.block(0)));
    TEST_ASSERT_EQUAL_STRING("short", d.text().c_str());
}

// ── Stream tracker ───────────────────────────────────────────────────────────

StreamPacket pkt(uint16_t sid, uint16_t fn, bool last = false) {
    StreamPacket p = samplePacket();
    p.streamId = sid;
    p.frameNumber = fn;
    p.last = last;
    return p;
}

void test_tracker_clean_stream() {
    StreamTracker t;
    TEST_ASSERT_TRUE(t.onPacket(pkt(1, 0), 1000) == StreamEvent::Started);
    TEST_ASSERT_EQUAL_STRING("N0CALL", t.stats().src.c_str());
    TEST_ASSERT_EQUAL_STRING("M17-M17 C", t.stats().dst.c_str());
    for (uint16_t fn = 1; fn < 10; ++fn)
        TEST_ASSERT_TRUE(t.onPacket(pkt(1, fn), 1000 + 40 * fn) == StreamEvent::Continued);
    TEST_ASSERT_TRUE(t.onPacket(pkt(1, 10, true), 1400) == StreamEvent::Ended);
    TEST_ASSERT_FALSE(t.active());
    TEST_ASSERT_EQUAL(11, t.stats().frames);
    TEST_ASSERT_EQUAL(0, t.stats().lost);
    TEST_ASSERT_EQUAL(0, t.stats().late);
    // A straggler of the ended stream doesn't restart it.
    TEST_ASSERT_TRUE(t.onPacket(pkt(1, 9), 1420) == StreamEvent::Ignored);
}

void test_tracker_loss_late_and_wrap() {
    StreamTracker t;
    t.onPacket(pkt(7, 0x7FFD), 0);
    TEST_ASSERT_TRUE(t.onPacket(pkt(7, 0x7FFE), 40) == StreamEvent::Continued);
    TEST_ASSERT_TRUE(t.onPacket(pkt(7, 0x0001), 120) == StreamEvent::Continued);   // lost 7FFF, 0000
    TEST_ASSERT_EQUAL(2, t.stats().lost);
    TEST_ASSERT_TRUE(t.onPacket(pkt(7, 0x7FFF), 130) == StreamEvent::Ignored);     // late
    TEST_ASSERT_TRUE(t.onPacket(pkt(7, 0x0001), 140) == StreamEvent::Ignored);     // duplicate
    TEST_ASSERT_EQUAL(2, t.stats().late);
    TEST_ASSERT_EQUAL(3, t.stats().frames);
}

void test_tracker_timeout_and_second_talker() {
    StreamTracker t;
    t.onPacket(pkt(1, 0), 0);
    TEST_ASSERT_TRUE(t.onPacket(pkt(2, 0), 100) == StreamEvent::Ignored);   // other talker blocked
    TEST_ASSERT_FALSE(t.poll(400));
    TEST_ASSERT_TRUE(t.poll(600));
    TEST_ASSERT_TRUE(t.stats().endedByTimeout);
    TEST_ASSERT_FALSE(t.poll(700));                                         // reported once
    TEST_ASSERT_TRUE(t.onPacket(pkt(2, 50), 700) == StreamEvent::Started);  // joined mid-stream
    TEST_ASSERT_EQUAL_HEX16(2, t.stats().streamId);
}

void test_tracker_single_packet_stream() {
    StreamTracker t;
    TEST_ASSERT_TRUE(t.onPacket(pkt(3, 0, true), 0) == StreamEvent::Ended);
    TEST_ASSERT_EQUAL(1, t.stats().frames);
    TEST_ASSERT_FALSE(t.active());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_spec_vectors);
    RUN_TEST(test_crc_over_data_and_crc_is_zero);
    RUN_TEST(test_callsign_spec_example);
    RUN_TEST(test_callsign_edges);
    RUN_TEST(test_callsign_rejects);
    RUN_TEST(test_callsign_special_ranges);
    RUN_TEST(test_reflector_destination);
    RUN_TEST(test_build_control_packets);
    RUN_TEST(test_parse_control_packets);
    RUN_TEST(test_voice_stream_type);
    RUN_TEST(test_stream_build_layout);
    RUN_TEST(test_stream_round_trip_and_last_bit);
    RUN_TEST(test_stream_parse_rejects);
    RUN_TEST(test_meta_text_encode_blocks);
    RUN_TEST(test_meta_text_truncates_on_utf8_boundary);
    RUN_TEST(test_meta_text_decode_any_order);
    RUN_TEST(test_meta_text_decode_ignores_other_meta);
    RUN_TEST(test_meta_text_new_message_resets);
    RUN_TEST(test_tracker_clean_stream);
    RUN_TEST(test_tracker_loss_late_and_wrap);
    RUN_TEST(test_tracker_timeout_and_second_talker);
    RUN_TEST(test_tracker_single_packet_stream);
    return UNITY_END();
}
