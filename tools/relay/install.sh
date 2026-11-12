#!/bin/sh
# Installs (or updates) the weather relay as a systemd service.
# Run from the folder holding weather_relay.py:  sudo sh install.sh [port]
set -e
PORT="${1:-8080}"
HERE="$(cd "$(dirname "$0")" && pwd)"

if [ "$(id -u)" -ne 0 ]; then
  echo "Run it with sudo: sudo sh $0 $PORT"; exit 1
fi
if [ ! -f "$HERE/weather_relay.py" ]; then
  echo "weather_relay.py is not next to install.sh in $HERE"; exit 1
fi
python3 -c 'import sys; sys.exit(sys.version_info < (3, 7))' \
  || { echo "Needs Python 3.7 or newer: $(python3 --version 2>&1)"; exit 1; }
if ss -ltn | grep -q ":$PORT "; then
  if ! systemctl is-active --quiet weather-relay; then
    echo "Port $PORT is already in use; pick another: sudo sh $0 <port>"; exit 1
  fi
fi

install -D -m 755 "$HERE/weather_relay.py" /opt/weather-relay/weather_relay.py
cat > /etc/systemd/system/weather-relay.service <<EOF
[Unit]
Description=Weather relay for esp32-weather-epd
After=network-online.target
Wants=network-online.target

[Service]
ExecStart=/usr/bin/python3 /opt/weather-relay/weather_relay.py --port $PORT -v
DynamicUser=yes
Restart=always
RestartSec=10

[Install]
WantedBy=multi-user.target
EOF
systemctl daemon-reload
systemctl enable weather-relay >/dev/null 2>&1
systemctl restart weather-relay
sleep 2
if systemctl is-active --quiet weather-relay; then
  echo "Weather relay running on port $PORT. Status: http://$(hostname -I | cut -d' ' -f1):$PORT/status"
else
  echo "The relay did not start:"; journalctl -u weather-relay -n 20 --no-pager; exit 1
fi
