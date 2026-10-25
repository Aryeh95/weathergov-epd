/* Stand-in for WiFi.h on a PC: the types that headers mention. */
#ifndef SIM709_WIFI_H
#define SIM709_WIFI_H
class WiFiClient {};
typedef enum { WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_CONNECTED = 3 } wl_status_t;
#endif
