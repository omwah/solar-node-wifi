#pragma once

#include "commands.h"
#include "controller.h"
#include "emulator.h"
#include "mqtt_link.h"
#include "nvs_store.h"
#include "ota.h"
#include "payload.h"
#include "settings.h"

#include <memory>
#include <string>

namespace bridge
{

// The firmware's main loop: decides when to poll (modes::Controller), runs the poll
// (WiFi -> SNTP -> MQTT -> telemetry + commands -> state), keeps the emulator's
// snapshot fresh, and sleeps lightly while the node is down. All commands, from the
// serial console or MQTT, run here, so settings and the controller need no locking.
class App : public cli::Actions
{
  public:
    explicit App(senxx::Emulator &emulator);
    [[noreturn]] void run();

    // cli::Actions
    void status(JsonObject out) override;
    void applySettings() override;
    void pollNow() override { pollNowRequested_ = true; }
    void reboot() override { rebootRequested_ = true; }
    void factoryReset() override;
    void startSession(uint32_t minutes) override;
    void endSession() override;
    void log(size_t lines, JsonArray out) override;
    bool startOta(const std::string &url, const std::string &sha256, std::string &error) override;

  private:
    static uint64_t nowMs();
    modes::Config modeConfig() const;
    payload::ParseConfig parseConfig() const;

    void tick(uint64_t now);
    void poll(uint64_t now);
    void serveSession();
    void handleConsole();
    void handleMqttCommand(const std::string &json);
    void applyTelemetry(const std::string &raw);
    void refreshMeasurement();
    void publishState();
    void nodeDownSleep(uint64_t now);
    void maybeReboot();
    void runPendingOta();

    senxx::Emulator &emu_;
    NvsStore store_;
    settings::Settings settings_;
    std::unique_ptr<modes::Controller> controller_;
    cli::Dispatcher dispatcher_;
    MqttLink link_;

    std::string lastPayload_;
    payload::Telemetry telemetry_;
    bool payloadOk_ = false;
    uint64_t lastRefreshMs_ = 0;
    uint64_t lastStateMs_ = 0;
    uint64_t lastSntpMs_ = 0;
    bool sntpEver_ = false;

    uint32_t lastTransactions_ = 0;
    uint64_t lastBatteryMs_ = 0;
    float batteryV_ = NAN;

    uint32_t debugHandledKey_ = 0;
    bool debugArmed_ = true;

    uint32_t polls_ = 0;
    uint32_t pollFailures_ = 0;
    uint32_t malformed_ = 0;
    bool pollNowRequested_ = false;
    bool rebootRequested_ = false;
    bool busLowLogged_ = false;

    OtaVerifier otaVerifier_;
    bool lastPollOk_ = false;
    std::string otaUrl_;
    std::string otaSha_;
    std::string otaResult_;
};

} // namespace bridge
