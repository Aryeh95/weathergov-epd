# WiFiClientSecure (vendored)

A verbatim copy of the WiFiClientSecure library from arduino-esp32 2.0.17
(PlatformIO espressif32 @ 6.13.0), placed in the project's lib/ directory so
that it shadows the framework's copy, with ONE change in src/ssl_client.cpp:

mbedtls_ssl_conf_curves() is called after mbedtls_ssl_config_defaults() to
offer X25519 and P-256 first for the ECDHE key exchange. The framework never
sets a curve preference, so mbedTLS's default order goes out with secp521r1
first; servers that honor the client's preference (api.open-meteo.com and
air-quality-api.open-meteo.com do) then pick P-521, which the ESP32 computes
in software at roughly 1.5 s per handshake. Measured on the reTerminal E1002:
2.7-3.0 s per Open-Meteo handshake before this change.

If the pinned framework version changes, re-copy the library from
~/.platformio/packages/framework-arduinoespressif32/libraries/WiFiClientSecure
and re-apply that one block (search for preferred_curves).
