#include "net/wifi_manager.h"

#include <Arduino.h>
#include <ESPmDNS.h>
#include <WiFi.h>

#include "app/status.h"
#include "config/settings_store.h"

namespace {

constexpr const char *MDNS_NAME = "m17";
constexpr uint32_t STA_CONNECT_TIMEOUT_MS = 20000;
constexpr uint32_t STA_RETRY_MS = 30000;

const IPAddress AP_IP(192, 168, 4, 1);
bool s_portal = false;
bool s_mdns = false;
uint32_t s_connectStart = 0;
uint32_t s_lastRetry = 0;
uint32_t s_lastStatus = 0;
std::string s_apSsid;

// Fixed, product-named SSID as camillia-mt does ("camillia-mt"): it is what
// the README tells people to look for.
std::string apSsid() { return "buttons-m17"; }

void startPortal(bool keepSta) {
    WiFi.mode(keepSta ? WIFI_AP_STA : WIFI_AP);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));
    // Open AP: it only exists before setup or while the saved network is
    // unreachable, and every page behind it is basic-auth once a password
    // has been set.
    //
    // No captive portal, deliberately, as in camillia-mt: no DNS server and
    // no redirect of the OS connectivity probes, so phones don't pop their
    // cut-down captive-sheet browser (which breaks file download/upload for
    // backups). Browse to http://192.168.4.1/ directly.
    WiFi.softAP(s_apSsid.c_str());
    WiFi.setSleep(false);   // modem sleep stalls the synchronous WebServer
    s_portal = true;
    Serial.printf("[wifi] portal up: SSID \"%s\", http://%s/\n", s_apSsid.c_str(),
                  AP_IP.toString().c_str());
    statusUpdate([&](AppStatus &st) {
        st.wifi = keepSta ? WifiState::PortalFallback : WifiState::Portal;
        st.apSsid = s_apSsid;
    });
}

void stopPortal() {
    WiFi.softAPdisconnect(true);
    WiFi.mode(WIFI_STA);
    s_portal = false;
    Serial.println("[wifi] portal closed");
}

void startSta(const cfg::Settings &s) {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(MDNS_NAME);
    WiFi.setAutoReconnect(true);
    WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
    s_connectStart = millis();
    Serial.printf("[wifi] joining \"%s\"\n", s.wifiSsid.c_str());
    statusUpdate([&](AppStatus &st) {
        st.wifi = WifiState::Connecting;
        st.ssid = s.wifiSsid;
    });
}

void onConnected() {
    const std::string ip = WiFi.localIP().toString().c_str();
    Serial.printf("[wifi] connected, ip %s rssi %d\n", ip.c_str(), WiFi.RSSI());
    if (!s_mdns && MDNS.begin(MDNS_NAME)) {
        MDNS.addService("http", "tcp", 80);
        s_mdns = true;
        Serial.printf("[wifi] mDNS http://%s.local/\n", MDNS_NAME);
    }
    if (s_portal) stopPortal();
    statusUpdate([&](AppStatus &st) {
        st.wifi = WifiState::Connected;
        st.ip = ip;
    });
}

}  // namespace

void wifiBegin() {
    WiFi.persistent(false);   // credentials live in our NVS namespace only
    s_apSsid = apSsid();
    const cfg::Settings s = settingsGet();
    if (s.wifiSsid.empty()) {
        startPortal(false);
    } else {
        startSta(s);
    }
}

void wifiLoop() {
    const uint32_t now = millis();
    const AppStatus st = statusGet();
    const bool up = WiFi.status() == WL_CONNECTED;

    if (up && st.wifi != WifiState::Connected) {
        onConnected();
    } else if (!up && st.wifi == WifiState::Connected) {
        Serial.println("[wifi] link lost, reconnecting");
        statusUpdate([](AppStatus &a) { a.wifi = WifiState::Connecting; a.ip.clear(); });
        s_connectStart = now;
    } else if (!up && st.wifi == WifiState::Connecting &&
               now - s_connectStart > STA_CONNECT_TIMEOUT_MS) {
        Serial.println("[wifi] could not join; opening setup portal");
        startPortal(true);
        s_lastRetry = now;
    } else if (!up && st.wifi == WifiState::PortalFallback && now - s_lastRetry > STA_RETRY_MS) {
        // Keep trying the saved network in the background (AP+STA). The AP
        // follows the STA channel while scanning, so clients may blip.
        s_lastRetry = now;
        const cfg::Settings s = settingsGet();
        WiFi.begin(s.wifiSsid.c_str(), s.wifiPass.c_str());
    }

    if (now - s_lastStatus > 1000) {
        s_lastStatus = now;
        const int rssi = up ? WiFi.RSSI() : 0;
        statusUpdate([&](AppStatus &a) { a.rssi = rssi; });
    }
}

bool wifiPortalActive() { return s_portal; }

const char *wifiHostname() { return MDNS_NAME; }
