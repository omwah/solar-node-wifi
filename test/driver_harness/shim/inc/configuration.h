// Host shim for Meshtastic's configuration.h: just enough for SENXXSensor.
#pragma once

#include "Arduino.h"
#include "harness_log.h"

#include <cmath>
#include <cstring>
#include <memory>

#define HAS_TELEMETRY 1
#define MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR 0
#define MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR 0
#define SEN5X_ADDR 0x69
#define SEN6X_ADDR 0x6B
