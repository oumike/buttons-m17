#include "config/backup.h"

#include <cstdlib>

namespace cfg {

namespace {

std::string quote(const std::string &s) {
    std::string o = "\"";
    for (char c : s) {
        switch (c) {
            case '"': o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n"; break;
            case '\r': break;
            case '\t': o += "\\t"; break;
            default: o += c;
        }
    }
    return o + "\"";
}

// Parses a double-quoted scalar starting at v[0] == '"'. Returns false on a
// malformed string. Trailing "# comment" after the closing quote is allowed.
bool unquote(const std::string &v, std::string &out) {
    if (v.empty() || v[0] != '"') return false;
    out.clear();
    size_t i = 1;
    for (; i < v.size(); ++i) {
        char c = v[i];
        if (c == '"') break;
        if (c == '\\') {
            if (++i >= v.size()) return false;
            switch (v[i]) {
                case 'n': out += '\n'; break;
                case 't': out += '\t'; break;
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                default: return false;
            }
        } else {
            out += c;
        }
    }
    if (i >= v.size()) return false;   // no closing quote
    for (size_t j = i + 1; j < v.size(); ++j) {
        if (v[j] == '#') break;
        if (v[j] != ' ' && v[j] != '\t') return false;
    }
    return true;
}

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r')) --e;
    return s.substr(b, e - b);
}

// Bare scalar: strip a trailing comment.
std::string bare(const std::string &v) {
    const size_t h = v.find(" #");
    return trim(h == std::string::npos ? v : v.substr(0, h));
}

bool toLong(const std::string &v, long &out) {
    const std::string b = bare(v);
    if (b.empty()) return false;
    char *end = nullptr;
    out = std::strtol(b.c_str(), &end, 10);
    return *end == '\0';
}

bool toBool(const std::string &v, bool &out) {
    const std::string b = bare(v);
    if (b == "true") { out = true; return true; }
    if (b == "false") { out = false; return true; }
    return false;
}

}  // namespace

std::string settingsToYaml(const Settings &s) {
    std::string y;
    y += "# buttons-m17 settings backup. Contains Wi-Fi credentials: keep it private.\n";
    y += std::string("format: ") + quote(BACKUP_FORMAT) + "\n";
    y += "version: " + std::to_string(BACKUP_VERSION) + "\n";
    y += "wifi_ssid: " + quote(s.wifiSsid) + "\n";
    y += "wifi_pass: " + quote(s.wifiPass) + "\n";
    y += "callsign: " + quote(s.callsign) + "\n";
    y += "reflector: " + quote(s.reflector) + "\n";
    y += "reflector_module: " + quote(std::string(1, s.reflectorModule)) + "\n";
    y += "extra_hosts: " + quote(s.extraHosts) + "\n";
    y += std::string("auto_link: ") + (s.autoLink ? "true" : "false") + "\n";
    y += std::string("listen_only: ") + (s.listenOnly ? "true" : "false") + "\n";
    y += std::string("ptt_mode: ") +
         quote(s.pttMode == PttMode::Toggle ? "toggle" : "momentary") + "\n";
    y += "volume: " + std::to_string(s.volume) + "\n";
    y += "mic_gain_db: " + std::to_string(s.micGainDb) + "\n";
    return y;
}

std::string settingsFromYaml(const std::string &text, const Settings &base, Settings &out) {
    if (text.size() > BACKUP_MAX_BYTES) return "Backup file is too large";
    Settings s = base;
    bool sawFormat = false;

    size_t pos = 0;
    int lineNo = 0;
    while (pos < text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;

        const size_t colon = line.find(':');
        const std::string where = "Line " + std::to_string(lineNo) + ": ";
        if (colon == std::string::npos || colon == 0) return where + "expected key: value";
        const std::string key = trim(line.substr(0, colon));
        const std::string val = trim(line.substr(colon + 1));

        std::string str;
        long n = 0;
        bool b = false;
        auto needStr = [&]() { return unquote(val, str); };

        if (key == "format") {
            if (!needStr() || str != BACKUP_FORMAT) return where + "not a buttons-m17 backup";
            sawFormat = true;
        } else if (key == "version") {
            if (!toLong(val, n)) return where + "bad version";
            if (n > BACKUP_VERSION) return where + "backup is from newer firmware (version " +
                                           std::to_string(n) + ")";
        } else if (key == "wifi_ssid") {
            if (!needStr() || str.size() > 32) return where + "bad wifi_ssid";
            s.wifiSsid = str;
        } else if (key == "wifi_pass") {
            if (!needStr() || str.size() > 63) return where + "bad wifi_pass";
            s.wifiPass = str;
        } else if (key == "callsign") {
            if (!needStr()) return where + "bad callsign";
            s.callsign = str;
        } else if (key == "reflector_module") {
            if (!needStr() || !normalizeModule(str, s.reflectorModule).empty())
                return where + "bad reflector_module";
        } else if (key == "reflector") {
            if (!needStr()) return where + "bad reflector";
            s.reflector = str;
        } else if (key == "extra_hosts") {
            if (!needStr()) return where + "bad extra_hosts";
            s.extraHosts = str;
        } else if (key == "auto_link") {
            if (!toBool(val, b)) return where + "bad auto_link";
            s.autoLink = b;
        } else if (key == "listen_only") {
            if (!toBool(val, b)) return where + "bad listen_only";
            s.listenOnly = b;
        } else if (key == "ptt_mode") {
            if (!needStr() || (str != "toggle" && str != "momentary")) return where + "bad ptt_mode";
            s.pttMode = str == "toggle" ? PttMode::Toggle : PttMode::Momentary;
        } else if (key == "volume") {
            if (!toLong(val, n) || n < 0 || n > 100) return where + "bad volume";
            s.volume = static_cast<uint8_t>(n);
        } else if (key == "mic_gain_db") {
            if (!toLong(val, n) || n < MIC_GAIN_MIN_DB || n > MIC_GAIN_MAX_DB)
                return where + "bad mic_gain_db";
            s.micGainDb = static_cast<int8_t>(n);
        }
        // Unknown keys: ignored on purpose (forward compatibility).
    }

    if (!sawFormat) return "Not a buttons-m17 backup (no format line)";
    s.adminPass = base.adminPass;
    const std::string err = validate(s);
    if (!err.empty()) return err;
    out = s;
    return "";
}

}  // namespace cfg
