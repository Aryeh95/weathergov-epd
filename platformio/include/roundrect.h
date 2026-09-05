/* The vertical spans that make up a filled rounded rectangle.
 *
 * This is Adafruit_GFX's own fillRoundRect decomposition -- a centre
 * rectangle plus two Bresenham circle helpers -- with the drawing calls
 * replaced by a callback, so a caller that cannot use fillRoundRect can still
 * walk exactly the same pixels.
 *
 * The reason it exists: the Spectra 6 panel has no ink for orange, plum or
 * maroon, so the risk badges in those colors are painted as a two-ink
 * checkerboard, which fillRoundRect cannot do (it takes one color). Painting
 * that checker over a plain rectangle is what gave those badges square
 * corners next to their rounded neighbours. Walking these spans instead keeps
 * the silhouette PIXEL-IDENTICAL to the solid badges rather than
 * approximately similar -- tools/chip_test rasterises both and diffs them.
 *
 * Header-only and display-agnostic on purpose: the firmware passes a lambda
 * that dithers, the host test passes one that records.
 */
#ifndef __ROUNDRECT_H__
#define __ROUNDRECT_H__

#include <cstdint>

/* Emit every vertical span of a filled rounded rectangle.
 *
 * @param x,y   top-left corner
 * @param w,h   size
 * @param r     corner radius (clamped to the minor half-axis, as GFX does)
 * @param emit  called as emit(x, y, height) once per vertical span. Spans may
 *              overlap, exactly as they do inside GFX itself.
 */
template <typename EmitVSpan>
void roundRectSpans(int16_t x, int16_t y, int16_t w, int16_t h, int16_t r,
                    EmitVSpan emit)
{
  const int16_t max_radius = ((w < h) ? w : h) / 2; // 1/2 minor axis
  if (r > max_radius)
  {
    r = max_radius;
  }

  // writeFillRect(x + r, y, w - 2 * r, h), as columns
  for (int16_t xx = x + r; xx < x + w - r; ++xx)
  {
    emit(xx, y, h);
  }

  // fillCircleHelper(x + w - r - 1, y + r, r, 1, h - 2r - 1) and
  // fillCircleHelper(x + r,         y + r, r, 2, h - 2r - 1)
  const int16_t cx[2] = { static_cast<int16_t>(x + w - r - 1),
                          static_cast<int16_t>(x + r) };
  const int16_t cy    = static_cast<int16_t>(y + r);
  const int16_t delta = static_cast<int16_t>(h - 2 * r - 1 + 1); // GFX's delta++
  for (int side = 0; side < 2; ++side)
  {
    const int16_t sgn = (side == 0) ? 1 : -1; // corners 1 = right, 2 = left
    int16_t f = 1 - r, ddF_x = 1, ddF_y = static_cast<int16_t>(-2 * r);
    int16_t xi = 0, yi = r, px = 0, py = r;
    while (xi < yi)
    {
      if (f >= 0)
      {
        yi--;
        ddF_y += 2;
        f += ddF_y;
      }
      xi++;
      ddF_x += 2;
      f += ddF_x;
      // GFX's own guards against drawing a span twice
      if (xi < (yi + 1))
      {
        emit(static_cast<int16_t>(cx[side] + sgn * xi),
             static_cast<int16_t>(cy - yi),
             static_cast<int16_t>(2 * yi + delta));
      }
      if (yi != py)
      {
        emit(static_cast<int16_t>(cx[side] + sgn * py),
             static_cast<int16_t>(cy - px),
             static_cast<int16_t>(2 * px + delta));
        py = yi;
      }
      px = xi;
    }
  }
}

#endif
