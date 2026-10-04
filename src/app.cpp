#include "app.h"

#include "battery.h"
#include "discovery.h"
#include "console.h"
#include "i2c_slave.h"
#include "log_buffer.h"
#include "net.h"

#include "driver/gpio.h"
#include "esp_app_desc.h"
#include "esp_log.h"
#include "esp_sleep.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <algorithm>

namespace bridge
{

namespace
{

const char *TAG = "app";
constexpr uint64_t S = 1000;
constexpr uint32_t WIFI_TIMEOUT_MS = 15000;
constexpr uint32_t SNTP_TIMEOUT_MS = 10000;
constexpr uint32_t MQTT_TIMEOUT_MS = 10000;
constexpr uint32_t TELEMETRY_WAIT_MS = 3000;
constexpr uint32_t COMMAND_WAIT_MS = 1000;
constexpr uint64_t SNTP_RESYNC_MS = 6 * 3600 * S;
constexpr uint64_t REFRESH_MS = 60 * S;
constexpr uint64_t SESSION_STATE_MS = 60 * S;
constexpr uint64_t BATTERY_MS = 60 * S;

const char *resetReason()
{
    switch (esp_reset_reason()) {
    case ESP_RST_POWERON:
        return "power-on";
    case ESP_RST_SW:
        return "software";
    case ESP_RST_PANIC:
        return "panic";
    case ESP_RST_INT_WDT:
    case ESP_RST_TASK_WDT:
    case ESP_RST_WDT:
        return "watchdog";
    case ESP_RST_BROWNOUT:
        return "brownout";
    case ESP_RST_DEEPSLEEP:
        return "deep-sleep";
    default:
        return "other";
    }
}

const char *sensorStateName(senxx::SensorState s)
{
    switch (s) {
    case senxx::SensorState::Idle:
        return "idle";
    case senxx::SensorState::Measuring:
        return "measuring";
    case senxx::SensorState::RhtGasOnly:
        return "rht-gas";
    }
    return "?";
}

void putIfKnown(JsonObject out, const char *key, float v)
{
    if (!std::isnan(v)) {
        out[key] = v;
    }
}

} // namespace

App::App(senxx::Emulator &emulator) : emu_(emulator), settings_(store_), dispatcher_(settings_, *this)
{
    settings_.load();
    controller_ = std::make_unique<modes::Controller>(modeConfig(), nowMs(), settings_.complete());
    if (settings_.flag("batt_enabled")) {
        batteryInit();
    }
    otaVerifier_.begin(nowMs());
}

uint64_t App::nowMs()
{
    return static_cast<uint64_t>(esp_timer_get_time() / 1000);
}

modes::Config App::modeConfig() const
{
    modes::Config c;
    c.pollIntervalS = settings_.u32("poll_interval_s");
    c.nodeSilenceS = settings_.u32("node_silence_s");
    c.nodeDownHeartbeatS = settings_.u32("heartbeat_s");
    c.sessionCapS = settings_.u32("session_cap_s");
    c.batteryInterlock = settings_.flag("batt_enabled");
    c.battLowV = settings_.f32("batt_low_v");
    c.battResumeV = settings_.f32("batt_resume_v");
    return c;
}

payload::ParseConfig App::parseConfig() const
{
    payload::ParseConfig c;
    c.maxAgeS = settings_.u32("max_age_s");
    c.thFallbackAq = settings_.flag("th_fallback_aq");
    return c;
}

[[noreturn]] void App::run()
{
    ESP_LOGI(TAG, "mode %s, settings %s", modes::modeName(controller_->mode(nowMs())),
             settings_.complete() ? "complete" : "incomplete (commissioning)");
    for (;;) {
        uint64_t now = nowMs();
        tick(now);
        handleConsole();
        maybeReboot();

        bool lowBatt = controller_->mode(now) == modes::Mode::LowBatt;
        if ((pollNowRequested_ && !lowBatt) || controller_->pollDue(now)) {
            pollNowRequested_ = false;
            poll(now);
            continue;
        }
        if (controller_->mode(now) == modes::Mode::NodeDown) {
            nodeDownSleep(now);
        } else {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
}

void App::tick(uint64_t now)
{
    uint32_t tx = emu_.transactionCount();
    if (tx != lastTransactions_) {
        lastTransactions_ = tx;
        controller_->onNodeActivity(now);
    }
    if (settings_.flag("batt_enabled") && now - lastBatteryMs_ >= BATTERY_MS) {
        lastBatteryMs_ = now;
        batteryV_ = batteryVolts(settings_.f32("batt_cal"));
        controller_->onBattery(now, batteryV_);
    }
    if (now - lastRefreshMs_ >= REFRESH_MS) {
        refreshMeasurement();
    }
    otaVerifier_.update(now, controller_->nodeSeen(), lastPollOk_);
}

void App::poll(uint64_t now)
{
    controller_->onPollStarted(now);
    polls_++;
    auto fail = [&](modes::PollResult r) {
        pollFailures_++;
        publishLogNext_ = true;
        controller_->onPollResult(nowMs(), r);
        link_.disconnect();
        wifiStop();
    };

    if (!settings_.complete()) {
        controller_->onPollResult(now, modes::PollResult::NoData); // waiting for setup over serial
        return;
    }
    if (settings_.flag("batt_enabled")) {
        batteryV_ = batteryVolts(settings_.f32("batt_cal"));
        controller_->onBattery(now, batteryV_);
        if (controller_->mode(now) == modes::Mode::LowBatt) {
            return;
        }
    }

    if (!wifiConnect(settings_, WIFI_TIMEOUT_MS)) {
        ESP_LOGW(TAG, "WiFi connect failed");
        return fail(modes::PollResult::ConnectFailed);
    }
    if (!sntpEver_ || nowMs() - lastSntpMs_ > SNTP_RESYNC_MS) {
        if (sntpSync(settings_.str("ntp_server").c_str(), SNTP_TIMEOUT_MS)) {
            sntpEver_ = true;
            lastSntpMs_ = nowMs();
        } else {
            ESP_LOGW(TAG, "SNTP sync failed");
        }
    }

    ConnectResult cr = link_.connect(settings_, MQTT_TIMEOUT_MS);
    if (cr != ConnectResult::Ok) {
        ESP_LOGW(TAG, "broker connect failed%s", cr == ConnectResult::AuthRejected ? " (auth rejected)" : "");
        return fail(cr == ConnectResult::AuthRejected ? modes::PollResult::AuthFailed
                                                      : modes::PollResult::ConnectFailed);
    }

    publishDiscovery();
    if (publishLogNext_) {
        // Recent log lines explain the earlier failure.
        JsonDocument logDoc;
        copyRecentLog(20, logDoc.to<JsonArray>());
        std::string out;
        serializeJson(logDoc, out);
        publishLogNext_ = !link_.publish("log", out, false, 0);
    }

    std::string msg;
    bool gotTelemetry = link_.takeTelemetry(msg, TELEMETRY_WAIT_MS);
    if (gotTelemetry) {
        applyTelemetry(msg);
    }
    while (link_.takeCommand(msg, COMMAND_WAIT_MS)) {
        handleMqttCommand(msg);
    }

    bool ok = gotTelemetry && payloadOk_;
    if (!ok) {
        pollFailures_++;
        publishLogNext_ = true;
    }
    lastPollOk_ = lastPollOk_ || ok;
    controller_->onPollResult(nowMs(), ok ? modes::PollResult::Ok : modes::PollResult::NoData);
    runPendingOta();
    publishState();

    if (controller_->keepConnected(nowMs())) {
        serveSession();
    }
    maybeReboot();
    link_.disconnect();
    wifiStop();
}

void App::serveSession()
{
    ESP_LOGI(TAG, "live session started");
    while (controller_->keepConnected(nowMs()) && link_.connected()) {
        uint64_t now = nowMs();
        std::string msg;
        if (link_.takeTelemetry(msg, 0)) {
            applyTelemetry(msg);
        }
        if (link_.takeCommand(msg, 100)) {
            handleMqttCommand(msg);
            runPendingOta();
        }
        tick(now);
        handleConsole();
        if (rebootRequested_) {
            break;
        }
        if (now - lastStateMs_ >= SESSION_STATE_MS) {
            publishState();
        }
    }
    publishState();
    ESP_LOGI(TAG, "live session ended");
}

void App::handleConsole()
{
    std::string line;
    while (takeConsoleLine(line)) {
        cli::Request req;
        JsonDocument response;
        if (!cli::parseLine(line, req)) {
            consoleReply(R"({"ok":false,"error":"unterminated quote"})");
            continue;
        }
        dispatcher_.run(req, response);
        std::string out;
        serializeJson(response, out);
        consoleReply(out);
    }
}

void App::handleMqttCommand(const std::string &json)
{
    JsonDocument doc;
    JsonDocument response;
    cli::Request req;
    if (!cli::parseJson(json.data(), json.size(), doc, req)) {
        response["ok"] = false;
        response["error"] = "malformed command";
    } else {
        dispatcher_.run(req, response);
    }
    std::string out;
    serializeJson(response, out);
    link_.publish("resp", out, false, 1);
}

void App::applyTelemetry(const std::string &raw)
{
    lastPayload_ = raw;
    refreshMeasurement();
    if (!payloadOk_) {
        return;
    }

    // Live session from the payload's debug flag. A flag without debug_until starts one
    // session and re-arms only after a payload with debug false, so a forgotten retained
    // flag can't keep WiFi on (docs/mqtt_payload.md §3.1).
    if (!telemetry_.debug) {
        debugArmed_ = true;
        return;
    }
    uint32_t epoch = epochNow();
    uint32_t key = telemetry_.debugUntil;
    bool honour = key == 0 ? debugArmed_ : (epoch != 0 && key > epoch && key != debugHandledKey_);
    if (honour) {
        uint32_t minutes = key == 0 ? 0 : std::max<uint32_t>(1, (key - epoch) / 60);
        controller_->requestSession(nowMs(), minutes * 60);
        debugHandledKey_ = key;
        debugArmed_ = false;
    }
}

void App::refreshMeasurement()
{
    lastRefreshMs_ = nowMs();
    if (lastPayload_.empty()) {
        return;
    }
    payload::Telemetry t;
    payload::ParseStatus st = payload::parse(lastPayload_.data(), lastPayload_.size(), epochNow(), parseConfig(), t);
    payloadOk_ = st == payload::ParseStatus::Ok;
    if (!payloadOk_) {
        malformed_++;
        ESP_LOGW(TAG, "telemetry rejected (%s)", st == payload::ParseStatus::TooLarge ? "too large" : "malformed");
        lastPayload_.clear();
        telemetry_ = payload::Telemetry{};
        emu_.setMeasurement(senxx::Measurement{});
        return;
    }
    telemetry_ = t;
    emu_.setMeasurement(t.measurement);
}

void App::publishDiscovery()
{
    if (discoveryPublished_ || !settings_.flag("ha_discovery")) {
        return;
    }
    bool ok = true;
    for (const discovery::Message &m :
         discovery::build(settings_.str("node_id"), esp_app_get_description()->version, settings_.flag("batt_enabled"))) {
        ok &= link_.publishAbsolute(m.topic, m.payload, true, 1);
    }
    discoveryPublished_ = ok;
}

void App::publishState()
{
    lastStateMs_ = nowMs();
    JsonDocument doc;
    status(doc.to<JsonObject>());
    std::string out;
    serializeJson(doc, out);
    link_.publish("state", out, true, 0);
}

void App::status(JsonObject out)
{
    uint64_t now = nowMs();
    out["mode"] = modes::modeName(controller_->mode(now));
    out["fw"] = esp_app_get_description()->version;
    out["uptime_s"] = now / S;
    out["reset_reason"] = resetReason();
    out["settings_complete"] = settings_.complete();
    out["node_seen"] = controller_->nodeSeen();
    if (controller_->nodeSeen()) {
        out["last_node_activity_s"] = (now - controller_->lastNodeActivityMs()) / S;
    }
    out["node_transactions"] = emu_.transactionCount();
    out["sensor_state"] = sensorStateName(emu_.state());
    out["clock_valid"] = clockValid();
    out["polls"] = polls_;
    out["poll_failures"] = pollFailures_;
    out["malformed_payloads"] = malformed_;
    out["backoff_s"] = controller_->currentBackoffS();
    out["wifi_starts_today"] = controller_->wifiStartsToday();
    uint64_t next = controller_->nextPollAtMs(now);
    if (next != UINT64_MAX) {
        out["next_poll_s"] = next > now ? (next - now) / S : 0;
    }
    if (otaVerifier_.pending()) {
        out["ota"] = "pending verification";
    } else if (!otaResult_.empty()) {
        out["ota"] = otaResult_;
    }
    out["free_heap"] = esp_get_free_heap_size();
    out["min_free_heap"] = esp_get_minimum_free_heap_size();
    int rssi = wifiRssi();
    if (rssi != 0) {
        out["rssi"] = rssi;
    }
    if (settings_.flag("batt_enabled")) {
        putIfKnown(out, "battery_v", batteryV_);
    }
    if (payloadOk_) {
        JsonObject p = out["payload"].to<JsonObject>();
        uint32_t epoch = epochNow();
        if (epoch && telemetry_.publishedAt && epoch >= telemetry_.publishedAt) {
            p["age_s"] = epoch - telemetry_.publishedAt;
        }
        p["stale_values"] = telemetry_.staleValues;
        p["unknown_units"] = telemetry_.unknownUnits;
        putIfKnown(p, "irradiance", telemetry_.irradiance);
        const senxx::Measurement &m = telemetry_.measurement;
        JsonObject s = p["serving"].to<JsonObject>();
        putIfKnown(s, "pm1", m.pm1);
        putIfKnown(s, "pm2_5", m.pm25);
        putIfKnown(s, "pm4", m.pm4);
        putIfKnown(s, "pm10", m.pm10);
        putIfKnown(s, "co2", m.co2);
        putIfKnown(s, "voc_index", m.vocIndex);
        putIfKnown(s, "nox_index", m.noxIndex);
        putIfKnown(s, "temperature", m.temperature);
        putIfKnown(s, "humidity", m.humidity);
    }
}

void App::applySettings()
{
    controller_->setConfig(modeConfig());
    discoveryPublished_ = false; // node_id or ha_discovery may have changed
    if (settings_.flag("batt_enabled")) {
        batteryInit();
    }
    refreshMeasurement(); // max_age / fallback may have changed
}

void App::factoryReset()
{
    settings_.factoryReset();
    controller_->requestCommissioning(nowMs());
    rebootRequested_ = true;
}

void App::startSession(uint32_t minutes)
{
    controller_->requestSession(nowMs(), minutes * 60);
}

void App::endSession()
{
    controller_->endSession();
}

void App::log(size_t lines, JsonArray out)
{
    copyRecentLog(lines, out);
}

bool App::startOta(const std::string &url, const std::string &sha256, std::string &error)
{
    if (controller_->mode(nowMs()) == modes::Mode::LowBatt) {
        error = "battery low";
        return false;
    }
    if (otaVerifier_.pending()) {
        error = "current image not yet verified";
        return false;
    }
    if (!link_.connected()) {
        error = "OTA needs a broker connection (send it over MQTT)";
        return false;
    }
    // Runs after the reply is published, while WiFi is still up.
    otaUrl_ = url;
    otaSha_ = sha256;
    return true;
}

void App::runPendingOta()
{
    if (otaUrl_.empty()) {
        return;
    }
    std::string url = otaUrl_;
    std::string sha = otaSha_;
    otaUrl_.clear();
    otaSha_.clear();
    ESP_LOGI(TAG, "OTA from %s", url.c_str());
    std::string error;
    if (otaInstall(url, sha, error)) {
        otaResult_ = "installed; rebooting";
        publishState();
        rebootRequested_ = true;
    } else {
        otaResult_ = "failed: " + error;
    }
}

void App::maybeReboot()
{
    if (!rebootRequested_) {
        return;
    }
    ESP_LOGW(TAG, "rebooting on request");
    vTaskDelay(pdMS_TO_TICKS(500)); // let the reply go out
    link_.disconnect();
    wifiStop();
    esp_restart();
}

void App::nodeDownSleep(uint64_t now)
{
    // GPIO wake needs an idle (high) bus. If the node's pull-ups are unpowered while it's
    // shut down, the lines sit low and would wake us continuously: stay awake instead.
    if (gpio_get_level(GROVE_SCL) == 0 || gpio_get_level(GROVE_SDA) == 0) {
        if (!busLowLogged_) {
            ESP_LOGW(TAG, "I2C bus held low while the node is down; not sleeping");
            busLowLogged_ = true;
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
        return;
    }
    busLowLogged_ = false;

    uint64_t next = controller_->nextPollAtMs(now);
    uint64_t sleepMs = next > now ? std::min<uint64_t>(next - now, 24 * 3600 * S) : 0;
    if (sleepMs < 1000) {
        vTaskDelay(pdMS_TO_TICKS(100));
        return;
    }
    ESP_LOGI(TAG, "node down: light sleep up to %llu s", static_cast<unsigned long long>(sleepMs / S));
    vTaskDelay(pdMS_TO_TICKS(20)); // flush the log line

    gpio_wakeup_enable(GROVE_SCL, GPIO_INTR_LOW_LEVEL);
    gpio_wakeup_enable(GROVE_SDA, GPIO_INTR_LOW_LEVEL);
    esp_sleep_enable_gpio_wakeup();
    esp_sleep_enable_timer_wakeup(sleepMs * 1000);
    esp_light_sleep_start();
    gpio_wakeup_disable(GROVE_SCL);
    gpio_wakeup_disable(GROVE_SDA);
    esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
}

} // namespace bridge
