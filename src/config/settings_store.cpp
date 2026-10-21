#include "config/settings_store.h"

#include <Arduino.h>
#include <Preferences.h>

#include <mutex>

namespace {

constexpr const char *NS = "m17";
std::mutex s_mu;
cfg::Settings s_cur;

std::string getStr(Preferences &p, const char *key, const std::string &def) {
    if (!p.isKey(key)) return def;
    return std::string(p.getString(key, def.c_str()).c_str());
}

}  // namespace

void settingsBegin() {
    cfg::Settings s;
    s.extraHosts = cfg::DEFAULT_EXTRA_HOSTS;

    Preferences p;
    if (p.begin(NS, true)) {
        s.wifiSsid = getStr(p, "wifi_ssid", s.wifiSsid);
        s.wifiPass = getStr(p, "wifi_pass", s.wifiPass);
        s.adminPass = getStr(p, "admin_pass", s.adminPass);
        s.callsign = getStr(p, "callsign", s.callsign);
        s.reflector = getStr(p, "reflector", s.reflector);
        s.reflectorModule = static_cast<char>(p.getUChar("refl_mod", s.reflectorModule));
        s.extraHosts = getStr(p, "extra_hosts", s.extraHosts);
        s.autoLink = p.getBool("auto_link", s.autoLink);
        s.listenOnly = p.getBool("listen_only", s.listenOnly);
        s.pttMode = static_cast<cfg::PttMode>(p.getUChar("ptt_mode", (uint8_t)s.pttMode));
        s.volume = p.getUChar("volume", s.volume);
        s.micGainDb = p.getChar("mic_gain", s.micGainDb);
        p.end();
    }

    // A corrupt or older-format store must not brick the device: fall back to
    // defaults for the user fields but keep Wi-Fi and admin credentials.
    cfg::Settings checked = s;
    const std::string err = cfg::validate(checked);
    if (!err.empty()) {
        Serial.printf("[config] stored settings invalid (%s); using defaults\n", err.c_str());
        cfg::Settings d;
        d.extraHosts = cfg::DEFAULT_EXTRA_HOSTS;
        d.wifiSsid = s.wifiSsid;
        d.wifiPass = s.wifiPass;
        d.adminPass = s.adminPass;
        checked = d;
    }

    std::lock_guard<std::mutex> lk(s_mu);
    s_cur = checked;
}

cfg::Settings settingsGet() {
    std::lock_guard<std::mutex> lk(s_mu);
    return s_cur;
}

bool settingsSave(const cfg::Settings &s) {
    Preferences p;
    if (!p.begin(NS, false)) return false;
    bool ok = true;
    // putX returns bytes written; 0 is a failure except for empty strings.
    auto putS = [&](const char *k, const std::string &v) {
        if (p.putString(k, v.c_str()) == 0 && !v.empty()) ok = false;
    };
    putS("wifi_ssid", s.wifiSsid);
    putS("wifi_pass", s.wifiPass);
    putS("admin_pass", s.adminPass);
    putS("callsign", s.callsign);
    putS("reflector", s.reflector);
    ok &= p.putUChar("refl_mod", s.reflectorModule) == 1;
    putS("extra_hosts", s.extraHosts);
    ok &= p.putBool("auto_link", s.autoLink) == 1;
    ok &= p.putBool("listen_only", s.listenOnly) == 1;
    ok &= p.putUChar("ptt_mode", (uint8_t)s.pttMode) == 1;
    ok &= p.putUChar("volume", s.volume) == 1;
    ok &= p.putChar("mic_gain", s.micGainDb) == 1;
    p.end();

    if (ok) {
        std::lock_guard<std::mutex> lk(s_mu);
        s_cur = s;
    }
    return ok;
}

void settingsFactoryReset() {
    Preferences p;
    if (p.begin(NS, false)) {
        p.clear();
        p.end();
    }
}
