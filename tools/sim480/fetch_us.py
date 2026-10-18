"""Saves the replies the firmware would fetch for the location in config.json
(weather.gov, Open-Meteo, AirNow, Google Pollen) for sim480's US mode.
Keys and coordinates are read from config.json and never printed."""
import json, os, re, sys, urllib.request, urllib.parse

CFG, OUT = sys.argv[1], sys.argv[2]
os.makedirs(OUT, exist_ok=True)


def strip(t):
    out, i, q = [], 0, False
    while i < len(t):
        c = t[i]
        if q:
            out.append(c)
            if c == "\\":
                out.append(t[i + 1]); i += 1
            elif c == '"':
                q = False
        elif c == '"':
            q = True; out.append(c)
        elif t.startswith("//", i):
            while i < len(t) and t[i] != "\n":
                i += 1
            continue
        elif t.startswith("/*", i):
            j = t.index("*/", i + 2); i = j + 2
            continue
        else:
            out.append(c)
        i += 1
    return re.sub(r",(\s*[}\]])", r"\1", "".join(out))


cfg = json.loads(strip(open(CFG, encoding="utf-8").read()))
LAT, LON = str(cfg["location"]["lat"]), str(cfg["location"]["lon"])
api = cfg.get("api", {})
UA = api.get("nws_user_agent") or "esp32-weather-epd"
AIRNOW = api.get("airnow_api_key", "")
GKEY = api.get("pollen_api_key", "")


def get(url, name, ua=None, secret=""):
    req = urllib.request.Request(url, headers={"User-Agent": ua or "esp32-weather-epd sim480",
                                               "Accept": "application/geo+json, application/json"})
    try:
        with urllib.request.urlopen(req, timeout=60) as r:
            data = r.read()
        open(os.path.join(OUT, name), "wb").write(data)
        print("%-24s %7d bytes" % (name, len(data)))
        return json.loads(data)
    except Exception as e:
        print("%-24s FAILED %s %s" % (name, type(e).__name__, str(getattr(e, "code", "")).replace(secret or "\0", "<key>")))
        return None


pts = get("https://api.weather.gov/points/%s,%s" % (LAT, LON), "nws_points.json", UA)
fc, fch = pts["properties"]["forecast"], pts["properties"]["forecastHourly"]
get(fc, "nws_forecast.json", UA)
get(fch, "nws_hourly.json", UA)
get(fch[:fch.index("/forecast")], "nws_gridpoint.json", UA)
get("https://api.weather.gov/alerts/active?point=%s,%s" % (LAT, LON), "nws_alerts.json", UA)
get("https://api.open-meteo.com/v1/forecast?latitude=%s&longitude=%s" % (LAT, LON)
    + "&current=temperature_2m,apparent_temperature,relative_humidity_2m,dew_point_2m,pressure_msl,"
      "cloud_cover,visibility,wind_speed_10m,wind_gusts_10m,wind_direction_10m&wind_speed_unit=ms"
    + "&daily=precipitation_sum&forecast_days=7&timezone=auto&timeformat=unixtime", "openmeteo_current.json")
get("https://air-quality-api.open-meteo.com/v1/air-quality?latitude=%s&longitude=%s" % (LAT, LON)
    + "&hourly=pm10,pm2_5,carbon_monoxide,nitrogen_dioxide,sulphur_dioxide,ozone,uv_index"
      "&timezone=UTC&past_hours=24&forecast_hours=0", "openmeteo_air.json")
if AIRNOW:
    lat, lon = float(LAT), float(LON)
    q = ("format=application/json&parameters=OZONE,PM25,PM10&BBOX=%.4f,%.4f,%.4f,%.4f"
         "&dataType=B&monitorType=2&verbose=0&includerawconcentrations=0&API_KEY=%s"
         % (lon - 0.3, lat - 0.3, lon + 0.3, lat + 0.3, AIRNOW))
    get("https://www.airnowapi.org/aq/data/?" + q, "airnow.json", secret=AIRNOW)
if GKEY:
    get("https://pollen.googleapis.com/v1/forecast:lookup?location.latitude=%s&location.longitude=%s&days=1&key=%s"
        % (LAT, LON, GKEY), "google_pollen.json", secret=GKEY)
    if api.get("current_source") == "google":
        get("https://weather.googleapis.com/v1/currentConditions:lookup?location.latitude=%s&location.longitude=%s&key=%s"
            % (LAT, LON, GKEY), "google_current.json", secret=GKEY)
print("current_source:", api.get("current_source"), " aqi_source:", api.get("aqi_source", "auto"))
