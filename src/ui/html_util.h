#pragma once
// Escaping helpers for the web UI. Pure C++, host-tested.

#include <string>

std::string htmlEscape(const std::string &s);
std::string jsonEscape(const std::string &s);
