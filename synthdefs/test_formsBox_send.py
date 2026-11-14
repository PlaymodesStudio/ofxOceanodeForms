#!/usr/bin/env python3
"""Checks the renders of test_formsBox_send_nrt.scd: the per-layer reverb send.
usage: python3 test_formsBox_send.py <dir>"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_formsBox_levels import read_wav, db

fails = 0
def check(cond, msg):
    global fails
    fails += 0 if cond else 1
    print(f"  [{'PASS' if cond else 'FAIL'}] {msg}")

def amp_at(x, sr, hz):
    # amplitude of a sine at hz (Hann window, both channels summed / 2)
    m = x.mean(axis=1); w = np.hanning(len(m))
    sp = np.abs(np.fft.rfft(m * w)) * 2 / w.sum()
    f = np.fft.rfftfreq(len(m), 1 / sr)
    k = np.argmin(np.abs(f - hz))
    return sp[max(0, k - 3):k + 4].max()

d = sys.argv[1]
r = {}
for n in ("s_dry", "s_wet", "s_half", "s_mixed"):
    x, sr = read_wav(os.path.join(d, n + ".wav"))
    play, tail = x[int(0.4 * sr):int(0.9 * sr)], x[int(1.3 * sr):int(2.3 * sr)]
    r[n] = dict(p440=amp_at(play, sr, 440), p659=amp_at(play, sr, 659.26),
                t440=amp_at(tail, sr, 440), t659=amp_at(tail, sr, 659.26),
                tailrms=np.sqrt(np.mean(tail ** 2)), nan=int(np.sum(~np.isfinite(x))))
    v = r[n]
    print(f"{n}: playing 440 {db(v['p440']):.1f} 659 {db(v['p659']):.1f} dB | tail 440 {db(v['t440']):.1f} 659 {db(v['t659']):.1f} dB, rms {db(v['tailrms']):.1f} dBFS | nan {v['nan']}")
for n, v in r.items(): check(v["nan"] == 0, f"{n}: no NaN/inf")
check(db(r["s_dry"]["tailrms"]) < -90, "send 0: no tail after the layer stops (dry only)")
check(db(r["s_wet"]["tailrms"]) > -60, "send 1: a reverb tail after the layer stops")
check(db(r["s_half"]["tailrms"]) > -60, "send 0.5: a tail")
# (no exact tail ratio between renders: the reverb's allpass delays are random per synth, Rand)
check(abs(db(r["s_half"]["tailrms"] / r["s_wet"]["tailrms"])) < 6, "send 0.5 tail within 6 dB of send 1's (about -3 dB, reverbs differ per render)")
check(r["s_half"]["p440"] > 1e-3, "send 0.5: the layer is heard while playing")
m = r["s_mixed"]
check(m["p440"] > 1e-3 and m["p659"] > 1e-3, "two layers: both heard while playing")
# (MicroSpace is a modulated reverb: its tail of the 659 Hz layer spreads a little around it. Measured
# 2026-11-11: 440 Hz is 34 dB under 659 Hz in the tail with the 659 layer alone, 28 dB with the dry
# 440 layer added, so the limit is 20 dB, not the 40 of the static allpass reverb before it.)
check(db(m["t659"]) - db(m["t440"]) > 20, "two layers: the tail holds the sent layer (659 Hz), not the dry one (440 Hz)")
print("ALL OK" if fails == 0 else f"{fails} FAILED")
sys.exit(1 if fails else 0)
