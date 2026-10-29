/* Stand-in for esp_sleep.h: display_utils.cpp's light-sleep busy wait. */
#pragma once
#include <cstdint>
typedef enum { ESP_PD_DOMAIN_VDDSDIO } esp_sleep_pd_domain_t;
typedef enum { ESP_PD_OPTION_ON } esp_sleep_pd_option_t;
typedef enum { ESP_SLEEP_WAKEUP_TIMER = 4, ESP_SLEEP_WAKEUP_GPIO = 7 } esp_sleep_source_t;
inline int esp_sleep_pd_config(esp_sleep_pd_domain_t, esp_sleep_pd_option_t) { return 0; }
inline int esp_sleep_enable_gpio_wakeup() { return 0; }
inline int esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
inline int esp_sleep_disable_wakeup_source(esp_sleep_source_t) { return 0; }
inline int esp_light_sleep_start() { return 0; }
