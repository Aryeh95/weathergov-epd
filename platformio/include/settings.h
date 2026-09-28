/* Runtime settings loader declarations for esp32-weather-epd.
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

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#include <Arduino.h>
#include <ArduinoJson.h>

/* Mounts the device's LittleFS filesystem and loads /config.json, which
 * overwrites the compiled-in fallback values declared in config.h/defined in
 * config.cpp (WiFi credentials, location, time, battery thresholds, and
 * widget layout) for any key present in the file. This is how the project is
 * meant to be reconfigured day-to-day: edit data/config.json and run
 * `pio run --target uploadfs`, no firmware recompile required. See
 * data/config.json for the schema and current values.
 *
 * Returns true if config.json was found and successfully applied, false if
 * the filesystem or file could not be mounted/read/parsed (in which case the
 * compiled-in defaults from config.cpp remain in effect).
 */
bool loadSettings();

/* Checks a configuration before it replaces the one on the device. Returns
 * an empty string when it can be used, otherwise a sentence saying what is
 * wrong with it. The portal's form has limits of its own, but its raw
 * editor and anything that posts to /config directly do not pass through
 * them -- and a refresh interval of 0 divides by zero at the first sleep.
 *
 * loadSettings() does not reject: a device has to boot with whatever file
 * it holds. It brings every number into range instead.
 */
String settingsProblem(JsonVariantConst doc);

// limits shared by the check above and the loader
#define SETTINGS_MAX_BYTES 16384

#endif
