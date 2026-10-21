// Host tests for config validation, settings backup round-trip, the web
// request guard and escaping. Run: pio test -e native
#include <unity.h>

#include "config/backup.h"
#include "config/settings.h"
#include "ui/html_util.h"
#include "ui/http_guard.h"

using namespace cfg;

void setUp() {}
void tearDown() {}

// ── Callsign / transmit inhibit ──────────────────────────────────────────────

void test_callsign_normalised() {
    std::string out;
    TEST_ASSERT_EQUAL_STRING("", normalizeCallsign("  k8uns ", out).c_str());
    TEST_ASSERT_EQUAL_STRING("K8UNS", out.c_str());
}

void test_callsign_m17_alphabet() {
    std::string out;
    TEST_ASSERT_EQUAL_STRING("", normalizeCallsign("k8uns/p", out).c_str());
    TEST_ASSERT_EQUAL_STRING("K8UNS/P", out.c_str());
    TEST_ASSERT_EQUAL_STRING("", normalizeCallsign("AB1CD-1.X", out).c_str());  // 9 chars
}

void test_callsign_rejects_bad() {
    std::string out;
    TEST_ASSERT_FALSE(normalizeCallsign("AB", out).empty());          // too short
    TEST_ASSERT_FALSE(normalizeCallsign("AB1CD-1.XY", out).empty());  // too long (10)
    TEST_ASSERT_FALSE(normalizeCallsign("KDAAA", out).empty());       // no digit
    TEST_ASSERT_FALSE(normalizeCallsign("12345", out).empty());       // no letter
    TEST_ASSERT_FALSE(normalizeCallsign("K8 UNS", out).empty());      // inner space
    TEST_ASSERT_FALSE(normalizeCallsign("K8UNS!", out).empty());      // not in alphabet
    TEST_ASSERT_FALSE(normalizeCallsign("/K8UNS", out).empty());      // must start alnum
}

void test_transmit_inhibited_without_callsign() {
    Settings s;
    TEST_ASSERT_FALSE(canTransmit(s));
    s.callsign = "N0CALL";
    TEST_ASSERT_TRUE(canTransmit(s));
    s.listenOnly = true;
    TEST_ASSERT_FALSE(canTransmit(s));
    s.listenOnly = false;
    s.callsign = "bad!";
    TEST_ASSERT_FALSE(canTransmit(s));
}

void test_reflector_names() {
    std::string out;
    TEST_ASSERT_EQUAL_STRING("", normalizeReflector(" m17-usa ", out).c_str());
    TEST_ASSERT_EQUAL_STRING("M17-USA", out.c_str());
    TEST_ASSERT_FALSE(normalizeReflector("XRF089", out).empty());
    TEST_ASSERT_FALSE(normalizeReflector("M17-US", out).empty());
    TEST_ASSERT_FALSE(normalizeReflector("M17-U.A", out).empty());
}

// ── Reflector directory ──────────────────────────────────────────────────────

void test_host_list_parse() {
    std::vector<HostEntry> h;
    const std::string err =
        parseHostList("# comment\n\nm17-zzz m17.example.org\nM17-ZZY 10.0.0.5 17002\n", h);
    TEST_ASSERT_EQUAL_STRING("", err.c_str());
    TEST_ASSERT_EQUAL(2, h.size());
    TEST_ASSERT_EQUAL_STRING("M17-ZZZ", h[0].name.c_str());
    TEST_ASSERT_EQUAL_STRING("m17.example.org", h[0].host.c_str());
    TEST_ASSERT_EQUAL(M17_DEFAULT_PORT, h[0].port);
    TEST_ASSERT_EQUAL(17002, h[1].port);
}

void test_host_list_errors_name_line() {
    std::vector<HostEntry> h;
    TEST_ASSERT_EQUAL_STRING("Host list line 2: bad port",
                             parseHostList("M17-AAA a.b\nM17-AAB c.d 99999\n", h).c_str());
    TEST_ASSERT_FALSE(parseHostList("M17-AAA a.b 0\n", h).empty());
    TEST_ASSERT_FALSE(parseHostList("M17-AAA\n", h).empty());
    TEST_ASSERT_FALSE(parseHostList("M17-AAA a;b\n", h).empty());
    TEST_ASSERT_FALSE(parseHostList("XRF001 a.b\n", h).empty());
}

