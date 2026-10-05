/* Stand-in for Arduino.h, for building the 800x480 renderer on a PC.
 * Only what renderer.cpp, display_utils.cpp, api_response.cpp, the Adafruit
 * GFX library and the files they pull in use.
 * Part of esp32-weather-epd; GNU General Public License v3 or later.
 */
#ifndef SIM480_ARDUINO_H
#define SIM480_ARDUINO_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
#include <climits>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#ifndef ARDUINO
#define ARDUINO 10000
#endif
#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
#define pgm_read_word(p) (*(const uint16_t *)(p))
#define pgm_read_dword(p) (*(const uint32_t *)(p))
#define pgm_read_float(p) (*(const float *)(p))
#define pgm_read_pointer(p) (*(void *const *)(p))
#define memcpy_P memcpy
#define strlen_P strlen
#define F(x) (x)
class __FlashStringHelper;
typedef uint8_t byte;
inline void yield() {}
#define radians(deg) ((deg) * M_PI / 180.0)
#define degrees(rad) ((rad) * 180.0 / M_PI)
#define sq(x) ((x) * (x))
#define LED_BUILTIN 2
#define constrain(v, lo, hi) ((v) < (lo) ? (lo) : ((v) > (hi) ? (hi) : (v)))
#define DEC 10
typedef bool boolean;

#ifdef _WIN32
inline struct tm *localtime_r(const time_t *t, struct tm *out)
{
  localtime_s(out, t);
  return out;
}
#endif

class String
{
public:
  String() {}
  String(const char *s) : v(s ? s : "") {}
  String(const std::string &s) : v(s) {}
  String(char c) : v(1, c) {}
  String(int n) : v(std::to_string(n)) {}
  String(unsigned n) : v(std::to_string(n)) {}
  String(long n) : v(std::to_string(n)) {}
  String(unsigned long n) : v(std::to_string(n)) {}
  String(long long n) : v(std::to_string(n)) {}
  String(unsigned long long n) : v(std::to_string(n)) {}
  String(float f, int places = 2) { set(f, places); }
  String(double f, int places = 2) { set(f, places); }

  const char *c_str() const { return v.c_str(); }
  unsigned length() const { return static_cast<unsigned>(v.size()); }
  bool isEmpty() const { return v.empty(); }
  char charAt(unsigned i) const { return i < v.size() ? v[i] : 0; }
  void setCharAt(unsigned i, char c) { if (i < v.size()) v[i] = c; }
  char operator[](unsigned i) const { return charAt(i); }

