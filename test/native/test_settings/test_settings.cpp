#include <cstring>
#include <map>
#include <settings.h>
#include <unity.h>

using namespace settings;

namespace
{

class MemoryStore : public Store
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

} // namespace

void setUp() {}
void tearDown() {}

void test_key_names_fit_nvs()
{
    size_t count;
    const KeyInfo *keys = Settings::keys(count);
    for (size_t i = 0; i < count; i++) {
        TEST_ASSERT_TRUE_MESSAGE(std::strlen(keys[i].name) <= 15, keys[i].name);
    }
}

void test_defaults()
{
    MemoryStore store;
    Settings s(store);
    s.load();
    TEST_ASSERT_EQUAL_UINT32(600, s.u32("poll_interval_s"));
    TEST_ASSERT_EQUAL_UINT32(1883, s.u32("mqtt_port"));
    TEST_ASSERT_FALSE(s.flag("mqtt_tls"));
    TEST_ASSERT_EQUAL_FLOAT(3.40f, s.f32("batt_low_v"));
    TEST_ASSERT_EQUAL_STRING("site-01", s.str("node_id").c_str());
    TEST_ASSERT_FALSE(s.complete());
}

void test_set_validates_types_and_ranges()
{
    MemoryStore store;
    Settings s(store);
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::UnknownKey), static_cast<int>(s.set("nope", "1")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::BadValue), static_cast<int>(s.set("poll_interval_s", "ten")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::BadValue), static_cast<int>(s.set("poll_interval_s", "600.5")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::OutOfRange), static_cast<int>(s.set("poll_interval_s", "30")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::BadValue), static_cast<int>(s.set("mqtt_tls", "maybe")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::OutOfRange), static_cast<int>(s.set("node_id", "")));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::Ok), static_cast<int>(s.set("mqtt_tls", "on")));
    TEST_ASSERT_TRUE(s.flag("mqtt_tls"));
    TEST_ASSERT_EQUAL(static_cast<int>(SetResult::Ok), static_cast<int>(s.set("batt_low_v", "3.3")));
}

void test_commit_persists_only_after_commit()
{
    MemoryStore store;
    Settings s(store);
    s.set("wifi_ssid", "home");
    TEST_ASSERT_TRUE(s.dirty());
    TEST_ASSERT_TRUE(store.data.empty());
    TEST_ASSERT_TRUE(s.commit());
    TEST_ASSERT_FALSE(s.dirty());
    TEST_ASSERT_EQUAL_STRING("home", store.data["wifi_ssid"].c_str());

    Settings reloaded(store);
    reloaded.load();
    TEST_ASSERT_EQUAL_STRING("home", reloaded.str("wifi_ssid").c_str());
}

void test_secrets_are_redacted()
{
    MemoryStore store;
    Settings s(store);
    TEST_ASSERT_EQUAL_STRING("", s.display("wifi_pass").c_str());
    s.set("wifi_pass", "hunter2");
    TEST_ASSERT_EQUAL_STRING("<set>", s.display("wifi_pass").c_str());
    TEST_ASSERT_EQUAL_STRING("hunter2", s.str("wifi_pass").c_str());
}

void test_complete_and_factory_reset()
{
    MemoryStore store;
    Settings s(store);
    s.set("wifi_ssid", "home");
    s.set("mqtt_host", "10.0.0.2");
    TEST_ASSERT_TRUE(s.complete());
    s.commit();
    s.factoryReset();
    TEST_ASSERT_TRUE(store.data.empty());
    TEST_ASSERT_FALSE(s.complete());
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_key_names_fit_nvs);
    RUN_TEST(test_defaults);
    RUN_TEST(test_set_validates_types_and_ranges);
    RUN_TEST(test_commit_persists_only_after_commit);
    RUN_TEST(test_secrets_are_redacted);
    RUN_TEST(test_complete_and_factory_reset);
    return UNITY_END();
}