void test_builtin_reflectors_sane() {
    TEST_ASSERT_TRUE(BUILTIN_REFLECTOR_COUNT > 50);
    std::string prev, norm;
    for (size_t i = 0; i < BUILTIN_REFLECTOR_COUNT; ++i) {
        const ReflectorEntry &r = BUILTIN_REFLECTORS[i];
        TEST_ASSERT_EQUAL_STRING_MESSAGE("", normalizeReflector(r.name, norm).c_str(), r.name);
        TEST_ASSERT_TRUE_MESSAGE(prev < r.name, r.name);  // sorted, unique
        TEST_ASSERT_TRUE(r.port != 0 && r.host[0] != '\0');
        prev = r.name;
    }
}

void test_find_reflector_extra_overrides_builtin() {
    std::vector<HostEntry> extra;
    HostEntry e;
    TEST_ASSERT_TRUE(findReflector(DEFAULT_REFLECTOR, extra, e));
    TEST_ASSERT_FALSE(findReflector("M17-ZZZ", extra, e));
    TEST_ASSERT_EQUAL_STRING("", parseHostList("M17-M17 override.example 17009\n", extra).c_str());
    TEST_ASSERT_TRUE(findReflector("M17-M17", extra, e));
    TEST_ASSERT_EQUAL_STRING("override.example", e.host.c_str());
    TEST_ASSERT_EQUAL(17009, e.port);
}

void test_default_settings_valid() {
    Settings s;
    s.extraHosts = DEFAULT_EXTRA_HOSTS;
    TEST_ASSERT_EQUAL_STRING("", validate(s).c_str());
}

void test_validate_rejects_unknown_reflector() {
    Settings s;
    s.reflector = "M17-ZZZ";
    TEST_ASSERT_FALSE(validate(s).empty());
    s.extraHosts = "M17-ZZZ m17.example.org\n";
    TEST_ASSERT_EQUAL_STRING("", validate(s).c_str());
}

// ── Backup round-trip ────────────────────────────────────────────────────────

Settings sample() {
    Settings s;
    s.wifiSsid = "Club \"Net\"";
    s.wifiPass = "p\\ss#word";
    s.adminPass = "secret-admin";
    s.callsign = "K8UNS";
    s.reflector = "M17-ZZZ";
    s.reflectorModule = 'C';
    s.extraHosts = "# hosts: \"mine\"\nM17-ZZZ m17.example.org 17002\n";
    s.autoLink = true;
    s.listenOnly = true;
    s.pttMode = PttMode::Toggle;
    s.volume = 42;
    s.micGainDb = -6;
    return s;
}

void test_backup_round_trip() {
    const Settings s = sample();
    const std::string y = settingsToYaml(s);
    Settings base;  // different admin pass, defaults elsewhere
    base.adminPass = "device-admin";
    base.extraHosts = DEFAULT_EXTRA_HOSTS;
    Settings out;
    TEST_ASSERT_EQUAL_STRING("", settingsFromYaml(y, base, out).c_str());
    TEST_ASSERT_EQUAL_STRING(s.wifiSsid.c_str(), out.wifiSsid.c_str());
    TEST_ASSERT_EQUAL_STRING(s.wifiPass.c_str(), out.wifiPass.c_str());
    TEST_ASSERT_EQUAL_STRING("K8UNS", out.callsign.c_str());
    TEST_ASSERT_EQUAL_STRING("M17-ZZZ", out.reflector.c_str());
    TEST_ASSERT_EQUAL('C', out.reflectorModule);
    TEST_ASSERT_EQUAL_STRING(s.extraHosts.c_str(), out.extraHosts.c_str());
    TEST_ASSERT_TRUE(out.listenOnly);
    TEST_ASSERT_TRUE(out.pttMode == PttMode::Toggle);
    TEST_ASSERT_TRUE(out.autoLink);
    TEST_ASSERT_EQUAL(42, out.volume);
    TEST_ASSERT_EQUAL(-6, out.micGainDb);
    // Admin password is never exported, and never overwritten by a restore.
    TEST_ASSERT_TRUE(y.find("secret-admin") == std::string::npos);
    TEST_ASSERT_EQUAL_STRING("device-admin", out.adminPass.c_str());
}