  String substring(unsigned from) const
  {
    return from >= v.size() ? String() : String(v.substr(from));
  }
  String substring(unsigned from, unsigned to) const
  {
    if (from >= v.size() || to <= from) return String();
    return String(v.substr(from, to - from));
  }
  int indexOf(char c, unsigned from = 0) const
  {
    const size_t p = v.find(c, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int indexOf(const String &s, unsigned from = 0) const
  {
    const size_t p = v.find(s.v, from);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  int lastIndexOf(const String &s) const
  {
    const size_t p = v.rfind(s.v);
    return p == std::string::npos ? -1 : static_cast<int>(p);
  }
  bool startsWith(const String &s) const { return v.rfind(s.v, 0) == 0; }
  bool endsWith(const String &s) const
  {
    return v.size() >= s.v.size()
        && v.compare(v.size() - s.v.size(), s.v.size(), s.v) == 0;
  }
  void replace(const String &a, const String &b)
  {
    if (a.v.empty()) return;
    size_t p = 0;
    while ((p = v.find(a.v, p)) != std::string::npos)
    {
      v.replace(p, a.v.size(), b.v);
      p += b.v.size();
    }
  }
  void remove(unsigned i) { if (i < v.size()) v.erase(i); }
  void remove(unsigned i, unsigned n) { if (i < v.size()) v.erase(i, n); }
  void trim()
  {
    size_t a = 0, b = v.size();
    while (a < b && isspace(static_cast<unsigned char>(v[a]))) ++a;
    while (b > a && isspace(static_cast<unsigned char>(v[b - 1]))) --b;
    v = v.substr(a, b - a);
  }
  void toLowerCase() { for (char &c : v) c = static_cast<char>(tolower(static_cast<unsigned char>(c))); }
  void toUpperCase() { for (char &c : v) c = static_cast<char>(toupper(static_cast<unsigned char>(c))); }
  double toDouble() const { return atof(v.c_str()); }
  float toFloat() const { return static_cast<float>(atof(v.c_str())); }
  long toInt() const { return atol(v.c_str()); }

  void reserve(unsigned) {}
  bool concat(const char *s, unsigned n) { v.append(s, n); return true; }
  bool concat(const char *s) { v += s; return true; }
  bool concat(char c) { v += c; return true; }
  bool equalsIgnoreCase(const String &s) const
  {
    if (v.size() != s.v.size()) return false;
    for (size_t i = 0; i < v.size(); ++i)
      if (tolower(static_cast<unsigned char>(v[i])) != tolower(static_cast<unsigned char>(s.v[i]))) return false;
    return true;
  }
  String &operator+=(const String &s) { v += s.v; return *this; }
  String &operator+=(const char *s) { v += s; return *this; }
  String &operator+=(char c) { v += c; return *this; }
  bool operator==(const String &s) const { return v == s.v; }
  bool operator==(const char *s) const { return v == s; }
  bool operator!=(const String &s) const { return v != s.v; }
  bool operator!=(const char *s) const { return v != s; }
  bool operator<(const String &s) const { return v < s.v; }

  friend String operator+(const String &a, const String &b) { return String(a.v + b.v); }
  friend String operator+(const String &a, const char *b) { return String(a.v + b); }
  friend String operator+(const char *a, const String &b) { return String(std::string(a) + b.v); }
  friend String operator+(const String &a, char b) { return String(a.v + b); }

private:
  std::string v;
  void set(double f, int places)
  {
    char buf[48];
    snprintf(buf, sizeof(buf), "%.*f", places, f);
    v = buf;
  }
};

inline char toUpperCase(char c) { return static_cast<char>(toupper(static_cast<unsigned char>(c))); }
inline char toLowerCase(char c) { return static_cast<char>(tolower(static_cast<unsigned char>(c))); }

/* Arduino's Print, as far as Adafruit_GFX and the renderer use it. */
class Print
{
public:
  virtual ~Print() {}
  virtual size_t write(uint8_t) = 0;
  virtual size_t write(const uint8_t *buf, size_t n)
  {
    size_t k = 0;
    while (n--) k += write(*buf++);
    return k;
  }
  size_t write(const char *s) { return s ? write(reinterpret_cast<const uint8_t *>(s), strlen(s)) : 0; }
  size_t print(const String &s) { return write(reinterpret_cast<const uint8_t *>(s.c_str()), s.length()); }
  size_t print(const char *s) { return write(s); }
  size_t print(char c) { return write(static_cast<uint8_t>(c)); }
  size_t print(int n) { return print(String(n)); }
  size_t print(unsigned n) { return print(String(n)); }
  size_t print(long n) { return print(String(n)); }
  size_t print(unsigned long n) { return print(String(n)); }
  size_t print(double d, int places = 2) { return print(String(d, places)); }
  size_t println() { return write("\n"); }
  template <typename T> size_t println(const T &t) { size_t k = print(t); return k + println(); }
};

struct SerialStub
{
  void begin(unsigned long) {}
  void print(const String &s) { fputs(s.c_str(), stdout); }
  void println(const String &s) { puts(s.c_str()); }
  void println() { puts(""); }
  void printf(const char *, ...) {}
};
extern SerialStub Serial;

struct SPIStub
{
  void begin(int = -1, int = -1, int = -1, int = -1) {}
  void end() {}
};
extern SPIStub SPI;

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}
inline int analogRead(int) { return 2000; }
inline uint32_t analogReadMilliVolts(int) { return 2000; }
typedef int gpio_num_t;
inline void gpio_hold_en(gpio_num_t) {}
inline void gpio_deep_sleep_hold_en() {}

using std::max;
using std::min;

#endif
