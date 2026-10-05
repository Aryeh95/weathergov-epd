/* Stand-in for the ESP32 ADC driver: display_utils.cpp's battery reading. */
#pragma once
#define ADC_UNIT_1 1
#define ADC_ATTEN_11db 3
#define ADC_WIDTH_BIT_12 3
inline void adc_power_acquire() {}
inline void adc_power_release() {}
