#pragma once

#include <string>
#include <vector>

namespace discovery
{

struct Message {
    std::string topic;   // homeassistant/<component>/<object_id>/config
    std::string payload; // retained JSON config
};

// Home Assistant MQTT Discovery configs for one bridge: diagnostic sensors read from the
// retained state topic, plus buttons that publish commands to the cmd topic.
std::vector<Message> build(const std::string &nodeId, const std::string &firmwareVersion, bool batteryWire);

} // namespace discovery
