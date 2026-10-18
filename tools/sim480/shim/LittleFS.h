/* Stand-in for LittleFS on a PC: the device's file system is a folder,
 * named by the SIM480_FS environment variable, so settings.cpp can read a
 * real config.json. Read-only use is all the simulator needs. */
#ifndef SIM480_LITTLEFS_H
#define SIM480_LITTLEFS_H
#include <cstdio>
#include <cstdlib>
#include <string>

class File
{
public:
  File(FILE *f = nullptr) : f_(f) {}
  explicit operator bool() const { return f_ != nullptr; }
  int read() { return f_ ? fgetc(f_) : -1; }
  size_t readBytes(char *buf, size_t n) { return f_ ? fread(buf, 1, n, f_) : 0; }
  size_t size()
  {
    if (!f_) return 0;
    long at = ftell(f_);
    fseek(f_, 0, SEEK_END);
    long n = ftell(f_);
    fseek(f_, at, SEEK_SET);
    return static_cast<size_t>(n);
  }
  void close()
  {
    if (f_) fclose(f_);
    f_ = nullptr;
  }
private:
  FILE *f_;
};

class LittleFSClass
{
public:
  bool begin(bool = false) { return true; }
  bool exists(const char *p)
  {
    FILE *f = fopen(path(p).c_str(), "rb");
    if (f) fclose(f);
    return f != nullptr;
  }
  File open(const char *p, const char *mode)
  {
    return File(fopen(path(p).c_str(), mode[0] == 'r' ? "rb" : "wb"));
  }
private:
  static std::string path(const char *p)
  {
    const char *root = getenv("SIM480_FS");
    return std::string(root ? root : "fs") + p;
  }
};

inline LittleFSClass LittleFS;
#endif
