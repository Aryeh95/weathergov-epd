/* S6 register LUT decoder, fast-full builder, and safety lint for GDEB0709E01.
 *
 * Codec, charge model, lint gate, and fast709 recipe follow Jonas Niesner's
 * "E-paper LUT research" (https://github.com/jonasniesner/epaper-lut-research;
 * code MIT License, docs CC BY 4.0).
 */

#ifndef EPD709_LUT_STANDALONE
#include "config.h"
#endif

#if defined(DISP_7C_709)

#include "epd709_lut.h"

#include <math.h>
#include <string.h>

namespace Epd709Lut
{

namespace
{

// Level bitmask: bit 0 = S, bit 1 = W, bit 2 = K (7 = SWK float).
enum : uint8_t
{
  LV_0   = 0,
  LV_S   = 1 << 0,
  LV_W   = 1 << 1,
  LV_SW  = LV_S | LV_W,
  LV_K   = 1 << 2,
  LV_SK  = LV_S | LV_K,
  LV_WK  = LV_W | LV_K,
  LV_SWK = LV_S | LV_W | LV_K
};

const uint8_t ACTIVE_SLOTS[6] = {0, 1, 2, 3, 5, 6};

struct BitWeight { uint8_t bit; uint8_t weight; };
const BitWeight BIT_W[8] = {
  {4, 1}, {7, 2}, {6, 4}, {1, 8}, {0, 16}, {3, 32}, {2, 64}, {5, 128}
};

struct LevelBitSpec { uint8_t byte; uint8_t bit; };
// Per phase A1..B3: (S, W, K) bit locations inside an 11-byte row record.
const LevelBitSpec LEVEL_BITS[6][3] = {
  {{0, 2}, {0, 5}, {0, 3}}, // A1
  {{0, 1}, {0, 0}, {0, 6}}, // A2
  {{0, 4}, {0, 7}, {4, 5}}, // A3
  {{1, 2}, {1, 5}, {1, 3}}, // B1
  {{1, 1}, {1, 0}, {1, 6}}, // B2
  {{1, 4}, {1, 7}, {8, 5}}, // B3
};
const uint8_t TP_BYTE[6] = {2, 3, 4, 6, 7, 8};

struct Phase
{
  uint8_t lv;
  uint8_t tp;
};

struct Row
{
  uint8_t M;
  uint8_t RA;
  uint8_t RB;
  Phase ph[6];

  uint32_t frames() const
  {
    uint32_t a = static_cast<uint32_t>(ph[0].tp) + ph[1].tp + ph[2].tp;
    uint32_t b = static_cast<uint32_t>(ph[3].tp) + ph[4].tp + ph[5].tp;
    return static_cast<uint32_t>(M) * (RA * a + RB * b);
  }
};

struct Block
{
  Row rows[8];
  uint8_t mIn;
  uint8_t tailByte;

