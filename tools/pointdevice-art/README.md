# Pointdevice menu art

The Pointdevice menus (mode choice, resume prompt, hit and pause menus) are
drawn from one sprite atlas per game, embedded in `src/PointDeviceArt.hpp`.
The atlas is drawn in a browser so it can use the same fonts and effects as
the design mockups, then turned into a C++ header.

To change the art or the wording:

1. Edit `gen.html` (one function per game: `th06()` and `th07()`).
2. Run `python3 server.py 8790` in this folder and open
   `http://127.0.0.1:8790/gen.html` in a desktop browser (it needs internet
   access for Google Fonts). The page writes `out/th06_menu.{png,json}` and
   `out/th07_menu.{png,json}`.
3. From this folder, regenerate the header for this game:
   - th06: `python3 make_header.py out/th06_menu ../../src/PointDeviceArt.hpp`
   - th07: `python3 make_header.py out/th07_menu ../../src/PointDeviceArt.hpp`

Sprites are drawn at 2x the game's 640x480 coordinates. Numbers (stage,
score, retry count) are built at run time from digit sprites.

Fonts, all under the SIL Open Font License, from Google Fonts: Nunito,
M PLUS 1p, Shippori Mincho B1 and Libre Baskerville.
