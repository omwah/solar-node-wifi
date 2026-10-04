#include "settings.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace settings
{

namespace
{

const KeyInfo KEYS[] = {
    {"wifi_ssid", Type::String, "", false, 0, 32, "WiFi network name"},
    {"wifi_pass", Type::String, "", true, 0, 64, "WiFi password"},
    {"ip", Type::String, "", false, 0, 15, "Static IPv4 address; empty = DHCP"},
    {"gateway", Type::String, "", false, 0, 15, "Static gateway"},
    {"netmask", Type::String, "255.255.255.0", false, 0, 15, "Static netmask"},
    {"dns", Type::String, "", false, 0, 15, "Static DNS server"},
    {"mqtt_host", Type::String, "", false, 0, 64, "Broker host name or IP"},
    {"mqtt_port", Type::Uint, "1883", false, 1, 65535, "Broker port (8883 for TLS)"},
    {"mqtt_user", Type::String, "", false, 0, 64, "Broker user name"},
    {"mqtt_pass", Type::String, "", true, 0, 64, "Broker password"},
    {"mqtt_tls", Type::Bool, "false", false, 0, 1, "Use TLS to the broker"},
    {"node_id", Type::String, "site-01", false, 1, 32, "Topic slug: solarnode/<node_id>/..."},
    {"ntp_server", Type::String, "pool.ntp.org", false, 1, 64, "SNTP server"},
    {"poll_interval_s", Type::Uint, "600", false, 120, 3600, "MQTT poll interval"},
    {"max_age_s", Type::Uint, "1800", false, 60, 86400, "Older values are served as unknown"},
    {"th_fallback_aq", Type::Bool, "false", false, 0, 1, "Use AirGradient T/RH when Tempest's is missing"},
    {"node_silence_s", Type::Uint, "10800", false, 600, 86400, "Node silence before NODE_DOWN (3x its interval)"},
    {"heartbeat_s", Type::Uint, "21600", false, 3600, 86400, "Poll interval while NODE_DOWN"},
    {"session_cap_s", Type::Uint, "7200", false, 300, 7200, "Longest live management session"},
    {"batt_enabled", Type::Bool, "false", false, 0, 1, "Battery-sense wire fitted"},
    {"batt_low_v", Type::Float, "3.40", false, 3.0, 4.2, "Enter LOW_BATT below this"},
    {"batt_resume_v", Type::Float, "3.65", false, 3.0, 4.2, "Leave LOW_BATT at or above this"},
    {"batt_cal", Type::Float, "1.0", false, 0.8, 1.2, "Battery reading correction factor"},
};

constexpr size_t KEY_COUNT = sizeof(KEYS) / sizeof(KEYS[0]);
static_assert(KEY_COUNT <= 32, "grow Settings::values_");

bool parseBool(const std::string &v, bool &out)
{
    if (v == "true" || v == "1" || v == "on" || v == "yes") {
        out = true;
        return true;
    }
    if (v == "false" || v == "0" || v == "off" || v == "no") {
        out = false;
        return true;
    }
    return false;
}

bool parseNumber(const std::string &v, double &out)
{
    if (v.empty()) {
        return false;
    }
    char *end = nullptr;
    errno = 0;
    out = std::strtod(v.c_str(), &end);
    return errno == 0 && end && *end == '\0';
}

} // namespace

const KeyInfo *Settings::keys(size_t &count)
{
    count = KEY_COUNT;
    return KEYS;
}

const KeyInfo *Settings::find(const char *name)
{
    for (const KeyInfo &k : KEYS) {
        if (std::strcmp(k.name, name) == 0) {
            return &k;
        }
    }
    return nullptr;
}

Settings::Settings(Store &store) : store_(store)
{
    for (size_t i = 0; i < KEY_COUNT; i++) {
        values_[i] = KEYS[i].defaultValue;
    }
}

int Settings::index(const char *name) const
{
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (std::strcmp(KEYS[i].name, name) == 0) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

void Settings::load()
{
    for (size_t i = 0; i < KEY_COUNT; i++) {
        std::string v;
        values_[i] = store_.load(KEYS[i].name, v) ? v : KEYS[i].defaultValue;
        changed_[i] = false;
    }
    dirty_ = false;
}

SetResult Settings::set(const char *name, const std::string &value)
{
    int i = index(name);
    if (i < 0) {
        return SetResult::UnknownKey;
    }
    const KeyInfo &k = KEYS[i];
    std::string normalized = value;

    switch (k.type) {
    case Type::String:
        if (value.size() < k.min || value.size() > k.max) {
            return SetResult::OutOfRange;
        }
        break;
    case Type::Bool: {
        bool b;
        if (!parseBool(value, b)) {
            return SetResult::BadValue;
        }
        normalized = b ? "true" : "false";
        break;
    }
    case Type::Uint:
    case Type::Float: {
        double d;
        if (!parseNumber(value, d)) {
            return SetResult::BadValue;
        }
        if (k.type == Type::Uint && (d < 0 || d != static_cast<double>(static_cast<uint32_t>(d)))) {
            return SetResult::BadValue;
        }
        if (d < k.min || d > k.max) {
            return SetResult::OutOfRange;
        }
        break;
    }
    }

    values_[i] = normalized;
    changed_[i] = true;
    dirty_ = true;
    return SetResult::Ok;
}

bool Settings::commit()
{
    bool ok = true;
    for (size_t i = 0; i < KEY_COUNT; i++) {
        if (changed_[i]) {
            ok &= store_.save(KEYS[i].name, values_[i]);
            changed_[i] = false;
        }
    }
    dirty_ = !ok;
    return ok;
}

void Settings::factoryReset()
{
    for (size_t i = 0; i < KEY_COUNT; i++) {
        store_.erase(KEYS[i].name);
        values_[i] = KEYS[i].defaultValue;
        changed_[i] = false;
    }
    dirty_ = false;
}

std::string Settings::display(const char *name) const
{
    int i = index(name);
    if (i < 0) {
        return "";
    }
    if (KEYS[i].secret) {
        return values_[i].empty() ? "" : "<set>";
    }
    return values_[i];
}

std::string Settings::str(const char *name) const
{
    int i = index(name);
    return i < 0 ? "" : values_[i];
}

uint32_t Settings::u32(const char *name) const
{
    return static_cast<uint32_t>(std::strtoul(str(name).c_str(), nullptr, 10));
}

float Settings::f32(const char *name) const
{
    return std::strtof(str(name).c_str(), nullptr);
}

bool Settings::flag(const char *name) const
{
    return str(name) == "true";
}

bool Settings::complete() const
{
    return !str("wifi_ssid").empty() && !str("mqtt_host").empty() && !str("node_id").empty();
}

const char *setResultName(SetResult result)
{
    switch (result) {
    case SetResult::Ok:
        return "ok";
    case SetResult::UnknownKey:
        return "unknown key";
    case SetResult::BadValue:
        return "bad value";
    case SetResult::OutOfRange:
        return "out of range";
    }
    return "?";
}

} // namespace settings
