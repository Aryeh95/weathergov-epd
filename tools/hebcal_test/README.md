# Desktop check for the Hebrew calendar

`src/hebcal.cpp` gives `date_format` its `%K` and `%J` (the Hebrew calendar
date, with and without the year). This checks Rosh Hashanah against the
published dates for several years, a leap year's two Adars, Pesach, that
consecutive days stay consecutive across a year boundary, the gematria
numerals (ט"ו, ט"ז, geresh and gershayim) and the turnover at sunset.

    g++ -std=gnu++17 -fsanitize=address,undefined -I. -I../../platformio/include \
        hebcal_test.cpp ../../platformio/src/hebcal.cpp -o hebcal_test && ./hebcal_test

`_locale.h` here stands in for the project's own; `-I.` has to come first.
