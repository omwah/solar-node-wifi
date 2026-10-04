#include <commands.h>
#include <cstring>
#include <map>
#include <unity.h>

using namespace cli;

namespace
{

class MemoryStore : public settings::Store
{
  public:
    std::map<std::string, std::string> data;
    bool load(const char *key, std::string &value) override
    {
        auto it = data.find(key);
        if (it == data.end()) {
            return false;
        }
        value = it->second;
        return true;
    }
    bool save(const char *key, const std::string &value) override
    {
        data[key] = value;
        return true;
    }
    bool erase(const char *key) override
    {
        data.erase(key);
        return true;
    }
};

class FakeActions : public Actions
{
  public:
    int applied = 0, polls = 0, reboots = 0, resets = 0, sessionsEnded = 0;
    int sessionMinutes = -1;
    std::string otaUrl;
    void status(JsonObject out) override { out["mode"] = "NORMAL"; }
    void applySettings() override { applied++; }
    void pollNow() override { polls++; }
    void reboot() override { reboots++; }
    void factoryReset() override { resets++; }
    void startSession(uint32_t minutes) override { sessionMinutes = static_cast<int>(minutes); }
    void endSession() override { sessionsEnded++; }
    void log(size_t lines, JsonArray out) override
    {
        for (size_t i = 0; i < lines && i < 3; i++) {
            out.add("line");
        }
    }
    bool startOta(const std::string &url, const std::string &, std::string &) override
    {
        otaUrl = url;
        return true;
    }
};

struct Fixture {
    MemoryStore store;
    settings::Settings settings{store};
    FakeActions actions;
    Dispatcher dispatcher{settings, actions};
    JsonDocument response;

    JsonDocument &line(const char *text)
    {
        Request r;
        TEST_ASSERT_TRUE(parseLine(text, r));
        dispatcher.run(r, response);
        return response;
    }

    JsonDocument &json(const char *text)
    {
        JsonDocument doc;
        Request r;
        TEST_ASSERT_TRUE(parseJson(text, std::strlen(text), doc, r));
        dispatcher.run(r, response);
        return response;
    }
};

const char *SHA = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

} // namespace

void setUp() {}
void tearDown() {}

void test_parse_line_quotes_and_escapes()
{
    Request r;
    TEST_ASSERT_TRUE(parseLine("  set wifi_pass \"two \\\"quoted\\\" words\"  ", r));
    TEST_ASSERT_EQUAL_STRING("set", r.cmd.c_str());
    TEST_ASSERT_EQUAL_UINT(2, r.args.size());
    TEST_ASSERT_EQUAL_STRING("two \"quoted\" words", r.args[1].c_str());
    TEST_ASSERT_FALSE(parseLine("set x \"unterminated", r));
    TEST_ASSERT_FALSE(parseLine("   ", r));
    TEST_ASSERT_TRUE(parseLine("set ssid \"\"", r));
    TEST_ASSERT_EQUAL_STRING("", r.args[1].c_str());
}

void test_set_commit_get_round_trip()
{
    Fixture f;
    TEST_ASSERT_TRUE(f.line("set mqtt_host 10.0.0.2")["ok"]);
    TEST_ASSERT_TRUE(f.line("get")["pending_commit"]);
    TEST_ASSERT_TRUE(f.line("commit")["ok"]);
    TEST_ASSERT_EQUAL(1, f.actions.applied);
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", f.line("get mqtt_host")["result"]["mqtt_host"]);
    TEST_ASSERT_EQUAL_STRING("10.0.0.2", f.store.data["mqtt_host"].c_str());
}

void test_get_never_returns_secrets()
{
    Fixture f;
    f.line("set mqtt_pass s3cret");
    TEST_ASSERT_EQUAL_STRING("<set>", f.line("get mqtt_pass")["result"]["mqtt_pass"]);
    TEST_ASSERT_EQUAL_STRING("<set>", f.line("get")["result"]["mqtt_pass"]);
}

void test_set_errors_are_reported()
{
    Fixture f;
    JsonDocument &r = f.line("set poll_interval_s 5");
    TEST_ASSERT_FALSE(r["ok"]);
    TEST_ASSERT_EQUAL_STRING("out of range: poll_interval_s", r["error"]);
    TEST_ASSERT_FALSE(f.line("set only_key")["ok"]);
    TEST_ASSERT_FALSE(f.line("get bogus")["ok"]);
}

