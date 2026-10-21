#pragma once
// Shared runtime status, written by the owning task and read by the web UI,
// display and LEDs. Mutex-guarded snapshot; never hold it across I/O.

#include <cstdint>
#include <string>
#include <vector>

enum class WifiState : uint8_t { Off, Portal, Connecting, Connected, PortalFallback };

struct LastHeardEntry {
    std::string callsign, destination, message;
    uint32_t startedAtS = 0;    // uptime seconds
    uint32_t durationMs = 0;
};

struct AppStatus {
    WifiState wifi = WifiState::Off;
    std::string ssid, ip, apSsid;
    int rssi = 0;

    std::string linkState = "not implemented (milestone 5)";
    bool expanderOk = false;

    uint32_t rxFrames = 0, lateFrames = 0, lostFrames = 0;
    std::vector<LastHeardEntry> lastHeard;
};

AppStatus statusGet();

// Apply `fn` to the live status under the lock.
template <typename F> void statusUpdate(F fn);

const char *wifiStateName(WifiState s);

// ── implementation detail ──
#include <mutex>
std::mutex &statusMutex();
AppStatus &statusRef();
template <typename F> void statusUpdate(F fn) {
    std::lock_guard<std::mutex> lk(statusMutex());
    fn(statusRef());
}
