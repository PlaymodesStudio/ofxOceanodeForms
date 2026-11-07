#!/bin/bash
# Phase 5's "done" test, end to end: a scene built from scratch with Forms Box's own calls
# (test_new_scene.cpp, on a copy of the app's data folder) is drawn by the renderer (src/formsGL,
# render_requests.cpp), and the images are checked (written, the right size, drawn on).
# Usage: tests/test_new_scene.sh <work dir> [generator 1] [generator 2] [data folder]
set -e
WORK="$1"; G1="${2:-pulse}"; G2="${3:-sweep}"
HERE="$(cd "$(dirname "$0")" && pwd)"
OF="$(cd "$HERE/../../.." && pwd)"
DATA="${4:-$OF/apps/Playmodes/OceanodeScroller/bin/data/Forms}"
rm -rf "$WORK"; mkdir -p "$WORK/renderer" "$WORK/out"
for d in scenes presets library setlists; do cp -R "$DATA/$d" "$WORK/renderer/"; done
cp "$DATA/generators.json" "$WORK/renderer/"
clang++ -std=c++17 -O1 -I "$HERE/../src/formsBox" -I "$OF/libs/json/include" "$HERE/test_new_scene.cpp" -o "$WORK/newscene"
"$WORK/newscene" "$WORK/renderer" "$G1" "$G2" | tee "$WORK/engine.log"
clang++ -std=c++17 -O2 -I "$HERE/../src/formsGL" -I "$OF/libs/json/include" "$HERE/formsGL/render_requests.cpp" -lz -o "$WORK/render"
echo "drawing..."
"$WORK/render" "$WORK/renderer/generators.json" "$WORK/out" $(grep '^REQUEST ' "$WORK/engine.log" | cut -d' ' -f2) > "$WORK/render.log" 2>&1 || true
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