void test_json_command_with_id()
{
    Fixture f;
    JsonDocument &r = f.json(R"({"id": 42, "cmd": "set", "args": {"key": "poll_interval_s", "value": 900}})");
    TEST_ASSERT_TRUE(r["ok"]);
    TEST_ASSERT_EQUAL(42, r["id"].as<int>());
    TEST_ASSERT_EQUAL_UINT32(900, f.settings.u32("poll_interval_s"));
}

void test_json_session_and_end()
{
    Fixture f;
    f.json(R"({"id": "a", "cmd": "session", "args": {"minutes": 45}})");
    TEST_ASSERT_EQUAL(45, f.actions.sessionMinutes);
    f.json(R"({"cmd": "session", "args": {"end": true}})");
    TEST_ASSERT_EQUAL(1, f.actions.sessionsEnded);
    f.line("session");
    TEST_ASSERT_EQUAL(0, f.actions.sessionMinutes);
}

void test_poll_now_reboot_log_status()
{
    Fixture f;
    f.json(R"({"cmd": "poll_now"})"); // underscore spelling accepted
    TEST_ASSERT_EQUAL(1, f.actions.polls);
    f.line("reboot");
    TEST_ASSERT_EQUAL(1, f.actions.reboots);
    TEST_ASSERT_EQUAL_UINT(3, f.line("log 10")["result"].size());
    TEST_ASSERT_EQUAL_STRING("NORMAL", f.line("status")["result"]["mode"]);
}

void test_ota_validates_arguments()
{
    Fixture f;
    TEST_ASSERT_FALSE(f.line("ota ftp://x/fw.bin 00")["ok"]);
    std::string bad = std::string("ota http://10.0.0.2/fw.bin ") + "xyz";
    TEST_ASSERT_FALSE(f.line(bad.c_str())["ok"]);
    std::string good = std::string("ota http://10.0.0.2/fw.bin ") + SHA;
    TEST_ASSERT_TRUE(f.line(good.c_str())["ok"]);
    TEST_ASSERT_EQUAL_STRING("http://10.0.0.2/fw.bin", f.actions.otaUrl.c_str());
}

void test_factory_reset_requires_node_id()
{
    Fixture f;
    TEST_ASSERT_FALSE(f.line("factory-reset")["ok"]);
    TEST_ASSERT_FALSE(f.line("factory-reset wrong")["ok"]);
    TEST_ASSERT_EQUAL(0, f.actions.resets);
    TEST_ASSERT_TRUE(f.json(R"({"cmd": "factory-reset", "args": {"confirm": "site-01"}})")["ok"]);
    TEST_ASSERT_EQUAL(1, f.actions.resets);
}

void test_unknown_command_and_help()
{
    Fixture f;
    TEST_ASSERT_FALSE(f.line("frobnicate")["ok"]);
    JsonDocument &r = f.line("help");
    TEST_ASSERT_TRUE(r["result"]["commands"]["status"].is<const char *>());
    TEST_ASSERT_TRUE(r["result"]["keys"]["wifi_ssid"].is<const char *>());
}

void test_malformed_json_rejected()
{
    JsonDocument doc;
    Request r;
    TEST_ASSERT_FALSE(parseJson("{\"cmd\": ", 8, doc, r));
    TEST_ASSERT_FALSE(parseJson("{\"id\": 1}", 9, doc, r));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_parse_line_quotes_and_escapes);
    RUN_TEST(test_set_commit_get_round_trip);
    RUN_TEST(test_get_never_returns_secrets);
    RUN_TEST(test_set_errors_are_reported);
    RUN_TEST(test_json_command_with_id);
    RUN_TEST(test_json_session_and_end);
    RUN_TEST(test_poll_now_reboot_log_status);
    RUN_TEST(test_ota_validates_arguments);
    RUN_TEST(test_factory_reset_requires_node_id);
    RUN_TEST(test_unknown_command_and_help);
    RUN_TEST(test_malformed_json_rejected);
    return UNITY_END();
}
