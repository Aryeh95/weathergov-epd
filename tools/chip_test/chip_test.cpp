/* Does the dithered risk badge have the same silhouette as a solid one?
 *
 * The Spectra 6 panel has no orange, plum or maroon ink, so those badges are
 * painted as a two-ink checker rather than with fillRoundRect. Before this
 * test they were painted over a plain rectangle and came out square-cornered
 * beside their rounded neighbours (spotted on the E1002, UV High / AQI USG /
 * pollen 3). The fix walks Adafruit GFX's own fillRoundRect spans instead
 * (include/roundrect.h), and "the same spans" is a claim worth checking
 * rather than asserting.
 *
 * So: rasterise both into a grid and diff them. The reference below is
 * COPIED from Adafruit GFX 1.12.1's fillRoundRect + fillCircleHelper (the
 * version in platformio/.pio/libdeps). If that library is upgraded and its
 * rasterisation changes, this test keeps comparing against the old shape --
 * re-copy the reference then.
 *
 * Build and run:
 *   g++ -std=gnu++17 -I platformio/include tools/chip_test/chip_test.cpp \
 *       -o /tmp/chip_test && /tmp/chip_test
 */
#include "roundrect.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

static const int W = 200, H = 60;

struct Grid
{
  std::vector<uint8_t> px = std::vector<uint8_t>(W * H, 0);
  void set(int16_t x, int16_t y)
  {
    if (x >= 0 && x < W && y >= 0 && y < H) { px[y * W + x] = 1; }
  }
  void vline(int16_t x, int16_t y, int16_t h)
  {
    for (int16_t i = 0; i < h; ++i) { set(x, static_cast<int16_t>(y + i)); }
  }
  void rect(int16_t x, int16_t y, int16_t w, int16_t h)
  {
    for (int16_t j = 0; j < h; ++j)
      for (int16_t i = 0; i < w; ++i) { set(static_cast<int16_t>(x + i), static_cast<int16_t>(y + j)); }
  }
};

// ---- reference: Adafruit_GFX 1.12.1, verbatim apart from the draw calls ----
static void refCircleHelper(Grid &g, int16_t x0, int16_t y0, int16_t r,
                            uint8_t corners, int16_t delta)
{
  int16_t f = 1 - r, ddF_x = 1, ddF_y = -2 * r, x = 0, y = r, px = x, py = y;
  delta++;
  while (x < y)
  {
    if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
    x++; ddF_x += 2; f += ddF_x;
    if (x < (y + 1))
    {
      if (corners & 1) { g.vline(x0 + x, y0 - y, 2 * y + delta); }
      if (corners & 2) { g.vline(x0 - x, y0 - y, 2 * y + delta); }
    }
    if (y != py)
    {
      if (corners & 1) { g.vline(x0 + py, y0 - px, 2 * px + delta); }
      if (corners & 2) { g.vline(x0 - py, y0 - px, 2 * px + delta); }
      py = y;
    }
    px = x;
  }
}

static void refFillRoundRect(Grid &g, int16_t x, int16_t y, int16_t w, int16_t h, int16_t r)
{
  int16_t max_radius = ((w < h) ? w : h) / 2;
  if (r > max_radius) { r = max_radius; }
  g.rect(x + r, y, w - 2 * r, h);
  refCircleHelper(g, x + w - r - 1, y + r, r, 1, h - 2 * r - 1);
  refCircleHelper(g, x + r, y + r, r, 2, h - 2 * r - 1);
}

static int compare(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r)
{
  Grid ref, mine;
  refFillRoundRect(ref, x, y, w, h, r);
  roundRectSpans(x, y, w, h, r,
                 [&](int16_t sx, int16_t sy, int16_t sh) { mine.vline(sx, sy, sh); });
  int diff = 0, filled = 0;
  for (int i = 0; i < W * H; ++i)
  {
    filled += ref.px[i];
    diff += (ref.px[i] != mine.px[i]);
  }
  printf("  %3dx%-3d r=%d  filled=%-5d %s\n", w, h, r, filled,
         diff ? "MISMATCH" : "identical");
  if (diff)
  {
    printf("    %d differing pixels\n", diff);
    for (int yy = y - 1; yy < y + h + 1 && yy < H; ++yy)
    {
      printf("    ");
      for (int xx = x - 1; xx < x + w + 1 && xx < W; ++xx)
      {
        const int a = ref.px[yy * W + xx], b = mine.px[yy * W + xx];
        putchar(a && b ? '#' : a ? 'R' : b ? 'M' : '.');
      }
      putchar('\n');
    }
  }
  return diff;
}

int main()
{
  int fails = 0;
  // The badge as drawn: 16 px tall, radius 3, widths across the real labels
  // ("Low" through "Unhealthy for Sensitive Groups" at 7pt and 5pt).
  printf("risk badge geometry (h=16, r=3):\n");
  for (int16_t w = 12; w <= 150; w += 1) { fails += compare(10, 10, w, 16, 3); }
  printf("edge cases:\n");
  // radius clamped to the minor half-axis, degenerate sizes, r=0
  fails += compare(10, 10, 8, 8, 9);
  fails += compare(10, 10, 5, 16, 3);
  fails += compare(10, 10, 16, 5, 3);
  fails += compare(10, 10, 20, 16, 0);
  fails += compare(10, 10, 1, 1, 3);
  fails += compare(10, 10, 40, 16, 8);
  printf(fails ? "\nFAILURES\n" : "\nALL IDENTICAL\n");
  return fails ? 1 : 0;
}
