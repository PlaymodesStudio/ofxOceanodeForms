"""Audio tuning test of the voices (offline, scsynth NRT: nothing is played).

For each case written by test_voice_tuning.py: renders kick_808 (808), SuperSaw13 (supersaw) and
Analog13 (the nebula's 13 voices) at the pitches the patch-7 macros compute, plus Forms Box's own
FormsBank512 with a few partials snapped to the scale, then measures every channel's fundamental
and checks it against the expected MIDI note (cents) and that its pitch class is in Scale Out.
Usage: python3 -I test_voice_audio.py <voices.json> <work dir> <app data Synthdefs folder> <FormsBank folder> [cases]
"""
import json, math, os, subprocess, sys
import numpy as np

CASES, WORK, DEFS, FORMS = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
N = int(sys.argv[5]) if len(sys.argv) > 5 else 24
SCLANG = "/Applications/SuperCollider.app/Contents/MacOS/sclang"
SR = 48000
os.makedirs(WORK, exist_ok=True)
cases = json.load(open(CASES))
step = max(1, len(cases) // N)
cases = cases[::step][:N]

def sc_list(v): return "[" + ",".join("%.6f" % x for x in v) + "]"
defs = [os.path.join(DEFS, "OLD/kick_808.scsyndef"), os.path.join(DEFS, "Source/Analog/SuperSaw/SuperSaw13.scsyndef"),
        os.path.join(DEFS, "Source/Analog/Analog/Analog13.scsyndef"), os.path.join(FORMS, "FormsBank512.scsyndef")]
lines = ["(", "var opts, renders = List[], runNext;",
         "opts = ServerOptions.new.numOutputBusChannels_(43).numInputBusChannels_(0).sampleRate_(%d).memSize_(65536)"
         ".maxSynthDefs_(4096).numControlBusChannels_(16384).numAudioBusChannels_(1024).numWireBufs_(4096).blockSize_(64);" % SR]
for k, c in enumerate(cases):
    neb = c["nebula"][:13]
    # Forms partials: up to 6 partials on the scale notes above the chord root (an octave up)
    notes = [c["rootOut"] + 12 + s for s in sorted(set((s - c["rootOut"]) % 12 for s in c["scaleOut"]))][:6]
    c["formsNotes"] = notes
    pitch = [60.0] * 512
    amps = [0.0] * 512
    for i, n in enumerate(notes): pitch[i * 40] = float(n); amps[i * 40] = 1.0
    ev = [
        "[0.0] ++ %s.collect({ |p| [\\d_load, p] })" % ("[" + ",".join('"%s"' % d for d in defs) + "]"),
        "[0.0, [\\c_setn, 0, 512] ++ %s]" % sc_list(amps),
        "[0.01, [\\s_new, \\kick_808, 1000, 1, 0, \\out, 0, \\pitch, %f, \\decay, 1.7239, \\attack, 0.0001, \\tri, 0, \\levels, 1, \\trig, 1]]" % c["kick808"],
        "[0.01, [\\s_new, \\SuperSaw13, 1001, 1, 0, \\out, 2, \\pitch_sel, 0, \\levels_sel, 0]]",
        "[0.01, [\\n_setn, 1001, \\pitch, 13] ++ %s]" % sc_list([c["supersaw"]] * 13),
        "[0.01, [\\n_setn, 1001, \\levels, 13] ++ %s]" % sc_list([1.0] * 13),
        "[0.01, [\\n_setn, 1001, \\mix, 13] ++ %s]" % sc_list([1.0] * 13),
        "[0.01, [\\n_setn, 1001, \\detune, 13] ++ %s]" % sc_list([0.694] * 13),
        "[0.01, [\\s_new, \\Analog13, 1002, 1, 0, \\out, 15, \\pitch_sel, 0, \\levels_sel, 0]]",
        "[0.01, [\\n_setn, 1002, \\pitch, 13] ++ %s]" % sc_list(neb),
        "[0.01, [\\n_setn, 1002, \\levels, 13] ++ %s]" % sc_list([1.0] * 13),
        "[0.01, [\\n_setn, 1002, \\type, 13] ++ %s]" % sc_list([0.0] * 13),
        "[0.01, [\\s_new, \\SuperSaw13, 1004, 1, 0, \\out, 30, \\pitch_sel, 0, \\levels_sel, 0]]",
        "[0.01, [\\n_setn, 1004, \\pitch, 13] ++ %s]" % sc_list([c["supersaw"]] * 13),
        "[0.01, [\\n_setn, 1004, \\levels, 13] ++ %s]" % sc_list([1.0] * 13),
        "[0.01, [\\n_setn, 1004, \\mix, 13] ++ %s]" % sc_list([1.0] * 13),
        "[0.01, [\\n_setn, 1004, \\detune, 13] ++ %s]" % sc_list([0.0] * 13),
        "[0.01, [\\s_new, \\FormsBank512, 1003, 1, 0, \\out, 28, \\ampbus, 0, \\duckbus, 4000, \\gain, 1, \\lag, 0.001]]",
        "[0.01, [\\n_setn, 1003, \\pitch, 512] ++ %s]" % sc_list(pitch),
        "[0.01, [\\n_setn, 1003, \\gl, 512] ++ %s]" % sc_list([1.0] * 512),
        "[0.01, [\\n_setn, 1003, \\gr, 512] ++ %s]" % sc_list([1.0] * 512),
        "[2.0, [\\c_set, 0, 0]]",
    ]
    lines.append("renders.add([%s, %s]);" % ('"%s"' % os.path.join(WORK, "case_%02d" % k), "[" + ",\n".join(ev) + "]"))
lines.append("""runNext = { |i|
    if(i >= renders.size) { "ALL RENDERED".postln; 0.exit } {
        var r = renders[i];
        Score(r[1]).recordNRT(r[0] ++ ".osc", r[0] ++ ".wav", nil, %d, "WAV", "float", opts, "", 2.0, { ("RENDERED " ++ i).postln; runNext.(i + 1) });
    };
};
runNext.(0);
)""" % SR)
script = "\n".join(lines).replace("\\\\", "\\")
open(os.path.join(WORK, "render.scd"), "w").write(script)
for f in os.listdir(WORK):
    if f.endswith(".wav"): os.remove(os.path.join(WORK, f))
r = subprocess.run(["perl", "-e", "alarm shift; exec @ARGV", "900", SCLANG, os.path.join(WORK, "render.scd")],
                   stdin=subprocess.DEVNULL, capture_output=True, text=True)
open(os.path.join(WORK, "render.log"), "w").write(r.stdout + r.stderr)

def read(path):
    b = open(path, "rb").read()
    i, ch, data = 12, 0, None
    while i < len(b) - 8:
        cid, ln = b[i:i + 4], int.from_bytes(b[i + 4:i + 8], "little")
        if cid == b"fmt ": ch = int.from_bytes(b[i + 10:i + 12], "little")
        if cid == b"data": data = np.frombuffer(b[i + 8:i + 8 + ln], dtype="<f4")
        i += 8 + ln + (ln & 1)
    return data.reshape(-1, ch)

def centre(x, expect):
    """Energy-weighted centre (MIDI) of the spectrum within +-1 semitone of `expect`"""
    x = x - x.mean()
    n = 1 << 20
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), n)) ** 2
    fr = np.fft.rfftfreq(n, 1 / SR)
    f = 440 * 2 ** ((expect - 69) / 12)
    lo, hi = np.searchsorted(fr, f * 2 ** (-1 / 12)), np.searchsorted(fr, f * 2 ** (1 / 12))
    m = 69 + 12 * np.log2(fr[lo:hi] / 440)
    return float((m * X[lo:hi]).sum() / X[lo:hi].sum())

