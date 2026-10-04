#pragma once

#include <cstddef>
#include <cstdint>

// Fake clock: delay() advances it, so driver timing runs instantly and deterministically.
uint32_t millis();
void delay(uint32_t ms);

class Print
{
};
