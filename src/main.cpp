#include "emulator.h"
#include "i2c_slave.h"

#include "esp_log.h"

static const char *TAG = "main";

// Static so it lives in internal RAM for the interrupt handlers.
static senxx::Emulator emulator(senxx::Model::SEN66);

extern "C" void app_main(void)
{
    // Boot order (docs/PLAN.md §4.1): the I²C slave comes up first, serving "unknown"
    // values, so the node's boot scan finds it as early as possible.
    ESP_ERROR_CHECK(bridge::startI2cSlave(emulator));

#ifdef BRIDGE_FIXED_TEST_VALUES
    // Phase 0 bench build: serve fixed values so a mesh packet can be checked end to end.
    senxx::Measurement m;
    m.pm1 = 1.0f;
    m.pm25 = 2.0f;
    m.pm10 = 3.0f;
    m.humidity = 45.0f;
    m.temperature = 21.5f;
    m.vocIndex = 100.0f;
    m.noxIndex = 1.0f;
    m.co2 = 450.0f;
    emulator.setMeasurement(m);
    ESP_LOGW(TAG, "serving fixed test values");
#endif

    ESP_LOGI(TAG, "solar-node-wifi started");
}
