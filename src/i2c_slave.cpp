#include "i2c_slave.h"

#include "driver/i2c_slave.h"
#include "esp_attr.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

namespace bridge
{

namespace
{

const char *TAG = "i2c_slave";

// Above the WiFi task (23) so a stretched read is served promptly even mid-poll.
constexpr UBaseType_t SERVE_TASK_PRIORITY = 24;
constexpr uint32_t SERVE_TASK_STACK = 3072;

enum class Event : uint8_t { ResetTx, Serve };

senxx::Emulator *emu = nullptr;
i2c_slave_dev_handle_t handle = nullptr;
QueueHandle_t events = nullptr;

IRAM_ATTR bool onReceive(i2c_slave_dev_handle_t, const i2c_slave_rx_done_event_data_t *evt, void *)
{
    emu->onWrite(evt->buffer, evt->length);
    BaseType_t woken = pdFALSE;
    Event e = Event::ResetTx;
    xQueueSendFromISR(events, &e, &woken);
    return woken == pdTRUE;
}

IRAM_ATTR bool onRequest(i2c_slave_dev_handle_t, const i2c_slave_request_event_data_t *, void *)
{
    BaseType_t woken = pdFALSE;
    Event e = Event::Serve;
    xQueueSendFromISR(events, &e, &woken);
    return woken == pdTRUE;
}

void serveTask(void *)
{
    // An address-only read with nothing staged still has to release the stretched clock.
    static const uint8_t idle = 0xFF;
    for (;;) {
        Event e;
        if (xQueueReceive(events, &e, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        if (e == Event::ResetTx) {
            // Drop bytes left over from a read the master cut short.
            i2c_slave_reset_tx_fifo(handle);
            continue;
        }
        const uint8_t *data = emu->response();
        uint32_t len = emu->responseLength();
        if (len == 0) {
            data = &idle;
            len = 1;
        }
        uint32_t written = 0;
        esp_err_t err = i2c_slave_write(handle, data, len, &written, 50);
        if (err != ESP_OK || written != len) {
            ESP_LOGW(TAG, "served %u of %u bytes (%s)", static_cast<unsigned>(written), static_cast<unsigned>(len),
                     esp_err_to_name(err));
        }
    }
}

} // namespace

esp_err_t startI2cSlave(senxx::Emulator &emulator, gpio_num_t sda, gpio_num_t scl)
{
    emu = &emulator;
    events = xQueueCreate(8, sizeof(Event));
    if (!events) {
        return ESP_ERR_NO_MEM;
    }

    i2c_slave_config_t config = {};
    config.i2c_port = -1;
    config.sda_io_num = sda;
    config.scl_io_num = scl;
    config.clk_source = I2C_CLK_SRC_DEFAULT;
    config.send_buf_depth = 128;   // largest reply is 48 bytes
    config.receive_buf_depth = 32; // largest command is 14 bytes
    config.slave_addr = emulator.address();
    config.addr_bit_len = I2C_ADDR_BIT_LEN_7;
    config.flags.enable_internal_pullup = 0; // the Solar Node provides the pull-ups

    esp_err_t err = i2c_new_slave_device(&config, &handle);
    if (err != ESP_OK) {
        return err;
    }

    i2c_slave_event_callbacks_t callbacks = {};
    callbacks.on_request = onRequest;
    callbacks.on_receive = onReceive;
    err = i2c_slave_register_event_callbacks(handle, &callbacks, nullptr);
    if (err != ESP_OK) {
        return err;
    }

    if (xTaskCreate(serveTask, "i2c_serve", SERVE_TASK_STACK, nullptr, SERVE_TASK_PRIORITY, nullptr) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "slave at 0x%02X on SDA=%d SCL=%d", emulator.address(), sda, scl);
    return ESP_OK;
}

} // namespace bridge
