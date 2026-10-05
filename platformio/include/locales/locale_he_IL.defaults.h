/* Build defaults of the he_IL locale for esp32-weather-epd.
 * Included by config.h right after LOCALE is set, before the units and
 * fonts are chosen. Anything defined here can still be overridden on the
 * command line (-D) -- each define is guarded.
 */
#pragma once
// Israel: metric units, and the Heebo family for the Hebrew alphabet.
#if !(defined(UNITS_TEMP_KELVIN) || defined(UNITS_TEMP_CELSIUS) || defined(UNITS_TEMP_FAHRENHEIT))
  #define UNITS_TEMP_CELSIUS
#endif
#if !(defined(UNITS_SPEED_METERSPERSECOND) || defined(UNITS_SPEED_KILOMETERSPERHOUR) || defined(UNITS_SPEED_FEETPERSECOND) || defined(UNITS_SPEED_MILESPERHOUR) || defined(UNITS_SPEED_KNOTS) || defined(UNITS_SPEED_BEAUFORT))
  #define UNITS_SPEED_KILOMETERSPERHOUR
#endif
#if !(defined(UNITS_PRES_HECTOPASCALS) || defined(UNITS_PRES_MILLIBARS) || defined(UNITS_PRES_PASCALS) || defined(UNITS_PRES_MILLIMETERSOFMERCURY) || defined(UNITS_PRES_INCHESOFMERCURY) || defined(UNITS_PRES_ATMOSPHERES) || defined(UNITS_PRES_GRAMSPERSQUARECENTIMETER) || defined(UNITS_PRES_POUNDSPERSQUAREINCH))
  #define UNITS_PRES_HECTOPASCALS
#endif
#if !(defined(UNITS_DIST_MILES) || defined(UNITS_DIST_KILOMETERS))
  #define UNITS_DIST_KILOMETERS
#endif
#if !(defined(UNITS_DAILY_PRECIP_POP) || defined(UNITS_DAILY_PRECIP_MILLIMETERS) || defined(UNITS_DAILY_PRECIP_CENTIMETERS) || defined(UNITS_DAILY_PRECIP_INCHES))
  #define UNITS_DAILY_PRECIP_MILLIMETERS
#endif
#ifndef FONT_INCLUDE_Heebo
  #define FONT_INCLUDE_Heebo 1
#endif
