# The moon's surface

`lroc_color_poles_1k.jpg` is NASA's colour map of the moon, assembled from
Lunar Reconnaissance Orbiter Camera images, from the CGI Moon Kit of NASA's
Scientific Visualization Studio: https://svs.gsfc.nasa.gov/4720

NASA imagery is in the public domain.

`tools/gen_assets709.py moon` wraps it on a sphere, lights it from the side
for each of 32 phases, and writes the result as 72 px bitmaps in
`platformio/lib/esp32-weather-epd-assets/assets709/moon709.h`.
