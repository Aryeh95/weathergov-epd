// Desktop check for the firmware target scanner. See README.md.
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "ota_target.h"

static const char OWN[] = "WGEPD-TARGET:board_a/DISP_BW_V2;";
static const size_t PREFIX = 13;

static int failures = 0;

// Feeds `data` to a scanner `chunk` bytes at a time.
static TargetScanner scan(const std::string &data, size_t chunk)
{
  TargetScanner s(OWN, PREFIX);
  for (size_t at = 0; at < data.size(); at += chunk)
  {
    const size_t n = (data.size() - at < chunk) ? data.size() - at : chunk;
    s.feed(reinterpret_cast<const uint8_t *>(data.data()) + at, n);
  }
  return s;
}

static void check(const char *what, const std::string &data, bool found,
                  const char *target, bool matches)
{
  // every chunk size, so that the line is split at every possible place
  static const size_t CHUNKS[] = {1, 2, 3, 5, 7, 13, 16, 64, 1436, 4096};
  for (size_t chunk : CHUNKS)
  {
    const TargetScanner s = scan(data, chunk);
    const bool ok = s.found() == found
                 && (!found || strcmp(s.target(), target) == 0)
                 && s.matches() == matches;
    if (!ok)
    {
      ++failures;
      printf("FAIL  %s (chunks of %u): found=%d target='%s' matches=%d\n",
             what, static_cast<unsigned>(chunk), s.found(), s.target(),
             s.matches());
      return;
    }
  }
  printf("ok    %s\n", what);
}

int main(int argc, char **argv)
{
  // binary filler with every byte value, as machine code would have
  std::string noise;
  for (int i = 0; i < 3000; ++i)
  {
    noise += static_cast<char>((i * 37 + 11) & 0xFF);
  }

  check("own line", noise + OWN + noise, true, "board_a/DISP_BW_V2", true);
  check("line at the very start", std::string(OWN) + noise, true,
        "board_a/DISP_BW_V2", true);
  check("line at the very end", noise + OWN, true, "board_a/DISP_BW_V2",
        true);
  check("another board", noise + "WGEPD-TARGET:board_b/DISP_BW_V2;" + noise,
        true, "board_b/DISP_BW_V2", false);
  check("another panel", noise + "WGEPD-TARGET:board_a/DISP_3C_B;" + noise,
        true, "board_a/DISP_3C_B", false);
  check("a longer name that starts the same",
        noise + "WGEPD-TARGET:board_a/DISP_BW_V2x;" + noise, true,
        "board_a/DISP_BW_V2x", false);
  check("no line", noise + noise, false, "", false);
  check("empty file", "", false, "", false);
  check("prefix only, file ends", noise + "WGEPD-TARGET:board_a", false, "",
        false);
  check("prefix restarts inside a failed match",
        noise + "WGEPD-WGEPD-TARGET:board_a/DISP_BW_V2;", true,
        "board_a/DISP_BW_V2", true);
  check("prefix restarts on its last character",
        noise + "WGEPD-TARGETWGEPD-TARGET:board_a/DISP_BW_V2;", true,
        "board_a/DISP_BW_V2", true);
  check("a chance match in binary, then the real line",
        noise + "WGEPD-TARGET:\x01\x02" + noise + OWN, true,
        "board_a/DISP_BW_V2", true);
  check("a chance match runs straight into the real line",
        std::string("WGEPD-TARGET:ab\x01") + OWN, true, "board_a/DISP_BW_V2",
        true);
  check("a chance match broken by the first letter of the real line",
        std::string("WGEPD-TARGET:ab\xC3") + "W" + (OWN + 1), true,
        "board_a/DISP_BW_V2", true);
  check("text that never ends",
        noise + "WGEPD-TARGET:" + std::string(500, 'a') + noise, false, "",
        false);
  check("the first line is the one that counts",
        noise + "WGEPD-TARGET:board_b/DISP_BW_V2;" + noise + OWN, true,
        "board_b/DISP_BW_V2", false);

  // real firmware files: <file> <expected target>
  for (int i = 1; i + 1 < argc; i += 2)
  {
    FILE *f = fopen(argv[i], "rb");
    if (f == nullptr)
    {
      ++failures;
      printf("FAIL  cannot open %s\n", argv[i]);
      continue;
    }
    std::string data;
    std::vector<char> buf(65536);
    size_t n;
    while ((n = fread(buf.data(), 1, buf.size(), f)) > 0)
    {
      data.append(buf.data(), n);
    }
    fclose(f);
    const std::string what = std::string(argv[i]) + " ("
                           + std::to_string(data.size()) + " bytes)";
    check(what.c_str(), data, true, argv[i + 1], false);
  }

  if (failures)
  {
    printf("%d FAILED\n", failures);
    return 1;
  }
  printf("all ok\n");
  return 0;
}
