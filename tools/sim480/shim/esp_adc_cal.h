/* Stand-in for esp_adc_cal.h: display_utils.cpp's battery reading. */
#pragma once
#include <cstdint>
typedef struct { int unused; } esp_adc_cal_characteristics_t;
typedef enum { ESP_ADC_CAL_VAL_EFUSE_VREF, ESP_ADC_CAL_VAL_EFUSE_TP, ESP_ADC_CAL_VAL_DEFAULT_VREF } esp_adc_cal_value_t;
inline esp_adc_cal_value_t esp_adc_cal_characterize(int, int, int, uint32_t, esp_adc_cal_characteristics_t *)
{
  return ESP_ADC_CAL_VAL_DEFAULT_VREF;
}
inline uint32_t esp_adc_cal_raw_to_voltage(uint32_t raw, const esp_adc_cal_characteristics_t *) { return raw; }
