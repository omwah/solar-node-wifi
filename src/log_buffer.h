#pragma once

#include <ArduinoJson.h>
#include <cstddef>

namespace bridge
{

// Keeps the most recent log lines in RAM for the `log` command and the MQTT log topic,
// while still printing everything to the console.
void startLogCapture();
void copyRecentLog(size_t lines, JsonArray out);

} // namespace bridge
