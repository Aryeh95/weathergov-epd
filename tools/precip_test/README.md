# Desktop check for the daily precipitation source picker

`src/precip.cpp` decides, per forecast-row day, whether the amount comes from
weather.gov's gridpoint QPF or from Open-Meteo, and how a 6-hour UTC bucket
that straddles local midnight is split. That is the part worth running off the
device. `precip_test_data.h` is a snapshot of real responses for Pikesville MD
taken 2026-09-04 15:27 UTC (gridpoint QPF, Open-Meteo daily sums, the NWS
12-hour forecast's period start times), with expected results computed by an
independent Python implementation of the same rule.

    g++ -std=gnu++17 -I../../platformio/include -I. \
        precip_test.cpp ../../platformio/src/precip.cpp -o precip_test && ./precip_test

Expected: today through Sunday from NWS (0.21 / 0.04 / 0.00 in — today counts
only what is still ahead of 15:27 UTC), Monday onward from Open-Meteo, the
duration parser's edge cases, the no-QPF fallback and the no-source NaN.
