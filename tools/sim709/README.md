# PC preview of the 1600x1200 layout

The GDEB0709E01 takes about half a minute to refresh, and for a long time
there was no panel to refresh at all. This folder builds the firmware's own
drawing code for a PC and saves what it draws as pictures, so the layout can
be looked at and changed without the hardware.

It compiles the same `renderer709.cpp` and `epd709.cpp` that go into the
firmware. Only the panel is replaced (the frame is written to a file
instead of being sent over SPI), along with the few Arduino and ESP32
headers the code mentions (`shim/`).

## Running it

```
pip install pillow
python tools/sim709/build.py [output folder]
```

It needs a C++17 compiler. `clang++` or `g++` on the path is used if there
is one. Otherwise point the `ZIG` environment variable at a Zig executable:
`pip install ziglang` brings one, as `ziglang/zig.exe` inside the package.

## What it writes

| File | What it shows |
|---|---|
| `sim_day.png` | An afternoon, no alerts |
| `sim_night.png` | The same day at 11:40 pm |
| `sim_alert1.png`, `sim_alert2.png`, `sim_alert4.png` | One, two and four alerts |
| `sim_status.png` | A failed request reported in the status line |
| `sim_gaps.png` | Readings that were not fetched, and a forecast two days short |
| `sim_error.png`, `sim_portal.png` | The error and setup screens |
| `sim_testcard.png` | The test card of the `_testcard` build |

Each comes twice. The plain file has the dots exactly as the panel is told
to set them, in the inks' measured colours. The `_blended` file averages
them 2x2, which is closer to what the eye makes of 282 dots per inch from
across a desk.

## What it cannot tell you

Whether single dots hold their shape on real ink, how the inks look beside
each other, and whether the panel lights at all. That is what the test card
is for.

The sample weather is in `sim_main.cpp`. A few helper functions of
`display_utils.cpp` and `history.cpp` are repeated there in simplified form,
because those files need the ESP32's own headers.