void test_backup_rejects_and_leaves_output_untouched() {
    Settings base;
    base.extraHosts = DEFAULT_EXTRA_HOSTS;
    Settings out;
    out.callsign = "UNCHANGED1";
    TEST_ASSERT_FALSE(settingsFromYaml("callsign: \"K8UNS\"\n", base, out).empty());  // no format
    TEST_ASSERT_FALSE(settingsFromYaml("format: \"something-else\"\n", base, out).empty());
    const std::string hdr = std::string("format: \"") + BACKUP_FORMAT + "\"\n";
    TEST_ASSERT_FALSE(settingsFromYaml(hdr + "volume: 101\n", base, out).empty());
    TEST_ASSERT_FALSE(settingsFromYaml(hdr + "callsign: \"BAD!\"\n", base, out).empty());
    TEST_ASSERT_FALSE(settingsFromYaml(hdr + "callsign: \"unterminated\n", base, out).empty());
    TEST_ASSERT_FALSE(settingsFromYaml(hdr + "version: 99\n", base, out).empty());
    TEST_ASSERT_EQUAL_STRING("UNCHANGED1", out.callsign.c_str());
}

void test_backup_partial_and_unknown_keys() {
    Settings base;
    base.extraHosts = DEFAULT_EXTRA_HOSTS;
    base.volume = 10;
    Settings out;
    const std::string y = std::string("format: \"") + BACKUP_FORMAT +
                          "\"\nfuture_key: 1\ncallsign: \"n0call\"  # mine\n";
    TEST_ASSERT_EQUAL_STRING("", settingsFromYaml(y, base, out).c_str());
    TEST_ASSERT_EQUAL_STRING("N0CALL", out.callsign.c_str());
    TEST_ASSERT_EQUAL(10, out.volume);  // absent key keeps current value
}

// ── Web request guard ────────────────────────────────────────────────────────

void test_host_header_guard() {
    TEST_ASSERT_TRUE(hostHeaderAllowed("192.168.4.1", "m17"));
    TEST_ASSERT_TRUE(hostHeaderAllowed("192.168.1.20:80", "m17"));
    TEST_ASSERT_TRUE(hostHeaderAllowed("m17.local", "m17"));
    TEST_ASSERT_TRUE(hostHeaderAllowed("M17.lan.", "m17"));
    TEST_ASSERT_TRUE(hostHeaderAllowed("", "m17"));
    TEST_ASSERT_FALSE(hostHeaderAllowed("evil.example.com", "m17"));
    TEST_ASSERT_FALSE(hostHeaderAllowed("m17x.local", "m17"));
}

void test_origin_guard() {
    TEST_ASSERT_TRUE(sourceMatchesHost("http://m17.local", "m17.local"));
    TEST_ASSERT_TRUE(sourceMatchesHost("http://192.168.4.1/backup?x=1", "192.168.4.1"));
    TEST_ASSERT_FALSE(sourceMatchesHost("http://evil.com/", "m17.local"));
    TEST_ASSERT_FALSE(sourceMatchesHost("null", "m17.local"));
    TEST_ASSERT_FALSE(sourceMatchesHost("http://m17.local@evil.com/", "m17.local"));
}

void test_escaping() {
    TEST_ASSERT_EQUAL_STRING("&lt;a href=&quot;x&quot;&gt;&amp;&#39;",
                             htmlEscape("<a href=\"x\">&'").c_str());
    TEST_ASSERT_EQUAL_STRING("a\\\"b\\\\c\\n\\u0001", jsonEscape("a\"b\\c\n\x01").c_str());
}

int main(int, char **) {
    UNITY_BEGIN();
    RUN_TEST(test_callsign_normalised);
    RUN_TEST(test_callsign_m17_alphabet);
    RUN_TEST(test_callsign_rejects_bad);
    RUN_TEST(test_transmit_inhibited_without_callsign);
    RUN_TEST(test_reflector_names);
    RUN_TEST(test_host_list_parse);
    RUN_TEST(test_host_list_errors_name_line);
    RUN_TEST(test_builtin_reflectors_sane);
    RUN_TEST(test_find_reflector_extra_overrides_builtin);
    RUN_TEST(test_default_settings_valid);
    RUN_TEST(test_validate_rejects_unknown_reflector);
    RUN_TEST(test_backup_round_trip);
    RUN_TEST(test_backup_rejects_and_leaves_output_untouched);
    RUN_TEST(test_backup_partial_and_unknown_keys);
    RUN_TEST(test_host_header_guard);
    RUN_TEST(test_origin_guard);
    RUN_TEST(test_escaping);
    return UNITY_END();
}
