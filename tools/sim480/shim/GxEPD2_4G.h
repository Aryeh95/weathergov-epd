/* Stand-in for GxEPD2_4G.h on a PC: its colour names (the four greys, and
 * the red its 3-colour panels call yellow too). */
#ifndef SIM480_GXEPD2_4G_H
#define SIM480_GXEPD2_4G_H
#define GxEPD_BLACK     0x0000
#define GxEPD_DARKGREY  0x7BEF
#define GxEPD_LIGHTGREY 0xC618
#define GxEPD_WHITE     0xFFFF
#define GxEPD_RED       0xF800
#if defined(COLOR_426)
// the 4.26in layout on the E1002's colours
#define GxEPD_YELLOW    0xFFE0
#define GxEPD_GREEN     0x07E0
#define GxEPD_BLUE      0x001F
#define GxEPD_ORANGE    0xFD20
#else
#define GxEPD_YELLOW    GxEPD_RED
#endif
#define GxEPD_COLORED   GxEPD_RED
#endif
