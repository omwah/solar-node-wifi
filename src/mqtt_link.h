#pragma once

#include "settings.h"

#include <cstdint>
#include <string>

namespace bridge
{

enum class ConnectResult { Ok, Failed, AuthRejected };

// One broker connection: subscribe to this node's telemetry and cmd topics, collect
// what arrives, publish state/resp/log. Uses a persistent session (fixed client id,
// clean session off) so QoS 1 commands queue at the broker while the C3 is offline.
class MqttLink
{
  public:
    MqttLink();
    ~MqttLink();

    ConnectResult connect(const settings::Settings &s, uint32_t timeoutMs);
    void disconnect();
    bool connected() const;

    // Next complete message on the telemetry topic (retained or live).
    bool takeTelemetry(std::string &out, uint32_t waitMs);
    // Next complete command message.
    bool takeCommand(std::string &out, uint32_t waitMs);

    bool publish(const char *suffix, const std::string &payload, bool retain, int qos);

    struct Impl;

  private:
    Impl *impl_;
};

} // namespace bridge
