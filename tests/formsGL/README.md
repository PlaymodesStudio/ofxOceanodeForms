# Forms GL Box tests (the renderer in C++)

All headless; no audio, no app. `S` = a scratch folder, `D` = the app's data folder
(`apps/Playmodes/OceanodeScroller/bin/data/Forms`), `G` = `$D/generators.json`.

Tests to run:
- `render_requests.cpp`: draws request files to PNG with the renderer:
  `clang++ -std=c++17 -O2 -I src/formsGL -I <OF>/libs/json/include tests/formsGL/render_requests.cpp -lz`
  `./a.out $G out req/*.json` (add `-DWITH_FONT -I <freetype2> -lfreetype` and `FORMSGL_FONT` for letters).
- `compare_renders.py a b [--strict]`: two folders of renders: max / mean difference, column and
  row profile correlation (what the sound reads), lit-pixel overlap, energy (a regression check
  between two versions of the renderer).
- `test_live_edit.cpp $D`: audition / immediate mode patching (formsLiveEdit.h).
- `test_thumbnails.cpp $D <sheet.ppm>`: Make thumbnails headless, every instrument.
- `test_gpu.cpp <scale> <requests>`: the GPU noise mask against the CPU.
- `make_schemas.py $G`: regenerates `src/formsGL/formsGLSchemas.h` after a generator's parameters change.

How the port was verified against the Processing sketch (2026-11-06/07; Processing is no longer
used, the tools for this are in git history before the commit that removed it):
1. Random streams, bit for bit: a small Processing sketch (`rngcheck.pde`) wrote its streams and
   `test_random.cpp rng_java.txt` compared them: `ALL EXACT` (java.util.Random, random /
   randomGaussian / noise / noiseDetail, PRNG.pde).
2. Renders: the same requests drawn by the sketch (offline mode) and by `render_requests.cpp`,
   compared with `compare_renders.py`.
3. Coverage (`coverage.cpp`): every one of 800 recent real requests and all 60 scenes is drawn by
   the renderer.

Results (2026-11-06; `--strict` passes at a max difference of 2 levels): sweep 10/10 pixel-identical
(max 0); hits 23/25 within 2 levels (most exactly 0), 2 off on a few pixels; real pulse requests
21/40 within 2 levels, the rest within 3-6 levels on a few pixels; real requests with processes
36/45 within 2 levels, the rest within 5. Masses (24 requests incl. all real ones) and
shapes (28 incl. every kind) are close: >= 98% of pixels within 2 levels, energy within ~1% (letters
excepted: they need the font, which only the app loads). Coverage: all 800 recent real requests and
all 60 scenes of the library are drawn here.

More checks (2026-11-07):
- letters: Processing's metrics for FuturaStd-Bold (a small sketch) against `formsGLFont.h`
  (FreeType): advances within 0.0001 px; the TOMITA_BAUHAUS letter scenes close to Processing
  (render_requests built with -DWITH_FONT and FORMSGL_FONT set).
- `test_thumbnails.cpp <renderer copy> <sheet.ppm>`: Make thumbnails headless, all 482 instruments:
  no errors, 37 ms each; the 11 empty ones were empty in Processing too (instruments for one bar).
- `test_gpu.cpp <scale> <requests>`: the GPU noise mask against the CPU: 0.002% of pixels differ
  (mostly by 1 level); no faster than the CPU cores on an M4 Max.
