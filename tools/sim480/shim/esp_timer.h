/* Stand-in for esp_timer.h: display_utils.cpp's light-sleep busy wait. */
#pragma once
#include <cstdint>
inline int64_t esp_timer_get_time() { return 0; }
