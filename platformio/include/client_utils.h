/* Client side utility declarations for esp32-weather-epd.
 * Copyright (C) 2022-2025  Luke Marzen
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#ifndef __CLIENT_UTILS_H__
#define __CLIENT_UTILS_H__

#include <vector>
#include <Arduino.h>
#include "api_response.h"
#include "config.h"
#ifdef USE_HTTP
  #include <WiFiClient.h>
#else
  #include <WiFiClientSecure.h>
#endif

wl_status_t startWiFi(int &wifiRSSI);
String getWifiFailureDetail(wl_status_t status);
void killWiFi();
bool waitForSNTPSync(tm *timeInfo);
bool printLocalTime(tm *timeInfo);

/* Fetches weather.gov's gridpoint forecast (12-hour periods and hourly), and
 * current conditions from the nearest observation station. Populates
 * current/hourly/daily.
 *
 * If a step in the chain fails, failedStep is set to a short description of
 * which step failed (used for the on-screen status message).
 *
 * Returns the HTTP status code of the request that failed, or HTTP_CODE_OK.
 */
int getNWSWeather(WiFiClient &client, owm_current_t &current,
                  owm_hourly_t *hourly, owm_daily_t *daily,
                  String &failedStep);

/* Fetches active weather alerts for LAT/LON from weather.gov.
 *
 * Returns the HTTP Status Code.
 */
int getNWSAlerts(WiFiClient &client, std::vector<owm_alerts_t> &alerts);

/* Fetches UV index and air pollutant concentrations for LAT/LON from
 * Open-Meteo (weather.gov does not provide either).
 *
 * Returns the HTTP Status Code.
 */
int getAirQuality(WiFiClient &client, owm_resp_air_pollution_t &air,
                  float &uvi);

/* Fetches the official US EPA AQI from AirNow (airnow.gov). Requires
 * AIRNOW_APIKEY to be configured; aqi stays -1 when no nearby monitor
 * reported.
 *
 * Returns the HTTP Status Code.
 */
int getAirNowAQI(WiFiClient &client, int &aqi);

#endif
