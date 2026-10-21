#pragma once
// META "Text Data" (spec, Stream Mode / Null Encryption / Text Data): a
// message of up to 52 bytes split into up to four 13-byte blocks. Each META
// field is a control byte plus one block:
//   control high nibble = bitmap of blocks in the message (0001,0011,0111,1111)
//   control low nibble  = this block's bit (0001,0010,0100,1000)
// A receiver ORs control bytes together; the message is complete when the
// two nibbles match. Control byte 0x00 = no text.

#include <string>

#include "m17/m17_defs.h"

namespace m17 {

class MetaTextEncoder {
public:
    // Longer messages are truncated to 52 bytes, backing off to a UTF-8
    // character boundary. Unused space in the last block is space-filled.
    void set(const std::string &message);
    size_t blockCount() const { return blocks_; }
    // META for block i (taken modulo blockCount); all zero when no text.
    Meta block(size_t i) const;

private:
    std::string text_;
    size_t blocks_ = 0;
};

class MetaTextDecoder {
public:
    void reset();
    // Feed the META of a stream packet together with its TYPE. Ignored unless
    // the stream is unencrypted with the text subtype. Returns true when this
    // call completed the message.
    bool feed(uint16_t type, const Meta &meta);
    bool complete() const;
    // Text received so far (missing blocks are blank), trailing spaces and
    // NULs removed.
    std::string text() const;

private:
    uint8_t seen_ = 0;      // OR of control bytes
    char blocks_[TEXT_MAX_BLOCKS][TEXT_BLOCK_LEN] = {};
};

}  // namespace m17
