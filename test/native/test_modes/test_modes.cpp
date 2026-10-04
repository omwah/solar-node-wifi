#include <controller.h>
#include <unity.h>

using namespace modes;

void setUp() {}
void tearDown() {}

namespace
{

constexpr uint64_t S = 1000;
constexpr uint64_t MIN = 60 * S;
constexpr uint64_t H = 60 * MIN;
constexpr uint64_t BOOT = 5 * S;

void assertMode(Mode expected, const Controller &c, uint64_t now)
{
    TEST_ASSERT_EQUAL_STRING(modeName(expected), modeName(c.mode(now)));
}

// Runs one poll at `now` with the given result, after checking it was due.
void poll(Controller &c, uint64_t now, PollResult result = PollResult::Ok)
{
    TEST_ASSERT_TRUE(c.pollDue(now));
    c.onPollStarted(now);
    c.onPollResult(now, result);
}

// A controller whose node is alive and talking, so node-down never triggers.
Controller alive(Config config = {}, bool configValid = true)
{
    Controller c(config, BOOT, configValid);
    c.onNodeActivity(BOOT + S);
    return c;
}

} // namespace

void test_no_wifi_during_boot_window()
{
    Controller c = alive();
    TEST_ASSERT_FALSE(c.pollDue(BOOT + 9 * S));
    TEST_ASSERT_TRUE(c.pollDue(BOOT + 10 * S));
}

void test_normal_poll_interval()
{
    Controller c = alive();
    uint64_t t = BOOT + 10 * S;
    poll(c, t);
    assertMode(Mode::Normal, c, t);
    c.onNodeActivity(t + 5 * MIN);
    TEST_ASSERT_FALSE(c.pollDue(t + 599 * S));
    TEST_ASSERT_TRUE(c.pollDue(t + 600 * S));
    TEST_ASSERT_EQUAL_UINT64(t + 600 * S, c.nextPollAtMs(t + S));
}

void test_backoff_doubles_caps_and_resets()
{
    Config config;
    config.nodeSilenceS = 24 * 3600; // backoffs run longer than the default silence timeout
    Controller c = alive(config);
    uint64_t t = BOOT + 10 * S;
    const uint32_t expected[] = {600, 1200, 2400, 4800, 9600, 19200, 21600, 21600};
    for (uint32_t backoff : expected) {
        c.onNodeActivity(t);
        poll(c, t, PollResult::ConnectFailed);
        assertMode(Mode::Backoff, c, t);
        TEST_ASSERT_EQUAL_UINT32(backoff, c.currentBackoffS());
        TEST_ASSERT_FALSE(c.pollDue(t + backoff * S - 1));
        t += backoff * S;
    }
    c.onNodeActivity(t);
    poll(c, t, PollResult::Ok);
    assertMode(Mode::Normal, c, t);
    TEST_ASSERT_EQUAL_UINT32(0, c.currentBackoffS());
}

void test_auth_failure_jumps_to_max_backoff()
{
    Controller c = alive();
    poll(c, BOOT + 10 * S, PollResult::AuthFailed);
    TEST_ASSERT_EQUAL_UINT32(6 * 3600, c.currentBackoffS());
}

void test_commissioning_fast_polls_then_exits_after_three_good()
{
    Controller c = alive({}, false);
    uint64_t t = BOOT + 10 * S;
    assertMode(Mode::Commissioning, c, t);
    poll(c, t, PollResult::ConnectFailed); // failures keep the fast cadence
    assertMode(Mode::Commissioning, c, t);
    for (int i = 0; i < 3; i++) {
        t += 30 * S;
        poll(c, t);
    }
    assertMode(Mode::Normal, c, t);
}

void test_commissioning_times_out_after_an_hour()
{
    Controller c(Config{}, BOOT, false);
    for (uint64_t t = BOOT + 10 * S; t < BOOT + H; t += 30 * S) {
        c.onNodeActivity(t);
        if (c.pollDue(t)) {
            c.onPollStarted(t);
            c.onPollResult(t, PollResult::NoData);
        }
    }
    c.onNodeActivity(BOOT + H + S);
    TEST_ASSERT_FALSE(c.mode(BOOT + H + S) == Mode::Commissioning);
}

void test_node_never_seen_goes_down_after_first_contact_window()
{
    Controller c(Config{}, BOOT, true);
    assertMode(Mode::Normal, c, BOOT + 14 * MIN);
    assertMode(Mode::NodeDown, c, BOOT + 16 * MIN);
}

void test_node_silence_goes_down_and_heartbeats_every_six_hours()
{
    Controller c = alive();
    uint64_t t = BOOT + 10 * S;
    poll(c, t);
    uint64_t down = BOOT + S + 3 * H + S;
    assertMode(Mode::NodeDown, c, down);
    TEST_ASSERT_EQUAL_UINT64(t + 6 * H, c.nextPollAtMs(down));
    TEST_ASSERT_FALSE(c.pollDue(t + 6 * H - S));
    TEST_ASSERT_TRUE(c.pollDue(t + 6 * H));
}

