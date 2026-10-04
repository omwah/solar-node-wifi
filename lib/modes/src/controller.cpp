#include "controller.h"

#include <algorithm>

namespace modes
{

namespace
{

constexpr uint64_t S = 1000;
constexpr uint64_t DAY_MS = 24 * 3600 * S;

} // namespace

Controller::Controller(const Config &config, uint64_t bootMs, bool configValid)
    : config_(config), bootMs_(bootMs), wifiNotBeforeMs_(bootMs + config.bootWifiDelayS * S),
      commissioning_(!configValid), commissioningStartMs_(bootMs), windowStartMs_(bootMs)
{
}

void Controller::onNodeActivity(uint64_t nowMs)
{
    // Coming back from silence means the node is (re)booting: give it the same
    // WiFi-free window as a C3 power-up, so the C3's startup current spike can't land on it.
    if (nodeDown(nowMs)) {
        wifiNotBeforeMs_ = std::max(wifiNotBeforeMs_, nowMs + config_.bootWifiDelayS * S);
    }
    nodeSeen_ = true;
    lastNodeActivityMs_ = nowMs;
}

void Controller::onPollStarted(uint64_t nowMs)
{
    if (nowMs - windowStartMs_ >= DAY_MS) {
        windowStartMs_ = nowMs;
        startsInWindow_ = 0;
    }
    startsInWindow_++;
    everPolled_ = true;
    lastPollStartMs_ = nowMs;
}

void Controller::onPollResult(uint64_t, PollResult result)
{
    if (result == PollResult::Ok) {
        backoffS_ = 0;
        if (commissioning_ && ++commissioningGood_ >= config_.commissioningGoodPolls) {
            commissioning_ = false;
        }
        return;
    }
    if (commissioning_) {
        commissioningGood_ = 0;
        return; // setup keeps its fast cadence until the timeout
    }
    if (result == PollResult::AuthFailed) {
        backoffS_ = config_.maxBackoffS; // not transient: don't hammer the broker
    } else {
        backoffS_ = backoffS_ == 0 ? config_.pollIntervalS : std::min(backoffS_ * 2, config_.maxBackoffS);
    }
}

void Controller::onBattery(uint64_t nowMs, float volts)
{
    if (!config_.batteryInterlock || std::isnan(volts)) {
        return;
    }
    if (!lowBatt_ && volts < config_.battLowV) {
        lowBatt_ = true;
        lowBattSinceMs_ = nowMs;
    } else if (lowBatt_ && volts >= config_.battResumeV && nodeSeen_ && lastNodeActivityMs_ > lowBattSinceMs_) {
        lowBatt_ = false;
    }
}

void Controller::requestSession(uint64_t nowMs, uint32_t seconds)
{
    uint32_t length = std::min(seconds == 0 ? config_.sessionDefaultS : seconds, config_.sessionCapS);
    sessionUntilMs_ = nowMs + length * S;
}

void Controller::endSession()
{
    sessionUntilMs_ = 0;
}

void Controller::requestCommissioning(uint64_t nowMs)
{
    commissioning_ = true;
    commissioningStartMs_ = nowMs;
    commissioningGood_ = 0;
    backoffS_ = 0;
}

bool Controller::nodeDown(uint64_t nowMs) const
{
    uint64_t since = nodeSeen_ ? lastNodeActivityMs_ : bootMs_;
    uint64_t limit = (nodeSeen_ ? config_.nodeSilenceS : config_.nodeFirstContactS) * S;
    return nowMs > since && nowMs - since > limit;
}

Mode Controller::mode(uint64_t nowMs) const
{
    if (lowBatt_) {
        return Mode::LowBatt;
    }
    if (nodeDown(nowMs) && nowMs >= sessionUntilMs_) {
        return Mode::NodeDown;
    }
    if (nowMs < sessionUntilMs_) {
        return Mode::Session;
    }
    if (commissioning_ && nowMs - commissioningStartMs_ < config_.commissioningTimeoutS * S) {
        return Mode::Commissioning;
    }
    if (backoffS_ != 0) {
        return Mode::Backoff;
    }
    return Mode::Normal;
}

uint64_t Controller::intervalMs(Mode m) const
{
    switch (m) {
    case Mode::Commissioning:
        return config_.commissioningIntervalS * S;
    case Mode::Backoff:
        return backoffS_ * S;
    case Mode::NodeDown:
        return config_.nodeDownHeartbeatS * S;
    case Mode::Session:
    case Mode::Normal:
        return config_.pollIntervalS * S;
    case Mode::LowBatt:
        break;
    }
    return UINT64_MAX;
}

bool Controller::wifiAllowed(uint64_t nowMs) const
{
    if (nowMs < wifiNotBeforeMs_) {
        return false;
    }
    bool newWindow = nowMs - windowStartMs_ >= DAY_MS;
    return newWindow || startsInWindow_ < config_.wifiStartsPerDay;
}

uint64_t Controller::nextPollAtMs(uint64_t nowMs) const
{
    Mode m = mode(nowMs);
    if (m == Mode::LowBatt) {
        return UINT64_MAX;
    }
    uint64_t at = everPolled_ ? lastPollStartMs_ + intervalMs(m) : wifiNotBeforeMs_;
    at = std::max(at, wifiNotBeforeMs_);
    if (startsInWindow_ >= config_.wifiStartsPerDay) {
        at = std::max(at, windowStartMs_ + DAY_MS); // daily cap reached
    }
    return at;
}

bool Controller::pollDue(uint64_t nowMs) const
{
    if (mode(nowMs) == Mode::LowBatt || !wifiAllowed(nowMs)) {
        return false;
    }
    return nowMs >= nextPollAtMs(nowMs);
}

const char *modeName(Mode mode)
{
    switch (mode) {
    case Mode::LowBatt:
        return "LOW_BATT";
    case Mode::NodeDown:
        return "NODE_DOWN";
    case Mode::Session:
        return "SESSION";
    case Mode::Commissioning:
        return "COMMISSIONING";
    case Mode::Backoff:
        return "BACKOFF";
    case Mode::Normal:
        return "NORMAL";
    }
    return "?";
}

} // namespace modes
