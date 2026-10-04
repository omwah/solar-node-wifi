#pragma once

#include "emulator.h"

#include "driver/gpio.h"
#include "esp_err.h"

namespace bridge
{

// XIAO ESP32-C3 pads wired to the Grove harness (docs/PLAN.md §4.7.1)
constexpr gpio_num_t GROVE_SDA = GPIO_NUM_6; // D4
constexpr gpio_num_t GROVE_SCL = GPIO_NUM_7; // D5

// Connects a senxx::Emulator to the ESP32-C3 I²C slave peripheral.
//
// The receive interrupt feeds each completed master write to the emulator and queues a
// TX FIFO reset (the bus is idle then: Sensirion masters STOP and wait >= 20 ms before
// reading). The request interrupt queues a "serve" event; a high-priority task then
// writes the staged reply while the hardware holds SCL low (clock stretching), because
// i2c_slave_write() can block and must not run in the interrupt.
esp_err_t startI2cSlave(senxx::Emulator &emulator, gpio_num_t sda = GROVE_SDA, gpio_num_t scl = GROVE_SCL);

} // namespace bridge
