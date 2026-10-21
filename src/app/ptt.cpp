#include "app/ptt.h"

void PttLogic::setToggle(bool toggle) {
    if (toggle == toggle_) return;
    toggle_ = toggle;
    keyed_ = false;
    waitRelease_ = stable_;
}

void PttLogic::cancel() {
    keyed_ = false;
    waitRelease_ = stable_;
}

bool PttLogic::update(bool rawPressed, uint32_t nowMs) {
    if (rawPressed != raw_) {
        raw_ = rawPressed;
        changedAt_ = nowMs;
    }
    if (raw_ == stable_ || nowMs - changedAt_ < debounceMs_) return keyed_;

    stable_ = raw_;
    if (!stable_) {
        waitRelease_ = false;
        if (!toggle_) keyed_ = false;
    } else if (!waitRelease_) {
        keyed_ = toggle_ ? !keyed_ : true;
    }
    return keyed_;
}
