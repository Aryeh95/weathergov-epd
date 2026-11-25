#!/usr/bin/env python3
"""Redraw the README's layout previews (showcase/preview-*.png), all from the
same weather: the weather.gov and Open-Meteo replies saved for New York in
tools/sim480/data/newyork, at the moment they were saved.

    python tools/showcase.py [scratch folder]

Runs tools/sim480 (the 800x480 layouts, the 4.26in pages, the 7.5in in four
greys) on those replies with the settings beside them (config.json there),
writes the weather it parsed to a file, and runs tools/sim709 (the 7.09in)
on that file, so every preview shows the same hour. Each picture is
white-balanced to one common white (--balance) and is the simulators'
`_blended` picture: the dots averaged 2x2, as the eye mixes them from a desk.

Needs what the two simulators need (see their READMEs): a C++17 compiler,
`pip install pillow numpy`, and one firmware build for .pio/libdeps. To
preview other weather, save replies with tools/sim480/fetch_us.py next to a
config.json like New York's (and a NOW file: the Unix time they were
saved) and change DATA below.
"""
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.normpath(os.path.join(HERE, ".."))
# SHOWCASE_DATA / SHOWCASE_DIR: other saved weather, other output (to compare)
DATA = os.environ.get("SHOWCASE_DATA") or os.path.join(HERE, "sim480", "data", "newyork")
NOW = open(os.path.join(DATA, "NOW")).read().strip()   # when the replies were saved
INDOOR = "21.5,46"           # the indoor sensor's reading: Celsius, %
SHOWCASE = os.environ.get("SHOWCASE_DIR") or os.path.join(ROOT, "showcase")

# panel -> [(picture sim480 drew, preview it becomes)]
SIM480 = {
    "e6":     [("sim_us_now_blended", "preview-e1002"),
               ("sim_error_battery_blended", "error-battery"),
               ("sim_error_wifi_blended", "error-wifi"),
               ("sim_error_api_blended", "error-api"),
               ("sim_error_time_blended", "error-time"),
               ("sim_portal_blended", "setup-screen")],
    "bwv2":   [("sim_us_now_blended", "preview-75-bw")],
    "3c":     [("sim_us_now_blended", "preview-75-3c")],
    "4g75":   [("sim_us_now_blended", "preview-75-grey"),
               ("sim_us_forecast_blended", "preview-75-grey-forecast")],
    "4g426":  [("sim_ee05_home_blended", "preview-ee05-426-home"),
               ("sim_ee05_hours_blended", "preview-ee05-426-hours"),
               ("sim_ee05_week_blended", "preview-ee05-426-week")],
    "e6_426": [("sim_ee05_home_blended", "preview-e1002-426c")],
}


def main():
    out = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "showcase_out"))
    os.makedirs(out, exist_ok=True)
    weather = os.path.join(out, "weather.txt")
    env = dict(os.environ, SIM480_FS=DATA, SIM480_NOW=NOW, SIM480_INDOOR=INDOOR)
    for panel, pictures in SIM480.items():
        # the 7.3in run also writes the weather for sim709 and the error
        # screens for the README's Troubleshooting section
        run_env = dict(env, SIM480_DUMP=weather, SIM480_ERRORS="1") if panel == "e6" else env
        subprocess.check_call([sys.executable, os.path.join(HERE, "sim480", "build.py"), out, DATA,
                               "--panel=" + panel, "--locale=en_US", "--balance"], env=run_env)
        folder = os.path.join(out, panel, "en_US") if panel != "e6" else os.path.join(out, "en_US")
        for src, dst in pictures:
            shutil.copyfile(os.path.join(folder, src + ".png"), os.path.join(SHOWCASE, dst + ".png"))
            print("showcase/%s.png" % dst)
    subprocess.check_call([sys.executable, os.path.join(HERE, "sim709", "build.py"),
                           os.path.join(out, "709"), "--balance", "--weather=" + weather])
    for src, dst in (("sim_weather_blended", "preview-gdeb0709e01"),
                     ("sim_weather_stacked_blended", "preview-gdeb0709e01-stacked")):
        shutil.copyfile(os.path.join(out, "709", src + ".png"),
                        os.path.join(SHOWCASE, dst + ".png"))
        print("showcase/%s.png" % dst)


if __name__ == "__main__":
    main()
