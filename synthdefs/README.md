# Forms Box SynthDefs

The SuperCollider side of **Forms Box**: it turns the image column into sound. The sources are in
`formsBox.scd`. The compiled defs are in `CompiledSynthdefs/Forms/*.scsyndef`. They are ports of the
Oceanode patch chain in `apps/Playmodes/OceanAudio/bin/data/Supercollider/SynthdefSources`
(`Additive1080_`, `BasicSampler`, `LongReverb`, `eq13`, `CompressorB`, `EQ5`, `filter13`,
`polymixer`/`output`). All four defs are stereo, and all control names are lower case.

Signal flow (each def is added to the tail in this order):

```
FormsKick ──(audio, 2ch)─────────────────────────────┐
   └─(duckbus, kr: 0..1 env)─> FormsBank512 (x N) ──> FormsFX ──> FormsMaster ──> out
```

## FormsBank512

This def has 512 partials. Each partial has a left sine and a right sine, so 1024 `SinOsc` in total.
Several banks write to the same `out` and add up there (`Out.ar`).

```
a_i = Lag(clip(In.kr(ampbus)[i], 0, 1), lag) * (1 - duckw_i * duck)
L   = sum_i SinOsc((pitch_i + bend).midicps,          Rand(-25,25)) * a_i * gl_i
R   = sum_i SinOsc((pitch_i + bend + detune).midicps, Rand(-25,25)) * a_i * gr_i
sig = [L, R] * gain * level
out    += sig * cos(send * pi/2)     // dry
wetout += sig * sin(send * pi/2)     // to FormsFX's reverb (equal power, like the old XFade2)
```

| control   | default | meaning |
|-----------|---------|---------|
| `out`     | 0       | stereo audio bus (adds) |
| `ampbus`  | 0       | control-bus index of the first of 512 brightness values (0..1). C++ writes them with `c_setn` every frame. |
| `pitch`   | 60 x512 | MIDI pitch per partial, final value. No fixed offset is added. |
| `bend`    | 0       | semitones added to every partial |
| `detune`  | 0.0746  | semitones added to the right bank |
| `gl`      | 0 x512  | left gain per partial (pan weight x equal loudness, from C++) |
| `gr`      | 0 x512  | right gain per partial |
| `duckw`   | 0 x512  | duck depth per partial, 0..1 |
| `duckbus` | 0       | control bus that holds the kick duck envelope (0..1) |
| `gain`    | 0.0216  | overall gain |
| `level`   | 1       | layer level |
| `send`    | 0       | reverb send 0..1 (lag 1/30): 0 dry, 1 all to the reverb |
| `wetout`  | 0       | stereo audio bus of the reverb sends (FormsFX `wetin`) |
| `lag`     | 0.05    | lag time of the amplitudes, in seconds |

Guards:
- NaN and inf amplitudes are zeroed before the Lag, so one bad frame cannot stick in the Lag state.
- The duck value is sanitized.
- Frequencies are sanitized and clipped to 0..20000 Hz.
- The output is sanitized.

## FormsKick

A stereo one-shot sample (two mono buffers) or synthesized kick, plus the duck envelope. The source is silent until the
first trigger, because `PlayBuf` would otherwise play once when the synth is created.

| control     | default | meaning |
|-------------|---------|---------|
| `out`       | 0       | stereo audio bus |
| `bufl`      | 0       | mono buffer, left channel (for example `b_allocReadChannel ... 0`) |
| `bufr`      | 1       | mono buffer, right channel (`b_allocReadChannel ... 1`) |
| `t_trig`    | 0       | trigger: restarts both PlayBufs and the duck envelope |
| `rate`      | 1       | speed (multiplied by `BufRateScale`) |
| `amp`       | 0.15    | sample gain |
| `gate`      | 1       | performance gate for sample and synth output, with a 1 ms edge; reopening resumes the running tail |
| `duckbus`   | 0       | control bus written with `Out.kr`: the duck envelope |
| `duckdecay` | 1       | duck decay in seconds. C++ sets it from the BPM (2 beats). |
| `duckcurve` | -4      | curve of the decay: fast at first, then slow |
| `source` | 0 | 0 sample, 1 synthesized kick |
| `synthstart` / `synthend` | 160 / 48 | initial / final sine-body frequency in Hz, before rate transposition |
| `synthdecay` | 0.45 | seconds to approximately -60 dB |
| `synthclick` / `synthnoise` / `synthdrive` | 0.3 / 0 / 0.1 | attack click, filtered noise blend, saturation |

The duck envelope is `Env([0,1,0], [0, duckdecay], duckcurve)`. It is 1 right after a trigger and 0
after `duckdecay`. Add FormsKick before the banks so that their `In.kr` reads the value of the same
block.

FormsMaster also accepts `duckbus` (default 0) and `duckdepth` (default 0). It attenuates only
the image input, after FormsFX, before adding `kickin`. This ducks the reverb too and leaves the
kick untouched. Forms Box's full-image duck mode sets bank duck weights to zero to avoid applying
the envelope twice. Frequency-selective mode retains the bank weights and sets master duckdepth 0.

`test_kick_synthesis_duck.scd <changed-def-dir> <bank-def-dir> <output-dir>` renders offline
kick, decay and ducking checks; `python3 check_kick_synthesis_duck.py <output-dir>` measures
the WAVs, including sample/synth gate silence and tail recovery, missing-sample ducking, recovery and exact kick immunity.

## FormsFX

The chain is:

