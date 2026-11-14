#!/usr/bin/env python3
"""Checks the NRT renders of test_formsBox_sharp_nrt.scd. numpy only. usage: test_formsBox_sharp.py <dir>"""
import os, struct, sys
import numpy as np
def read_wav(path):
    data = open(path, "rb").read(); pos = 12; ch = sr = raw = None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]; body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ": _, ch, sr = struct.unpack("<HHI", body[:8])
        elif cid == b"data": raw = body
        pos += 8 + size + (size & 1)
    return np.frombuffer(raw[: len(raw) // (4 * ch) * 4 * ch], "<f4").reshape(-1, ch).astype(np.float64), sr
d = sys.argv[1]; ok = True
def check(c, msg):
    global ok; ok &= bool(c); print(f"  [{'PASS' if c else 'FAIL'}] {msg}")
def env(x, sr, w=0.002):
    n = max(1, int(w * sr)); a = np.abs(x); return np.array([a[i:i + n].max() for i in range(0, len(a) - n, n)]), n / sr
c, sr = read_wav(os.path.join(d, "c_hit.wav")); s, _ = read_wav(os.path.join(d, "s_hit.wav"))
if os.path.exists(os.path.join(d, "c_old.wav")):
    o, _ = read_wav(os.path.join(d, "c_old.wav")); m = min(len(o), len(c))
    # the oscillators' random start phases differ per run, so compare the envelopes, not the samples
    ec, _ = env(c[:m, 0], sr, 0.01); eo, _ = env(o[:m, 0], sr, 0.01)
    check(np.max(np.abs(ec - eo)) < 0.12 * ec.max(), f"classic mode, new plugin vs the old plugin: envelopes within {np.max(np.abs(ec - eo)) / ec.max() * 100:.1f}% (phases are random)")
def rise(x):
    i0 = int(0.5 * sr); seg = np.abs(x[i0 - 480:i0 + int(0.2 * sr), 0]); peak = seg[int(0.1 * sr):].max()
    e = np.maximum.accumulate(seg); t10 = np.argmax(e > 0.1 * peak); t90 = np.argmax(e > 0.9 * peak)
    return (t10 - 480) / sr * 1000, (t90 - 480) / sr * 1000, peak
c10, c90, cp = rise(c); s10, s90, sp = rise(s)
print(f"hit at 0.5 s (2637 Hz partial): classic reaches 10% at {c10:.2f} ms and 90% at {c90:.2f} ms; sharp 10% at {s10:.2f} ms, 90% at {s90:.2f} ms (peaks {cp:.3f} / {sp:.3f})")
check(s90 < 1.5, f"sharp: 90% of the level within 1.5 ms of the message ({s90:.2f} ms; attack 1 ms)")
check(s90 < c90 * 0.5, "sharp: at least twice as fast as classic")
check(abs(sp - cp) < 0.02 * cp, "sharp: the same level as classic once it is there")
so, _ = read_wav(os.path.join(d, "s_soft.wav")); o10, o90, op = rise(so)
check(14 < o90 < 24, f"sharp with attack 20 ms: 90% at {o90:.2f} ms (a line over the attack)")
ch, _ = read_wav(os.path.join(d, "s_chord.wav")); i0 = int(0.5 * sr); chp = np.abs(ch[:, 0]).max()
check(np.abs(ch[:i0 + 1, 0]).max() < 1e-6, f"sharp, 4 partials: silent up to the message, the wave starts at zero ({np.abs(ch[:i0 + 1, 0]).max():.1e})")
check((ch[i0 + 1:i0 + 6, 0] > 0).all(), "sharp, 4 partials: they start together, all rising from their zero crossing")
step = np.abs(np.diff(ch[:, 0])).max(); held = np.abs(np.diff(ch[int(0.55 * sr):int(0.65 * sr), 0])).max()
check(step < 1.6 * held + 1e-9, f"sharp: no jump between samples larger than the held wave's own steps (max step {step:.4f}, held {held:.4f})")
tail_s = np.abs(s[int(0.95 * sr):int(1.1 * sr), 0]).max(); check(tail_s < 1e-4, f"sharp: silent 0.25 s after the release ({tail_s:.2e})")
rel_c = np.abs(c[int(0.71 * sr):int(0.72 * sr), 0]).max() / cp; rel_s = np.abs(s[int(0.71 * sr):int(0.72 * sr), 0]).max() / sp
check(abs(rel_c - rel_s) < 0.15, f"release as classic (10-20 ms after the off: classic {rel_c:.2f}, sharp {rel_s:.2f} of the peak)")
check(np.isfinite(s).all(), "sharp: no NaN / inf")
# the fade: how far the envelope is from the straight line it was sent as
def fade_err(x, f0):
    t = np.arange(len(x)) / sr; a = np.abs(x[:, 0]); n = int(sr / f0 * 2)   # peak per two periods = the envelope
    e = np.array([a[i:i + n].max() for i in range(int(0.3 * sr), int(1.1 * sr), n)]); tt = (np.arange(len(e)) * n + int(0.3 * sr) + n / 2) / sr
    k = np.polyfit(tt, e, 1); return np.sqrt(np.mean((e - np.polyval(k, tt)) ** 2)) / e.max()
cf, _ = read_wav(os.path.join(d, "c_fade.wav")); sf, _ = read_wav(os.path.join(d, "s_fade.wav"))
f0 = 440 * 2 ** ((80 - 69) / 12)
ec, es = fade_err(cf, f0), fade_err(sf, f0)
print(f"fade in over 1 s: envelope's distance from a straight line: classic (60 per second) {ec * 100:.3f}%, sharp (240 per second) {es * 100:.3f}%")
check(es <= ec * 1.2 + 0.002, "sharp: the fade is at least as straight as classic")
print("ALL OK" if ok else "FAILED"); sys.exit(0 if ok else 1)
