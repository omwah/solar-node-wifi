#include "log_buffer.h"

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <cstdio>
#include <cstring>

namespace bridge
{

namespace
{

constexpr size_t LINES = 40;
constexpr size_t LINE_LEN = 128;

char ring[LINES][LINE_LEN];
size_t head = 0; // next slot to write
size_t count = 0;
vprintf_like_t original = nullptr;
SemaphoreHandle_t lock = nullptr;

int capture(const char *fmt, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    if (lock && xSemaphoreTake(lock, 0) == pdTRUE) {
        char *line = ring[head];
        vsnprintf(line, LINE_LEN, fmt, copy);
        size_t len = strnlen(line, LINE_LEN);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (len > 0) {
            head = (head + 1) % LINES;
            count = count < LINES ? count + 1 : LINES;
        }
        xSemaphoreGive(lock);
    }
    va_end(copy);
    return original ? original(fmt, args) : vprintf(fmt, args);
}

} // namespace

void startLogCapture()
{
    lock = xSemaphoreCreateMutex();
    original = esp_log_set_vprintf(capture);
}

void copyRecentLog(size_t lines, JsonArray out)
{
    if (!lock || xSemaphoreTake(lock, pdMS_TO_TICKS(100)) != pdTRUE) {
        return;
    }
    size_t n = lines < count ? lines : count;
    for (size_t i = 0; i < n; i++) {
        out.add(ring[(head + LINES - n + i) % LINES]);
    }
    xSemaphoreGive(lock);
}

} // namespace bridge
