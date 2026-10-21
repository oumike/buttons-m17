#include "m17/meta_text.h"

#include <cstring>

namespace m17 {

void MetaTextEncoder::set(const std::string &message) {
    std::string t = message;
    if (t.size() > TEXT_MAX_LEN) {
        size_t n = TEXT_MAX_LEN;
        // Don't cut a multi-byte UTF-8 sequence: back off over continuation
        // bytes (10xxxxxx) to the lead byte and drop it as well.
        while (n > 0 && (static_cast<unsigned char>(t[n]) & 0xC0) == 0x80) --n;
        t.resize(n);
    }
    text_ = t;
    blocks_ = (t.size() + TEXT_BLOCK_LEN - 1) / TEXT_BLOCK_LEN;
}

Meta MetaTextEncoder::block(size_t i) const {
    Meta m{};
    if (blocks_ == 0) return m;
    i %= blocks_;
    m[0] = static_cast<uint8_t>((((1u << blocks_) - 1) << 4) | (1u << i));
    for (size_t k = 0; k < TEXT_BLOCK_LEN; ++k) {
        const size_t pos = i * TEXT_BLOCK_LEN + k;
        m[1 + k] = pos < text_.size() ? static_cast<uint8_t>(text_[pos]) : ' ';
    }
    return m;
}

void MetaTextDecoder::reset() {
    seen_ = 0;
    std::memset(blocks_, 0, sizeof(blocks_));
}

bool MetaTextDecoder::feed(uint16_t type, const Meta &meta) {
    if ((type & TYPE_ENC_MASK) != 0 || (type & TYPE_ENC_SUBTYPE_MASK) != TYPE_ENC_SUBTYPE_TEXT)
        return false;
    const uint8_t ctl = meta[0];
    const uint8_t map = ctl >> 4, which = ctl & 0x0F;
    // Valid bitmaps are contiguous from bit 0; exactly one block bit, inside it.
    if (ctl == 0 || (map & (map + 1)) != 0 || (which & (which - 1)) != 0 || !(which & map))
        return false;
    if ((seen_ >> 4) != 0 && (seen_ >> 4) != map) reset();   // a different message
    const bool wasComplete = complete();
    int idx = 0;
    while (!(which & (1 << idx))) ++idx;
    std::memcpy(blocks_[idx], meta.data() + 1, TEXT_BLOCK_LEN);
    seen_ |= ctl;
    return !wasComplete && complete();
}

bool MetaTextDecoder::complete() const { return seen_ != 0 && (seen_ >> 4) == (seen_ & 0x0F); }

std::string MetaTextDecoder::text() const {
    const uint8_t map = seen_ >> 4;
    std::string s;
    for (size_t b = 0; b < TEXT_MAX_BLOCKS && (map & (1u << b)); ++b) {
        if (seen_ & (1u << b))
            s.append(blocks_[b], TEXT_BLOCK_LEN);
        else
            s.append(TEXT_BLOCK_LEN, ' ');
    }
    while (!s.empty() && (s.back() == ' ' || s.back() == '\0')) s.pop_back();
    // A NUL inside a block ends the text early.
    const size_t nul = s.find('\0');
    if (nul != std::string::npos) s.resize(nul);
    return s;
}

}  // namespace m17
