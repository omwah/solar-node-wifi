#include "battery.h"

#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_log.h"

#include <cmath>

namespace bridge
{

namespace
{

const char *TAG = "battery";
constexpr adc_channel_t CHANNEL = ADC_CHANNEL_3; // GPIO3 = XIAO D1
constexpr float DIVIDER = 2.0f;                  // 1 MΩ over 1 MΩ
constexpr int SAMPLES = 16;

adc_oneshot_unit_handle_t unit = nullptr;
adc_cali_handle_t cali = nullptr;

} // namespace

bool batteryInit()
{
    if (unit) {
        return true;
    }
    adc_oneshot_unit_init_cfg_t unitCfg = {};
    unitCfg.unit_id = ADC_UNIT_1;
    if (adc_oneshot_new_unit(&unitCfg, &unit) != ESP_OK) {
        return false;
    }
    adc_oneshot_chan_cfg_t chanCfg = {};
    chanCfg.atten = ADC_ATTEN_DB_12;
    chanCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_oneshot_config_channel(unit, CHANNEL, &chanCfg) != ESP_OK) {
        return false;
    }
    adc_cali_curve_fitting_config_t caliCfg = {};
    caliCfg.unit_id = ADC_UNIT_1;
    caliCfg.chan = CHANNEL;
    caliCfg.atten = ADC_ATTEN_DB_12;
    caliCfg.bitwidth = ADC_BITWIDTH_DEFAULT;
    if (adc_cali_create_scheme_curve_fitting(&caliCfg, &cali) != ESP_OK) {
        ESP_LOGW(TAG, "no ADC calibration in eFuse; readings uncalibrated");
        cali = nullptr;
    }
    return true;
}

float batteryVolts(float calibration)
{
    if (!unit) {
        return NAN;
    }
    int sumMv = 0;
    for (int i = 0; i < SAMPLES; i++) {
        int raw = 0;
        if (adc_oneshot_read(unit, CHANNEL, &raw) != ESP_OK) {
            return NAN;
        }
        int mv = 0;
        if (cali) {
            adc_cali_raw_to_voltage(cali, raw, &mv);
        } else {
            mv = raw * 2500 / 4095; // rough, 12 dB range
        }
        sumMv += mv;
    }
    return (sumMv / static_cast<float>(SAMPLES)) / 1000.0f * DIVIDER * calibration;
}

} // namespace bridge
