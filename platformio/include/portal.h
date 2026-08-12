/* Configuration web portal declarations for esp32-weather-epd.
 * Copyright (C) 2022-2026  Luke Marzen
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

#ifndef __PORTAL_H__
#define __PORTAL_H__

/* Runs the configuration web portal, serving a browser UI (data/portal.html)
 * that edits /config.json on LittleFS -- WiFi, location, schedule, widgets,
 * everything -- without reflashing.
 *
 * The portal is entered from two triggers (see main.cpp):
 *   1. The device is unconfigured (config.json still has the placeholder
 *      WiFi SSID), ex. right after first flash -> hotspot mode.
 *   2. The RST button is pressed twice within one wake window (~a few
 *      seconds apart) -> config mode on the local network.
 *
 * If forceAp is false the portal first tries to join the configured WiFi
 * and serve on the LAN (http://<ip>/ and http://weatherepd.local/); if that
 * fails, or forceAp is true, it starts a WPA2 hotspot "WeatherEPD-Setup"
 * (password PORTAL_AP_PASSWORD) with a captive portal at http://192.168.4.1/.
 *
 * The e-paper shows how to reach the portal. After PORTAL_TIMEOUT minutes
 * with no HTTP activity, or after a saved configuration, the device restarts
 * into its normal wake/sleep cycle -- so the portal costs battery only while
 * it is actually being used.
 *
 * This function never returns.
 */
void runConfigPortal(bool forceAp);

#endif