1. `In.ar(in, 2)` (the banks' dry part) → Sanitize → LeakDC.
2. **LongReverb** of `In.ar(wetin, 2)` (what each bank sends, its own amount): predelay, then
   32 × `AllpassL` (max 0.05 s, random delays), then LPF and HPF. Added to the dry signal.
3. **eq13**: low shelf, three peaks and a high shelf, all with rq/rs 1. There is no tanh: in the
   original, `wet.tanh;` is a statement whose result is not used.
4. **CompressorB**: knee 0.01 dB, send 1. It is crossfaded with `LinXFade2`:
   `dry*(1-comp) + compressed*comp`.
5. **EQ5** high shelf.
6. **filter13** type 0: `RLPF`.
7. `* gain` → `Out.ar(out)`.

| control | default | meaning |
|---|---|---|
| `in`, `out` | 0, 0 | stereo audio buses (dry in, out) |
| `wetin` | 0 | stereo audio bus: the layers' reverb sends |
| `comp` | 0 | crossfade to the compressed signal, 0..1 (lag 1/30) |
| `gain` | 1.5 | output gain (the old spectrumGain, lag 1/30) |
| `revtime`, `predelay`, `revlowpass`, `revhipass` | 4, 0, 100, 17 | reverb decay (s), predelay (s), LPF and HPF (MIDI) |
| `lowf`, `lowdb` | 80, 10 | eq13 low shelf (Hz, dB) |
| `midlowf`, `midlowdb` | 270, 3.69 | eq13 peak |
| `midf`, `middb` | 800, 2.69 | eq13 peak |
| `midhighf`, `midhighdb` | 2000, 1.34 | eq13 peak |
| `highf`, `highdb` | 7623, 2.69 | eq13 high shelf |
| `compthr`, `compratio`, `compatk`, `comprel`, `compmakeup` | -24.18, 15.22, 0.0045, 0.01, 1 | compressor (dB, ratio, s, s, linear) |
| `shelff`, `shelfdb` | 3328, -6.72 | EQ5 high shelf |
| `lpfpitch`, `lpfrq` | 120.36, 1 | RLPF cutoff (MIDI, about 8.6 kHz, lag 0.05) and rq |

## FormsMaster

```
sig = In.ar(in,2) + In.ar(kickin,2) * kickgain
sig = LinXFade2(sig, sig * buscomp_gain, bcmix * bcon)      // MixerPro's compressor, off by default
sig = sig * fade * [lgain, rgain]
sig = limit ? Limiter.ar(sig, 0.99, 0.01) : sig
Out.ar(out, sig * volume)
```

| control | default | meaning |
|---|---|---|
| `in` | 0 | FormsFX output (stereo) |
| `kickin` | 0 | FormsKick output (stereo) |
| `out` | 0 | the node's output bus (stereo) |
| `kickgain` | 1.5 | kick level |
| `fade` | 1 | 0..1 fade |
| `lgain`, `rgain` | 1, 1 | L/R auto-balance |
| `volume` | 1 | applied after the limiter |
| `limit` | 1 | 1 = Limiter on, 0 = off |
| `bcon` | 0 | bus compressor on (lag 0.03) |
| `bcthr`, `bcratio`, `bcknee` | -18, 3, 6 | threshold (dB), ratio, soft knee (dB) |
| `bcatk`, `bcrel` | 10, 150 | attack and release (ms) |
| `bcmakeup`, `bcauto` | 0, 0 | makeup (dB); auto makeup (half the reduction at 0 dB) |
| `bcrms`, `bchpf`, `bcmix` | 0, 20, 1 | detector peak..RMS, sidechain HPF (Hz), parallel mix |
| `grbus` | -1 | control bus for the gain reduction (dB); -1: none |

The bus compressor is a copy of MixerPro's `mixerProComp` (ofxOceanodeSuperCollider,
`synthdefs/Defaults/MixerPro/mixerpro.scd`): log-domain soft knee, peak/RMS detector after a
sidechain HPF, linked L/R, decoupled ballistics. One change: with no `grbus` it writes to an index
past every bus (MixerPro writes to -1, which on arm64 becomes control bus 0).

`kickgain`, `fade`, `lgain`, `rgain` and `volume` have a 1/30 s lag.

## Recompile

```sh
cd addons/ofxOceanodeForms/synthdefs
perl -e 'alarm shift; exec @ARGV' 300 /Applications/SuperCollider.app/Contents/MacOS/sclang -D "$PWD/formsBox.scd" </dev/null
```

This writes `CompiledSynthdefs/Forms/` next to the script and exits by itself. FormsBank512 takes
about 5 s to build. Do not overwrite the `.scsyndef` files while a server that uses them is running.
Reload them with `/d_loadDir` (or `/d_load`) afterwards.

## Tests (offline, no audio out)

```sh
perl -e 'alarm shift; exec @ARGV' 900 /Applications/SuperCollider.app/Contents/MacOS/sclang -D "$PWD/test_formsBox_nrt.scd" [outdir] </dev/null
```

- `test_formsBox_nrt.scd` renders float WAVs with `Score.recordNRT`, using the compiled defs.
- It then runs `test_formsBox_levels.py <outdir>`, which needs numpy only. That script checks:
  - peak and RMS levels, and NaN or inf samples;
  - the duck: depth, timing, and that it reaches the banks;
  - that the kick is silent before the first trigger;
  - the limiter;
  - the CPU estimate.
- `test_formsBox_send_nrt.scd` (then `test_formsBox_send.py`): the per-layer reverb send. Send 0
  leaves no tail; send 1 and 0.5 do; with two layers the tail holds only the one that sends.
- `test_formsBox_comp_nrt.scd` (then `test_formsBox_comp.py`): the bus compressor. Off passes a
  -6 dBFS sine unchanged; on (-18 dB, 4:1) takes 9 dB off, the gain-reduction bus reads the same,
  and mix 0.5 lands in between.