  uint32_t frames() const
  {
    uint32_t sum = 0;
    for (int i = 0; i < 8; ++i) sum += rows[i].frames();
    return sum;
  }
};

struct Table
{
  uint8_t header[8];
  Block lutc;
  Block slots[8];
};

// Keep large working buffers off the ESP32 8 KB loopTask stack.
static Table s_tbl;
static Table s_lintTbl;

// Python's round() uses round-half-to-even; match it explicitly so boundary
// cases on charge folding and schedule splits never drift.
int pyRound(double x)
{
  double fl = floor(x);
  double frac = x - fl;
  long long i = llround(fl);
  if (frac > 0.5 || (frac == 0.5 && (i & 1)))
  {
    ++i;
  }
  return static_cast<int>(i);
}

uint8_t val(uint8_t x, bool skip5 = false)
{
  uint8_t v = static_cast<uint8_t>(~x);
  uint8_t out = 0;
  for (int i = 0; i < 8; ++i)
  {
    if (skip5 && BIT_W[i].bit == 5) continue;
    if ((v >> BIT_W[i].bit) & 1) out |= BIT_W[i].weight;
  }
  return out;
}

bool enc(uint32_t v, uint8_t &out, bool skip5 = false)
{
  if (v > (skip5 ? 127U : 255U)) return false;
  uint8_t x = 0xFF;
  for (int i = 0; i < 8; ++i)
  {
    if (skip5 && BIT_W[i].bit == 5) continue;
    if (v & BIT_W[i].weight) x &= static_cast<uint8_t>(~(1U << BIT_W[i].bit));
  }
  out = x;
  return true;
}

Row emptyRow()
{
  Row r;
  memset(&r, 0, sizeof(r));
  return r;
}

void decodeBlock(const uint8_t *e, size_t off, Block &blk)
{
  blk.mIn = val(e[off - 1]);
  blk.tailByte = e[off + 87];
  for (int ri = 0; ri < 8; ++ri)
  {
    const uint8_t *b = e + off + 11 * ri;
    Row &row = blk.rows[ri];
    row.M = val(e[off + 11 * ri - 1]);
    row.RA = val(b[5]);
    row.RB = val(b[9]);
    for (int pi = 0; pi < 6; ++pi)
    {
      uint8_t lv = 0;
      for (int li = 0; li < 3; ++li)
      {
        const LevelBitSpec &spec = LEVEL_BITS[pi][li];
        if (((b[spec.byte] >> spec.bit) & 1) == 0)
        {
          lv |= static_cast<uint8_t>(1U << li);
        }
      }
      const uint8_t tb = TP_BYTE[pi];
      row.ph[pi].lv = lv;
      row.ph[pi].tp = val(b[tb], tb == 4 || tb == 8);
    }
  }
}

void decodeTable(const uint8_t e[TABLE_LEN], Table &t)
{
  memcpy(t.header, e, 8);
  decodeBlock(e, 8, t.lutc);
  for (int k = 0; k < 8; ++k)
  {
    decodeBlock(e, 96 + 88 * k, t.slots[k]);
  }
}

bool encodeRow(const Row &row, uint8_t nextMByte, uint8_t out[11])
{
  memset(out, 0xFF, 11);
  for (int pi = 0; pi < 6; ++pi)
  {
    for (int li = 0; li < 3; ++li)
    {
      if ((row.ph[pi].lv >> li) & 1)
      {
        const LevelBitSpec &spec = LEVEL_BITS[pi][li];
        out[spec.byte] &= static_cast<uint8_t>(~(1U << spec.bit));
      }
    }
  }
  for (int pi = 0; pi < 6; ++pi)
  {
    const uint8_t tb = TP_BYTE[pi];
    const bool sk = (tb == 4 || tb == 8);
    uint8_t x = 0xFF;
    if (!enc(row.ph[pi].tp, x, sk)) return false;
    if (sk)
    {
      x = static_cast<uint8_t>((x & 0xDF) | (out[tb] & 0x20));
    }
    out[tb] = x;
  }
  if (!enc(row.RA, out[5]) || !enc(row.RB, out[9])) return false;
  out[10] = nextMByte;
  return true;
}

bool encodeBlock(const Block &blk, uint8_t out[88])
{
  for (int ri = 0; ri < 8; ++ri)
  {
    uint8_t nxt = blk.tailByte;
    if (ri < 7)
    {
      if (!enc(blk.rows[ri + 1].M, nxt)) return false;
    }
    if (!encodeRow(blk.rows[ri], nxt, out + 11 * ri)) return false;
  }
  return true;
}

bool finalizeTable(Table &t, uint8_t lastByte, uint8_t out[TABLE_LEN])
{
  if (!enc(t.lutc.rows[0].M, t.header[7])) return false;
  Block *blocks[9] = {
    &t.lutc, &t.slots[0], &t.slots[1], &t.slots[2], &t.slots[3],
    &t.slots[4], &t.slots[5], &t.slots[6], &t.slots[7]
  };
  for (int i = 0; i < 9; ++i)
  {
    blocks[i]->mIn = blocks[i]->rows[0].M;
    if (i < 8)
    {
      if (!enc(blocks[i + 1]->rows[0].M, blocks[i]->tailByte)) return false;
    }
    else
    {
      blocks[i]->tailByte = lastByte;
    }
  }
  memcpy(out, t.header, 8);
  if (!encodeBlock(t.lutc, out + 8)) return false;
  for (int k = 0; k < 8; ++k)
  {
    if (!encodeBlock(t.slots[k], out + 96 + 88 * k)) return false;
  }
  return true;
}

void computeLevelVolts(const uint8_t pwr[6], double V[8])
{
  // PWR[2..5] = (VSH_LV, VSL_LV, VSH_LV2, VSL_LV2)
  const double vshLv  = 3.0 + 0.1 * pwr[2];
  const double vslLv  = -(3.0 + 0.1 * pwr[3]);
  const double vshLv2 = 3.0 + 0.1 * pwr[4];
  const double vslLv2 = -(3.04 + 0.0992 * pwr[5]);
  V[LV_0]   = 0.0;
  V[LV_S]   = -15.0;
  V[LV_W]   = vslLv;
  V[LV_SW]  = vslLv2;
  V[LV_K]   = 15.0;
  V[LV_SK]  = vshLv;
  V[LV_WK]  = vshLv2;
  V[LV_SWK] = 0.0;
}

double rowQ(const Row &row, const double V[8])
{
  if (!row.M) return 0.0;
  double aSum = 0.0, bSum = 0.0;
  for (int i = 0; i < 3; ++i)
  {
    if (row.ph[i].tp) aSum += V[row.ph[i].lv & 7] * row.ph[i].tp;
    if (row.ph[i + 3].tp) bSum += V[row.ph[i + 3].lv & 7] * row.ph[i + 3].tp;
  }
  double totalV = static_cast<double>(row.M) * (row.RA * aSum + row.RB * bSum);
  return totalV / V[LV_S];
}

struct RowList
{
  Row r[12];
  uint8_t n = 0;

