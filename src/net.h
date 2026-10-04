#pragma once

#include "settings.h"

#include <cstdint>

namespace bridge
{

void netInit();

// Starts WiFi in station mode (static IP if configured) and waits for an address.
bool wifiConnect(const settings::Settings &s, uint32_t timeoutMs);
void wifiStop();
int wifiRssi();

// Syncs the clock over SNTP; returns true once the time is set.
bool sntpSync(const char *server, uint32_t timeoutMs);
bool clockValid();
uint32_t epochNow(); // 0 until the clock is valid

} // namespace bridge
