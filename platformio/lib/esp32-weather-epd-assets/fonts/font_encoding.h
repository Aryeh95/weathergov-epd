/* The code page a font family's 0xA0-0xFF slots carry (fontconvert.py
 * --encoding, recorded in <Family>/ENCODING): the renderer's shapeText()
 * transcodes the locale's UTF-8 strings to the page the locale needs
 * (LOCALE_FONT_ENCODING, from locales/locale_<code>.defaults.h), and
 * settings.cpp makes sure a family that carries it is in use. The numbers
 * are also what the defaults files spell out, so keep them in step.
 */
#pragma once
#include <stdint.h>

#define FONT_ENC_LATIN1     0   // ISO-8859-1: every family upstream ships
#define FONT_ENC_ISO8859_8  1   // Hebrew letters at 0xE0-0xFA (Heebo)
#define FONT_ENC_ISO8859_2  2   // Central European letters (Lato_CE)
