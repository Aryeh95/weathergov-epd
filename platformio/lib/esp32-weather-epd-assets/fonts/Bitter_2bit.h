// Bitter in 2 bits (tools/fontconvert2.py): the faces and the
// signature of each one's 1-bit twin, for renderer.cpp
#pragma once

#if defined(FONT_TABLE_HAS_4pt8b) && FONT_TABLE_HAS_4pt8b
#include "Bitter_2bit/Bitter_4pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_5pt8b) && FONT_TABLE_HAS_5pt8b
#include "Bitter_2bit/Bitter_5pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_6pt8b) && FONT_TABLE_HAS_6pt8b
#include "Bitter_2bit/Bitter_6pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_7pt8b) && FONT_TABLE_HAS_7pt8b
#include "Bitter_2bit/Bitter_7pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_8pt8b) && FONT_TABLE_HAS_8pt8b
#include "Bitter_2bit/Bitter_8pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_9pt8b) && FONT_TABLE_HAS_9pt8b
#include "Bitter_2bit/Bitter_9pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_10pt8b) && FONT_TABLE_HAS_10pt8b
#include "Bitter_2bit/Bitter_10pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_11pt8b) && FONT_TABLE_HAS_11pt8b
#include "Bitter_2bit/Bitter_11pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_12pt8b) && FONT_TABLE_HAS_12pt8b
#include "Bitter_2bit/Bitter_12pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_14pt8b) && FONT_TABLE_HAS_14pt8b
#include "Bitter_2bit/Bitter_14pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_16pt8b) && FONT_TABLE_HAS_16pt8b
#include "Bitter_2bit/Bitter_16pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_18pt8b) && FONT_TABLE_HAS_18pt8b
#include "Bitter_2bit/Bitter_18pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_20pt8b) && FONT_TABLE_HAS_20pt8b
#include "Bitter_2bit/Bitter_20pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_22pt8b) && FONT_TABLE_HAS_22pt8b
#include "Bitter_2bit/Bitter_22pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_24pt8b) && FONT_TABLE_HAS_24pt8b
#include "Bitter_2bit/Bitter_24pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_26pt8b) && FONT_TABLE_HAS_26pt8b
#include "Bitter_2bit/Bitter_26pt8b_2bit.h"
#endif
#if defined(FONT_TABLE_HAS_48pt8b_temperature) && FONT_TABLE_HAS_48pt8b_temperature
#include "Bitter_2bit/Bitter_48pt8b_temperature_2bit.h"
#endif

struct Font2Face
{
  const GFXfont *grey;
  const int16_t *sig;
};

static const Font2Face FONT2[] = {
#if defined(FONT_TABLE_HAS_4pt8b) && FONT_TABLE_HAS_4pt8b
  {&Bitter_4pt8b_2bit, Bitter_4pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_5pt8b) && FONT_TABLE_HAS_5pt8b
  {&Bitter_5pt8b_2bit, Bitter_5pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_6pt8b) && FONT_TABLE_HAS_6pt8b
  {&Bitter_6pt8b_2bit, Bitter_6pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_7pt8b) && FONT_TABLE_HAS_7pt8b
  {&Bitter_7pt8b_2bit, Bitter_7pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_8pt8b) && FONT_TABLE_HAS_8pt8b
  {&Bitter_8pt8b_2bit, Bitter_8pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_9pt8b) && FONT_TABLE_HAS_9pt8b
  {&Bitter_9pt8b_2bit, Bitter_9pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_10pt8b) && FONT_TABLE_HAS_10pt8b
  {&Bitter_10pt8b_2bit, Bitter_10pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_11pt8b) && FONT_TABLE_HAS_11pt8b
  {&Bitter_11pt8b_2bit, Bitter_11pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_12pt8b) && FONT_TABLE_HAS_12pt8b
  {&Bitter_12pt8b_2bit, Bitter_12pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_14pt8b) && FONT_TABLE_HAS_14pt8b
  {&Bitter_14pt8b_2bit, Bitter_14pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_16pt8b) && FONT_TABLE_HAS_16pt8b
  {&Bitter_16pt8b_2bit, Bitter_16pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_18pt8b) && FONT_TABLE_HAS_18pt8b
  {&Bitter_18pt8b_2bit, Bitter_18pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_20pt8b) && FONT_TABLE_HAS_20pt8b
  {&Bitter_20pt8b_2bit, Bitter_20pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_22pt8b) && FONT_TABLE_HAS_22pt8b
  {&Bitter_22pt8b_2bit, Bitter_22pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_24pt8b) && FONT_TABLE_HAS_24pt8b
  {&Bitter_24pt8b_2bit, Bitter_24pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_26pt8b) && FONT_TABLE_HAS_26pt8b
  {&Bitter_26pt8b_2bit, Bitter_26pt8b_2bitSig},
#endif
#if defined(FONT_TABLE_HAS_48pt8b_temperature) && FONT_TABLE_HAS_48pt8b_temperature
  {&Bitter_48pt8b_temperature_2bit, Bitter_48pt8b_temperature_2bitSig},
#endif
  {nullptr, nullptr},
};
