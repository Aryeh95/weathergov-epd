# Desktop check for the date formatter

`src/_strftime.cpp` formats dates from the format strings in config.json
(`time_format`, `date_format` and so on). A format may carry a field width,
and a width such as `%0100Y` used to write past the formatter's 100-byte
scratch buffer. This checks that ordinary formats still come out as before
and that no width, however large, produces more than the cap.

    g++ -std=gnu++17 -fsanitize=address,undefined -I. -I../../platformio/include \
        strftime_test.cpp ../../platformio/src/_strftime.cpp -o strftime_test && ./strftime_test

`_locale.h` here stands in for the project's own, which needs Arduino.h.
`-I.` has to come first so that it is the one found.
