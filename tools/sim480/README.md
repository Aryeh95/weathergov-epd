# PC preview of the 800x480 layout, on real data

Builds the firmware's own drawing code (`renderer.cpp`, `display_utils.cpp`)
and its own parsers (`api_response.cpp`) for a PC, feeds the parsers saved
API replies, and writes what the renderer drew as pictures in the Spectra 6
panel's measured ink colours (or the black-and-white and three-colour
7.5in panels, see `--panel`). Made for the Hebrew / Israel Meteorological
Service configuration, where the right-to-left text and the IMS feeds had
no panel to be tried on.

Only the panel is replaced (an Adafruit GFX canvas of ink indices, `shim/`),
along with the Arduino and ESP32 headers the code mentions. The Adafruit GFX
library itself is compiled as is, so text and shapes come out exactly as on
the device, and WiFiClient is a file, so the parsers run unchanged over the
replies in `data/`.

## Running it

```
pip install pillow numpy
pio run -e seeed_reterminal_e1002      # once, for .pio/libdeps (GFX, ArduinoJson)
python tools/sim480/build.py [output folder] [data folder] [--panel=e6|bwv2|3c]
```

Needs a C++17 compiler (clang++ or g++ on the path).

`--panel` picks the panel the page is drawn for, with the same defines as
the firmware's own environments: `e6` (default) is the reTerminal E1002's
Spectra 6, `bwv2` the 7.5in black-and-white panel of the reTerminal E1001
and the DESPI-C02 default build (`DISP_BW_V2`: line-art icons, dotted
dividers in the forecast row), `3c` the red/black/white 7.5in (B) panel
(`DISP_3C_B`). The other panels' pictures go into a subfolder named after
the panel. Their paper and ink colours are nominal, not measured like the
Spectra 6's.

## What it writes

| File | What it shows |
|---|---|
| `sim_netanya_dawn.png` | The moment the replies were saved (04:49), IMS's own current conditions |
| `sim_netanya_afternoon.png` | The same day at 13:30, current conditions from the forecast hour, the heat stress widget in the dew point's slot (the Hebrew build's default) and the Hebrew calendar date in the header |
| `sim_netanya_5day.png` | The same with a 5-day forecast row: wider columns, full day names |
| `sim_netanya_pollen.png` | The pollen widget (Google Pollen API) in place of the indoor humidity |
| `sim_netanya_alert.png` | With a warning worded as IMS words them, and a failed request in the status bar |
| `sim_netanya_alerts2.png` | Two warnings at once (the stacked 32px layout) |
| `sim_netanya_friday.png` | Friday 16:30: candle lighting in the sunset widget, and the date format with the holiday name (`%Q`, empty on an ordinary day) |
| `sim_netanya_shabbat.png` | Shabbat noon: when it ends |

Each comes twice: the plain file has the dots exactly as the panel is told
to set them; `_blended` shows them 2x2, closer to what the eye makes of
the panel from across a desk. For the Spectra 6 the colours are the
reTerminal E1002's own, measured on its ink card
(`seeed_reterminal_e1002_testcard`; a flatbed scan, and a raw photo beside
white paper for the panel's white, 41 % of the paper): `measured_e6.json`.
On this panel two inks dot by dot come out darker than their average (a
black/white checker is 29 % of white, not 51 %), so `_blended` takes the
mixes the card measured, and for other patterns the dots after the dot
gain fitted to them. Set `SIM480_TESTCARD=1` to draw the card itself.

## The data

`data/` holds the replies for Netanya (IMS location 7, warning region 101)
saved on 2026-10-05: `forecast_data` and `now_analysis` from ims.gov.il in
Hebrew, the `warnings` feed, Open-Meteo's air quality, the Ministry of Environmental Protection's
latest-index list (`sviva_index_region4.json`) and Google's pollen
forecast -- the same requests the firmware makes. `openmeteo_gaps.json`
is the Open-Meteo request that fills in pressure and visibility when IMS
supplies the current conditions; its values are a typical sample written
by hand in the API's shape, not a saved reply. Replace them with your
own location's to preview it (`sim_main.cpp` names the location id, region
and coordinates).

The sample has no indoor sensor, so the indoor cells show `--`; pressure
and visibility show `--` because IMS reports neither (the Open-Meteo or
Google current-conditions source fills them on the device).

## Your own device's page (`--locale=en_US`)

A US build draws the page a real device would show, from its own settings
and the replies for its location:

```
python tools/sim480/fetch_us.py path/to/config.json us_data
SIM480_FS=folder_with_config.json python tools/sim480/build.py out us_data --locale=en_US
```

`fetch_us.py` saves what the firmware would fetch: weather.gov's forecast,
hourly forecast, gridpoint precipitation and alerts, Open-Meteo's current
conditions and air quality, and AirNow and Google Pollen when config.json
has their keys. Keys and coordinates come from config.json and are not
written anywhere else -- but the replies name the place, so keep them out
of the repository. The page is drawn for the moment the simulator runs
(`SIM480_NOW`, a Unix time, to choose another); `SIM480_INDOOR="21.5,46"`
gives the indoor sensor a reading (Celsius, %).

## The XIAO EE05's 4.26in (`--panel=4g426`)

The EE05 build has a layout of its own (`renderer426.cpp`): a home page in
the panel's four greys and pages in black and white that KEY3 turns to.
`--panel=4g426 --locale=en_US` draws all of them -- `sim_ee05_home`,
`sim_ee05_hours`, `sim_ee05_week` -- in the greys measured on the
`seeed_xiao_ee05_testcard` card (`measured_426.json`): black, dark grey,
light grey and white are L* 16, 69, 81 and 100, the panel's white 68.5 %
of white paper's. Dark grey is far lighter than its name; the black/dark
grey and black/light grey checkers (L* 39 and 47) are the darker greys.
`_blended` colours each 2x2 window by the measured checkers, as for the
Spectra 6.

| Setting | Draws |
|---|---|
| `SIM480_TESTCARD=1` | the greys card instead of the weather |
| `SIM426_PALETTES=1` | the home page in each of its palettes, `sim_ee05_home_pN` |
| `SIM426_HOURS=1` | the hours page in each of its layouts, `sim_ee05_hours_lN` |
| `SIM426_RAIN=1` | a made-up afternoon of showers, to see the rain on a dry day |
| `SIM426_TILES="wind,moon_phase,indoor"` | the home tiles, as config.json `home_tiles` |
