/* Stand-in for the ESP32 GPIO driver: display_utils.cpp's light-sleep
 * busy wait, never reached on a PC (nothing refreshes a panel there). */
#pragma once
#include "Arduino.h"
typedef enum { GPIO_INTR_LOW_LEVEL = 4, GPIO_INTR_HIGH_LEVEL = 5 } gpio_int_type_t;
inline void gpio_pullup_en(gpio_num_t) {}
inline void gpio_pulldown_dis(gpio_num_t) {}
inline void gpio_wakeup_enable(gpio_num_t, gpio_int_type_t) {}
inline void gpio_wakeup_disable(gpio_num_t) {}
