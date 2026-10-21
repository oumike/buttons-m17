#include "ui/web_server.h"

#include <Arduino.h>
#include <WebServer.h>

#include <cstdlib>
#include <string>
#include <vector>

#include "app/status.h"
#include "audio/audio_engine.h"
#include "config/backup.h"
#include "config/settings_store.h"
#include "net/wifi_manager.h"
#include "storage/sd_storage.h"
#include "ui/html_util.h"
#include "ui/http_guard.h"

#ifndef APP_VERSION
#define APP_VERSION "unknown"
#endif

namespace {

WebServer s_srv(80);
constexpr const char *ADMIN_USER = "admin";

std::string arg(const char *name) { return std::string(s_srv.arg(name).c_str()); }

std::string pageHead(const char *title) {
    std::string h =
        "<!doctype html><html><head><meta charset=utf-8>"
        "<meta name=viewport content='width=device-width,initial-scale=1'><title>";
    h += title;
    h += " - buttons-m17</title><style>"
         "body{font-family:system-ui,sans-serif;max-width:46rem;margin:1rem auto;padding:0 1rem;"
         "background:#111;color:#ddd}a{color:#7cf}h1{font-size:1.3rem}"
         "table{border-collapse:collapse;width:100%}td,th{padding:.25rem .5rem;"
         "border-bottom:1px solid #333;text-align:left;vertical-align:top}"
         "input,select,textarea{background:#222;color:#eee;border:1px solid #555;padding:.3rem;"
         "width:100%;box-sizing:border-box}textarea{height:7rem;font-family:monospace}"
         "button{padding:.4rem 1rem;margin-top:.5rem}.err{color:#f77}.ok{color:#7f7}"
         ".warn{background:#430;padding:.5rem;border:1px solid #a60}"
         "nav a{margin-right:1rem}</style></head><body><nav>"
         "<a href='/'>Status</a><a href='/settings'>Settings</a><a href='/setup'>Wi-Fi &amp; password</a>"
         "<a href='/backup'>Backup</a>"
         "</nav><h1>";
    h += title;
    h += "</h1>";
    return h;
}

constexpr const char *PAGE_TAIL = "<p><small>buttons-m17 " APP_VERSION "</small></p></body></html>";

void sendHtml(int code, const std::string &body) {
    s_srv.send(code, "text/html; charset=utf-8", body.c_str());
}

void redirect(const char *to) {
    s_srv.sendHeader("Location", to, true);
    s_srv.send(302, "text/plain", "");
}

// Host/Origin check for POSTs and secret-bearing GETs (see ui/http_guard.h).
bool originOk(bool changesState) {
    const std::string host = s_srv.hostHeader().c_str();
    if (!hostHeaderAllowed(host, wifiHostname())) {
        Serial.printf("[web] refused %s: Host '%s'\n", s_srv.uri().c_str(), host.c_str());
        return false;
    }
    if (!changesState) return true;
    for (const char *h : {"Origin", "Referer"}) {
        if (!s_srv.hasHeader(h)) continue;
        const std::string v = s_srv.header(h).c_str();
        if (v.empty()) continue;
        if (sourceMatchesHost(v, host)) return true;
        Serial.printf("[web] refused %s: cross-origin %s\n", s_srv.uri().c_str(), h);
        return false;
    }
    return true;
}

// Auth check that never writes a response (for upload callbacks).
bool authedQuiet() {
    const cfg::Settings s = settingsGet();
    return !s.adminPass.empty() && s_srv.authenticate(ADMIN_USER, s.adminPass.c_str());
}

// Returns true if the request may proceed. `secret` marks a GET that hands
// out credentials (the backup download).
bool guard(bool isSetupPage, bool secret = false) {
    const bool post = s_srv.method() != HTTP_GET;
    if ((post || secret) && !originOk(post)) {
        s_srv.send(403, "text/plain", "Forbidden: cross-site request");
        return false;
    }
    const cfg::Settings s = settingsGet();
    if (s.adminPass.empty()) {
        if (isSetupPage) return true;
        redirect("/setup");
        return false;
    }
    if (!s_srv.authenticate(ADMIN_USER, s.adminPass.c_str())) {
        s_srv.requestAuthentication(BASIC_AUTH, "buttons-m17");
        return false;
    }
    return true;
}

std::string row(const char *k, const std::string &v) {
    return std::string("<tr><th>") + k + "</th><td>" + htmlEscape(v) + "</td></tr>";
}

// ── Status ───────────────────────────────────────────────────────────────────

void handleStatus() {
    if (!guard(false)) return;
    const AppStatus st = statusGet();
    const cfg::Settings s = settingsGet();

    std::string h = pageHead("Status");
    h.insert(h.find("<title>"), "<meta http-equiv=refresh content=5>");
    if (s.callsign.empty())
        h += "<p class=warn>Transmit inhibited: no callsign configured. "
             "Set one on the <a href='/settings'>Settings</a> page.</p>";
    else if (s.listenOnly)
        h += "<p class=warn>Listen-only mode: transmit disabled.</p>";

    h += "<table>";
    h += row("Wi-Fi", std::string(wifiStateName(st.wifi)) +
                          (st.ssid.empty() ? "" : " (" + st.ssid + ")"));
    if (!st.ip.empty()) h += row("IP", st.ip + "  /  m17.local");
    if (st.wifi == WifiState::Connected) h += row("RSSI", std::to_string(st.rssi) + " dBm");
    h += row("Callsign", s.callsign.empty() ? "(not set)" : s.callsign);
    std::string refl = s.reflector + " " + s.reflectorModule;
    std::vector<cfg::HostEntry> extra;
    cfg::HostEntry he;
    cfg::parseHostList(s.extraHosts, extra);
    if (cfg::findReflector(s.reflector, extra, he))
        refl += "  (" + he.host + ":" + std::to_string(he.port) + ")";
    if (s.autoLink) refl += " auto-link";
    if (s.listenOnly) refl += " listen-only";
    h += row("Reflector", refl);
    h += row("Link", st.linkState);
    h += row("Frames rx / late / lost", std::to_string(st.rxFrames) + " / " +
                                            std::to_string(st.lateFrames) + " / " +
                                            std::to_string(st.lostFrames));
    h += row("Board I/O", st.expanderOk ? "ok" : "EXPANDER FAILED");
    h += row("Audio", st.audio);
    {
        std::string mic = "slot 0: " + std::to_string(st.micPeakDb[0]) + " dBFS, slot 1: " +
                          std::to_string(st.micPeakDb[1]) + " dBFS";
        if (st.micSlot >= 0) mic += " (mic on slot " + std::to_string(st.micSlot) + ")";
        if (st.i2sErrors) mic += ", I2S errors " + std::to_string(st.i2sErrors);
        h += row("Mic peak", mic);
        h += row("Codec", st.codec2);
    }
    h += row("SD card", storageDescribe());
    h += row("Uptime", std::to_string(millis() / 1000) + " s");
    h += row("Free heap / PSRAM",
             std::to_string(ESP.getFreeHeap() / 1024) + " KB / " +
                 std::to_string(ESP.getFreePsram() / 1024) + " KB");
    h += "</table><h1>Last heard</h1>";
    if (st.lastHeard.empty()) {
        h += "<p>Nobody yet.</p>";
    } else {
        h += "<table><tr><th>Callsign</th><th>Destination</th><th>Message</th><th>When</th>"
             "<th>Duration</th></tr>";
        for (const auto &e : st.lastHeard) {
            h += "<tr><td>" + htmlEscape(e.callsign) + "</td><td>" + htmlEscape(e.destination) +
                 "</td><td>" + htmlEscape(e.message) + "</td><td>" +
                 std::to_string(e.startedAtS) + " s</td><td>" +
                 std::to_string(e.durationMs / 1000) + "." +
                 std::to_string((e.durationMs / 100) % 10) + " s</td></tr>";
        }
        h += "</table>";
    }
    h += "<h1>Audio test</h1><p>Echo test: hold the PTT button (the button on GPIO0) "
         "and talk, then release to hear it played back (up to 10 s). In toggle PTT mode, "
         "press once to start and again to stop.</p>"
         "<form method=post action='/audio/echo-mode'><p><label>Echo path<select name=mode>" +
         std::string("<option value=codec2") + (audioEchoCodec2() ? " selected" : "") +
         ">Through Codec2 3200 (as on air)</option><option value=raw" +
         (audioEchoCodec2() ? "" : " selected") +
         ">Raw (no codec)</option></select></label></p><button>Set echo path</button></form>"
         "<form method=post action='/audio/tone'><button>Play 1 kHz test tone (2 s)</button></form>";
    h += "<form method=post action='/reboot'><button>Reboot</button></form>";
    h += PAGE_TAIL;
    sendHtml(200, h);
}

void handleApiStatus() {
    if (!guard(false)) return;
    const AppStatus st = statusGet();
    const cfg::Settings s = settingsGet();
    std::string j = "{";
    j += "\"version\":\"" APP_VERSION "\",";
    j += "\"wifi\":\"" + std::string(wifiStateName(st.wifi)) + "\",";
    j += "\"ssid\":\"" + jsonEscape(st.ssid) + "\",";
    j += "\"ip\":\"" + jsonEscape(st.ip) + "\",";
    j += "\"rssi\":" + std::to_string(st.rssi) + ",";
    j += "\"callsign\":\"" + jsonEscape(s.callsign) + "\",";
    j += "\"tx_allowed\":" + std::string(cfg::canTransmit(s) ? "true" : "false") + ",";
    j += "\"link\":\"" + jsonEscape(st.linkState) + "\",";
    j += "\"reflector\":\"" + jsonEscape(s.reflector) + "\",";
    j += "\"module\":\"" + std::string(1, s.reflectorModule) + "\",";
    j += "\"rx_frames\":" + std::to_string(st.rxFrames) + ",";
    j += "\"late_frames\":" + std::to_string(st.lateFrames) + ",";
    j += "\"lost_frames\":" + std::to_string(st.lostFrames) + ",";
    j += "\"audio\":\"" + jsonEscape(st.audio) + "\",";
    j += "\"mic_peak_dbfs\":[" + std::to_string(st.micPeakDb[0]) + "," +
         std::to_string(st.micPeakDb[1]) + "],";
    j += "\"mic_slot\":" + std::to_string(st.micSlot) + ",";
    j += "\"i2s_errors\":" + std::to_string(st.i2sErrors) + ",";
    j += "\"codec2\":\"" + jsonEscape(st.codec2) + "\",";
    j += "\"uptime_s\":" + std::to_string(millis() / 1000) + ",";
    j += "\"last_heard\":[";
    for (size_t i = 0; i < st.lastHeard.size(); ++i) {
        const auto &e = st.lastHeard[i];
        if (i) j += ",";
        j += "{\"callsign\":\"" + jsonEscape(e.callsign) + "\",\"destination\":\"" +
             jsonEscape(e.destination) + "\",\"message\":\"" + jsonEscape(e.message) +
             "\",\"started_s\":" + std::to_string(e.startedAtS) +
             ",\"duration_ms\":" + std::to_string(e.durationMs) + "}";
    }
    j += "]}";
    s_srv.send(200, "application/json", j.c_str());
}

// ── Settings ─────────────────────────────────────────────────────────────────

std::string field(const char *label, const char *name, const std::string &val,
                  const char *extra = "") {
    return std::string("<p><label>") + label + "<input name=" + name + " value=\"" +
           htmlEscape(val) + "\" " + extra + "></label></p>";
}

std::string slider(const char *label, const char *name, int val, int lo, int hi,
                   const char *unit) {
    const std::string v = std::to_string(val);
    return std::string("<p><label>") + label + ": <output id=o_" + name + ">" + v +
           "</output>" + unit + "<input type=range name=" + name + " min=" +
           std::to_string(lo) + " max=" + std::to_string(hi) + " value=" + v +
           " oninput=\"o_" + name + ".value=this.value\"></label></p>";
}

std::string option(const char *value, const char *label, bool sel) {
    return std::string("<option value=") + value + (sel ? " selected" : "") + ">" + label +
           "</option>";
}

void renderSettings(const cfg::Settings &s, const std::string &msg, bool isErr) {
    std::string h = pageHead("Settings");
    if (!msg.empty()) h += std::string("<p class=") + (isErr ? "err" : "ok") + ">" +
                           htmlEscape(msg) + "</p>";
    h += "<form method=post action='/settings'>";
    h += field("Callsign (required to transmit)", "callsign", s.callsign, "maxlength=9");
    h += field("Reflector (M17-XXX)", "reflector", s.reflector, "maxlength=7 list=refl");
    h += "<datalist id=refl>";
    for (size_t i = 0; i < cfg::BUILTIN_REFLECTOR_COUNT; ++i) {
        const cfg::ReflectorEntry &r = cfg::BUILTIN_REFLECTORS[i];
        h += std::string("<option value=") + r.name + ">" + htmlEscape(r.host) + "</option>";
    }
    h += "</datalist>";
    h += field("Reflector module (A-Z)", "rmod", std::string(1, s.reflectorModule), "maxlength=1");
    h += "<p><label>Extra reflectors (M17-XXX host [port] per line; override the built-in "
         "list)<textarea name=hosts>" + htmlEscape(s.extraHosts) + "</textarea></label></p>";
    h += std::string("<p><small>Built-in list: ") + std::to_string(cfg::BUILTIN_REFLECTOR_COUNT) +
         " reflectors, snapshot " + cfg::BUILTIN_REFLECTORS_DATE +
         ". Reflector data provided by <a href='https://dvref.com/'>DVRef</a> (CC BY 4.0), "
         "via Pi-Star.</small></p>";
    h += std::string("<p><label><input type=checkbox name=autolink value=1 style='width:auto'") +
         (s.autoLink ? " checked" : "") + "> Auto-link on boot</label></p>";
    h += std::string("<p><label><input type=checkbox name=listen value=1 style='width:auto'") +
         (s.listenOnly ? " checked" : "") + "> Listen only (never transmit)</label></p>";
    h += "<p><label>PTT mode<select name=ptt>";
    h += option("momentary", "Momentary (hold to talk)", s.pttMode == cfg::PttMode::Momentary);
    h += option("toggle", "Toggle (press to start/stop)", s.pttMode == cfg::PttMode::Toggle);
    h += "</select></label></p>";
    h += slider("Speaker volume", "volume", s.volume, 0, 100, "%");
    h += slider("Mic gain (36 default, applies within a second of saving)", "micgain",
                s.micGainDb, cfg::MIC_GAIN_MIN_DB, cfg::MIC_GAIN_MAX_DB, " dB");
    h += "<button>Save</button></form>";
    h += PAGE_TAIL;
    sendHtml(isErr ? 400 : 200, h);
}

bool parseLong(const std::string &str, long &out) {
    char *end = nullptr;
    out = std::strtol(str.c_str(), &end, 10);
    return !str.empty() && end && *end == '\0';
}

void handleSettingsGet() {
    if (!guard(false)) return;
    renderSettings(settingsGet(), "", false);
}

void handleSettingsPost() {
    if (!guard(false)) return;
    cfg::Settings s = settingsGet();
    s.callsign = arg("callsign");
    const std::string rmod = arg("rmod");
    s.reflector = arg("reflector");
    s.extraHosts = arg("hosts");
    s.autoLink = s_srv.hasArg("autolink");
    s.listenOnly = s_srv.hasArg("listen");
    s.pttMode = arg("ptt") == "toggle" ? cfg::PttMode::Toggle : cfg::PttMode::Momentary;

    std::string err;
    long v = 0;
    err = cfg::normalizeModule(rmod, s.reflectorModule);
    if (err.empty() && (!parseLong(arg("volume"), v) || v < 0 || v > 100))
        err = "Volume must be 0-100";
    else if (err.empty()) s.volume = static_cast<uint8_t>(v);
    if (err.empty() && (!parseLong(arg("micgain"), v) || v < cfg::MIC_GAIN_MIN_DB ||
                        v > cfg::MIC_GAIN_MAX_DB))
        err = "Mic gain out of range";
    else if (err.empty()) s.micGainDb = static_cast<int8_t>(v);
    if (err.empty()) err = cfg::validate(s);

    if (!err.empty()) {
        renderSettings(s, err, true);
        return;
    }
    if (!settingsSave(s)) {
        renderSettings(s, "Saving to NVS failed", true);
        return;
    }
    Serial.printf("[web] settings saved (callsign=%s)\n", s.callsign.c_str());
    renderSettings(s, "Saved.", false);
}

// ── Setup (Wi-Fi + admin password) ───────────────────────────────────────────

void renderSetup(const std::string &msg, bool isErr) {
    const cfg::Settings s = settingsGet();
    const bool first = s.adminPass.empty();
    std::string h = pageHead(first ? "First-time setup" : "Wi-Fi & password");
    if (!msg.empty()) h += std::string("<p class=") + (isErr ? "err" : "ok") + ">" +
                           htmlEscape(msg) + "</p>";
    h += "<form method=post action='/setup'>";
    h += field("Wi-Fi network (SSID)", "ssid", s.wifiSsid, "maxlength=32 required");
    h += field("Wi-Fi password", "wpass", "",
               s.wifiPass.empty() ? "type=password maxlength=63"
                                  : "type=password maxlength=63 placeholder='(unchanged)'");
    h += std::string("<p>Web UI user is <b>admin</b>. ") +
         (first ? "Choose a password (8+ characters)." : "Leave blank to keep the current password.") +
         "</p>";
    h += field("Admin password", "apass", "", "type=password");
    h += field("Repeat admin password", "apass2", "", "type=password");
    h += "<button>Save and reboot</button></form>";
    h += PAGE_TAIL;
    sendHtml(isErr ? 400 : 200, h);
}

void handleSetupGet() {
    if (!guard(true)) return;
    renderSetup("", false);
}

void handleSetupPost() {
    if (!guard(true)) return;
    cfg::Settings s = settingsGet();
    const std::string ssid = arg("ssid"), wpass = arg("wpass");
    const std::string apass = arg("apass"), apass2 = arg("apass2");

    if (ssid.empty() || ssid.size() > 32) return renderSetup("SSID must be 1-32 characters", true);
    if (wpass.size() > 63) return renderSetup("Wi-Fi password too long", true);
    if (apass != apass2) return renderSetup("Admin passwords do not match", true);
    if (s.adminPass.empty() && apass.size() < cfg::ADMIN_PASS_MIN)
        return renderSetup("Admin password must be at least 8 characters", true);
    if (!apass.empty() && apass.size() < cfg::ADMIN_PASS_MIN)
        return renderSetup("Admin password must be at least 8 characters", true);

    // A blank Wi-Fi password keeps the stored one only for the same network;
    // a new SSID with a blank password means an open network.
    if (!wpass.empty() || ssid != s.wifiSsid) s.wifiPass = wpass;
    s.wifiSsid = ssid;
    if (!apass.empty()) s.adminPass = apass;

    if (!settingsSave(s)) return renderSetup("Saving to NVS failed", true);

    std::string h = pageHead("Saved");
    h += "<p>Rebooting and joining <b>" + htmlEscape(ssid) +
         "</b>. Reconnect your phone/PC to that network and open "
         "<a href='http://m17.local/'>http://m17.local/</a>.</p>";
    h += PAGE_TAIL;
    sendHtml(200, h);
    Serial.printf("[web] Wi-Fi set to \"%s\"; rebooting\n", ssid.c_str());
    delay(1000);
    ESP.restart();
}

void handleAudioEchoMode() {
    if (!guard(false)) return;
    audioSetEchoCodec2(arg("mode") != "raw");
    redirect("/");
}

void handleAudioTone() {
    if (!guard(false)) return;
    audioRequestTone();
    redirect("/");
}

void handleReboot() {
    if (!guard(false)) return;
    sendHtml(200, pageHead("Rebooting") + "<p>Back in a few seconds.</p>" + PAGE_TAIL);
    delay(500);
    ESP.restart();
}

// No captive-portal redirects (see net/wifi_manager.cpp): OS connectivity
// probes get a plain 404, so phones stay on the AP without a captive sheet.
void handleNotFound() { s_srv.send(404, "text/plain", "Not found"); }

// ── Backup / restore ─────────────────────────────────────────────────────────

void renderBackup(const std::string &msg, bool isErr) {
    std::string h = pageHead("Backup & restore");
    if (!msg.empty()) h += std::string("<p class=") + (isErr ? "err" : "ok") + ">" +
                           htmlEscape(msg) + "</p>";
    h += "<p class=warn>Backups contain your Wi-Fi password. The web admin password is "
         "never included.</p>";
    h += "<h1>This computer</h1>"
         "<p><a href='/backup/download'>&#11015; Download settings backup (.yaml)</a></p>"
         "<form method=post action='/backup/upload' enctype='multipart/form-data'>"
         "<p><label>Restore from file<input type=file name=file accept='.yaml,.yml,.txt' "
         "required></label></p><button>Upload and restore</button></form>";
    const bool card = storageBegin();
    h += "<h1>SD card</h1><p>" + htmlEscape(storageDescribe());
    if (card) h += storageBackupExists() ? std::string(" &mdash; backup present at ") + BACKUP_PATH
                                         : std::string(" &mdash; no backup yet");
    h += "</p>";
    if (card) {
        h += "<form method=post action='/backup/sd-save'><button>Save backup to SD</button></form>";
        if (storageBackupExists())
            h += "<form method=post action='/backup/sd-restore'>"
                 "<button>Restore from SD</button></form>";
    }
    h += PAGE_TAIL;
    sendHtml(isErr ? 400 : 200, h);
}

void handleBackupGet() {
    if (!guard(false)) return;
    renderBackup("", false);
}

void handleBackupDownload() {
    if (!guard(false, true)) return;
    const cfg::Settings s = settingsGet();
    std::string name = "buttons-m17-" + (s.callsign.empty() ? std::string("settings") : s.callsign) +
                       ".yaml";
    for (auto &c : name)
        if (c == '/') c = '_';   // M17 callsigns may contain '/'
    s_srv.sendHeader("Content-Disposition", ("attachment; filename=\"" + name + "\"").c_str());
    s_srv.send(200, "text/yaml", cfg::settingsToYaml(s).c_str());
}

// Applies a parsed backup. Reboots if the Wi-Fi credentials changed.
void applyRestore(const std::string &yaml, const char *source) {
    const cfg::Settings cur = settingsGet();
    cfg::Settings next;
    const std::string err = cfg::settingsFromYaml(yaml, cur, next);
    if (!err.empty()) return renderBackup(std::string("Restore failed: ") + err, true);
    if (!settingsSave(next)) return renderBackup("Restore failed: NVS write error", true);
    Serial.printf("[web] settings restored from %s\n", source);
    const bool wifiChanged = next.wifiSsid != cur.wifiSsid || next.wifiPass != cur.wifiPass;
    if (!wifiChanged) return renderBackup(std::string("Restored from ") + source + ".", false);
    sendHtml(200, pageHead("Restored") + "<p>Restored from " + htmlEscape(source) +
                      ". Wi-Fi settings changed: rebooting to join <b>" +
                      htmlEscape(next.wifiSsid) + "</b>.</p>" + PAGE_TAIL);
    delay(1000);
    ESP.restart();
}

// Upload goes into a static buffer; files over the limit are refused whole
// rather than parsed truncated (a truncated YAML still parses).
char s_upBuf[cfg::BACKUP_MAX_BYTES + 1];
size_t s_upLen = 0;
bool s_upOk = false, s_upOverflow = false;

void handleBackupUploadChunk() {
    if (!authedQuiet()) { s_upOk = false; s_upLen = 0; return; }
    HTTPUpload &u = s_srv.upload();
    if (u.status == UPLOAD_FILE_START) {
        s_upLen = 0;
        s_upOk = false;
        s_upOverflow = false;
    } else if (u.status == UPLOAD_FILE_WRITE) {
        if (s_upOverflow) return;
        if (u.currentSize > cfg::BACKUP_MAX_BYTES - s_upLen) { s_upOverflow = true; return; }
        memcpy(s_upBuf + s_upLen, u.buf, u.currentSize);
        s_upLen += u.currentSize;
    } else if (u.status == UPLOAD_FILE_END) {
        s_upOk = !s_upOverflow;
    }
}

void handleBackupUploadDone() {
    if (!guard(false)) return;
    if (s_upOverflow) return renderBackup("Restore failed: file is larger than 8 KB", true);
    if (!s_upOk) return renderBackup("Restore failed: no file received", true);
    const std::string yaml(s_upBuf, s_upLen);
    s_upOk = false;
    applyRestore(yaml, "uploaded file");
}

void handleSdSave() {
    if (!guard(false)) return;
    const std::string err = storageSaveBackup(cfg::settingsToYaml(settingsGet()));
    if (!err.empty()) return renderBackup("Save to SD failed: " + err, true);
    renderBackup(std::string("Saved to SD: ") + BACKUP_PATH, false);
}

void handleSdRestore() {
    if (!guard(false)) return;
    std::string yaml;
    const std::string err = storageLoadBackup(yaml);
    if (!err.empty()) return renderBackup("Restore from SD failed: " + err, true);
    applyRestore(yaml, BACKUP_PATH);
}

}  // namespace

