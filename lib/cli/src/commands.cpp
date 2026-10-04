#include "commands.h"

#include <cctype>
#include <cstdlib>

namespace cli
{

namespace
{

const char *const HELP[][2] = {
    {"status", "device status"},
    {"get [key]", "show one setting, or all (secrets redacted)"},
    {"set <key> <value>", "stage a setting change"},
    {"commit", "save staged changes and apply them"},
    {"poll-now", "poll the broker at the next opportunity"},
    {"session <minutes>|end", "keep WiFi/MQTT connected for interactive management"},
    {"log [lines]", "recent log lines"},
    {"ota <url> <sha256>", "download and install firmware; rolls back if it fails to verify"},
    {"reboot", "restart the C3"},
    {"factory-reset <node_id>", "erase all settings (the device becomes unreachable remotely)"},
    {"help", "this list"},
};

bool isHex64(const std::string &s)
{
    if (s.size() != 64) {
        return false;
    }
    for (char c : s) {
        if (!std::isxdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    return true;
}

bool parseUint(const std::string &s, uint32_t &out)
{
    if (s.empty()) {
        return false;
    }
    char *end = nullptr;
    unsigned long v = std::strtoul(s.c_str(), &end, 10);
    if (*end != '\0') {
        return false;
    }
    out = static_cast<uint32_t>(v);
    return true;
}

std::string normalize(std::string cmd)
{
    for (char &c : cmd) {
        if (c == '_') {
            c = '-';
        }
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return cmd;
}

void fail(JsonDocument &response, const std::string &error)
{
    response["ok"] = false;
    response["error"] = error;
}

} // namespace

bool parseLine(const std::string &line, Request &out)
{
    out = Request{};
    std::vector<std::string> tokens;
    std::string current;
    bool inToken = false;
    bool quoted = false;
    for (size_t i = 0; i < line.size(); i++) {
        char c = line[i];
        if (quoted) {
            if (c == '\\' && i + 1 < line.size()) {
                current += line[++i];
            } else if (c == '"') {
                quoted = false;
            } else {
                current += c;
            }
        } else if (c == '"') {
            quoted = true;
            inToken = true;
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (inToken) {
                tokens.push_back(current);
                current.clear();
                inToken = false;
            }
        } else {
            current += c;
            inToken = true;
        }
    }
    if (quoted) {
        return false;
    }
    if (inToken) {
        tokens.push_back(current);
    }
    if (tokens.empty()) {
        return false;
    }
    out.cmd = tokens[0];
    out.args.assign(tokens.begin() + 1, tokens.end());
    return true;
}

bool parseJson(const char *json, size_t len, JsonDocument &doc, Request &out)
{
    out = Request{};
    if (deserializeJson(doc, json, len) || !doc["cmd"].is<const char *>()) {
        return false;
    }
    out.id = doc["id"];
    out.cmd = doc["cmd"].as<const char *>();
    JsonVariantConst args = doc["args"];

    // Map named JSON arguments onto the positional form the serial console uses.
    std::string cmd = normalize(out.cmd);
    auto arg = [&](const char *name) {
        JsonVariantConst v = args[name];
        if (v.is<const char *>()) {
            out.args.push_back(v.as<const char *>());
        } else if (v.is<bool>()) {
            out.args.push_back(v.as<bool>() ? "true" : "false");
        } else if (v.is<double>()) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.10g", v.as<double>());
            out.args.push_back(buf);
        }
    };
    if (cmd == "get") {
        arg("key");
    } else if (cmd == "set") {
        arg("key");
        arg("value");
    } else if (cmd == "session") {
        if (args["end"].is<bool>() && args["end"].as<bool>()) {
            out.args.push_back("end");
        } else {
            arg("minutes");
        }
    } else if (cmd == "log") {
        arg("lines");
    } else if (cmd == "ota") {
        arg("url");
        arg("sha256");
    } else if (cmd == "factory-reset") {
        arg("confirm");
    }
    return true;
}

void Dispatcher::run(const Request &request, JsonDocument &response)
{
    response.clear();
    if (!request.id.isNull()) {
        response["id"] = request.id;
    }
    response["ok"] = true;
    const std::vector<std::string> &a = request.args;
    std::string cmd = normalize(request.cmd);

    if (cmd == "help") {
        JsonObject result = response["result"].to<JsonObject>();
        JsonObject commands = result["commands"].to<JsonObject>();
        for (const auto &h : HELP) {
            commands[h[0]] = h[1];
        }
        JsonObject keys = result["keys"].to<JsonObject>();
        size_t count;
        const settings::KeyInfo *info = settings::Settings::keys(count);
        for (size_t i = 0; i < count; i++) {
            keys[info[i].name] = info[i].help;
        }
    } else if (cmd == "status") {
        actions_.status(response["result"].to<JsonObject>());
    } else if (cmd == "get" || cmd == "show") {
        if (!a.empty()) {
            if (!settings::Settings::find(a[0].c_str())) {
                return fail(response, "unknown key: " + a[0]);
            }
            response["result"][a[0]] = settings_.display(a[0].c_str());
        } else {
            JsonObject result = response["result"].to<JsonObject>();
            size_t count;
            const settings::KeyInfo *info = settings::Settings::keys(count);
            for (size_t i = 0; i < count; i++) {
                result[info[i].name] = settings_.display(info[i].name);
            }
            response["pending_commit"] = settings_.dirty();
        }
    } else if (cmd == "set") {
        if (a.size() != 2) {
            return fail(response, "usage: set <key> <value>");
        }
        settings::SetResult r = settings_.set(a[0].c_str(), a[1]);
        if (r != settings::SetResult::Ok) {
            return fail(response, std::string(settings::setResultName(r)) + ": " + a[0]);
        }
        response["result"] = "staged; run commit to save";
    } else if (cmd == "commit") {
        if (!settings_.commit()) {
            return fail(response, "could not save settings");
        }
        actions_.applySettings();
        response["result"] = "saved";
    } else if (cmd == "poll-now") {
        actions_.pollNow();
        response["result"] = "poll scheduled";
    } else if (cmd == "session") {
        if (a.size() == 1 && a[0] == "end") {
            actions_.endSession();
            response["result"] = "session ended";
        } else {
            uint32_t minutes = 0;
            if (a.size() > 1 || (a.size() == 1 && !parseUint(a[0], minutes))) {
                return fail(response, "usage: session <minutes>|end");
            }
            actions_.startSession(minutes);
            response["result"] = "session started";
        }
    } else if (cmd == "log") {
        uint32_t lines = 50;
        if (!a.empty() && !parseUint(a[0], lines)) {
            return fail(response, "usage: log [lines]");
        }
        actions_.log(lines, response["result"].to<JsonArray>());
    } else if (cmd == "ota") {
        if (a.size() != 2 || (a[0].rfind("http://", 0) != 0 && a[0].rfind("https://", 0) != 0) || !isHex64(a[1])) {
            return fail(response, "usage: ota <http(s)-url> <sha256-hex>");
        }
        std::string error;
        if (!actions_.startOta(a[0], a[1], error)) {
            return fail(response, error);
        }
        response["result"] = "update started";
    } else if (cmd == "reboot") {
        actions_.reboot();
        response["result"] = "rebooting";
    } else if (cmd == "factory-reset") {
        if (a.size() != 1 || a[0] != settings_.str("node_id")) {
            return fail(response, "confirm with: factory-reset <node_id>");
        }
        actions_.factoryReset();
        response["result"] = "settings erased";
    } else {
        fail(response, "unknown command: " + request.cmd + " (try help)");
    }
}

} // namespace cli
