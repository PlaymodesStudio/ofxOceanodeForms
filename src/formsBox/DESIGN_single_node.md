# Forms Box as a single node — as built (2026-11-06)

Goal: the patch becomes **Forms Box + SC Output + Display Output** (+ the globals, the fullscreen
key and the custom panel's portals). Forms Box does the video post-processing and the
sonification itself, with its own SuperCollider SynthDefs. Patch `7--ESPECTRES_2027_5` is that
patch (generated from patch 6 by `tools/make_patch7.py`); patch 6 is untouched.

## Decisions (user, 2026-11-06)

- Only the kick among the extra voices (no 808, nebula, supersaw).
- Per-layer sonification: each layer's header column plays its own bank.
- 512 partials per layer, in one SynthDef (not chunks of 64).
- Fix the 1920 quirks instead of reproducing them: the scale grid follows the real root (was pitch
  class 3), the row → pitch mapping follows the renderer exactly (the 1920 one was off, and its
  −1.33 st only patched it), the kick ducking lasts beats (was a fixed 120 BPM phasor). Also
  dropped: the limiter's 20 ms dry delay, the 13-channel leftovers, the rotated L/R vector, the
  router clamping.
- L/R auto-balance kept as an option, off by default.
- Compatibility with old patches is not required; it is kept anyway (see "Old patches").

## What it replaces

Video: `HSLFilter_1` → `PixelStretch_1` → `Ranger_15` + `Mixer_4` (opacity) → `Levels_1` →
`Macro_12` + `Mixer_1` (the line). Audio: `Macro_2` (all voices, the sonificator), `Macro_4` (the
column), `Macro_11` (L/R), `SC_PolyMixer*_1`, the vector chains and their portals.

## Code (src/formsBox/)

- `formsBox.h` — the node: parameters, OSC, player, composite; `runSound()` reads the layers'
  header columns, detects the kick and fills the sound frame.
- `formsPost.h` — post-processing: one shader (stretch, HSV, opacity, levels, line with easing).
- `formsSoundModel.h` — the sonification maths (no OF / SC): pitches, scale grid, equal loudness,
  stereo field, duck weights, column → amplitudes, L/R balance, WAV header.
- `formsSound.h` — the SuperCollider engine: an internal `scNode` (Forms Box itself is not one,
  which would rename it "SC Forms Box"); the node's **Audio** output (ScBus) points at it, so
  serverManager builds it when Audio reaches an SC Output. One group per server: FormsKick →
  FormsBank512 x 4 → FormsFX → FormsMaster. It waits for scsynth to finish loading the SynthDefs
  before creating synths. Amplitudes go to 512 control buses per layer (`/c_setn` each frame);
  pitch / gain / duck arrays (`/n_setn`) only when the harmony or the settings change; layers
  without an image are paused.
- `formsControls.h` — the control registry: every window setting is an ofParameter with a key;
  right-click (or the Node tab) publishes / unpublishes it; values and the published list are saved
  with the patch and restored before the cables (`loadBeforeConnections`).
- `formsEditorPost.h`, `formsEditorSound.h` — the two tabs.
- SynthDefs: `synthdefs/formsBox.scd` → `synthdefs/CompiledSynthdefs/Forms/` (README there).

## Sonification

Per layer, per frame: the column at the header (1 x H) → partials laid out in pitch as the renderer
draws it (pitch p at y = H − p·H/notegrid − safey, safey = H/notegrid in whole pixels): a whole
number per semitone (5 at note grid 100, ~500 partials, up to 512), one exactly on each semitone
above the scene root; each takes the brightest max(R,G,B) of the rows nearest its pitch × Value ×
the header opacity. Pitch = scene root + p + pitch bend, crossfaded to the nearest scale note by the
scene's scale quantize; the grid is (root + transpose + scale) mod 12. Banks L and R ±0.0373 st
(the 1920 detune, centred). The 1920 mapping (root + (notegrid+1)·row/H − 1.33) was up to 60 cents off.
Gains: equal loudness (20 % blend) × a frozen per-partial stereo field (lows centred) × the layer's
pan. Duck: depth 0.89 × sideChain at the bottom, 0 above "Duck below" (MIDI 64), envelope of
"Duck beats" (2) at the current BPM.
Kick: the kick layer's bottom strip (100/3840 of the height) over the threshold (0.5), with a
minimum gap; the scene's sample (by file name, else by number), rate 2^((transpose + tune)/12),
the scene's kickVol, level 1.5. FX: the scene's reverbAmt is the reverb mix, its Compressor, its
SpectrumGain (smoothed) is the FX gain. Master: session fade, L/R (optional), limiter, Volume.

## The 1920 voices (tuning check)

Patch 7 also has SUPERSAW, 808 and NEBULA macros (`tools/make_patch7_voices.py`) driven by Root Out
/ Scale Out, mixed with Audio in an SC PolyMixer. Tested offline: their wiring over 736 harmonies
and their SynthDefs rendered and measured (in tune within 3 cents).

## Old patches

A patch without `formsPublished` in Forms Box's json gets the old node: the old parameters and
outputs published, post-processing off (Output = the plain composite). Patch 6 loads as before.

## Tested / not tested

Tested headless: the sound model (1146 checks), the shader against the patch's shaders in GL 4.1
(within 1/255; the line's width), the SynthDefs in NRT (levels, ducking, kick, no NaN, CPU ~4 % of
a core per bank), the scene requests (identical apart from the new `kickSample` key), the build.
Not tested: the app itself (the graph build through the internal node, the sound in real time,
levels against patch 6, the tabs' look).
