#include "m17/stream.h"

#include "m17/callsign.h"

namespace m17 {

StreamEvent StreamTracker::onPacket(const StreamPacket &p, uint32_t nowMs) {
    if (active_ && p.streamId != stats_.streamId) {
        if (nowMs - stats_.lastMs < STREAM_TIMEOUT_MS) return StreamEvent::Ignored;
        active_ = false;   // previous stream went silent; poll() wasn't called
    }

    if (!active_) {
        if (recentEnded_ && p.streamId == endedId_ && nowMs - endedMs_ < STREAM_TIMEOUT_MS)
            return StreamEvent::Ignored;
        stats_ = StreamStats{};
        stats_.streamId = p.streamId;
        stats_.src = decodeCallsign(p.src);
        stats_.dst = decodeCallsign(p.dst);
        stats_.startedMs = stats_.lastMs = nowMs;
        stats_.frames = 1;
        nextFn_ = static_cast<uint16_t>((p.frameNumber + 1) & FN_MASK);
        active_ = true;
        if (p.last) {
            active_ = false;
            recentEnded_ = true;
            endedId_ = p.streamId;
            endedMs_ = nowMs;
            return StreamEvent::Ended;
        }
        return StreamEvent::Started;
    }

    // Same stream. Distance ahead of the expected frame number, mod 2^15;
    // more than half the range ahead means it is actually behind (late).
    const uint16_t ahead = static_cast<uint16_t>((p.frameNumber - nextFn_) & FN_MASK);
    if (ahead >= 0x4000) {
        ++stats_.late;
        return StreamEvent::Ignored;
    }
    stats_.lost += ahead;
    ++stats_.frames;
    stats_.lastMs = nowMs;
    nextFn_ = static_cast<uint16_t>((p.frameNumber + 1) & FN_MASK);
    if (p.last) {
        active_ = false;
        recentEnded_ = true;
        endedId_ = p.streamId;
        endedMs_ = nowMs;
        return StreamEvent::Ended;
    }
    return StreamEvent::Continued;
}

bool StreamTracker::poll(uint32_t nowMs) {
    if (!active_ || nowMs - stats_.lastMs < STREAM_TIMEOUT_MS) return false;
    active_ = false;
    stats_.endedByTimeout = true;
    recentEnded_ = true;
    endedId_ = stats_.streamId;
    endedMs_ = nowMs;
    return true;
}

}  // namespace m17
