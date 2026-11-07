#!/usr/bin/env python3
"""Compares Forms GL Box's renders with Processing's for the same requests.
usage: compare_renders.py <processing out dir (searched recursively)> <c++ out dir> [--strict]
Per request: both composited over black (as Forms Box shows them); max / mean pixel difference,
the share of pixels within 2 levels, column and row brightness profile correlation (what the sound
reads), and lit-pixel overlap. numpy + zlib only."""
import os, sys, struct, zlib, glob
import numpy as np

def read_png(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    pos, idat, w = 8, b"", None
    while pos < len(data):
        n = struct.unpack(">I", data[pos:pos + 4])[0]; t = data[pos + 4:pos + 8]; body = data[pos + 8:pos + 8 + n]
        if t == b"IHDR": w, h, bd, ct, _, _, il = struct.unpack(">IIBBBBB", body); assert bd == 8 and il == 0, (bd, il)
        elif t == b"IDAT": idat += body
        pos += 12 + n
    ch = {6: 4, 2: 3, 0: 1, 4: 2}[ct]
    raw = np.frombuffer(zlib.decompress(idat), np.uint8); stride = w * ch
    out = np.zeros((h, stride), np.int32); prev = np.zeros(stride, np.int32)
    for y in range(h):
        f = raw[y * (stride + 1)]; line = raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)].astype(np.int32)
        if f == 0: cur = line
        elif f == 2: cur = (line + prev) & 255
        else:
            cur = np.zeros(stride, np.int32)
            for x in range(stride):
                a = cur[x - ch] if x >= ch else 0; b = prev[x]; c = prev[x - ch] if x >= ch else 0
                if f == 1: p = a
                elif f == 3: p = (a + b) // 2
                else:
                    pa, pb, pc = abs(b - c), abs(a - c), abs(a + b - 2 * c)
                    p = a if pa <= pb and pa <= pc else b if pb <= pc else c
                cur[x] = (line[x] + p) & 255
        out[y] = cur; prev = cur
    img = out.reshape(h, w, ch).astype(np.float64)
    if ch == 4: img = img[..., :3] * img[..., 3:4] / 255.0   # straight alpha over black
    return img

def corr(a, b):
    if a.std() < 1e-9 or b.std() < 1e-9: return 1.0 if np.allclose(a, b) else 0.0
    return float(np.corrcoef(a, b)[0, 1])

java_dir, cpp_dir = sys.argv[1], sys.argv[2]
strict = "--strict" in sys.argv
java = {}
for p in glob.glob(java_dir + "/**/*.png", recursive=True):
    base = os.path.basename(p)
    rid = base.split("_")[1] if base.startswith("L") else os.path.splitext(base)[0]
    java[rid] = p
fails = 0
for p in sorted(glob.glob(cpp_dir + "/*.png")):
    rid = os.path.splitext(os.path.basename(p))[0]
    if rid not in java: print(f"{rid}: no Processing render"); continue
    a, b = read_png(java[rid]), read_png(p)
    if a.shape != b.shape: print(f"{rid}: size {a.shape} vs {b.shape}"); fails += 1; continue
    d = np.abs(a - b)
    if a.max() == 0 and b.max() == 0:
        print(f"{rid}: both empty  ok"); continue
    near = float((d.max(axis=2) <= 2).mean())
    ca, cb = a.sum(axis=(0, 2)), b.sum(axis=(0, 2)); ra, rb = a.sum(axis=(1, 2)), b.sum(axis=(1, 2))
    la, lb = a.max(axis=2) > 8, b.max(axis=2) > 8
    iou = float((la & lb).sum() / max(1, (la | lb).sum()))
    energy = float(b.sum() / max(1e-9, a.sum()))
    ok = corr(ca, cb) > 0.99 and corr(ra, rb) > 0.99 and iou > 0.95 and abs(energy - 1) < 0.03
    if strict: ok = ok and d.max() <= 2
    fails += 0 if ok else 1
    print(f"{rid}: max diff {d.max():.0f}, mean {d.mean():.3f}, within 2 levels {near*100:.2f}%, columns r={corr(ca, cb):.5f}, rows r={corr(ra, rb):.5f}, lit overlap {iou*100:.1f}%, energy x{energy:.4f}  {'ok' if ok else 'DIFFERENT'}")
print("ALL CLOSE" if fails == 0 else f"{fails} DIFFERENT")
sys.exit(1 if fails else 0)