void test_node_return_restarts_wifi_delay()
{
    Controller c = alive();
    uint64_t t = BOOT + 10 * S;
    poll(c, t);
    uint64_t back = BOOT + 5 * H;
    assertMode(Mode::NodeDown, c, back);
    c.onNodeActivity(back);
    assertMode(Mode::Normal, c, back);
    TEST_ASSERT_FALSE(c.pollDue(back + 9 * S));
    TEST_ASSERT_TRUE(c.pollDue(back + 10 * S));
}

void test_session_keeps_connected_and_is_capped()
{
    Controller c = alive();
    uint64_t t = BOOT + 10 * S;
    poll(c, t);
    c.requestSession(t, 5 * 3600);
    assertMode(Mode::Session, c, t);
    TEST_ASSERT_TRUE(c.keepConnected(t + H));
    c.onNodeActivity(t + 1 * H);
    assertMode(Mode::Session, c, t + 2 * H - S);
    c.onNodeActivity(t + 2 * H);
    assertMode(Mode::Normal, c, t + 2 * H + S);
}

void test_session_default_length_and_early_end()
{
    Controller c = alive();
    uint64_t t = BOOT + 10 * S;
    c.requestSession(t, 0);
    assertMode(Mode::Session, c, t + 29 * MIN);
    c.endSession();
    assertMode(Mode::Normal, c, t + MIN);
}

void test_battery_ignored_without_interlock()
{
    Controller c = alive();
    c.onBattery(BOOT + 10 * S, 3.0f);
    assertMode(Mode::Normal, c, BOOT + 10 * S);
}

void test_low_battery_blocks_wifi_until_voltage_and_node_return()
{
    Config config;
    config.batteryInterlock = true;
    Controller c = alive(config);
    uint64_t t = BOOT + 10 * S;
    c.onBattery(t, 3.30f);
    assertMode(Mode::LowBatt, c, t);
    TEST_ASSERT_FALSE(c.pollDue(t));
    c.onBattery(t + H, 3.70f); // voltage back, but no node traffic since
    assertMode(Mode::LowBatt, c, t + H);
    c.onNodeActivity(t + H + S);
    c.onBattery(t + H + 2 * S, 3.70f);
    TEST_ASSERT_FALSE(c.mode(t + H + 2 * S) == Mode::LowBatt);
}

void test_low_battery_hysteresis()
{
    Config config;
    config.batteryInterlock = true;
    Controller c = alive(config);
    uint64_t t = BOOT + 10 * S;
    c.onBattery(t, 3.35f);
    c.onNodeActivity(t + S);
    c.onBattery(t + 2 * S, 3.50f); // above low, below resume
    assertMode(Mode::LowBatt, c, t + 2 * S);
}

void test_daily_wifi_cap()
{
    Config config;
    config.wifiStartsPerDay = 3;
    config.pollIntervalS = 60;
    config.nodeSilenceS = 48 * 3600;
    Controller c = alive(config);
    uint64_t t = BOOT + 10 * S;
    for (int i = 0; i < 3; i++) {
        c.onNodeActivity(t);
        poll(c, t);
        t += MIN;
    }
    c.onNodeActivity(t);
    TEST_ASSERT_FALSE(c.pollDue(t));
    uint64_t nextDay = BOOT + 24 * H; // the window starts at boot
    TEST_ASSERT_EQUAL_UINT64(nextDay, c.nextPollAtMs(t));
    c.onNodeActivity(nextDay);
    TEST_ASSERT_TRUE(c.pollDue(nextDay));
}

int main()
{
    UNITY_BEGIN();
    RUN_TEST(test_no_wifi_during_boot_window);
    RUN_TEST(test_normal_poll_interval);
    RUN_TEST(test_backoff_doubles_caps_and_resets);
    RUN_TEST(test_auth_failure_jumps_to_max_backoff);
    RUN_TEST(test_commissioning_fast_polls_then_exits_after_three_good);
    RUN_TEST(test_commissioning_times_out_after_an_hour);
    RUN_TEST(test_node_never_seen_goes_down_after_first_contact_window);
    RUN_TEST(test_node_silence_goes_down_and_heartbeats_every_six_hours);
    RUN_TEST(test_node_return_restarts_wifi_delay);
    RUN_TEST(test_session_keeps_connected_and_is_capped);
    RUN_TEST(test_session_default_length_and_early_end);
    RUN_TEST(test_battery_ignored_without_interlock);
    RUN_TEST(test_low_battery_blocks_wifi_until_voltage_and_node_return);
    RUN_TEST(test_low_battery_hysteresis);
    RUN_TEST(test_daily_wifi_cap);
    return UNITY_END();
}