  bool append(const Row &row)
  {
    if (n >= 12) return false;
    r[n++] = row;
    return true;
  }

  uint32_t frames() const
  {
    uint32_t s = 0;
    for (uint8_t i = 0; i < n; ++i) s += r[i].frames();
    return s;
  }
};

static RowList s_openings[6];
static RowList s_rests[6];
static RowList s_preTracks[6];
static RowList s_tmpList;

bool packSinglePulse(uint8_t lv, uint32_t frames, RowList &out)
{
  while (frames > 0)
  {
    Row r = emptyRow();
    r.M = 1;
    if (frames <= 127)
    {
      r.RA = 1;
      r.ph[0].lv = lv;
      r.ph[0].tp = static_cast<uint8_t>(frames);
      frames = 0;
    }
    else
    {
      uint32_t R = frames / 127;
      if (R > 255) R = 255;
      r.RA = static_cast<uint8_t>(R);
      r.ph[0].lv = lv;
      r.ph[0].tp = 127;
      frames -= 127 * R;
    }
    if (!out.append(r)) return false;
  }
  return true;
}

bool appendPadRows(uint32_t n, RowList &out)
{
  while (n > 0)
  {
    uint32_t a = (n > 250) ? 250 : n;
    Row r = emptyRow();
    r.M = 1;
    r.RA = 1;
    r.ph[0].lv = LV_0;
    r.ph[0].tp = static_cast<uint8_t>(a);
    if (!out.append(r)) return false;
    n -= a;
  }
  return true;
}

bool foldBalance(const Row &row0, const RowList &rest, const double V[8],
                 RowList &openingOut)
{
  openingOut.n = 0;
  Row r = row0;
  double q = rowQ(r, V);
  for (uint8_t i = 0; i < rest.n; ++i) q += rowQ(rest.r[i], V);

  if (fabs(q) <= 0.5)
  {
    return openingOut.append(r);
  }
  const uint8_t lv = (q > 0.0) ? LV_K : LV_S;
  const int n = pyRound(fabs(q));
  if (r.M == 1 && r.RA == 1)
  {
    Phase &p0 = r.ph[0];
    if (p0.lv == lv && static_cast<int>(p0.tp) + n <= 255)
    {
      p0.tp = static_cast<uint8_t>(p0.tp + n);
      return openingOut.append(r);
    }
    if ((p0.lv == LV_S || p0.lv == LV_K) && p0.lv != lv &&
        static_cast<int>(p0.tp) > n)
    {
      p0.tp = static_cast<uint8_t>(p0.tp - n);
      return openingOut.append(r);
    }
  }
  if (!packSinglePulse(lv, static_cast<uint32_t>(n), openingOut)) return false;
  return openingOut.append(r);
}

bool setBlockLiveRows(Block &blk, const RowList &rows)
{
  uint8_t count = 0;
  for (uint8_t i = 0; i < rows.n; ++i)
  {
    if (rows.r[i].M && rows.r[i].frames())
    {
      if (count >= 8) return false;
      blk.rows[count++] = rows.r[i];
    }
  }
  while (count < 8)
  {
    blk.rows[count++] = emptyRow();
  }
  return true;
}

} // namespace

int8_t setForTempC(float tempC, bool allowWarm)
{
  if (isnan(tempC)) return -1;
  if (tempC >= 17.0f && tempC < 21.0f) return 3;
  if (tempC >= 21.0f && tempC < 25.5f) return 4;
  if (allowWarm && tempC >= 25.5f && tempC <= 30.0f) return 5;
  return -1;
}

bool framesMatchStock(uint16_t frames, uint32_t stockMs)
{
  if (frames == 0 || stockMs == 0) return false;
  const float expected = frames * 20.35f;
  return fabsf(stockMs - expected) <= 0.03f * expected;
}

bool validateStockSet(const uint8_t in[TABLE_LEN], uint16_t *framesOut)
{
  if (!in || in[0] != 0xFF || val(in[7]) != 1) return false;
  for (int i = 1; i <= 4; ++i)
  {
    uint8_t c = val(in[i]);
    if (c < 5 || c > 90) return false;
  }
  decodeTable(in, s_tbl);
  const uint32_t lf = s_tbl.lutc.frames();
  if (lf == 0 || lf > 5000) return false;
  for (int i = 0; i < 6; ++i)
  {
    if (s_tbl.slots[ACTIVE_SLOTS[i]].frames() != lf) return false;
  }
  if (framesOut) *framesOut = static_cast<uint16_t>(lf);
  return true;
}

bool buildFast709(const uint8_t in[TABLE_LEN], uint8_t out[TABLE_LEN],
                  uint8_t pwrOut[6], bool g13, BuildInfo *info)
{
  uint16_t stockF = 0;
  if (!out || !pwrOut || !validateStockSet(in, &stockF)) return false;

  pwrOut[0] = 0x0F;
  pwrOut[1] = 0x00;
  for (int i = 0; i < 4; ++i) pwrOut[2 + i] = val(in[1 + i]);

  double V[8];
  computeLevelVolts(pwrOut, V);

  decodeTable(in, s_tbl);

  const uint8_t colourFrom = g13 ? 4 : 3;
  uint32_t olen = 0;

  for (int i = 0; i < 6; ++i)
  {
    s_openings[i].n = 0;
    s_rests[i].n = 0;
    s_preTracks[i].n = 0;
    const uint8_t k = ACTIVE_SLOTS[i];
    for (int ri = colourFrom; ri < 8; ++ri)
    {
      if (s_tbl.slots[k].rows[ri].M && s_tbl.slots[k].rows[ri].frames())
      {
        if (!s_rests[i].append(s_tbl.slots[k].rows[ri])) return false;
      }
    }
    if (!foldBalance(s_tbl.slots[k].rows[0], s_rests[i], V, s_openings[i]))
    {
      return false;
    }
    uint32_t of = s_openings[i].frames();
    if (of > olen) olen = of;
  }

  uint32_t L = 0;
  for (int i = 0; i < 6; ++i)
  {
    uint32_t of = s_openings[i].frames();
    if (!appendPadRows(olen - of, s_preTracks[i])) return false;
    for (uint8_t j = 0; j < s_openings[i].n; ++j)
    {
      if (!s_preTracks[i].append(s_openings[i].r[j])) return false;
    }
    for (uint8_t j = 0; j < s_rests[i].n; ++j)
    {
      if (!s_preTracks[i].append(s_rests[i].r[j])) return false;
    }
    uint32_t tf = s_preTracks[i].frames();
    if (tf > L) L = tf;
  }

  // Clear all 8 slots, then populate the 6 active right-aligned tracks.
  s_tmpList.n = 0;
  for (int k = 0; k < 8; ++k) setBlockLiveRows(s_tbl.slots[k], s_tmpList);

  for (int i = 0; i < 6; ++i)
  {
    const uint8_t k = ACTIVE_SLOTS[i];
    s_tmpList.n = 0;
    if (!appendPadRows(L - s_preTracks[i].frames(), s_tmpList)) return false;
    for (uint8_t j = 0; j < s_preTracks[i].n; ++j)
    {
      if (!s_tmpList.append(s_preTracks[i].r[j])) return false;
    }
    if (!setBlockLiveRows(s_tbl.slots[k], s_tmpList)) return false;
  }

  // Build LUTC frame-rate schedule at PLL 08 (opening=7, then 3->2 or 2).
  struct SchedStep { uint32_t frames; uint8_t rate; };
  SchedStep sched[3];
  int nSched = 0;
  if (g13)
  {
    uint32_t a = static_cast<uint32_t>(pyRound((L - olen) * 0.5));
    sched[nSched++] = {olen, 7};
    sched[nSched++] = {a, 3};
    sched[nSched++] = {L - olen - a, 2};
  }
  else
  {
    sched[nSched++] = {olen, 7};
    sched[nSched++] = {L - olen, 2};
  }

  s_tmpList.n = 0;
  for (int s = 0; s < nSched; ++s)
  {
    const uint32_t n = sched[s].frames;
    const uint8_t rt = sched[s].rate;
    const uint8_t lvA = static_cast<uint8_t>(((rt & 1) ? LV_S : 0) |
                                             ((rt & 2) ? LV_W : 0));
    const uint8_t lvB = static_cast<uint8_t>((rt & 4) ? LV_S : 0);
    const uint32_t q = n / 100;
    const uint32_t r = n % 100;
    if (q > 255) return false;
    if (q > 0)
    {
      Row row = emptyRow();
      row.M = 1;
      row.RA = static_cast<uint8_t>(q);
      row.ph[0] = {LV_0, 100};
      row.ph[2] = {lvA, 0};
      row.ph[5] = {lvB, 0};
      if (!s_tmpList.append(row)) return false;
    }
    if (r > 0)
    {
      Row row = emptyRow();
      row.M = 1;
      row.RA = 1;
      row.ph[0] = {LV_0, static_cast<uint8_t>(r)};
      row.ph[2] = {lvA, 0};
      row.ph[5] = {lvB, 0};
      if (!s_tmpList.append(row)) return false;
    }
  }
  if (!setBlockLiveRows(s_tbl.lutc, s_tmpList)) return false;
  if (!finalizeTable(s_tbl, in[TABLE_LEN - 1], out)) return false;

  float maxAbsQ = 0.0f;
  if (!lintCustom(out, pwrOut, &maxAbsQ)) return false;

  if (info)
  {
    info->stockFrames = stockF;
    info->fastFrames = static_cast<uint16_t>(L);
    info->maxAbsQ = maxAbsQ;
  }
  return true;
}

bool lintCustom(const uint8_t lut[TABLE_LEN], const uint8_t pwr[6],
                float *maxAbsQOut)
{
  if (!lut || !pwr) return false;

  // Hang guards (format-s6.md 12): never all-zero header or 8 consecutive 00s.
  bool hdrNonZero = false;
  for (int i = 0; i < 8; ++i) hdrNonZero |= (lut[i] != 0);
  if (!hdrNonZero) return false;

  int zeroRun = 0;
  for (size_t i = 0; i < TABLE_LEN; ++i)
  {
    zeroRun = (lut[i] == 0) ? (zeroRun + 1) : 0;
    if (zeroRun >= 8) return false;
  }

  // LUTC must not clear VCOM lo/hi swing bits (all bits except rate stay 1).
  for (int r = 0; r < 8; ++r)
  {
    size_t o = 8 + 11 * r;
    if ((lut[o] | 0x90) != 0xFF || (lut[o + 1] | 0x10) != 0xFF) return false;
  }

  decodeTable(lut, s_lintTbl);
  const uint32_t lf = s_lintTbl.lutc.frames();
  if (lf == 0 || lf > 5000) return false;

  double V[8];
  computeLevelVolts(pwr, V);

  float maxAbsQ = 0.0f;
  for (int i = 0; i < 6; ++i)
  {
    const Block &blk = s_lintTbl.slots[ACTIVE_SLOTS[i]];
    if (blk.frames() != lf) return false;
    double q = 0.0;
    for (int ri = 0; ri < 8; ++ri) q += rowQ(blk.rows[ri], V);
    float aq = static_cast<float>(fabs(q));
    if (aq > 1.0f + 1e-4f) return false;
    if (aq > maxAbsQ) maxAbsQ = aq;
  }
  if (maxAbsQOut) *maxAbsQOut = maxAbsQ;
  return true;
}

} // namespace Epd709Lut

#endif
