#pragma once
// PTT button logic: debounce plus momentary/toggle keying. Pure; the caller
// samples the pin and passes the time, so it tests on the host.

#include <cstdint>

class PttLogic {
public:
    explicit PttLogic(uint32_t debounceMs, bool toggle = false)
        : debounceMs_(debounceMs), toggle_(toggle) {}

    // Changing mode unkeys, so a latched toggle never survives a switch.
    void setToggle(bool toggle);

    // Feed the raw (already polarity-corrected) button state. Returns keyed().
    bool update(bool rawPressed, uint32_t nowMs);

    bool pressed() const { return stable_; }   // debounced button state
    bool keyed() const { return keyed_; }

    // Force unkey (e.g. transmit timeout). In momentary mode the button must
    // be released and pressed again to re-key.
    void cancel();

private:
    uint32_t debounceMs_;
    bool toggle_;
    bool raw_ = false, stable_ = false, keyed_ = false;
    bool waitRelease_ = false;
    uint32_t changedAt_ = 0;
};
