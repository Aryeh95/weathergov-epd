# Desktop check for the right-to-left text shaper

`src/rtl.cpp` turns the Hebrew locale's UTF-8 strings into what the panel
font draws: each line in visual order (Hebrew runs reversed, numbers and
Latin words kept in their own order, brackets mirrored) on the Hebrew font
family's ISO-8859-8 page. This runs it on a PC over Hebrew, mixed and
Latin-only strings and checks the bytes.

    g++ -std=gnu++17 -fsanitize=address,undefined -I. -I../../platformio/include \
        rtl_test.cpp ../../platformio/src/rtl.cpp -o rtl_test && ./rtl_test

`Arduino.h` and `_locale.h` here stand in for the project's own; `-I.` has
to come first so that they are the ones found.
