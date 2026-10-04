#pragma once

#include <cstdint>

enum RTCQuality { RTCQualityNone = 0, RTCQualityDevice = 1, RTCQualityFromNet = 2, RTCQualityNTP = 3, RTCQualityGPS = 4 };

RTCQuality getRTCQuality();
uint32_t getValidTime(RTCQuality minQuality, bool local = false);
