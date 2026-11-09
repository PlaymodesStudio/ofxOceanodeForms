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
- `test_echo_sync.cpp`: quantized whole/dotted echo steps and actual rendered repeat onsets at
  1919, 1920, 3840 and 4096 pixel widths, checking cumulative rounding does not move the rhythm.
- `test_layer_effect_preset.cpp <Forms data copy>`: effect-only scene selection, serialization,
  real request/render integration, final-item ordering, replacement/removal, solo and RNG isolation.
  Build these with C++17 and the formsBox, formsGL and OF JSON include paths.
- `../test_layer_fx.cpp src/formsBox/formsLayerFx.h`: offscreen GPU regressions for blur/trail,
  Perlin bypass/alpha/inversion/seed/threshold, animation and pause, scrolling and resolution
  invariance, plus per-image volume 0.5..2 with saturation. Build with C++17 and `-framework OpenGL`.
- `test_resolution_timing.cpp <Forms data copy> [session folder]`: compares the rhythmic
  TOMITA instruments' column envelopes at 1080p and 4K. An optional session checks the latest
  saved 4K recipe for each TOMITA_MULTILAYER layer; silent recipes are reported separately.
  Build with C++17 and the formsBox, formsGL and OF JSON include paths.
- `../test_resolution_sync.cpp`: checks normalized playhead position during live width changes,
  gate/repeat invariance, and placement of TOMITA's different-length layers at 720p, 1080p,
  UHD, DCI 4K and an odd-sized viewport. Build with the tests/stub, formsBox and OF JSON includes.
- `test_thumbnails.cpp $D <sheet.ppm>`: Make thumbnails headless, every instrument.
- `test_gpu.cpp <scale> <requests>`: the GPU noise mask against the CPU.
- `test_gpu_exact.cpp [--16k] [sparse_euclid_kicks.json]`: byte-for-byte comparisons of exact kick
  rasterization, transparent/additive blending, clipping and complete instrument recipes; shared
  worker uploads are checked for both 2D and rectangle textures, including concurrent GL work,
  fences and resource retirement. `--16k` includes native 15360 x 8640 uploads and renders.
  Build with C++17, `-O2 -DGL_SILENCE_DEPRECATION`, the formsGL and OF JSON include paths,
  and `-framework OpenGL`. Output and binaries belong in a scratch directory.
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

Exact GPU pipeline checks (2026-11-08): 540 overlapping kick comparisons and complete sparse Euclid
kick recipes at FHD, 4K, 15360 x 8640 and 16384 x 16384 produced identical RGBA bytes. Shared texture
uploads preserved all bytes and row order for both texture targets. An isolated GL update loop
continued during native 16K uploads (slowest measured update 1.69 ms); this is not a whole-app FPS
measurement. The exact kick shader was slower than parallel CPU drawing on this Mac, so GPU texture
streaming defaults on while GPU kick rasterization defaults off. The approximate noise shader stays
out of the exact application path.

Scene export integration:
- `test_export_media.cpp` checks aspect fitting and LCM duration, then pipes a 90-frame test
  video to FFmpeg and renders the real Forms SynthDefs with captured 60 Hz amplitude controls.
  Its 1-bar and 3-bar layers share a 1.5-second loop at 480 BPM. A visual and audio pulse both
  start at frame 30. The limiter is enabled, exercising lookahead compensation.
  Build: `clang++ -std=c++17 -O2 -I src/formsBox -I <OF>/libs/json/include tests/formsGL/test_export_media.cpp -o /tmp/forms-export-media-test`.
  Run: `/tmp/forms-export-media-test /tmp/forms-export-sync-test "$PWD/synthdefs/CompiledSynthdefs/Forms"`.
- `python3 tests/formsGL/check_export_media.py /tmp/forms-export-sync-test` checks the embedded
  track, exact common-loop duration, frame count, finite/non-silent WAV, visible pulse onset and
  WAV/AAC alignment (cross correlation within eight samples, allowing AAC waveform changes). Requires FFmpeg/FFprobe in Homebrew.

High-depth still image export:
- `test_export_tiff.cpp` checks an RGBA32F framebuffer read back as unsigned 16-bit channels,
  then saved and reloaded through the bundled FreeImage TIFF backend. It verifies 1024 distinct
  gradient levels, 64-bit RGBA storage (16 bits per channel), channel values and row order.
  Build on macOS with C++17, `-I <OF>/libs/FreeImage/include`, the bundled FreeImage and libpng
  macOS static libraries, `-lz -framework OpenGL`.
  Run with a scratch `.tif` destination. GPU tests need access to the system OpenGL context.
- The existing `tests/test_layer_fx.cpp` verifies the default live 8-bit path stays unchanged.

Pixel-stretch blending:
- `tests/test_stretch_blend.cpp` reads the actual post and background shaders and compares all
  six modes against CPU blend formulae, with stretch enabled/disabled. It verifies the right
  side stays unchanged and background coverage matches the selected mode.
  Build with C++17 and `-framework OpenGL`; run with
  `src/formsBox/formsPost.h src/formsBox/formsBackground.h` (GPU access required).

Cover and dynamic layers:
- `test_cover.cpp`: standalone renderer checks for silhouette isolation, seed invariance,
  per-bar clipping, zero gain, normal/stack preset resolution, scene persistence, twelve-layer
  image scheduling, and stable schedule references during growth.
  Build with C++17, the formsGL/formsBox and OF JSON include paths; pass a scratch directory.
- `test_cover_gl.cpp`: compiles the actual Cover shader sources and checks grow, feather,
  exact linear release, eraser transparency and black-mode alpha using an offscreen GL context.
  Build with C++17 and `-framework OpenGL`; pass `src/formsBox/formsCoverGL.h`.
- `TEST_LAYERS=12 tests/test_layers_live ...`: exercises the real scroller/rings/engine across
  large-to-small scene changes, hard cuts, beat repeats and shared harmony. The default remains
  the original four-buffer regression case. Use the headless OF stub and a scratch data copy.
