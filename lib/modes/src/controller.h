#pragma once

#include <cmath>
#include <cstdint>

namespace modes
{

// docs/PLAN.md §4.4. Listed in priority order: an earlier mode overrides a later one.
enum class Mode { LowBatt, NodeDown, Session, Commissioning, Backoff, Normal };

enum class PollResult { Ok, ConnectFailed, AuthFailed, NoData };

struct Config {
    uint32_t pollIntervalS = 600;
    uint32_t maxBackoffS = 6 * 3600;
    uint32_t commissioningIntervalS = 30;
    uint32_t commissioningTimeoutS = 3600;
    uint32_t commissioningGoodPolls = 3;
    uint32_t sessionCapS = 2 * 3600;
    uint32_t sessionDefaultS = 30 * 60;
    uint32_t nodeSilenceS = 3 * 3600;    // 3x the node's telemetry interval
    uint32_t nodeFirstContactS = 15 * 60; // after C3 power-up
    uint32_t nodeDownHeartbeatS = 6 * 3600; // rare poll so the C3 stays remotely reachable
    uint32_t bootWifiDelayS = 10;
    uint32_t wifiStartsPerDay = 400;
    bool batteryInterlock = false; // battery-sense wire fitted
    float battLowV = 3.40f;
    float battResumeV = 3.65f;
};

// Decides when the C3 may start WiFi. Pure logic: the caller supplies a monotonic
// clock in milliseconds and reports events; nothing here touches hardware.
class Controller
{
  public:
    Controller(const Config &config, uint64_t bootMs, bool configValid);

    void setConfig(const Config &config) { config_ = config; }
    const Config &config() const { return config_; }

    void onNodeActivity(uint64_t nowMs);
    void onPollStarted(uint64_t nowMs);
    void onPollResult(uint64_t nowMs, PollResult result);
    void onBattery(uint64_t nowMs, float volts);
    void requestSession(uint64_t nowMs, uint32_t seconds);
    void endSession();
    void requestCommissioning(uint64_t nowMs);

    Mode mode(uint64_t nowMs) const;

    // True when a poll should start now. A started poll must be reported with
    // onPollStarted() and later onPollResult().
    bool pollDue(uint64_t nowMs) const;

    // True while WiFi/MQTT should stay connected after a poll (live session).
    bool keepConnected(uint64_t nowMs) const { return mode(nowMs) == Mode::Session; }

    // Earliest time at which pollDue() can become true, for sleeping until then.
    uint64_t nextPollAtMs(uint64_t nowMs) const;

    bool nodeSeen() const { return nodeSeen_; }
    uint64_t lastNodeActivityMs() const { return lastNodeActivityMs_; }
    uint32_t currentBackoffS() const { return backoffS_; }
    uint32_t wifiStartsToday() const { return startsInWindow_; }

  private:
    bool nodeDown(uint64_t nowMs) const;
    bool wifiAllowed(uint64_t nowMs) const;
    uint64_t intervalMs(Mode mode) const;

    Config config_;
    uint64_t bootMs_;

    bool nodeSeen_ = false;
    uint64_t lastNodeActivityMs_ = 0;
    uint64_t wifiNotBeforeMs_;

    bool everPolled_ = false;
    uint64_t lastPollStartMs_ = 0;
    uint32_t backoffS_ = 0; // 0 = not backing off

    bool commissioning_;
    uint64_t commissioningStartMs_;
    uint32_t commissioningGood_ = 0;

    uint64_t sessionUntilMs_ = 0;

    bool lowBatt_ = false;
    uint64_t lowBattSinceMs_ = 0;

    uint64_t windowStartMs_;
    uint32_t startsInWindow_ = 0;
};

const char *modeName(Mode mode);

} // namespace modes
