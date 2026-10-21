#pragma once
// Receive-side stream tracking: one active stream at a time (a reflector
// module carries one talker), keyed by stream ID. Counts lost frames from
// frame-number gaps and late/duplicate frames, and ends a stream on the
// last-frame bit or after a silence timeout. Pure logic; time is passed in.

#include <cstdint>
#include <string>

#include "m17/packets.h"

namespace m17 {

// No spec value; ~0.5 s of silence (about 12 packets) ends a stream.
constexpr uint32_t STREAM_TIMEOUT_MS = 500;

struct StreamStats {
    uint16_t streamId = 0;
    std::string src, dst;
    uint32_t startedMs = 0, lastMs = 0;
    uint32_t frames = 0;      // packets accepted
    uint32_t lost = 0;        // missing frame numbers
    uint32_t late = 0;        // duplicates or arriving after a later frame
    bool endedByTimeout = false;
};

enum class StreamEvent : uint8_t {
    Ignored,     // late/duplicate, or a packet for another stream while one is active
    Started,     // first packet of a new stream (also delivered as audio)
    Continued,
    Ended,       // last-frame packet (also delivered as audio). A stream whose
                 // first packet is already the last reports Ended, frames == 1.
};

class StreamTracker {
public:
    // `nowMs` is a monotonic millisecond clock (wrap-safe).
    StreamEvent onPacket(const StreamPacket &p, uint32_t nowMs);
    // Returns true once when the active stream times out; stats() then holds
    // the final numbers with endedByTimeout set.
    bool poll(uint32_t nowMs);

    bool active() const { return active_; }
    const StreamStats &stats() const { return stats_; }

private:
    bool active_ = false;
    uint16_t nextFn_ = 0;
    // Stragglers of a just-ended stream must not start a "new" one.
    bool recentEnded_ = false;
    uint16_t endedId_ = 0;
    uint32_t endedMs_ = 0;
    StreamStats stats_;
};

}  // namespace m17
