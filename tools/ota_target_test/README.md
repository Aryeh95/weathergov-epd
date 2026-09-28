# Desktop check for the firmware target scanner

Every build carries a line of text naming the build target and the panel it
was built for. When a firmware file is uploaded in the portal,
`include/ota_target.h` looks for that line as the file streams past and the
portal refuses a file built for something else.

The upload arrives in chunks, so the line can be cut anywhere, and the rest
of the file is machine code that may hold part of the prefix by chance. This
feeds the scanner each case in chunks of many sizes, one byte at a time
included.

    g++ -std=gnu++17 -fsanitize=address,undefined -I../../platformio/include \
        ota_target_test.cpp -o ota_target_test && ./ota_target_test

Real firmware files can be checked as well, each followed by the target it
should report:

    ./ota_target_test ../../platformio/.pio/build/firebeetle32/firmware.bin \
        firebeetle32/DISP_BW_V2
