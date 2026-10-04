#include <ArduinoJson.h>
#include <discovery.h>
#include <unity.h>

void setUp() {}
void tearDown() {}

namespace
{

const discovery::Message *find(const std::vector<discovery::Message> &msgs, const std::string &topic)
{
    for (const auto &m : msgs) {
        if (m.topic == topic) {
            return &m;
        }
    }
    return nullptr;
}

} // namespace

void test_sensor_config()
{
    auto msgs = discovery::build("site-01", "0.1.0", false);
    const auto *m = find(msgs, "homeassistant/sensor/solarnode_site-01_rssi/config");
    TEST_ASSERT_NOT_NULL(m);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, m->payload));
    TEST_ASSERT_EQUAL_STRING("solarnode/site-01/state", doc["state_topic"]);
    TEST_ASSERT_EQUAL_STRING("dBm", doc["unit_of_measurement"]);
    TEST_ASSERT_EQUAL_STRING("solarnode_site-01", doc["device"]["identifiers"][0]);
    TEST_ASSERT_EQUAL_STRING("0.1.0", doc["device"]["sw_version"]);
}

void test_buttons_publish_valid_commands()
{
    auto msgs = discovery::build("site-01", "0.1.0", false);
    const auto *m = find(msgs, "homeassistant/button/solarnode_site-01_session/config");
    TEST_ASSERT_NOT_NULL(m);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, m->payload));
    TEST_ASSERT_EQUAL_STRING("solarnode/site-01/cmd", doc["command_topic"]);
    JsonDocument cmd;
    TEST_ASSERT_FALSE(deserializeJson(cmd, doc["payload_press"].as<const char *>()));
    TEST_ASSERT_EQUAL_STRING("session", cmd["cmd"]);
    TEST_ASSERT_EQUAL(30, cmd["args"]["minutes"].as<int>());
}

void test_battery_sensor_only_with_wire()
{
    TEST_ASSERT_NULL(find(discovery::build("a", "1", false), "homeassistant/sensor/solarnode_a_battery/config"));
    TEST_ASSERT_NOT_NULL(find(discovery::build("a", "1", true), "homeassistant/sensor/solarnode_a_battery/config"));
}

void test_node_seen_is_binary_sensor()
{
    auto msgs = discovery::build("a", "1", false);
    TEST_ASSERT_NOT_NULL(find(msgs, "homeassistant/binary_sensor/solarnode_a_node_seen/config"));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_sensor_config);
    RUN_TEST(test_buttons_publish_valid_commands);
    RUN_TEST(test_battery_sensor_only_with_wire);
    RUN_TEST(test_node_seen_is_binary_sensor);
    return UNITY_END();
}
