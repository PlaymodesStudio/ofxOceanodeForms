#!/usr/bin/env python3
"""Checks test_formsBox_comp_nrt.scd's renders. usage: python3 test_formsBox_comp.py <dir>"""
import os, sys
import numpy as np
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from test_formsBox_levels import read_wav, db
fails = 0
def check(c, m):
    global fails
    fails += 0 if c else 1
    print(f"  [{'PASS' if c else 'FAIL'}] {m}")
d = sys.argv[1]
v = {}
for n in ("c_off", "c_on", "c_mix"):
    x, sr = read_wav(os.path.join(d, n + ".wav"))
    seg = x[int(1.0 * sr):int(1.9 * sr)]
    v[n] = (np.sqrt(np.mean(seg[:, 0] ** 2)), np.mean(seg[:, 2]), int(np.sum(~np.isfinite(x))))
    print(f"{n}: rms {db(v[n][0]):.2f} dBFS, gain reduction bus {v[n][1]:.2f} dB, nan {v[n][2]}")
off, on, mix = v["c_off"], v["c_on"], v["c_mix"]
check(all(x[2] == 0 for x in v.values()), "no NaN/inf")
check(abs(db(off[0]) - db(0.5 / np.sqrt(2))) < 0.2, "off: the tone passes unchanged (-9.03 dBFS rms)")
check(off[1] < 0.01, "off: no gain reduction reported")
red = db(off[0]) - db(on[0])
check(7.5 < red < 10.0, f"on: {red:.2f} dB less (about 9: (18 - 6) x (1 - 1/4), peak detector on a sine)")
check(abs(on[1] - red) < 1.0, "on: the gain-reduction bus matches the level drop")
check(db(on[0]) < db(mix[0]) < db(off[0]), "mix 0.5: between dry and compressed")
print("ALL OK" if fails == 0 else f"{fails} FAILED")
sys.exit(1 if fails else 0)
