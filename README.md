# weathergov-epd — ESP32 E-Paper Weather Display

> [!NOTE]
> **This fork replaces OpenWeatherMap with the free, keyless [weather.gov (NWS) API](https://www.weather.gov/documentation/services-web-api).** US locations only. Major differences from upstream:
>
> - **No API keys or accounts required.** [weather.gov (NWS)](https://www.weather.gov/documentation/services-web-api) provides current conditions (from the nearest NWS observation station), hourly/daily forecasts, and weather alerts. [Open-Meteo](https://open-meteo.com/) (also free/keyless) fills in UV index and air-pollutant concentrations. Optionally, a free [AirNow](https://docs.airnowapi.org/) API key upgrades the Air Quality widget to the EPA's official US AQI from certified monitoring stations, with automatic fallback to Open-Meteo.
> - **Phone-based setup, no code editing.** A fresh device boots into a WiFi hotspot with a captive setup page — see [First-Time Setup](#first-time-setup-hotspot). Every runtime setting lives in [`platformio/data/config.json`](platformio/data/config.json) and can be changed from the [configuration web portal](#the-configuration-web-portal) afterwards, including **over-the-air firmware updates** — the only time a device needs a USB cable is its very first flash.
> - **Full-color weather icons on 7-color panels** (ACeP and Spectra 6): condition and widget icons derived from the [InkyPi](https://github.com/fatihak/InkyPi) icon set, quantized to the panels' native inks (gray dithered clouds, blue rain, yellow suns), plus a semantic color palette — red highs/blue lows, color-coded UV and AQI values. Black/white and 3-color panels keep the original line art.
> - **More widgets, flexible layout**: a moon phase widget (computed on-device, dithered grayscale icons on every panel type), 5–7 day forecast row, and a 5- or 6-row widget grid (10 or 12 slots) — all selectable at runtime.
> - **Sunrise/sunset are computed on-device** (NOAA solar algorithm in `platformio/src/sun.cpp`), and day/night icon selection follows those real sun times instead of NWS's fixed 6am/6pm icon boundary.
> - **Robust WiFi**: connects to the strongest access point on multi-AP (mesh) networks, the error screen explains *why* a connection failed (wrong password, network not found, no response...), and the device retries every 15 minutes (configurable) instead of requiring a manual reset.
> - **Supported boards**: the FireBeetle 2 ESP32-E wiring from upstream, plus native support for the [Seeed reTerminal E1002](https://www.seeedstudio.com/reTerminal-E1002-p-6533.html) — an all-in-one ESP32-S3 device with a built-in 7.3" Spectra 6 panel, battery, and buttons (`pio run -e seeed_reterminal_e1002`; its middle button opens the portal, the green button forces a refresh).
> - **Precipitation displays as probability (PoP %)** — NWS's simple forecast endpoints don't provide volume amounts.
> - HTTPS is required (all APIs are HTTPS-only); `cert.h` pins the root CAs for every host, valid until 2035.
>
> The [Setup Guide](#setup-guide) below has been rewritten for this fork; hardware, wiring, and assembly are unchanged from upstream.

A low-power weather display using a wifi-enabled ESP32 microcontroller and a 7.5" E-Paper display. Weather data is fetched from the weather.gov (NWS) API, and an onboard sensor provides indoor temperature and humidity.

<p float="left">
  <img src="showcase/demo-reterminal-e1002-front.jpg" />
  <img src="showcase/assembled-demo-raleigh-side.jpg" width="49%" />
  <img src="showcase/assembled-demo-raleigh-back.jpg" width="49%" />
  <img src="showcase/assembled-demo-bottom-cover.jpg" width="49%" />
  <img src="showcase/assembled-demo-bottom-cover-removed.jpg" width="49%" />
</p>

## Features

- Ultra-low power consumption: ~14μA in sleep, ~83mA during refresh (~1.5s on the B/W panel; color panels take longer).

- Long battery life: 6-12 months on a 5000mAh battery with 30-minute update frequency.

- Customizable display: multiple languages, units, time/date formats, AQI scales, 5-7 day forecast, 10 or 12 widget slots, and much more — most of it reconfigurable from your phone.

- Easy recharging: USB-C charging with battery monitoring.

The hourly outlook graph (bottom right) shows a line indicating temperature and shaded bars indicating probability of precipitation.

Here are two (slightly outdated) examples utilizing various configuration options:

<p float="left">
  <img src="showcase/demo-new-york.jpg" width="49%" />
  <img src="showcase/demo-london.jpg" width="49%" />
</p>

## Contents

- [Required Components](#required-components)
  - [Panel Support](#panel-support)
  - [Enclosure Options](#enclosure-options)
  - [Solder-Free Component Selection](#solder-free-component-selection-optional)
- [Setup Guide](#setup-guide)
  - [Wiring](#wiring)
  - [Compilation and First Flash](#compilation-and-first-flash)
  - [First-Time Setup (Hotspot)](#first-time-setup-hotspot)
  - [The Configuration Web Portal](#the-configuration-web-portal)
  - [Runtime Configuration: config.json](#runtime-configuration-configjson)
  - [Over-the-Air Firmware Updates](#over-the-air-firmware-updates)
- [Error Messages and Troubleshooting](#error-messages-and-troubleshooting)
  - [Low Battery](#low-battery)
  - [WiFi Connection](#wifi-connection)
  - [API Error](#api-error)
  - [Time Server Error](#time-server-error)
- [Licensing](#licensing)


## Required Components

  Some links below are affiliate links. Using them helps support the project at no extra cost to you—thanks for your support!

  | Component Type  | Component                                    | Notes                                                     | Link                                                                         |
  |-----------------|----------------------------------------------|-----------------------------------------------------------|------------------------------------------------------------------------------|
  | ESP32           | FireBeetle 2 ESP32-E                         | Features low-power design, USB-C, and battery management. | Available [here](https://www.dfrobot.com/product-2195.html?tracking=PfSxQ8). |
  | E-Paper Display | See [Panel Support](#panel-support).         | See [Panel Support](#panel-support).                      | See [Panel Support](#panel-support).                                         |
  | Adapter Board   | DESPI-C02                                    | Waveshare HATs (rev 2.2/2.3) are not recommended.         | Available [here](https://www.aliexpress.us/item/3256804446769469.html).      |
  | Sensor          | BME280 (optional)                            | Indoor temperature/humidity widgets. 3.3V/5V compatible.  | Available from multiple vendors.                                             |
  | Battery         | 3.7V LiPo w/ JST-PH2.0 connector             | Any capacity (e.g., 5000mAh for 6+ months runtime)        | Available from multiple vendors.                                             |
  | Enclosure       | See [Enclosure Options](#enclosure-options). | See [Enclosure Options](#enclosure-options).              | See [Enclosure Options](#enclosure-options).                                 |

  **All-in-one alternative:** the [Seeed reTerminal E1002](https://www.seeedstudio.com/reTerminal-E1002-p-6533.html) packs an ESP32-S3, a 7.3" Spectra 6 full-color panel, a battery, an enclosure, front buttons, and an SHT4x indoor sensor into one device — no wiring, soldering, or enclosure needed. Build for it with `pio run -e seeed_reterminal_e1002` and everything (panel, sensor, pins, buttons) is selected automatically. Note its full refresh takes ~28s, so battery life is shorter than the B/W FireBeetle build.

Other items needed:
- Wires ("Jumper Wires" if looking to minimize/avoid soldering).
- Solder Iron + Solder (unless following [Solder-Free Component Selection](#solder-free-component-selection-optional)).
- Linux, Windows, or MacOS computer (used for the first firmware flash).
- Push Button (optional, if you want a reset button mounted on your enclosure, else you can use the on-board reset button).

### Panel Support

  Waveshare and Good Display make equivalent panels. Either variant will work.

  | Panel                                   | Resolution | Colors          | Notes                                                                                                                 |
  |-----------------------------------------|------------|-----------------|-----------------------------------------------------------------------------------------------------------------------|
  | Waveshare 7.5in e-paper (v2)            | 800x480px  | Black/White     | Available [here](https://www.waveshare.com/product/7.5inch-e-paper.htm). (recommended)                                |
  | Good Display 7.5in e-paper (GDEY075T7)  | 800x480px  | Black/White     | Available [here](https://www.aliexpress.com/item/3256802683908868.html).             |
  | Waveshare 7.5in e-Paper (B)             | 800x480px  | Red/Black/White | Available [here](https://www.waveshare.com/product/7.5inch-e-paper-b.htm).                                            |
  | Good Display 7.5in e-paper (GDEY075Z08) | 800x480px  | Red/Black/White | Available [here](https://www.aliexpress.com/item/3256803540460035.html).                                              |
  | Waveshare 7.3in ACeP e-Paper (F)        | 800x480px  | 7-Color         | Available [here](https://www.waveshare.com/product/displays/e-paper/epaper-1/7.3inch-e-paper-f.htm). Full-color icons. |
  | Good Display 7.3in e-paper (GDEY073D46) | 800x480px  | 7-Color         | Available [here](https://www.aliexpress.com/item/3256805485098421.html). Full-color icons.                            |
  | Good Display 7.3in e-paper (GDEP073E01) | 800x480px  | 7-Color         | Available [here](https://www.good-display.com/blank7.html?productId=533). Spectra 6. Full-color icons. This is also the panel built into the reTerminal E1002. |
  | Waveshare 7.5in e-paper (v1)            | 640x384px  | Black/White     | Limited support. Some information not displayed, see [image](showcase/demo-waveshare75-version1.jpg).                 |
  | Good Display 7.5in e-paper (GDEW075T8)  | 640x384px  | Black/White     | Limited support. Some information not displayed, see [image](showcase/demo-waveshare75-version1.jpg).                 |

  On the 7-color panels this fork renders full-color condition and widget icons plus a semantic color palette (red highs / blue lows, color-coded UV and AQI); the 3-color panels use a single red accent; B/W panels use line art. Panels with additional colors have much longer refresh times (~12s for 3-color, ~28s for 7-color), which reduces battery life.

### Enclosure Options

You'll want a nice way to show off your project. Here are a few popular choices.

- DIY Wooden
  - I made a small stand by hollowing out a piece of wood from the bottom. On the back, I used a short USB extension cable so that I can charge the battery without needing to remove the components from the stand. I also wired a small reset button to refresh the display manually. Additionally, I 3d printed a cover for the bottom, which is held on by magnets. The E-paper screen is very thin, so I used a thin piece of acrylic to support it.
  - Measurements:
    - depth = 63mm <br>
      height = 49mm <br>
      width = 170.2mm (= width of the screen) <br>
      screen angle = 80deg <br>
      screen is 15mm from the front
- 3D Printable
  - Here is a list of community designs.
  
    | Contributor                                                          | Link                                                                                                      |
    |----------------------------------------------------------------------|-----------------------------------------------------------------------------------------------------------|
    | [Kingfisher](https://www.printables.com/@Kingfisher_32821)           | [Printables](https://www.printables.com/model/1139047-weather-station-e-ink-frame)                        |
    | [Francois Allard](https://www.printables.com/@FrAllard_1585397)      | [Printables](https://www.printables.com/model/791477-weather-station-using-a-esp32)                       |
    | [3D Nate](https://www.printables.com/@3DNate_451157)                 | [Printables](https://www.printables.com/model/661183-e-ink-weather-station-frame)                         |
    | [Sven F.](https://github.com/Spanholz)                               | [Printables](https://www.printables.com/model/657756-case-for-esp32-weather-station)                      |
    | [Layers Studio](https://www.printables.com/@LayersStudio)            | [Printables](https://www.printables.com/model/655768-esp32-e-paper-weather-display-stand)                 |
    | [PJ Veltri](https://www.printables.com/@PJVeltri_1590999)            | [Printables](https://www.printables.com/model/692944-base-and-display-holder-for-esp-32-e-paper-weather)  |
    | [TheMeanCanEHdian](https://www.printables.com/@TheMeanCanEH_1207348) | [Printables](https://www.printables.com/model/841458-weather-display-enclosure)                           |
    | [MPHarms](https://www.thingiverse.com/mpharms/designs)               | [Thingiverse](https://www.thingiverse.com/thing:6666148)                                                  |
    | [Plaste-Metz](https://www.printables.com/@PlasteMetz_576567)         | [Printables](https://www.printables.com/model/1160924-weather-station-case)                               |
    | [kenwch92](https://github.com/kenwch92)                              | [Printables](https://www.printables.com/model/1505838-over-engineered-display-stand-for-esp32-e-paper-we) |
    | [Eckerput](https://github.com/Eckerput)                              | [Thingiverse](https://www.thingiverse.com/thing:7112836)                                                  |
    | [Thomax ](https://www.printables.com/@Thomax_386720)                 | [Printables](https://www.printables.com/model/1363448-esp32-e-paper-weather-display) |

  - If you want to share your own 3D printable designs, your contributions are highly encouraged and welcome!
- Picture Frame

### Solder-Free Component Selection (Optional)

This project can be completed without any soldering, if you choose your component selection carefully.
- Buy "Jumper Wires" to connect your components.
- Buy the [FireBeetle 2 ESP32-E w/ Headers](https://www.dfrobot.com/product-2231.html?tracking=PfSxQ8).
- Buy a BME280 with headers soldered from the factory.
- Buy a reset switch that is compatible with jumper wires.


## Setup Guide

The short version: wire it up, flash it once over USB, then join the hotspot it creates and finish setup from your phone. After that, settings changes *and firmware updates* happen over WiFi.

### Wiring

*(Skip this section entirely for the reTerminal E1002 — everything is built in.)*

The battery can be charged by plugging the FireBeetle ESP32 into the wall via the USB-C connector while the battery is plugged into the ESP32's JST connector.

  > **Warning**
  > The polarity of JST-PH2.0 connectors is not standardized! You may need to swap the order of the wires in the connector.

NOTE: Waveshare now ships revision 2.3 of their e-paper HAT (no longer rev 2.2 ). Rev 2.3 has an additional `PWR` pin (not depicted in the wiring diagrams below); connect this pin to 3.3V.

IMPORTANT: The DESPI-C02 adapter has one physical switch that MUST be set correctly for the display to work.

- RESE: Set switch to position 0.47.

IMPORTANT: The Waveshare E-Paper Driver HAT has two physical switches that MUST be set correctly for the display to work.

- Display Config: Set switch to position B.

- Interface Config: Set switch to position 0.

Cut the low power pad for even longer battery life.

- From <https://wiki.dfrobot.com/FireBeetle_Board_ESP32_E_SKU_DFR0654>

  > Low Power Pad: This pad is specially designed for low power consumption. It is connected by default. You can cut off the thin wire in the middle with a knife to disconnect it. After disconnection, the static power consumption can be reduced by 500 μA. The power consumption can be reduced to 13 μA after controlling the maincontroller enter the sleep mode through the program. Note: when the pad is disconnected, you can only drive RGB LED light via the USB Power supply.

![Wiring diagram with DESPI-C02 driver board.](showcase/wiring_diagram_despi-c02.png)


### Compilation and First Flash

PlatformIO for VSCode is used for managing dependencies, code compilation, and uploading to the ESP32. The first flash is the only step that requires a computer and USB cable.

1. Clone this repository or download and extract the .zip.

2. Install VSCode.

3. Follow these instructions to install the PlatformIO extension for VSCode: <https://platformio.org/install/ide?install=vscode>

4. Open the project in VSCode.

   a. File > Open Folder...

   b. Navigate to this project and select the folder called "platformio".

5. Configure hardware options in [config.h](platformio/include/config.h).

   Only *hardware* choices are compiled in; everything else (WiFi, location, schedule, layout) is configured later from your phone. In config.h, select exactly one of each:

   - **E-Paper panel** (`DISP_BW_V2`, `DISP_3C_B`, `DISP_7C_F`, `DISP_7C_E6`, or `DISP_BW_V1`).
   - **Indoor sensor** (`SENSOR_BME280` or `SENSOR_BME680`).
   - **Units** (temperature, wind speed, pressure, distance) and **locale/language**.

   *(reTerminal E1002 owners skip this step — building with the `seeed_reterminal_e1002` environment selects the panel, sensor, and pins automatically.)*

6. Build and Upload.

   a. Connect the ESP32 to your computer via USB.

   b. Upload the **firmware**: click the upload arrow along the bottom of the VSCode window ("PlatformIO: Upload"). For the reTerminal E1002, select the `seeed_reterminal_e1002` environment first (or run `pio run -e seeed_reterminal_e1002 -t upload`).

   c. Upload the **filesystem** (the settings file and the portal web page): run "PlatformIO: Upload Filesystem Image" from the task list (or `pio run -t uploadfs`).

      - If using a FireBeetle 2 ESP32-E and you receive the error `Wrong boot mode detected (0x13)! The chip needs to be in download mode.` unplug the power from the board, connect GPIO0 ([labeled 0/D5](https://wiki.dfrobot.com/FireBeetle_Board_ESP32_E_SKU_DFR0654#target_5)) to GND, and power it back up to put the board in download mode.

      - If you are getting other errors during the upload process, you may need to install drivers to allow you to upload code to the ESP32.

### First-Time Setup (Hotspot)

A freshly flashed device has no WiFi credentials, so on boot it starts its own hotspot and shows the connection details on the e-paper screen:

1. On your phone, join the WiFi network **`WeatherEPD-Setup`** (password: **`weatherepd`**).
2. The setup page should pop up automatically (captive portal). If it doesn't, open **http://192.168.4.1/** in a browser.
3. Fill in the page:
   - **WiFi** — tap **🔍 Scan** to list nearby networks (strongest first) and tap yours, then enter its password. 2.4 GHz networks only.
   - **Location** — tap **📍 Detect my location**, or enter latitude/longitude manually (long-press your spot in a maps app to copy coordinates). weather.gov covers US locations only.
   - **Time zone** and formats — dropdowns with live previews.
   - Everything else (schedule, forecast days, widget layout, AirNow key) can be set now or changed later.
4. Tap **Save & Restart**. The device joins your WiFi and shows the weather within a minute or two.

If setup isn't completed within 10 minutes, the device shows a "Setup paused" screen and deep-sleeps to protect the battery — press the reset (RST) button to start the setup hotspot again. This means an unconfigured device can safely sit on battery power (in a gift box, say) without draining itself.

### The Configuration Web Portal

The same page used for first-time setup remains available after the device is configured:

- **Enter it** by pressing the RST button **twice, a few seconds apart** (on the reTerminal E1002, just press the **middle front button** once while it sleeps). The display shows where to reach it — `http://weatherepd.local/` or the device's IP — for the next 10 minutes (configurable).
- **Every setting** in `config.json` is editable: WiFi (with network scan), location (with phone-GPS detection), time zone and clock/date formats (dropdowns with live examples), refresh schedule and bedtime hours, forecast days (5–7), widget rows (5 = 10 slots, 6 = 12 slots — enough for every widget at once), and a per-slot widget picker that mirrors the physical layout.
- An **advanced raw-JSON editor** exposes the settings not in the form (battery thresholds, NTP servers, portal options). Saves are validated on-device and keep a one-generation backup (`config.bak`).
- The portal serves plain HTTP on your LAN while active.

On the reTerminal E1002 the **green (right) front button** also wakes the device for an immediate weather refresh.

### Runtime Configuration: config.json

All non-hardware settings live in [`platformio/data/config.json`](platformio/data/config.json) on the device's flash filesystem, with `//` comments documenting every option: WiFi, location, timezone and formats, sleep schedule and bedtime, WiFi retry interval, battery thresholds, API options, forecast days, widget rows, and widget positions.

Three ways to change it, in order of convenience:

1. **The web portal** (above) — from any phone or computer on your network.
2. **Edit the file and run `pio run -t uploadfs`** — useful when the device is already on USB.
3. Delete a key to fall back to the compiled-in default in `config.cpp`.

Notes on specific settings:

- `api.nws_user_agent` — weather.gov requests a contact email in the User-Agent so they can reach you if your device misbehaves. Please set one.
- `api.airnow_api_key` — optional; a free [AirNow key](https://docs.airnowapi.org/) upgrades the AQI widget to the EPA's official measured US AQI. Leave empty to use Open-Meteo's modeled values.
- `graph_dewpoint` — optional second curve on the hourly graph: the forecast dew point, thinner than the temperature line and blue on full-color panels, with a small legend above the graph. The temperature axis rescales to include it. Handy for judging mugginess and overnight fog/frost risk (dew point closing on the temperature). Off by default; also a toggle in the portal.
- `widget_positions` — there are 12 widgets (sunrise, sunset, humidity, dewpoint, wind, UV, pressure, air quality, visibility, moon phase, indoor temp, indoor humidity) for 10 or 12 slots depending on `widget_rows`. Set a widget to -1 to hide it.

### Over-the-Air Firmware Updates

After the first USB flash, firmware updates can be installed through the portal:

1. Build the new firmware (`pio run`, for your board's environment) and locate `.pio/build/<board>/firmware.bin`.
2. Open the portal, scroll to **Firmware update** (it shows the currently installed build's timestamp), choose the `.bin`, and tap **Upload & Install**.
3. The image installs to a spare flash slot and only takes effect once it completes and verifies — a failed or interrupted upload leaves the running firmware untouched. The device restarts on the new firmware.

Make sure to upload the firmware built for **that device's** panel/board. Changes to the portal page itself or to `config.json` defaults still require a USB `uploadfs` (updating the filesystem over USB erases the on-device `config.json`, so re-enter settings via the portal afterward — or copy them into `data/config.json` first).

## Error Messages and Troubleshooting

### Low Battery
<img src="showcase/demo-error-low-battery.jpg" align="left" width="25%" />
This error screen appears once the battery voltage has fallen below <code>low_voltage_mv</code> (default = 3.462v, ~10%). The display will not refresh again until the battery is charged. While between <code>low_voltage_mv</code> and <code>very_low_voltage_mv</code> (default = 3.442v) the esp32 deep-sleeps for <code>low_sleep_interval_minutes</code> (default = 30min) between voltage checks; below that, for <code>very_low_sleep_interval_minutes</code> (default = 120min). If the voltage falls below <code>crit_low_voltage_mv</code> (default = 3.404v), the esp32 hibernates and requires a manual press of the reset (RST) button once charged. All thresholds are configurable in the portal's raw-JSON editor.

<br clear="left"/>

### WiFi Connection
<img src="showcase/demo-error-wifi.jpg" align="left" width="25%" />
This error screen appears when the ESP32 fails to connect to WiFi, with a second line explaining the likely cause: authentication failure (wrong password), network not found (mistyped SSID or out of range), no response from the access point, and so on. The device automatically retries every <code>wifi_retry_interval_minutes</code> (default = 15min) and recovers on its own once the network is reachable — no reset needed. To save battery, the error screen is only drawn once per outage. On mesh networks the device connects to the strongest access point broadcasting your SSID (logged over serial with its MAC and signal strength).

<br clear="left"/>

### API Error
<img src="showcase/demo-error-api.jpg" align="left" width="25%" />
This error screen appears if an error (client or server) occurs when making an API request to weather.gov or Open-Meteo. The second line gives the error code followed by a descriptor phrase. Positive error codes correspond to HTTP response status codes, while error codes <= 0 indicate a client (esp32) error. The esp32 will retry at the next scheduled refresh (default = every 15min). AirNow errors never show here — the AQI widget silently falls back to Open-Meteo (a note appears in the serial log).

<br clear="left"/>

### Time Server Error
<img src="showcase/demo-error-time.jpg" align="left" width="25%" />
This error screen appears when the esp32 fails to fetch the time from <code>ntp_server_1</code>/<code>ntp_server_2</code>. This error sometimes occurs immediately after uploading to the esp32; in this case, just hit the reset button or wait for the automatic retry (every <code>wifi_retry_interval_minutes</code>, default = 15min). If the error persists, try selecting closer/lower latency time servers or increasing <code>ntp_timeout_ms</code> in the portal's raw-JSON editor.

<br clear="left"/>

## Licensing

esp32-weather-epd is licensed under the [GNU General Public License v3.0](LICENSE) with tools, fonts, and icons whose licenses are as follows:

| Name | License | Description |
|---------------------------------------------------------------------------------------------------------------|---------------------------------------------------------------------------------------|------------------------------------------------------------------------------------|
| [Adafruit-GFX-Library: fontconvert](https://github.com/adafruit/Adafruit-GFX-Library/tree/master/fontconvert) | [BSD License](fonts/fontconvert/license.txt) | CLI tool for preprocessing fonts to be used with the Adafruit_GFX Arduino library. |
| [pollutant-concentration-to-aqi](https://github.com/lmarzen/pollutant-concentration-to-aqi) | [GNU Lesser General Public License v2.1](platformio/lib/pollutant-concentration-to-aqi/LICENSE) | C library that converts pollutant concentrations to Air Quality Index(AQI). |
| [InkyPi weather icons](https://github.com/fatihak/InkyPi) | [GNU General Public License v3.0](https://github.com/fatihak/InkyPi/blob/main/LICENSE) | (tools/inkypi_icons) Full-color weather icon artwork by Faith Akici, quantized for 7-color e-paper panels by tools/generate_color_icons.py. |
| [GNU FreeFont](https://www.gnu.org/software/freefont/) | [GNU General Public License v3.0](https://www.gnu.org/software/freefont/license.html) | Font Family |
| [Lato](https://fonts.google.com/specimen/Lato) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Montserrat](https://fonts.google.com/specimen/Montserrat) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Open Sans](https://fonts.google.com/specimen/Open+Sans) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Poppins](https://fonts.google.com/specimen/Poppins) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Quicksand](https://fonts.google.com/specimen/Quicksand) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Raleway](https://fonts.google.com/specimen/Raleway) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | Font Family |
| [Roboto](https://fonts.google.com/specimen/Roboto) | [Apache License v2.0](https://www.apache.org/licenses/LICENSE-2.0) | Font Family |
| [Roboto Mono](https://fonts.google.com/specimen/Roboto+Mono) | [Apache License v2.0](https://www.apache.org/licenses/LICENSE-2.0) | Font Family |
| [Roboto Slab](https://fonts.google.com/specimen/Roboto+Slab) | [Apache License v2.0](https://www.apache.org/licenses/LICENSE-2.0) | Font Family |
| [Ubuntu font](https://design.ubuntu.com/font) | [Ubuntu Font Licence v1.0](https://ubuntu.com/legal/font-licence) | Font Family |
| [Weather Themed Icons](https://github.com/erikflowers/weather-icons) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | (wi-**.svg) Weather icon family by Lukas Bischoff/Erik Flowers. |
| [Google Icons](https://fonts.google.com/icons) | [Apache License v2.0](https://www.apache.org/licenses/LICENSE-2.0) | (battery**.svg, visibility_icon.svg) Battery and visibility icons from Google Icons. |
| [Biological Hazard Symbol](https://svgsilh.com/image/37775.html) | [CC0 v1.0](https://en.wikipedia.org/wiki/Public_domain) | (biological_hazard_symbol.svg) Biohazard icon. |
| [House Icon](https://seekicon.com/free-icon/house_16) | [MIT License](http://opensource.org/licenses/mit-license.html) | (house.svg) House icon. |
| [Indoor Temerature/Humidity Icons](icons/svg) | [SIL OFL v1.1](http://scripts.sil.org/OFL) | (house_**.svg) Indoor temerature/humidity icons. |
| [Ionizing Radiation Symbol](https://svgsilh.com/image/309911.html) | [CC0 v1.0](https://creativecommons.org/publicdomain/zero/1.0/) | (ionizing_radiation_symbol.svg) Ionizing radiation icons. |
| [Phosphor Icons](https://github.com/phosphor-icons/homepage) | [MIT License](http://opensource.org/licenses/mit-license.html) | (wifi**.svg, warning_icon.svg, error_icon.svg) WiFi, Warning, and Error icons from Phosphor Icons. |
| [Wind Direction Icon](https://www.onlinewebfonts.com/icon/251550) | [CC BY v3.0](http://creativecommons.org/licenses/by/3.0) | (meteorological_wind_direction_**deg.svg) Meteorological wind direction icon from Online Web Fonts. |
