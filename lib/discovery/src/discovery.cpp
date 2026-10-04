#include "discovery.h"

#include <ArduinoJson.h>

namespace discovery
{

namespace
{

struct Sensor {
    const char *key;
    const char *name;
    const char *valueTemplate;
    const char *unit;
    const char *deviceClass;
    const char *component; // "sensor" or "binary_sensor"
};

const Sensor SENSORS[] = {
    {"mode", "Mode", "{{ value_json.mode }}", nullptr, nullptr, "sensor"},
    {"node_seen", "Node talking", "{{ 'ON' if value_json.node_seen else 'OFF' }}", nullptr, "connectivity",
     "binary_sensor"},
    {"last_node_activity", "Last node read", "{{ value_json.last_node_activity_s | default(none) }}", "s",
     "duration", "sensor"},
    {"rssi", "WiFi signal", "{{ value_json.rssi | default(none) }}", "dBm", "signal_strength", "sensor"},
    {"uptime", "Uptime", "{{ value_json.uptime_s }}", "s", "duration", "sensor"},
    {"poll_failures", "Poll failures", "{{ value_json.poll_failures }}", nullptr, nullptr, "sensor"},
    {"backoff", "Backoff", "{{ value_json.backoff_s }}", "s", "duration", "sensor"},
    {"payload_age", "Payload age", "{{ value_json.payload.age_s | default(none) }}", "s", "duration", "sensor"},
    {"free_heap", "Free heap", "{{ value_json.free_heap }}", "B", "data_size", "sensor"},
    {"firmware", "Firmware", "{{ value_json.fw }}", nullptr, nullptr, "sensor"},
    {"ota", "OTA", "{{ value_json.ota | default('') }}", nullptr, nullptr, "sensor"},
};

const Sensor BATTERY = {"battery", "Battery", "{{ value_json.battery_v | default(none) }}", "V", "voltage", "sensor"};

struct Button {
    const char *key;
    const char *name;
    const char *command; // cmd topic payload
};

const Button BUTTONS[] = {
    {"poll_now", "Poll now", R"({"id":"ha","cmd":"poll-now"})"},
    {"session", "Start 30 min session", R"({"id":"ha","cmd":"session","args":{"minutes":30}})"},
    {"reboot", "Reboot", R"({"id":"ha","cmd":"reboot"})"},
};

void addDevice(JsonObject doc, const std::string &nodeId, const std::string &fw)
{
    JsonObject dev = doc["device"].to<JsonObject>();
    dev["identifiers"].to<JsonArray>().add("solarnode_" + nodeId);
    dev["name"] = "Solar Node bridge " + nodeId;
    dev["model"] = "XIAO ESP32-C3 SEN66 bridge";
    dev["sw_version"] = fw;
    JsonObject origin = doc["origin"].to<JsonObject>();
    origin["name"] = "solar-node-wifi";
}

Message sensor(const Sensor &s, const std::string &nodeId, const std::string &fw)
{
    std::string objectId = "solarnode_" + nodeId + "_" + s.key;
    JsonDocument doc;
    doc["name"] = s.name;
    doc["unique_id"] = objectId;
    doc["object_id"] = objectId;
    doc["state_topic"] = "solarnode/" + nodeId + "/state";
    doc["value_template"] = s.valueTemplate;
    doc["entity_category"] = "diagnostic";
    if (s.unit) {
        doc["unit_of_measurement"] = s.unit;
    }
    if (s.deviceClass) {
        doc["device_class"] = s.deviceClass;
    }
    addDevice(doc.as<JsonObject>(), nodeId, fw);
    Message m;
    m.topic = std::string("homeassistant/") + s.component + "/" + objectId + "/config";
    serializeJson(doc, m.payload);
    return m;
}

Message button(const Button &b, const std::string &nodeId, const std::string &fw)
{
    std::string objectId = "solarnode_" + nodeId + "_" + b.key;
    JsonDocument doc;
    doc["name"] = b.name;
    doc["unique_id"] = objectId;
    doc["object_id"] = objectId;
    doc["command_topic"] = "solarnode/" + nodeId + "/cmd";
    doc["payload_press"] = b.command;
    doc["qos"] = 1;
    addDevice(doc.as<JsonObject>(), nodeId, fw);
    Message m;
    m.topic = "homeassistant/button/" + objectId + "/config";
    serializeJson(doc, m.payload);
    return m;
}

} // namespace

std::vector<Message> build(const std::string &nodeId, const std::string &firmwareVersion, bool batteryWire)
{
    std::vector<Message> out;
    for (const Sensor &s : SENSORS) {
        out.push_back(sensor(s, nodeId, firmwareVersion));
    }
    if (batteryWire) {
        out.push_back(sensor(BATTERY, nodeId, firmwareVersion));
    }
    for (const Button &b : BUTTONS) {
        out.push_back(button(b, nodeId, firmwareVersion));
    }
    return out;
}

} // namespace discovery
