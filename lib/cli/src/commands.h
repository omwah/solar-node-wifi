#pragma once

#include "settings.h"

#include <ArduinoJson.h>
#include <string>
#include <vector>

namespace cli
{

struct Request {
    JsonVariantConst id; // echoed back for MQTT correlation; null on serial
    std::string cmd;
    std::vector<std::string> args;
};

// "set wifi_pass \"two words\"" -> cmd + args. Returns false on an unterminated quote.
bool parseLine(const std::string &line, Request &out);

// {"id": 42, "cmd": "set", "args": {"key": "...", "value": "..."}}. The document must
// outlive the request (id points into it). Returns false if malformed.
bool parseJson(const char *json, size_t len, JsonDocument &doc, Request &out);

// Device-side effects, implemented by the firmware (and by a fake in tests).
class Actions
{
  public:
    virtual ~Actions() {}
    virtual void status(JsonObject out) = 0;
    virtual void applySettings() = 0; // after a successful commit
    virtual void pollNow() = 0;
    virtual void reboot() = 0;
    virtual void factoryReset() = 0;
    virtual void startSession(uint32_t minutes) = 0; // 0 = default length
    virtual void endSession() = 0;
    virtual void log(size_t lines, JsonArray out) = 0;
    virtual bool startOta(const std::string &url, const std::string &sha256, std::string &error) = 0;
};

// One command set for every transport: USB serial on the bench, MQTT in the field.
// Responses are JSON: {"id": ..., "ok": true|false, "result": ..., "error": "..."}.
class Dispatcher
{
  public:
    Dispatcher(settings::Settings &settings, Actions &actions) : settings_(settings), actions_(actions) {}

    void run(const Request &request, JsonDocument &response);

  private:
    settings::Settings &settings_;
    Actions &actions_;
};

} // namespace cli
