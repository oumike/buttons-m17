#include "app/status.h"

namespace {
std::mutex s_mu;
AppStatus s_status;
}  // namespace

std::mutex &statusMutex() { return s_mu; }
AppStatus &statusRef() { return s_status; }

AppStatus statusGet() {
    std::lock_guard<std::mutex> lk(s_mu);
    return s_status;
}

const char *wifiStateName(WifiState s) {
    switch (s) {
        case WifiState::Off: return "off";
        case WifiState::Portal: return "setup AP (192.168.4.1)";
        case WifiState::Connecting: return "connecting";
        case WifiState::Connected: return "connected";
        case WifiState::PortalFallback: return "join failed, setup AP open (192.168.4.1)";
    }
    return "?";
}
