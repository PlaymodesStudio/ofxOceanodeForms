# Forms GL Box tests (Phase 7: the renderer in C++)

All headless; no audio, no app. `S` = a scratch folder, `G` = the sketch's `generators.json`.

1. Random streams, bit for bit against Processing:
   - `rngcheck/rngcheck.pde` (a separate Processing sketch) writes `rng_java.txt`:
     `Processing cli --sketch=$PWD/tests/formsGL/rngcheck --output=$S/b --force --run`
   - `clang++ -std=c++17 -O2 -I src/formsGL tests/formsGL/test_random.cpp && ./a.out rng_java.txt`
     → `ALL EXACT` (java.util.Random, random / randomGaussian / noise / noiseDetail, PRNG.pde).
2. Renders against Processing (same requests):
   - `render_requests.cpp` renders request files with the C++ port to PNG:
     `clang++ -std=c++17 -O2 -I src/formsGL -I <OF>/libs/json/include tests/formsGL/render_requests.cpp -lz`
     `./a.out $G out_cpp req/*.json`
   - the same requests with the sketch, offline mode (a config with `"offline": {"enabled": true,
     "requests": [...], "ignoreRequestSize": false}` and `outputRoot` in the scratch folder):
     `ESPECTRES_CONFIG=config.json Processing cli --sketch=<ESPECTRES_2027> --output=$S/b --force --run`
   - `python3 compare_renders.py out_java out_cpp [--strict]`: max / mean difference, column and
     row profile correlation (what the sound reads), lit-pixel overlap, energy.
3. `coverage.cpp <G> <requests...>`: how many real requests Forms GL Box draws itself, and why the
   others go to Processing.

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
  no errors, 37 ms each; the 11 empty ones are empty in Processing too (instruments for one bar).
- `test_gpu.cpp <scale> <requests>`: the GPU noise mask against the CPU: 0.002% of pixels differ
  (mostly by 1 level); no faster than the CPU cores on an M4 Max.
