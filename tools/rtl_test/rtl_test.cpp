/* Desktop check for the right-to-left text shaper (src/rtl.cpp).
 *
 * Feeds UTF-8 Hebrew, mixed and Latin strings through shapeText() and
 * checks the ISO-8859-8 bytes that come out: Hebrew runs reversed, numbers
 * and Latin words in their own order, signs attached to their numbers,
 * brackets mirrored, Latin-1 escapes passed through, Latin-only text
 * untouched. Built with -D LOCALE=pl_PL it checks the other job of
 * shapeText instead: UTF-8 onto the ISO-8859-2 page of a Central European
 * font family, no reordering.
 */
#include <cstdio>
#include <cstdlib>
#include <string>
#include "Arduino.h"
#include "_locale.h"
#include "config.h"
#include "fonts/font_encoding.h"
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
  LC_RTL = false;
#if LOCALE_FONT_ENCODING == FONT_ENC_ISO8859_2
  // a Latin-2 locale: the Polish letters land on the ISO-8859-2 slots the
  // Lato_CE family carries, in order, nothing reversed
  {
    String s = shapeText(String("ąćęłńóśźż ŁŚŻ"));
    static const unsigned char want[] = {0xB1, 0xE6, 0xEA, 0xB3, 0xF1, 0xF3, 0xB6, 0xBC, 0xBF,
                                         0x20, 0xA3, 0xA6, 0xAF};
    bool ok = s.length() == sizeof want;
    for (size_t i = 0; ok && i < sizeof want; ++i)
    {
      ok = static_cast<unsigned char>(s.c_str()[i]) == want[i];
    }
    printf("%s %-28s %s\n", ok ? "ok  " : "FAIL", "Polish letters on the page", hex(s).c_str());
    if (!ok) ++failures;
  }
  // ASCII and the Latin-1 degree escape pass through; typographic dashes
  // and quotes fall back to ASCII
  {
    String s = shapeText(String("Odczuwalna 24\260C \xE2\x80\x93 \xE2\x80\x9E" "5" "\xE2\x80\x9D"));
    bool ok = s == "Odczuwalna 24\260C - \"5\"";
    printf("%s %-28s %s\n", ok ? "ok  " : "FAIL", "ASCII and escapes kept", hex(s).c_str());
    if (!ok) ++failures;
  }
  // a letter with no slot on the page becomes '?' rather than vanishing
  {
    String s = shapeText(String("לחות 91%"));
    bool ok = s == "???? 91%";
    printf("%s %-28s %s\n", ok ? "ok  " : "FAIL", "no slot -> '?'", hex(s).c_str());
    if (!ok) ++failures;
  }
#else
  // with LC_RTL off (a Latin-1 locale) nothing changes at all
  {
    String s = shapeText(String("לחות 91%"));
    bool ok = s == "לחות 91%";
    printf("%s %-28s\n", ok ? "ok  " : "FAIL", "LTR locale untouched");
    if (!ok) ++failures;
  }
#endif
  printf("%d failure(s)\n", failures);
  return failures ? 1 : 0;
}
