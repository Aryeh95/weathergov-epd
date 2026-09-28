/* Wake diagnostics declarations for esp32-weather-epd.
 * Copyright (C) 2026
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

#ifndef __DIAG_H__
#define __DIAG_H__

#include <Arduino.h>
#include <ArduinoJson.h>

/* What the last wake did and how long each part of it took, kept in NVS so
 * that the portal can answer "why did the screen not update?" from a phone.
 * Until now that took a USB cable and a serial monitor, and on boards whose
 * only serial port is USB it took being there when it happened.
 *
 * Two records are kept: the last wake, whatever came of it, and the last
 * wake that went wrong, which stays until another one does.
 */

#define DIAG_VERSION 1

// how the wake ended
#define DIAG_OK          0 // weather fetched and drawn
#define DIAG_HELD        1 // fetch failed; the last good screen was left up
#define DIAG_WIFI        2 // no WiFi; error screen
#define DIAG_CLOCK       3 // no time from the network; error screen
#define DIAG_WEATHER     4 // weather.gov did not answer; error screen
#define DIAG_BATTERY     5 // battery low; slept without going online
#define DIAG_OTHER       6 // the wake ended some other way

typedef struct wake_diag
{
  uint8_t  version;
  uint8_t  outcome;    // DIAG_*
  int16_t  code;       // HTTP status or client error of a failed request, else 0
  int64_t  when;       // end of the wake, Unix UTC; 0 if the clock was not set
  uint32_t awakeMs;    // whole wake
  uint32_t wifiMs;     // connecting
  uint32_t clockMs;    // waiting for network time (0 when the RTC was trusted)
  uint32_t fetchMs;    // every API request together
  uint32_t drawMs;     // drawing and refreshing the panel
  uint32_t sleepS;     // how long it then slept for; 0 = until reset
  uint16_t batteryMv;
  int8_t   rssi;       // 0 = not connected
  uint8_t  cause;      // esp_sleep_wakeup_cause_t
  char     note[72];   // what went wrong or was missing, in words
} wake_diag_t;

extern wake_diag_t wakeDiag; // the wake in progress

// Record how the wake ended. The note of an earlier call is kept if this one
// has none, so a warning about a missing reading survives a successful wake.
void diagOutcome(uint8_t outcome, int code, const String &note);

// Store the record. Called on the way into deep sleep.
void diagSave(unsigned long startTime, uint32_t sleepSeconds);

// Add both stored records to a JSON reply, as "last_wake" and "last_problem".
void diagToJson(JsonDocument &doc);

#endif
