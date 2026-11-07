#!/bin/bash
# Phase 5's "done" test, end to end: a scene built from scratch in Forms Box (test_new_scene.cpp,
# on a copy of the renderer's data) is rendered by the real ESPECTRES_2027 sketch (offline mode),
# and the images are checked (written, the right size, drawn on).
# Usage: tests/test_new_scene.sh <work dir> [generator 1] [generator 2]
set -e
WORK="$1"; G1="${2:-pulse}"; G2="${3:-sweep}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OF="$(cd "$HERE/../../.." && pwd)"
SKETCH="/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027"
PROCESSING=/Applications/Processing.app/Contents/MacOS/Processing
rm -rf "$WORK"; mkdir -p "$WORK/renderer" "$WORK/out"
for d in scenes presets library setlists; do cp -R "$SKETCH/$d" "$WORK/renderer/"; done
cp "$SKETCH/generators.json" "$WORK/renderer/"
clang++ -std=c++17 -O1 -I "$HERE/../src/formsBox" -I "$OF/libs/json/include" "$HERE/test_new_scene.cpp" -o "$WORK/newscene"
"$WORK/newscene" "$WORK/renderer" "$G1" "$G2" | tee "$WORK/engine.log"
python3 -I - "$WORK" <<'PY'
import json, sys
w = sys.argv[1]
reqs = [l.split()[1] for l in open(w + "/engine.log") if l.startswith("REQUEST ")]
json.dump({"width": 1920, "height": 1080, "outputRoot": w + "/out",
           "offline": {"enabled": True, "ignoreRequestSize": False, "requests": reqs}}, open(w + "/config.json", "w"), indent=1)
PY
echo "rendering with the sketch..."
ESPECTRES_CONFIG="$WORK/config.json" perl -e 'alarm shift; exec @ARGV' 600 "$PROCESSING" cli --sketch="$SKETCH" --output="$WORK/build" --force --run > "$WORK/render.log" 2>&1 || true
python3 -I - "$WORK" <<'PY'
import glob, os, subprocess, sys, struct
w = sys.argv[1]
pngs = sorted(glob.glob(w + "/out/**/*.png", recursive=True))
print(len(pngs), "images rendered")
bad = 0 if pngs else 1
for p in pngs:
    bmp = p[:-4] + ".check.bmp"
    subprocess.run(["sips", "-s", "format", "bmp", p, "--out", bmp], capture_output=True)
    b = open(bmp, "rb").read()
    off = struct.unpack_from("<I", b, 10)[0]; W, H = struct.unpack_from("<ii", b, 18); bpp = struct.unpack_from("<H", b, 28)[0] // 8
    H = abs(H); stride = (W * bpp + 3) & ~3
    lit = 0; tot = 0
    for y in range(0, H, 4):
        row = b[off + y * stride: off + y * stride + W * bpp]
        for x in range(0, W, 4):
            px = row[x * bpp: x * bpp + 3]; tot += 1
            if max(px) > 24: lit += 1
    share = 100.0 * lit / tot
    ok = (W, H) == (1920, 1080) and share > 0.5
    if not ok: bad += 1
    print("  %s  %dx%d  drawn on %.1f%% of the image  %s" % (os.path.basename(p), W, H, share, "ok" if ok else "FAIL"))
print("RESULT", "ALL OK" if bad == 0 else "%d problem(s)" % bad)
sys.exit(1 if bad else 0)
PY
