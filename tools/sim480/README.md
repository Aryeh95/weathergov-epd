# PC preview of the 800x480 layout, on real data

Builds the firmware's own drawing code (`renderer.cpp`, `display_utils.cpp`)
and its own parsers (`api_response.cpp`) for a PC, feeds the parsers saved
API replies, and writes what the renderer drew as pictures in the Spectra 6
panel's measured ink colours. Made for the Hebrew / Israel Meteorological
Service configuration, where the right-to-left text and the IMS feeds had
no panel to be tried on.

Only the panel is replaced (an Adafruit GFX canvas of ink indices, `shim/`),
along with the Arduino and ESP32 headers the code mentions. The Adafruit GFX
library itself is compiled as is, so text and shapes come out exactly as on
the device, and WiFiClient is a file, so the parsers run unchanged over the
replies in `data/`.

## Running it

```
pip install pillow
pio run -e seeed_reterminal_e1002      # once, for .pio/libdeps (GFX, ArduinoJson)
python tools/sim480/build.py [output folder] [data folder]
```

Needs a C++17 compiler (clang++ or g++ on the path).

## What it writes

| File | What it shows |
|---|---|
| `sim_netanya_dawn.png` | The moment the replies were saved (04:49), IMS's own current conditions |
| `sim_netanya_afternoon.png` | The same day at 13:30, current conditions from the forecast hour, the heat stress widget in the dew point's slot (the Hebrew build's default) and the Hebrew calendar date in the header |
| `sim_netanya_pollen.png` | The pollen widget (Google Pollen API) in place of the indoor humidity |
| `sim_netanya_alert.png` | With a warning worded as IMS words them, and a failed request in the status bar |

Each comes twice: the plain file has the dots exactly as the panel is told
to set them; `_blended` averages them 2x2, closer to what the eye makes of
the panel from across a desk.

## The data

`data/` holds the replies for Netanya (IMS location 7, warning region 101)
saved on 2026-10-05: `forecast_data` and `now_analysis` from ims.gov.il in
Hebrew, the `warnings` feed, Open-Meteo's air quality and Google's pollen
forecast -- the same requests the firmware makes. Replace them with your
own location's to preview it (`sim_main.cpp` names the location id, region
and coordinates).

The sample has no indoor sensor, so the indoor cells show `--`; pressure
and visibility show `--` because IMS reports neither (the Open-Meteo or
Google current-conditions source fills them on the device).
