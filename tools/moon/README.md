# The moon's surface

`lroc_color_poles_1k.jpg` is NASA's colour map of the moon, assembled from
Lunar Reconnaissance Orbiter Camera images, from the CGI Moon Kit of NASA's
Scientific Visualization Studio: https://svs.gsfc.nasa.gov/4720

NASA imagery is in the public domain.

`tools/gen_moon426.py` wraps it on the near side of a sphere and writes its
brightness to `platformio/lib/esp32-weather-epd-assets/assets426/moon426.h`.
