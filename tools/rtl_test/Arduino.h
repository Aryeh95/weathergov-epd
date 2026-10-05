// Stand-in for Arduino.h on the PC: just enough String for rtl.cpp.
#pragma once
#include <cstdint>
#include <cstring>
#include <string>

class String
{
public:
  String() {}
  String(const char *s) : s_(s ? s : "") {}
  String(const std::string &s) : s_(s) {}
  size_t length() const { return s_.size(); }
  bool isEmpty() const { return s_.empty(); }
  const char *c_str() const { return s_.c_str(); }
  void reserve(size_t n) { s_.reserve(n); }
  String &operator+=(char c) { s_.push_back(c); return *this; }
  String &operator+=(const char *s) { s_ += s; return *this; }
  String &operator+=(const String &o) { s_ += o.s_; return *this; }
  bool operator==(const String &o) const { return s_ == o.s_; }
  bool operator==(const char *o) const { return s_ == o; }
  const std::string &std() const { return s_; }
private:
  std::string s_;
};
inline String operator+(const String &a, const char *b) { String r(a); r += b; return r; }
