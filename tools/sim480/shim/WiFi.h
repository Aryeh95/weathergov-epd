/* Stand-in for WiFi.h on a PC. WiFiClient here is a file: ArduinoJson's
 * generic reader only needs read() and readBytes(), so the firmware's own
 * parsers (api_response.cpp) run unchanged over saved replies. */
#ifndef SIM480_WIFI_H
#define SIM480_WIFI_H
#include <cstdio>
#include <string>
class WiFiClient
{
public:
  bool load(const char *path)
  {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    char buf[4096];
    size_t n;
    data_.clear();
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) data_.append(buf, n);
    fclose(f);
    pos_ = 0;
    return true;
  }
  int read() { return pos_ < data_.size() ? static_cast<unsigned char>(data_[pos_++]) : -1; }
  int peek() { return pos_ < data_.size() ? static_cast<unsigned char>(data_[pos_]) : -1; }
  int available() { return static_cast<int>(data_.size() - pos_); }
  size_t readBytes(char *buf, size_t n)
  {
    size_t k = 0;
    while (k < n && pos_ < data_.size()) buf[k++] = data_[pos_++];
    return k;
  }
private:
  std::string data_;
  size_t pos_ = 0;
};
typedef enum {
  WL_NO_SHIELD = 255, WL_IDLE_STATUS = 0, WL_NO_SSID_AVAIL = 1, WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3, WL_CONNECT_FAILED = 4, WL_CONNECTION_LOST = 5, WL_DISCONNECTED = 6
} wl_status_t;
#endif
