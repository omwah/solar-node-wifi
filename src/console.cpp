#include "console.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <cstdio>

namespace bridge
{

namespace
{

constexpr size_t MAX_LINE = 512;
QueueHandle_t lines = nullptr;

void consoleTask(void *)
{
    std::string current;
    for (;;) {
        int c = fgetc(stdin);
        if (c == EOF) {
            vTaskDelay(pdMS_TO_TICKS(20));
            continue;
        }
        if (c == '\r' || c == '\n') {
            if (!current.empty()) {
                auto *line = new std::string(std::move(current));
                if (xQueueSend(lines, &line, pdMS_TO_TICKS(100)) != pdTRUE) {
                    delete line;
                }
                current.clear();
            }
        } else if (current.size() < MAX_LINE) {
            current += static_cast<char>(c);
        }
    }
}

} // namespace

void startConsole()
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);
    usb_serial_jtag_vfs_use_driver();
    setvbuf(stdin, nullptr, _IONBF, 0);
    lines = xQueueCreate(4, sizeof(std::string *));
    xTaskCreate(consoleTask, "console", 4096, nullptr, 5, nullptr);
}

bool takeConsoleLine(std::string &out)
{
    std::string *line = nullptr;
    if (!lines || xQueueReceive(lines, &line, 0) != pdTRUE) {
        return false;
    }
    out = std::move(*line);
    delete line;
    return true;
}

void consoleReply(const std::string &json)
{
    printf("%s\n", json.c_str());
    fflush(stdout);
}

} // namespace bridge
