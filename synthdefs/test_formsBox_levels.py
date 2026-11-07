#!/usr/bin/env python3
"""Level checks for the NRT renders of test_formsBox_nrt.scd (float WAVs). numpy only.

usage: python3 test_formsBox_levels.py <dir with t*.wav>
Prints peak / RMS (dBFS) per channel and per time window, NaN/inf counts, and PASS/FAIL lines.
"""
import os
import struct
import sys

import numpy as np


def read_wav(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] == b"RIFF" and data[8:12] == b"WAVE", path
    pos, ch, sr, fmt, bits, raw = 12, None, None, None, None, None
    while pos + 8 <= len(data):
        cid, size = data[pos:pos + 4], struct.unpack("<I", data[pos + 4:pos + 8])[0]
        body = data[pos + 8:pos + 8 + size]
        if cid == b"fmt ":
            fmt, ch, sr = struct.unpack("<HHI", body[:8])
            bits = struct.unpack("<H", body[14:16])[0]
            if fmt == 0xFFFE:  # extensible: subformat GUID starts with the format code
                fmt = struct.unpack("<H", body[24:26])[0]
        elif cid == b"data":
            raw = body
        pos += 8 + size + (size & 1)
    assert fmt == 3 and bits == 32, f"{path}: expected float32, got fmt {fmt} bits {bits}"
    x = np.frombuffer(raw[: len(raw) // (4 * ch) * 4 * ch], "<f4").reshape(-1, ch)
    return x.astype(np.float64), sr


def db(v):
    return 20 * np.log10(max(v, 1e-12))


def stats(x):
    return np.max(np.abs(x)), np.sqrt(np.mean(x ** 2))


results = []


def check(name, cond, msg):
    results.append((name, bool(cond)))
    print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")


def win(x, sr, t0, t1):
    return x[int(t0 * sr):int(t1 * sr)]


def report(name, x, sr, chans):
    print(f"\n== {name}: {x.shape[0] / sr:.2f} s, {sr} Hz")
    bad = int(np.sum(~np.isfinite(x[:, chans])))
    check(name, bad == 0, f"NaN/inf samples on ch {chans}: {bad}")
    for c in chans:
        p, r = stats(x[:, c])
        print(f"  ch{c}: peak {p:.4f} ({db(p):6.1f} dBFS)  rms {r:.4f} ({db(r):6.1f} dBFS)")


def main(d):
    def load(n):
        return read_wav(os.path.join(d, n + ".wav"))

    # 1. 4 partials, gain 0.0216: expected rms per ch = 0.0216*sqrt(4/2) = 0.0305 (-30.3 dBFS)
    x, sr = load("t1_bank4")
    report("t1_bank4", x, sr, [0, 1])
    exp = 0.0216 * np.sqrt(2)
    for c in (0, 1):
        _, r = stats(win(x[:, c], sr, 0.5, 2.0))
        check("t1", abs(db(r) - db(exp)) < 1.0, f"ch{c} rms {db(r):.2f} dBFS vs expected {db(exp):.2f}")
        p, _ = stats(x[:, c])
        check("t1", p <= 4 * 0.0216 + 1e-4, f"ch{c} peak {p:.4f} <= 4*0.0216")

    # 2. duck: before (0.6..1.0 s) 4 partials, after (1.1..2) 2 partials -> rms -3 dB
    x, sr = load("t2_bank4_duck")
    report("t2_bank4_duck", x, sr, [0, 1])
    for c in (0, 1):
        _, r0 = stats(win(x[:, c], sr, 0.7, 1.0))
        _, r1 = stats(win(x[:, c], sr, 1.2, 2.0))
        _, rn = stats(win(x[:, c], sr, 0.2, 0.5))
        print(f"  ch{c}: rms before NaN {db(rn):.2f}, after NaN {db(r0):.2f}, ducked {db(r1):.2f} dBFS")
        check("t2", abs(db(r0) - db(rn)) < 0.5, f"ch{c} a NaN amp frame did not poison the bank")
        check("t2", abs((db(r1) - db(r0)) + 3.01) < 0.6, f"ch{c} duck=1 on 2 of 4 partials: {db(r1) - db(r0):.2f} dB (expect -3.01)")
        # spectrum after the duck: 40/60 midi (82/262 Hz) gone, 80/100 (831/2637 Hz) kept
        seg = win(x[:, c], sr, 1.2, 2.0)
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1 / sr)
        band = lambda hz: sp[(f > hz * 0.97) & (f < hz * 1.03)].max()
        lo, hi = band(440 * 2 ** ((40 - 69) / 12)), band(440 * 2 ** ((80 - 69) / 12))
        check("t2", db(hi) - db(lo) > 60, f"ch{c} ducked 82 Hz is {db(hi) - db(lo):.0f} dB under unducked 831 Hz")

    # 3. kick + duck tap (ch2) + ducked bank (ch3, ch4)
    x, sr = load("t3_kick_duck")
    report("t3_kick_duck", x, sr, [0, 1, 2, 3, 4])
    for c in (0, 1):
        p0, _ = stats(win(x[:, c], sr, 0.0, 0.24))
        p1, _ = stats(win(x[:, c], sr, 0.25, 0.8))
        p2, _ = stats(win(x[:, c], sr, 1.5, 2.0))
        print(f"  kick ch{c}: before trig {p0:.4f}, 1st hit peak {p1:.4f} ({db(p1):.1f}), 2nd hit {p2:.4f} ({db(p2):.1f})")
        check("t3", p0 < 1e-6 and p1 > 0.01 and p2 > 0.01 and abs(db(p1) - db(p2)) < 0.5, f"kick ch{c} silent until trig, both hits play equally")
    dk = x[:, 2]
    at = lambda t: dk[int(t * sr)]
    print(f"  duck: t=0.20 {at(0.2):.3f}  t=0.26 {at(0.26):.3f}  t=0.5 {at(0.5):.3f}  t=0.75 {at(0.75):.3f}  t=1.2 {at(1.2):.3f}  t=1.26 {at(1.26):.4f}  t=1.51 {at(1.51):.3f}  t=2.6 {at(2.6):.4f}")
    check("t3", at(0.2) == 0 and at(0.26) > 0.95 and at(1.26) < 0.01 and at(1.51) > 0.95 and at(2.6) < 0.01,
          "duck env 0 before, ~1 right after each trigger, back to 0 after duckdecay")
    seg_full = win(x[:, 3], sr, 0.20, 0.24)    # bank before kick: 45 midi + 90 midi
    seg_duck = win(x[:, 3], sr, 0.255, 0.295)  # right after kick (duck ~0.97..0.8): 45 midi ducked
    def amp_at(seg, hz):
        sp = np.abs(np.fft.rfft(seg * np.hanning(len(seg))))
        f = np.fft.rfftfreq(len(seg), 1 / sr)
        return sp[(f > hz * 0.9) & (f < hz * 1.1)].max()
    f45, f90 = 440 * 2 ** ((45 - 69) / 12), 440 * 2 ** ((90 - 69) / 12)
    dl = db(amp_at(seg_duck, f45)) - db(amp_at(seg_full, f45))
    dh = db(amp_at(seg_duck, f90)) - db(amp_at(seg_full, f90))
    print(f"  bank under the kick: 110 Hz partial (duckw 1) {dl:+.1f} dB, 1480 Hz partial (duckw 0) {dh:+.1f} dB")
    check("t3", dl < -10 and abs(dh) < 1, "kick duck reaches the bank in the same graph (In.kr after Out.kr)")

    # 4. tone -> FX -> master (+ kick)
    for nm in ("t4_tone_fx_master", "t5_tone_fx_comp"):
        x, sr = load(nm)
        report(nm, x, sr, [0, 1])
        for c in (0, 1):
            p, r = stats(win(x[:, c], sr, 0.5, 0.95))
            print(f"  ch{c} tone only (0.5..0.95 s): peak {db(p):.1f} rms {db(r):.1f} dBFS")
            pk, _ = stats(x[:, c])
            check(nm, r > 1e-3 and pk <= 0.99 + 1e-3, f"ch{c} present and limited (peak {pk:.4f} <= 0.99)")
    # 6. full chain
    x, sr = load("t6_full_1080")
    report("t6_full_1080", x, sr, [0, 1])
    for c in (0, 1):
        pk, r = stats(win(x[:, c], sr, 1, 20))
        check("t6", r > 1e-3 and pk <= 0.99 + 1e-3, f"ch{c} full chain present and limited")
    for nm in ("t7_cpu_3banks", "t8_cpu_1bank"):
        x, sr = load(nm)
        report(nm, x, sr, [0, 1])
    print("\n== render wall times")
    for f in sorted(os.listdir(d)):
        if f.endswith(".time"):
            w = float(open(os.path.join(d, f)).read())
            print(f"  {f[:-5]}: {w:.2f} s")
    tf = lambda n: float(open(os.path.join(d, n + ".time")).read()) if os.path.exists(os.path.join(d, n + ".time")) else None
    base, b1, b3 = tf("t9_empty"), tf("t8_cpu_1bank"), tf("t7_cpu_3banks")
    if None not in (base, b1, b3):
        print(f"  CPU (120 s renders, minus the empty-score baseline {base:.2f} s):")
        print(f"    1 x FormsBank512 (1024 SinOsc): {(b1 - base) / 120 * 100:.1f} % of one core")
        print(f"    3 x FormsBank512 (3072 SinOsc): {(b3 - base) / 120 * 100:.1f} % of one core")
    nf = sum(1 for _, ok in results if not ok)
    print(f"\n{len(results) - nf} passed, {nf} failed")
    return nf


if __name__ == "__main__":
    sys.exit(1 if main(sys.argv[1]) else 0)
