#pragma once
// Cross-site request / DNS-rebinding guard for the web UI, after camillia-mt's.
// Pure C++, host-tested.
//
// State-changing requests (every POST) and GETs that hand out secrets (the
// backup download) must name this device in Host, and any Origin/Referer the
// browser sent must carry that same authority. Requests with neither header
// (curl, scripts) are accepted.

#include <string>

// `hostHeader` is the raw Host header; `deviceName` the mDNS/DHCP hostname
// ("m17"). Allowed: empty Host, IPv4/IPv6 literals, localhost, and the
// device name bare or under any DNS suffix (m17.local, m17.lan ...).
bool hostHeaderAllowed(const std::string &hostHeader, const std::string &deviceName);

// `source` is an Origin ("http://a:80") or Referer ("http://a/x?y") value.
// True when its authority equals `hostHeader` (case-insensitive).
bool sourceMatchesHost(const std::string &source, const std::string &hostHeader);
