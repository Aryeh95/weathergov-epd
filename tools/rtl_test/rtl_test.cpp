/* Desktop check for the right-to-left text shaper (src/rtl.cpp).
 *
 * Feeds UTF-8 Hebrew, mixed and Latin strings through shapeText() and
 * checks the ISO-8859-8 bytes that come out: Hebrew runs reversed, numbers
 * and Latin words in their own order, signs attached to their numbers,
 * brackets mirrored, Latin-1 escapes passed through, Latin-only text
 * untouched.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Arduino.h"
#include "_locale.h"
#include "rtl.h"

bool LC_RTL = true;

static int failures = 0;

// ISO-8859-8 bytes back to UTF-8, for printing what the panel would show
// left to right.
static std::string show(const String &s)
{
  std::string out;
  for (unsigned char b : s.std())
  {
    if (b >= 0xE0 && b <= 0xFA)
    {
      unsigned cp = 0x5D0 + (b - 0xE0);
      out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
      out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
    }
    else if (b >= 0x80)
    {
      out.push_back(static_cast<char>(0xC0 | (b >> 6)));
      out.push_back(static_cast<char>(0x80 | (b & 0x3F)));
    }
    else
    {
      out.push_back(static_cast<char>(b));
    }
  }
  return out;
}

static std::string hex(const String &s)
{
  std::string out;
  char buf[4];
  for (unsigned char b : s.std())
  {
    snprintf(buf, sizeof(buf), "%02X ", b);
    out += buf;
  }
  return out;
}

// `expectVisual` is the expected panel order written left to right in
// UTF-8 (Hebrew letters typed in the order they appear on the panel).
static void check(const char *name, const char *input, const std::string &expectVisual)
{
  String got = shapeText(String(input));
  std::string shown = show(got);
  bool ok = shown == expectVisual;
  printf("%s %-28s in: %s\n%s           out: %s   [%s]\n", ok ? "ok  " : "FAIL",
         name, input, ok ? "" : "    ", shown.c_str(), hex(got).c_str());
  if (!ok)
  {
    printf("          expected: %s\n", expectVisual.c_str());
    ++failures;
  }
}

int main()
{
  // a single Hebrew word reverses
  check("word", "לחות", "תוחל");
  // the number keeps its order and its sign, and sits at the left
  check("label + percent", "לחות 91%", "91% תוחל");
  check("label + degrees", "מרגיש כמו 24°C", "24°C ומכ שיגרמ");
  check("negative", "טמפרטורה -3°", "-3° הרוטרפמט");
  check("time", "זריחה 06:42", "06:42 החירז");
  // Hebrew on both sides of a number
  check("date", "יום ג' 5 באוק'", "'קואב 5 'ג םוי");
  // brackets mirror inside Hebrew
  check("brackets", "(סערה)", "(הרעס)");
  // Latin word inside Hebrew keeps its letters in order
  check("latin inside", "מדד UV גבוה", "הובג UV דדמ");
  // Latin-only lines are left alone, including the Latin-1 degree escape
  check("latin only", "weather.gov API (points)", "weather.gov API (points)");
  check("latin-1 escape", "24\260C", "24\xC2\xB0" "C");
  // UTF-8 degree sign lands on the same 0xB0 slot
  {
    String a = shapeText(String("24\260C"));
    String b = shapeText(String("24°C"));
    bool ok = a == b;
    printf("%s %-28s %s\n", ok ? "ok  " : "FAIL", "degree slots agree", hex(a).c_str());
    if (!ok) ++failures;
  }
  // every Hebrew letter maps into 0xE0-0xFA, nothing becomes '?'
  {
    String all = shapeText(String("אבגדהוזחטיךכלםמןנסעףפץצקרשת"));
    bool ok = all.length() == 27;
    for (unsigned char b : all.std()) ok = ok && b >= 0xE0 && b <= 0xFA;
    printf("%s %-28s %zu bytes\n", ok ? "ok  " : "FAIL", "alphabet in page", all.length());
    if (!ok) ++failures;
  }
  // with LC_RTL off nothing changes at all
  LC_RTL = false;
  {
    String s = shapeText(String("לחות 91%"));
    bool ok = s == "לחות 91%";
    printf("%s %-28s\n", ok ? "ok  " : "FAIL", "LTR locale untouched");
    if (!ok) ++failures;
  }
  printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
