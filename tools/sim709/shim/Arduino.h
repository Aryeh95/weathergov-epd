/* Stand-in for Arduino.h, for building the 1600x1200 renderer on a PC.
 * Only what renderer709.cpp, epd709.cpp and the files they pull in use.
 * Part of esp32-weather-epd; GNU General Public License v3 or later.
 */
#ifndef SIM709_ARDUINO_H
#define SIM709_ARDUINO_H

#ifndef _USE_MATH_DEFINES
#define _USE_MATH_DEFINES
#endif
#include <algorithm>
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

#define PROGMEM
#define pgm_read_byte(p) (*(const uint8_t *)(p))
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

struct SerialStub
{
  void begin(unsigned long) {}
  void print(const String &s) { fputs(s.c_str(), stdout); }
  void println(const String &s) { puts(s.c_str()); }
  void println() { puts(""); }
  void printf(const char *, ...) {}
};
extern SerialStub Serial;

inline unsigned long millis() { return 0; }
inline void delay(unsigned long) {}

#define OUTPUT 1
#define INPUT 0
#define HIGH 1
#define LOW 0
inline void pinMode(int, int) {}
inline void digitalWrite(int, int) {}

using std::max;
using std::min;

#endif
