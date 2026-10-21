#include "config/settings.h"

#include <cctype>
#include <cstdlib>

namespace cfg {

const char *const DEFAULT_EXTRA_HOSTS =
    "# Extra or overriding reflectors, one per line: M17-XXX host [port]\n"
    "# Port defaults to 17000. Entries here win over the built-in list.\n";

namespace {

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

std::string upper(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

bool isAlnumAscii(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

}  // namespace

std::string normalizeCallsign(const std::string &in, std::string &out) {
    const std::string s = upper(trim(in));
    if (s.size() < 3 || s.size() > CALLSIGN_MAX) return "Callsign must be 3-9 characters";
    // M17 address alphabet (spec, Address Encoding): A-Z 0-9 - / . (space is
    // padding only). A callsign must start with a letter or digit.
    bool letter = false, digit = false;
    for (char c : s) {
        if (c >= 'A' && c <= 'Z') letter = true;
        else if (c >= '0' && c <= '9') digit = true;
        else if (c != '-' && c != '/' && c != '.')
            return "Callsign may only contain A-Z, 0-9, '-', '/' and '.'";
    }
    if (!isAlnumAscii(s[0])) return "Callsign must start with a letter or digit";
    if (!letter || !digit) return "Callsign must contain letters and a digit";
    out = s;
    return "";
}

std::string normalizeModule(const std::string &in, char &out) {
    const std::string s = upper(trim(in));
    if (s.size() != 1 || s[0] < 'A' || s[0] > 'Z') return "Module must be one letter A-Z";
    out = s[0];
    return "";
}

std::string normalizeReflector(const std::string &in, std::string &out) {
    const std::string s = upper(trim(in));
    if (s.size() != 7 || s.compare(0, 4, "M17-") != 0)
        return "Reflector must look like M17-XXX";
    for (size_t i = 4; i < 7; ++i)
        if (!isAlnumAscii(s[i])) return "Reflector name must end in 3 letters/digits";
    out = s;
    return "";
}

std::string parseHostList(const std::string &text, std::vector<HostEntry> &out) {
    out.clear();
    size_t pos = 0;
    int lineNo = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        const std::string line = trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        ++lineNo;
        if (line.empty() || line[0] == '#') continue;

        std::vector<std::string> tok;
        size_t i = 0;
        while (i < line.size()) {
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) ++i;
            size_t j = i;
            while (j < line.size() && !std::isspace(static_cast<unsigned char>(line[j]))) ++j;
            if (j > i) tok.push_back(line.substr(i, j - i));
            i = j;
        }
        const std::string where = "Host list line " + std::to_string(lineNo) + ": ";
        if (tok.size() < 2 || tok.size() > 3) return where + "expected M17-XXX host [port]";

        HostEntry e;
        if (!normalizeReflector(tok[0], e.name).empty()) return where + "bad reflector name";
        e.host = tok[1];
        for (char c : e.host) {
            const bool ok = std::isalnum(static_cast<unsigned char>(c)) || c == '.' || c == '-';
            if (!ok) return where + "bad hostname";
        }
        if (tok.size() == 3) {
            char *end = nullptr;
            const long p = std::strtol(tok[2].c_str(), &end, 10);
            if (*end != '\0' || p < 1 || p > 65535) return where + "bad port";
            e.port = static_cast<uint16_t>(p);
        }
        out.push_back(e);
    }
    return "";
}

std::string validate(Settings &s) {
    std::string err;
    if (!s.callsign.empty() && !(err = normalizeCallsign(s.callsign, s.callsign)).empty()) return err;
    if (!(err = normalizeReflector(s.reflector, s.reflector)).empty()) return err;
    if (!(err = normalizeModule(std::string(1, s.reflectorModule), s.reflectorModule)).empty())
        return "Reflector " + err;
    std::vector<HostEntry> hosts;
    if (!(err = parseHostList(s.extraHosts, hosts)).empty()) return err;
    HostEntry found;
    if (!findReflector(s.reflector, hosts, found))
        return "Unknown reflector " + s.reflector + "; add it under extra hosts";
    if (s.volume > 100) return "Volume must be 0-100";
    if (s.micGainDb < MIC_GAIN_MIN_DB || s.micGainDb > MIC_GAIN_MAX_DB)
        return "Mic gain must be " + std::to_string(MIC_GAIN_MIN_DB) + ".." +
               std::to_string(MIC_GAIN_MAX_DB) + " dB";
    return "";
}

bool canTransmit(const Settings &s) {
    std::string norm;
    return !s.listenOnly && !s.callsign.empty() && normalizeCallsign(s.callsign, norm).empty();
}

}  // namespace cfg