def f0(x, expect, win=1.5):
    """Fundamental near `expect` (MIDI): the strongest spectral peak within +-1.5 semitones,
    refined by parabolic interpolation (zero-padded FFT, Hann window)."""
    x = x - x.mean()
    n = 1 << 20
    X = np.abs(np.fft.rfft(x * np.hanning(len(x)), n))
    fr = np.fft.rfftfreq(n, 1 / SR)
    f = 440 * 2 ** ((expect - 69) / 12)
    lo, hi = np.searchsorted(fr, f * 2 ** (-win / 12)), np.searchsorted(fr, f * 2 ** (win / 12))
    k = lo + int(np.argmax(X[lo:hi]))
    a, b_, c = np.log(X[k - 1] + 1e-12), np.log(X[k] + 1e-12), np.log(X[k + 1] + 1e-12)
    p = 0.5 * (a - c) / (a - 2 * b_ + c)
    fk = (k + p) * SR / n
    # is it really the fundamental? nothing strong an octave below
    lo2, hi2 = np.searchsorted(fr, fk / 2 * 0.97), np.searchsorted(fr, fk / 2 * 1.03)
    sub = X[lo2:hi2].max() / X[k] if hi2 > lo2 else 0
    return 69 + 12 * math.log2(fk / 440), sub

fails, checks, worst = 0, 0, {}
def check(ok, msg):
    global fails, checks
    checks += 1
    if not ok:
        fails += 1
        if fails <= 15: print("FAIL", msg)
for k, c in enumerate(cases):
    p = os.path.join(WORK, "case_%02d.wav" % k)
    if not os.path.exists(p):
        check(False, "no render for case %d (see render.log)" % k); continue
    a = read(p)
    pcs = c["scaleOut"]
    inscale = lambda m: min(min(abs(m % 12 - s), 12 - abs(m % 12 - s)) for s in pcs) < 0.1
    def test(voice, sig, expect, win=1.5):
        got, sub = f0(sig, expect, win)
        cents = 100 * (got - expect)
        worst[voice] = max(worst.get(voice, 0), abs(cents))
        check(abs(cents) < 8, "%s %s: expected %.2f got %.2f (%+.1f cents)" % (c["case"], voice, expect, got, cents))
        check(inscale(round(got)), "%s %s: %.2f not in Scale Out %s" % (c["case"], voice, got, pcs))
        check(sub < 0.3, "%s %s: energy an octave below (%.2f)" % (c["case"], voice, sub))
    # the 808: its tail (the pitch sweep has settled), left channel
    test("808", a[int(0.6 * SR):int(1.8 * SR), 0], c["kick808"])
    test("supersaw (detune 0)", a[int(0.2 * SR):int(1.9 * SR), 30], c["supersaw"])
    cc = 100 * (centre(a[int(0.2 * SR):int(1.9 * SR), 2], c["supersaw"]) - c["supersaw"])
    worst["supersaw detuned 0.694: centre"] = max(worst.get("supersaw detuned 0.694: centre", 0), abs(cc))
    # reported only: the detuned saws (JP-8000 spread around a centre saw at the pitch) beat, so the
    # energy centre wanders a little with the random phases; the detune-0 render checks the tuning
    for v in range(13):
        test("nebula", a[int(0.2 * SR):int(1.9 * SR), 15 + v], c["nebula"][v])
    for n in c["formsNotes"]:
        test("forms partials", a[int(0.2 * SR):int(1.9 * SR), 28], n, win=0.4)
print("%d cases rendered: %d checks, %d failed" % (len(cases), checks, fails))
print("largest deviation (cents):", {k: round(v, 2) for k, v in worst.items()})
sys.exit(1 if fails else 0)
