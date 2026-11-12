# Local weather relay

An optional helper for an always-on machine at home: a Raspberry Pi
running Pi-hole, a Home Assistant box, a NAS. Without it, each wake the
display opens a TLS connection to every weather service, which takes about
10-20 seconds of WiFi. With it, the display asks the relay for everything over
plain HTTP on the home network. The relay has usually fetched the replies
a minute or two before the display wakes, so the whole fetch takes about a
second.

The relay doesn't interpret any of the data. It fetches whatever URL the
firmware asks for and passes back the status and body unchanged, so every
forecast source works through it and new sources need no change here. If
the relay is down or slow to answer, the display fetches directly for the
rest of that wake, so a powered-off Pi never leaves the screen stale.

## How it works

- The display requests
  `http://<relay>:8080/https/api.weather.gov/alerts/active?...`. The relay
  fetches `https://api.weather.gov/alerts/active?...`, passing along the
  display's User-Agent and any extra headers.
- The relay records when each URL is requested and estimates the interval
  from the median gap between requests. About 2 minutes (`--lead`) before
  the next expected request, it fetches the URL again.
- A copy up to 10 minutes old (`--max-age`) is served from the cache;
  anything older is fetched live. Error replies are never cached.
- URLs the display stops requesting are no longer prefetched, and are
  forgotten after a day. So the relay never makes more requests than the
  display would make itself, and API quotas (AirNow, Google) are unaffected.
- Only the services the firmware uses are relayed (`ALLOWED_HOSTS`; add
  others with `--allow`), so the relay isn't an open proxy for your
  network.
- `http://<relay>:8080/status` lists every URL, how often it is requested,
  and how old the cached copy is.

Keep in mind that requests between the display and the relay are plain
HTTP, API keys included. They stay on your home network, but run the relay
only on a network you trust, and don't expose its port to the internet.

## Install on a Raspberry Pi (Pi-hole or anything else)

Needs Python 3.7 or newer (Raspberry Pi OS has it) and no extra packages.

1. Copy `weather_relay.py` and `install.sh` to the Pi, for example from
   this folder:

   ```bash
   scp weather_relay.py install.sh pi@<pi-address>:/tmp/
   ```

2. On the Pi, run the installer. It checks the Python version and that the
   port is free (Pi-hole uses 80 and 443), installs the script to
   `/opt/weather-relay`, and starts it as a service that also starts at
   boot. Add a port number to use one other than 8080.

   ```bash
   sudo sh /tmp/install.sh
   ```

3. Check it from any computer on the network:

   ```bash
   curl "http://<pi-address>:8080/https/api.open-meteo.com/v1/forecast?latitude=40.71&longitude=-74.01&current=temperature_2m"
   ```

   Run it twice: the second reply comes from the cache. Follow the log with
   `journalctl -u weather-relay -f`.

4. In the display's settings portal, under API, set **Local relay** to
   `<pi-address>:8080`. Use the IP address: the display can't resolve
   `.local` names. A fixed DHCP lease for the Pi keeps that address stable.

On the display's serial log, relayed requests show as
`200 OK  via relay (12 ms to headers, 40 ms body, fetched 95 s earlier)`.

To update the relay later, copy the new script over and run the installer again. To remove it:
`sudo systemctl disable --now weather-relay`, then delete
`/etc/systemd/system/weather-relay.service` and `/opt/weather-relay`.

## Options

| Option | Default | |
|---|---|---|
| `--port` | 8080 | Port to listen on |
| `--bind` | 0.0.0.0 | Address to listen on |
| `--max-age` | 600 | Seconds a fetched copy may be served |
| `--lead` | 120 | Seconds before the expected wake to prefetch |
| `--allow HOST` | | Relay another host as well (repeatable) |
| `-v` | | Log every request (keys are masked) |
