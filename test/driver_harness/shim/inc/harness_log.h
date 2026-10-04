#pragma once

#include <cstdio>

// Counts errors so tests can assert the driver ran without complaint.
void harnessLog(const char *level, const char *fmt, ...) __attribute__((format(printf, 2, 3)));

#define LOG_TRACE(...)
#define LOG_DEBUG(...) harnessLog("DEBUG", __VA_ARGS__)
#define LOG_INFO(...) harnessLog("INFO", __VA_ARGS__)
#define LOG_WARN(...) harnessLog("WARN", __VA_ARGS__)
#define LOG_ERROR(...) harnessLog("ERROR", __VA_ARGS__)
