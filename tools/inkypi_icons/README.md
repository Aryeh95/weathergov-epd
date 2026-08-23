# InkyPi icon sources

Weather and widget icon artwork from the InkyPi project by Faith Akici,
https://github.com/fatihak/InkyPi (`src/plugins/weather/icons/`), licensed
GPL-3.0 — the same license as this project.

These PNGs are the inputs to `tools/generate_color_icons.py`, which
quantizes them to the native inks of multicolor e-paper panels and emits
`platformio/lib/esp32-weather-epd-assets/icons/icons_color.h`. They are
vendored here so the generated header stays reproducible from this
repository alone, independent of the upstream repo's future changes.

The dewpoint / indoor temperature / indoor humidity icons are NOT from
InkyPi — the generator script draws them itself (in the same flat style)
each time it runs.