void webBegin() {
    s_srv.on("/", HTTP_GET, handleStatus);
    s_srv.on("/api/status", HTTP_GET, handleApiStatus);
    s_srv.on("/settings", HTTP_GET, handleSettingsGet);
    s_srv.on("/settings", HTTP_POST, handleSettingsPost);
    s_srv.on("/setup", HTTP_GET, handleSetupGet);
    s_srv.on("/setup", HTTP_POST, handleSetupPost);
    s_srv.on("/reboot", HTTP_POST, handleReboot);
    s_srv.on("/audio/tone", HTTP_POST, handleAudioTone);
    s_srv.on("/audio/echo-mode", HTTP_POST, handleAudioEchoMode);
    s_srv.on("/backup", HTTP_GET, handleBackupGet);
    s_srv.on("/backup/download", HTTP_GET, handleBackupDownload);
    s_srv.on("/backup/upload", HTTP_POST, handleBackupUploadDone, handleBackupUploadChunk);
    s_srv.on("/backup/sd-save", HTTP_POST, handleSdSave);
    s_srv.on("/backup/sd-restore", HTTP_POST, handleSdRestore);
    const char *hdrs[] = {"Origin", "Referer"};
    s_srv.collectHeaders(hdrs, 2);
    s_srv.enableDelay(false);   // the web task already yields
    s_srv.onNotFound(handleNotFound);
    s_srv.begin();
    Serial.println("[web] listening on :80");
}

void webLoop() { s_srv.handleClient(); }
