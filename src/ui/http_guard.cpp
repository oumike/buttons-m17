#include "ui/http_guard.h"

#include <cctype>

namespace {

std::string lower(std::string s) {
    for (auto &c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string &s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

bool isIpv4Literal(const std::string &h) {
    int dots = 0;
    if (h.empty()) return false;
    for (char c : h) {
        if (c == '.') ++dots;
        else if (c < '0' || c > '9') return false;
    }
    return dots == 3;
}

}  // namespace

bool hostHeaderAllowed(const std::string &hostHeader, const std::string &deviceName) {
    const std::string raw = trim(hostHeader);
    if (raw.empty()) return true;   // HTTP/1.0: nothing to rebind
    if (raw[0] == '[') return true; // IPv6 literal

    std::string host = raw;
    const size_t colon = host.find(':');
    if (colon != std::string::npos) host = host.substr(0, colon);
    host = lower(host);
    if (!host.empty() && host.back() == '.') host.pop_back();

    if (isIpv4Literal(host) || host == "localhost") return true;
    const std::string n = lower(deviceName);
    if (n.empty()) return false;
    if (host == n) return true;
    return host.size() > n.size() + 1 && host.compare(0, n.size(), n) == 0 && host[n.size()] == '.';
}

bool sourceMatchesHost(const std::string &source, const std::string &hostHeader) {
    const std::string src = trim(source);
    const size_t scheme = src.find("://");
    if (scheme == std::string::npos) return false;   // includes "null"
    std::string auth = src.substr(scheme + 3);
    const size_t end = auth.find_first_of("/?#");
    if (end != std::string::npos) auth = auth.substr(0, end);
    const size_t at = auth.rfind('@');
    if (at != std::string::npos) auth = auth.substr(at + 1);
    return lower(auth) == lower(trim(hostHeader));
}
