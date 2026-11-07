# ofxOceanodeForms

FORMS / Scroller nodes for ofxOceanode, registered under the `Forms` category:

- **Forms Scroller** – scrolls through numbered PNGs (`N.png`) driven by a phase input
- **Forms Scroller Folder** – scrolls a folder of PNGs and outputs musical data read from `N_bars.txt`, `N_scale.txt`, `N_root.txt`, `N_trans.txt`, `N_range.txt`
- **FORMS P5 PNG Director** – probabilistic element sequencer
- **Texture Unifier Forms**
- **FORMS->P5 OSC Sender** – requires `OFXOCEANODE_USE_OSC`
- **Forms GL Box** (ESPECTRES 2027) – the ESPECTRES 2027 player: scenes and setlist as JSON files in
  the data folder, the requests for each image drawn in the app by its renderer (`src/formsGL`, the
  ESPECTRES_2027 Processing sketch ported to C++), the musical values of the image under the header;
  plays the images (built-in player), post-processes them and sonifies them (SuperCollider; the
  sound needs ofxOceanodeSuperCollider). Patch `8--ESPECTRES_2027_GL`.
- *Removed (2026-11-07):* Forms Box (the same node asking the Processing sketch for its images over
  OSC; patches 6-- and 7--, now in `bin/data/Presets_archive_ESPECTRES_Processing`), and before it
  Layer Scroller, Forms Transport and Forms Header (patches 3-- to 5--). They are in git history.
  The other nodes (Forms Scroller, Scroller Folder, P5 PNG Director, Texture Unifier, P5 OSC Sender)
  are used by other Playmodes shows (FORMS, GRANDPALAIS, ...) and stay.

The ESPECTRES 2027 logic is plain C++ in `src/formsBox/formsCore.h` and `formsEngine.h` (no OF, testable on its own); the nodes are thin wrappers. See the roadmap, `ESPECTRES_2027/ROADMAP.md` in the ESPECTRES project folder.

## Data folder

Forms GL Box keeps its data in the app's data folder, `bin/data/Forms` (inspector **Renderer**:
a path relative to the data folder, or absolute): `generators.json` (the parameter schemas; written
from the embedded ones if missing), `scenes/`, `presets/`, `library/` (scales, palettes),
`setlists/`, `thumbnails/`, `FuturaStd-Bold.otf` (letters), `samples/KICKS/` (the kick samples)
and `sessions/` (each session's images, sidecars, requests and timeline). **Pitchclass** is
relative too (`Supercollider/Pitchclass`). Moving the app moves everything; nothing outside it is
read. (Until 2026-11-07 this was the Processing sketch's folder in Dropbox; its older sessions stay
there.)

## The window (2026-11-07)

Top bar: the image under the header, the next scene (a menu with the setlist off), Use setlist,
Render next now, and an AUDITION marker while Audition is on. Tabs, in groups:
- **Play · Live** — playing: the view and the transport / header / scroller; the scene under the
  header, its layers, colour and harmony, performance.
- **Scenes · Instruments · Setlist · Library** — composing, each with its own Save. Instruments
  starts with the playing scene's instruments and has a LIVE EDIT row (Audition, Immediate).
- **Post-processing · Sonification** — the output.
- **Gallery · System** (right edge) — sessions; data folder, image size, session, the renderer
  (timings, Archive images, GPU, Make thumbnails), the node's published settings, the 1920 import,
  layer timing, the log.

Under the layer stack (Scenes and Live) a second, green box holds the selected layer's effects
(blur, trail) and sound (level, pan, spread, reverb): the same controls as in Post-processing and
Sonification. They are not the scene's: they apply to that layer in every scene and are saved with
the patch, like everything marked "saved with the patch" (Play controls, Post-processing,
Sonification, the renderer switches, published settings).

## Mute and solo (2026-11-07)

Performance state: not part of a scene and not saved with the patch (a reload starts with
everything playing). The top bar shows what is muted / soloed, with **clear**.
- **Layers** (M / S on each row of the layer stack, Scenes and Live): a muted layer, or one not soloed
  while another is, is neither drawn (composite and background) nor heard, and does not fire the
  kick; it keeps scrolling and receiving its pages, so unmuting is instant. Muted wins over soloed.
  Not the Sonification tab's "Layer N" switch, which only silences the sound. Audition is heard alone
  whatever is muted.
- **Instruments** (M / S in the Instruments tab's playing-scene list, and for the selected layer in
  Live): the pages on stage and waiting are drawn again without the muted instruments (or with only
  the soloed ones), and so are new pages; muting a stack mutes the presets it plays. Items with no
  instrument (legacy methods) stay under mute and go under solo. The recipes stay whole, so unmuting
  draws them complete again. These redraws are never written to the archive (it keeps each page as
  first drawn; a page first drawn while something was muted is archived that way, its sidecar's
  `itemFilter` says what was left out). Editor previews are always whole. Pages without a kept
  recipe (older than the last 400) are not redrawn.
- **Edit playing** (top of the Scenes tab's list) selects the scene under the header; Live's
  **Edit in Scenes tab** now also switches to the Scenes tab.

## Usage

```cpp
#include "ofxOceanodeForms.h"
ofxOceanodeForms::registerModels(oceanode);
```

## Custom GUI compatibility note

Parameters meant to be bound to an Oceanode custom-GUI **MultiToggle** must be `int` or `float`
(scalar or vector), never `bool`: the widget treats any other type as `vector<int>` and reads garbage
(huge draw loops → ~2 fps, or a `length_error` crash). That is why Forms Transport's **Pause** is an
`int` 0..1.

## Instruments tab (Forms Box)

This tab edits the presets of the renderer's parameterised generators (`presets/<gen>/<name>.json`
in the data folder; their schema comes from `generators.json`).

- **List:** the generators, each with its presets. Use **+ new**, **Duplicate**, **Delete**, or
  rename a preset in its name field; scenes that use it follow the rename.
- **Parameters:**
  - Tick **vary** to give a number a range: Forms Box picks a value from it for each image.
  - Tick several options of a choice to have one of them picked for each image.
  - Range parameters (`intRange`, `floatRange`) are drawn per step or note by the generator.
  - List parameters take comma-separated numbers; separate several lists with `|` (`5, 7 | -5`)
    to have one of them picked for each image.
  - **x** returns a parameter to the schema default (shown in grey).
- **Preview** renders one image with this preset alone, using the bars, harmony, scale and
  palette of the chosen scene. The image shows in the tab, never in the scroller. It is also
  saved as the preset's thumbnail (`thumbnails/<gen>/<name>.png`). **Values used for this
  image** lists what was picked.
- **Add to <scene>** puts the preset in the selected scene. Presets also appear in the Methods
  browser and the picker, under **Instruments**.
- **Stacks** (layered presets) draw their layers in order. The pick mode says which layers play:
  **all layers** (each layer may have a chance %, and layers with the same group number share one
  roll), **one layer per image** (weighted), or **one group per image** (the layers of one group
  play together; a group's weight is its first layer's; chance then rolls per layer).
  **+ layer > a preset** adds a layer that plays another preset, resolved per image (stacks inside
  stacks, up to 5 deep; a stack can't include itself). Renaming a preset updates the stacks that
  play it; the header shows how many stacks use a preset.
- **Linked picks** (under the parameters, for a preset or a stack layer): a JSON list of
  parameter sets, e.g. `[{"steps": 4, "patternSteps": 8}, {"steps": 6, "patternSteps": 12, "weight": 2}]`.
  One set is chosen per image and laid over the parameters before they resolve, so values change
  together. A list of such lists makes several independent choices.
  On a stack itself (top level, next to `layers`) the chosen set is shared: it is laid over every
  layer's parameters, including layers that play another preset. Per layer the order is: its
  parameters, then the stack's shared set, then the layer's own linked picks.
- **Per-bar choices** ("redraw every N bars", under the parameters, for a preset, a stack or a
  stack layer; `"perBar": n` in the file): the values (ranges, options, linked picks) are drawn
  again for every N bars, and each draw paints only its own bars. Many legacy methods choose per
  bar; without it a preset chooses once per image. A drawing with N = 1 renders bars-many passes.
- Unsaved edits are marked `*`. They take effect in live renders straight away, like scene edits.
  **Save** writes the file; **Revert** reloads it.

Code: `formsGenerators.h` (schema, presets, `resolve`), with presets and preview routing in
`formsEngine.h` and the tab in `formsEditor.h`.

## Layers and parallax (Phase 3)

A scene can have up to 4 layers. The Scenes tab shows them as a **stack** (top of the stack first),
next to the **Scene** settings (BPM, palette, scale, notegrid, root note, harmony) that all layers
share. Each layer row has its **opacity** slider and **up / down** arrows to change which layer is
drawn on top; below the stack, the selected layer's **bars**, **blend** (lighten / add / normal),
**kick** and **remove**, and **+ layer**. Opacity, blend and order apply live to what is playing.
Layer 1 sets the clock wherever it sits in the stack. Under the two panels, the selected layer's
**instruments** (+ Add instrument, image chance; groups and rows under "advanced").
Right-click a method's thumbnail to **Move to layer N** (or to a new layer): it keeps its chance
and group and takes the first free row of that layer.
- **Layer 1** keeps the clock's timing exactly as before (GO, analog mode, sequence, repeats).
  Its scroller also counts the **beat under the header**: each image is bars x 4 beats.
- **Layers 2..4** are placed in absolute beats. Every image is one view wide, so a layer with more
  bars scrolls more slowly (parallax), and the header sits at the same beat in every layer: all
  layers cross each beat and bar line at the header together. Forms Box asks the renderer for
  them ahead of time (one image beyond the current one), only when no layer-1 request is waiting
  (layer 1 has priority and one place in the renderer queue stays free for its next GO; the
  renderer is one queue, so short layers at high BPM can outrun it). A new layer starts 2 bars
  after the header (**start bars ahead** in the Engine tab).
  Each layer renders two images ahead (the next image enters at the right edge well before the
  header reaches it). If it isn't ready when the right edge needs it, the previous image repeats
  there; an image that arrives after its place is on screen goes to the next free place instead,
  so nothing pops in mid-view (Engine tab: "late (moved on)"; its harmony is that of its first place).
- **Harmony** is one timeline in absolute bars: layer 1 makes it as before, the other layers read
  the same bars, so all layers play the same degrees and transposes at the same moment.
- **Composite:** each layer is drawn with premultiplied colour and its alpha, then the layers go
  over black from layer 1 up: lighten = the brighter, add = the sum, normal = over (by alpha);
  gain scales the colour. A single-layer scene comes out as before.
- **Save frame** (Play tab) writes `data/FormsBox_frame.png` (the composite) and `.txt`: the header
  beat and position, and per layer the images drawn and where (to check alignment and alpha).
- **Engine tab:** the header beat, per layer its position in its image and counts (shown,
  repeated, too late, waiting), and "Sync at each bar line" (each layer's position at every bar).
  At a bar line every layer reads a multiple of 1 / its bars (a 6-bar layer: 0, 0.167, 0.333 …),
  plus the tiny step of one frame.
- **Test scene:** `PARALLAX_TEST` (layers of 8 / 6 / 4 bars drawing a thin line at every bar, in
  the top / middle / bottom third): at the header the three lines meet.
- **Scenes follow layer 1, bar by bar.** Forms Box keeps the plan of layer 1's images (start
  bar, length, scene); an extra layer uses the scene layer 1 plays at the bar it renders (past
  the plan: the scene layer 1 asks for next). A scene without that layer leaves it silent.
- **Scene changes:** *staggered* (default): each extra layer switches at its own next image.
  *Hard cut*: every layer switches at the first bar of the new scene; an image running across
  it is cut there. Per setlist entry (Setlist tab, **cut** column) and, without the setlist, the
  node's **Hard Cut** setting. Images rendered ahead with the old scene are cancelled and asked
  again (unless already on screen).
- **Harmony stays aligned.** Layer 1's first image starts at beat 0; when a layer-1 image reaches
  the header at another bar than planned (a repeat, a lost request), the rest of the plan and
  the harmony timeline move with it, so the musical outputs (from layer 1's image) and the other
  layers agree again from the next image.
- **Kick output:** the kick layer's lowest rows at the header (28 of 1080, the 100 of 3840 the
  1920 probe read), mean above 0.5, times the scene's Kick detect. Mark the kick layer in the
  Scenes tab (**kick layer**); none marked: layer 1. (In the old `6--ESPECTRES_2027_4`, Macro_2's kick
  input came from it: Macro_4's probe at Y = 3740 read outside the 1080-high image and never fired.)
- **Timeline:** `sessions/<session>/timeline.jsonl`, one line per image as it reaches the header
  (layer, id, start beat, length, png, blend, gain).
- **Export panorama** (Engine tab): the last whole cycle of all layers (the least common multiple
  of their lengths) on one beat axis at layer 1's scale, composited as live, as one PNG in the
  session folder (up to 16384 px wide).
- Not yet: per-layer sound columns (the sound reads the composite).

## Gallery (Phase 5)

Every session is archived in `<renderer>/sessions/<session>/` (each image's PNG and its recipe).
The **Gallery** tab browses them:
- **Sessions** on the left (newest first; "now" marks the running one), **Refresh** to rescan.
- A **thumbnail grid** of the session (made in the background and cached in `.gallery/`), with a
  layer filter and a scene search.
- Click an image: it shows large, with its **recipe**: scene, layer, seed, bars, BPM, scale,
  palette, the harmony per bar, and every instrument in drawing order with its render time;
  open one to see the values it had (only those that differ from the generator's defaults).
  **Copy recipe** puts the whole sidecar on the clipboard.
- **Replay this session** plays the session again from its archive, in its order, through the
  player, with no rendering, looping until **Stop replay**. Sessions with a timeline (since the
  layers) replay all their layers at their beats; older ones replay layer 1.
- **Freeze as scene**: a new scene whose layer always draws exactly this image (its recipe and seed:
  nothing is chosen at random; its harmony goes on the timeline so other layers follow it). The
  Scenes tab marks a frozen layer and can **unfreeze** it.
- **Promote to scene**: a new scene with this image's settings and instruments (each at 100%), to
  vary from.
- **Re-render x2 / x4**: the same recipe at 2 or 4 times the size, for print, into
  `sessions/prints/` (shown as a session; the Gallery lists finished prints). The generators draw
  the same image at any size: the request carries `"refSize"` (the original size), and the noise
  bands then read their noise at the original image's scale. Checked on the 199-request
  regression without the legacy underlay at 2x: 188 images within 3 levels (out of 255) of the
  original, none over 7 (anti-aliasing and soft gradients). Legacy methods (the old underlay) do
  not reproduce at another size.

## One node: Forms Box's built-in player

Forms Box now contains the clock (Forms Transport), the play-head (Forms Header) and the scroller
(Layer Scroller). It has their inputs and outputs:
- **Transport:** Speed, Pause, Analog Mode, BPM Add, Sequence, Max Minutes, Seq Fade, New PNG;
  outputs Phase, BPM, Pitch Bend, Fade, Seq Minutes, Speed Out, Auto, Sequence Out, Restart Setlist.
- **Header:** Header, Header+, Smooth, Auto Header, Header Min / Max; outputs Header Pos,
  Header Norm, phLock, phOff, actualBar, Opacity.
  **Opacity** is the beat-repeat fade x the strobe gate. The scenes' opacity tremolo (an LFO on
  it) is off unless **Tremolo** is ticked (Play tab / node parameter; off by default, since 2026-11-05).
- **Scroller:** Width and Height; outputs Output (the scrolling picture) and In Header.

At each wrap it asks for the next image itself. **Built-in Player** (inspector) switches this off;
then Forms Box works with separate Forms Transport / Forms Header / Layer Scroller nodes through
its Schedule output, as in the older patches (3--, 4--, 5--, which have it off). Preset
`6--ESPECTRES_2027_4` is 5-- with the three nodes replaced by Forms Box (generated, same wiring).

### Play tab

The window's first tab:
- **View:** the scroller's output with the header line. Drag in the view to move the header (it
  sets **Header**). **Flip view** (off by default) turns it upside down if ever needed.
- **Transport, Header and Scroller controls** with meters (phase, session, fade, phLock, opacity),
  the image under the header, and Save Frame.

The widgets write Forms Box's own parameters, so presets, connections and the custom panel keep
working. A parameter driven by a connection follows it again whenever its source changes.

### Instrument parameters in tabs

The parameters of the selected preset are in tabs, one per group (pattern, pitch, chord, cell,
colour, band…). All the processes share one "processes" tab with a list on the left. A dot after a
tab or process name means the preset sets something there; everything else is at its default (off,
for processes). Notes are in a collapsible section below.

## Single node: post-processing and sonification (2026-11-06)

Forms Box now produces the final picture and the sound itself. Patch `7--ESPECTRES_2027_5` is
Forms Box + SC Output + Display Output (plus the globals, the fullscreen key and the custom-GUI
portals); patch 6 is unchanged as a backup. `tools/make_patch7.py` generated it from patch 6.

**Node.** Always on it: Show, Preview, GO, Setlist, Scene, Speed, Pause, Sequence, Seq Fade, New
PNG, Header, Header+, Smooth; outputs **Output** (the final image), **Audio** (ScBus), Header Pos,
Header Norm, Phase, BPM, Kick, **Root Out** and **Scale Out**. Root Out is the chord root of the
bar under the playhead as a MIDI note (scene root + transpose + the scale step at the bar's
degree; a degree past the scale's end goes up an octave). Scale Out is the scale's pitch classes
on that root + transpose, sorted, 0..11: the notes the partials snap to. Both change at bar lines;
wire them to other synths. A new node also publishes Volume, Hue, Levels and Kick On. Every
other setting of the window (and every old output: Opacity, phLock, the actual* values, the scene
performance outputs…) can be published to the node: right-click its control, or tick it in the
**Node** tab. Published controls and the window's values are saved with the patch (`formsControls`,
`formsPublished` in the node's json) and restored before the cables. A patch saved before this
(no `formsPublished`) gets the old node back: the old parameters and outputs, and post-processing
off, so Output is the plain composite as before.

**Post-processing tab** (`formsPost.h`, `formsEditorPost.h`): one shader pass on the composite —
stretch left of the header, hue / saturation / value, the header's opacity, levels (min, max,
gamma, low, high), then the header line (thickness, softness, intensity, colour, easing:
exponential / linear glide / spring, offset). It matches the patch's HSLFilter → PixelStretch →
opacity → Levels chain within 1/255 (`tests/test_post_gl.cpp`, in an offscreen GL 4.1 context).
If the shader fails, Output is the plain composite.

**Sonification tab** (`formsSoundModel.h` maths, `formsSound.h` SuperCollider engine,
`formsEditorSound.h`): each layer's header column plays its own bank of sine partials (left, and
right detuned 0.0746 st; the pair is centred on the pitch). The partials are laid out the way the
renderer draws pitch (`y = H - p·H/notegrid - safey`, safey = H/notegrid in whole pixels): a whole
number per semitone (5 at note grid 100, about 500 partials), one exactly on every semitone above
the scene root, each reading the rows of its own pitch band (max(R,G,B), times Value and the
header opacity). So a note drawn on the scale sounds exactly on its pitch, in tune with Root Out /
Scale Out and the patch's voices; the scene's scale quantize can still pull the rest to the scale.
(The 1920 patch mapped rows linearly to root + note grid + 1 and patched it with -1.33 st: up to
60 cents off, depending on the height.) Plus the clock's pitch bend.
Per layer: on, level, pan, stereo spread, reverb (x the scene's reverbAmt: each layer sends its
own amount to the one reverb). Per partial: equal-loudness gain, a frozen stereo field
(lows centred), and a kick-ducking depth (partials below "Duck below" are ducked, deepest at the
bottom). The kick: detection on the kick layer's bottom strip (threshold, strip height, minimum
gap), a sample per scene (Performance > "Kick sample file"; double-click a file in the tab for the
scene at the header; empty = the scene's kick number), tune on top of the transpose, level. Then
FX (LeakDC, long reverb of the layers' sends, EQ, the scene's compressor, high shelf,
lowpass, the scene's spectrum gain) and master (session fade, optional scene L/R auto-balance,
limiter, Volume). Only the kick remains of the extra voices (no 808, supersaw, nebula).
Fixed from the 1920 patch: the pitch mapping (above), the scale grid follows the real root (it had
pitch class 3 hard-coded), the kick's ducking lasts a number of beats (it ran on a fixed 120 BPM).

The graph (one group per server: FormsKick → 4 x FormsBank512 → FormsFX → FormsMaster) is built
when **Audio** is connected to an SC Output. Forms Box is not an SC node (that would rename it);
it owns an internal one that its Audio port points at. SynthDefs: `synthdefs/formsBox.scd`,
compiled into `synthdefs/CompiledSynthdefs/Forms` (see `synthdefs/README.md`; offline tests there).
The app registers this collection after ofxOceanodeSuperCollider's, so Forms Box gets the servers.

**The 1920 voices, back as macros (tuning check):** `tools/make_patch7_voices.py` (run after
`make_patch7.py`) adds three macros to patch 7, fed by Root Out / Scale Out / Pitch Bend: SUPERSAW
(SuperSaw13 at Root Out), 808 (Kick_808 at Root Out, on Kick) and NEBULA (Analog13: a 14-note chord
of the scale's pitch classes above the chord root, voiced like the 1920 nebula, which used a fixed
root of 75). Each has a level input (set in the patch) x Opacity; they mix with
Audio in an SC PolyMixer. Not included: the old side-chain ducking of supersaw / nebula, their
13-channel delays; the nebula's PanAz has 2 speakers (it was 13, inaudible in stereo).
`tools/make_patch7_nogui.py` (third, in place) removes the ofxSantiNodes portal widgets (Slider,
Value, Toggle, Button) and the portals that only linked them; the custom GUI panel and Forms Box's
window replace them. Patch 7 is then 21 nodes.
Tuning tests: `tests/harmony_cases.cpp` (Root Out / Scale Out for 736 harmonies) piped into
`tests/test_voice_tuning.py <patch 7>` (runs the macros' wiring from their files: 13248 checks)
and `tests/test_voice_audio.py` (renders the real SynthDefs offline and measures them: 808 within
0.06 cents, nebula and Forms partials exact, supersaw within 3 cents).

Tests: `tests/test_sound_model.cpp` (the partial layout against notes drawn the renderer's way at
several sizes / note grids: every note has a partial on its pitch, the lit partials centred within
6 cents; scale grid, loudness, stereo, ducking, L/R, kick files), `tests/test_real_images.cpp`
(line centres in rendered images: the formula, the sound, the 1920 mapping) and `tests/test_post_gl.cpp` (the shader against the patch's shaders):
`clang++ -std=c++17 -I src/formsBox -I <OF>/libs/json/include tests/test_sound_model.cpp && ./a.out <kicks folder>`;
`clang++ -std=c++17 tests/test_post_gl.cpp -framework OpenGL && ./a.out <app>/bin/data/Effects <frag.glsl> [image.bmp]`.

## Transport and timeline (2026-11-06)

Forms Box plays on its own clock either way; none of this changes it, and all of it is off
until used.

- **Drive transport** (Play tab, publishable): Forms Box becomes the transport's external clock
  (the hook MIDI clock uses): every frame its beat under the header (beat repeats left out, so
  the transport never runs back), its tempo as it really moves, and Pause. Timelines, synced
  Phasors and box synths follow ESPECTRES' beat. A header moved by hand is a seek; with negative
  Speed the transport holds and goes on when Forms Box passes that point again. Turning it off
  frees the transport and leaves it stopped (press play to run it on its own). (`formsTransportFeed.h`; a MIDI clock driving it is taken over while on.)
- **Add to Timeline** (right-click any control in the window, with Publish to Node; the Play tab's
  Speed, Pause, Sequence, Seq Fade, Header, Header+ and Smooth too): the box
  synths' timeline / MIDI menu (`scTimelineMenu`). A control only the timeline (or MIDI) needs is
  published hidden; "Publish to Node" shows it; the Node tab marks it "(timeline)".
- **Scene lane** (Play tab, "Scene lane to timeline"): a timeline track with a Scenes lane (the
  chord-bank lane HarmonyBox uses): a row per scene, blocks choose what plays when; where the lane
  is empty the setlist (or manual scene) plays. Right-click a row: hard cut into that scene, or play
  it now. With Drive transport on the lane is read ahead (each layer-1 image gets the scene of the
  block at the bar it starts; the extra layers and the setlist prediction follow); off, the scene
  the lane plays now is used for the next images. Rows keep their order (saved with the patch).
  (`formsLane.h`, `Engine::laneSceneAt`.)

## Scene banks, Live tab, grids, line softness (2026-11-06)

- **Scene banks:** a bank is a folder under the renderer's `scenes/`; the scenes imported from 1920
  stay in `scenes/` itself (the "Legacy" bank). Scenes tab: "New in" picks the bank New, Duplicate
  and the Gallery's Freeze / Promote save into (with "new bank" in the same menu; saved with the
  patch), "Show" lists one bank or all (grouped), and a scene's Bank menu moves its file. Names stay
  unique across banks (the setlist and the scene lane use them).
- **Live tab** (after Play): the scene playing now, its settings at hand. At once: layer opacity,
  blend, order and kick layer, BPM, the root note (sound) and the performance settings (beat repeat,
  gate, tremolo, header jumps, speed modulation, sound levels; the clock and the header now read the
  scene's current values). Next images: palette, scale, note grid, harmony ("Render next now" asks
  one at once). Save / Revert as in the Scenes tab.
- **Grids** (Play tab, over the view, which now shows the final Output): Pitch grid: where the
  renderer draws each semitone above the scene root, the scale's notes brighter, the bar's chord root
  (Root Out) red with its name. Beat grid: layer 1's bars and beats as they scroll, the other layers'
  bar lines dashed in their colours.
- **Header line softness:** Softness H (the line's sides, as before) and Softness V: the line fades
  in from the image's top and bottom edges over that many pixels, inside the image.

## Effects before the sound, Set value (2026-11-06)

The Post-processing tab has two sides, split by a line marked "sound reads here":
- **Before the sound** (each layer's buffer, before the composite and the sonification, so heard
  and seen; `formsLayerFx.h`): **H blur**, **V blur** (Gaussian sigma in px; only pixels inside the
  image count, renormalized, so nothing comes from outside and edges keep their brightness) and
  **Trail** (a tail behind what moves, to its right, fading exponentially like a reverb; length in
  beats, the same for every layer whatever its speed; Trail level = how bright it starts).
- **After the sound** (the final image only): stretch, colour, opacity, levels, header line.
  Value and the header opacity also scale the sound (as in the patch).

Right-click any window control: **Set value** (type and Enter: a number, true / false, or text),
then publish / timeline / MIDI.

## Reverb per layer, page repetition, bus compressor (2026-11-06)

- **Reverb per layer** (Sonification > Layers, `L1..L4 Reverb`, 0..2, default 1): each layer's bank
  sends its own amount to the one reverb (equal power: dry cos, wet sin, like the old crossfade).
  The send = the scene's reverbAmt x Reverb x scene x the layer's Reverb (clipped to 1).
- **Repeat** (Scenes / Live tab, the selected layer, next to bars; saved in the scene as `repeat`
  only when > 1): each image (page) of the layer plays that many times in a row, then a new one.
  One render stands for the N pages: it becomes N copies when it arrives (`Engine::expandRepeats`).
  Layer 1 asks for a new page once every N wraps, when the previous page's first copy starts, so
  the new page is rendered before the repetition ends. On layer 1 the harmony repeats with the page
  (`harmonyBar` folds the bars onto the first copy's), so the other layers play its chords. The
  extra layers: copies placed one after another; a hard cut cuts the copy it falls in and drops the
  rest. A change that lands mid-repetition (setlist, lane) waits for the repetition to end; a
  setlist entry's "images" counts new pages.
- **Compressor** (Sonification > FX): MixerPro's compressor on the whole mix (partials, reverb and
  kick) before the limiter, with MixerPro's controls and a gain-reduction meter. Off by default.
  The scenes' own Compressor (Performance) is unchanged in FormsFX.

## Forms GL Box: the renderer in the app, no Processing (Phase 7, 2026-11-06/07)

**Forms GL Box** (Forms menu; patch `8--ESPECTRES_2027_GL`) draws every request itself with the
ESPECTRES_2027 renderer ported to C++ (`src/formsGL/`, header only), on background threads, into
`sessions/<session>/L<layer>_<id>_s<seed>.png` + sidecar (`"renderer": "FormsGL"`), then hands it to
the engine. Its parameter schemas are its own (`src/formsGL/formsGLSchemas.h`, regenerated with
`tests/formsGL/make_schemas.py`). A legacy `method` item (no current scene has one) would be left out
of its image and counted in the Node tab. The node class derives from `formsBox` (formsBox.h: the
player, abstract since Forms Box was removed). Processing is no longer used anywhere.

Ported: every generator (sweep, hits, pulse, masses, shapes, effects) and every process. How it stays
the sketch's image:
- the random streams are the sketch's, bit for bit (java.util.Random, PApplet random / noise,
  PRNG.pde), measured against Processing;
- the canvas is on the CPU and draws like Processing's Java2D, measured: a weight-1 point is one
  pixel (no anti-aliasing; a ~0.001 px snap with the PROJECT / SQUARE caps; ROUND x181/255); axis
  lines, fills (box coverage), thick PROJECT points; non-premultiplied ARGB layers with Java2D's
  mul8 / div8 tables, composited as `image()` does; strokes (curves, glides, diagonals, outlines) as
  one anti-aliased shape with Java2D's vertex snapping; Processing's ADD blend;
- every decimal literal is a float (the Processing preprocessor), Java's left-to-right argument
  order is kept, and floating-point contraction is off (`#pragma clang fp contract(off)`).
How the port was verified, against the sketch on the same requests (`tests/formsGL/README.md`): sweep pixel-identical;
hits, pulse, processes within 2 levels on most images (a few pixels a few levels off on the rest);
masses and shapes close (anti-aliased curves: >= 98% of pixels within 2 levels, energy within ~1%).
Letters are read from the same font file with FreeType (`src/formsGL/formsGLFont.h`): advances
match Processing's textWidth within 0.0001 px, ascent / descent are Java's; the letter scenes match
Processing closely (99.6% of lit pixels overlapping, same energy).
Speed: the per-pixel processes run on all CPU cores, still bit-exact (about 19 ms per 1080p image on
average, 260 ms at x4). The noise mask can also run on the GPU for images of 4 MP or more ("GPU for
big images" in the Node tab, off by default: on an M4 Max it is no faster than the CPU cores and a
few pixels differ by a level; `src/formsGL/formsGLGpu.h`, its own offscreen context on the render
thread).
**Make thumbnails** (Engine tab) draws a preview of every instrument (the Instruments tab's preview
scene, one seed for all) and keeps it as `thumbnails/<ref>.png`. The sketch's test underlay (legacy t3_clouds / r12_fullEuclidean / h2_iridescence, not
ported) is replaced, in thumbnails and in "test image underneath" previews, by a busy image from
ported generators (noise bands, a note grid, kicks, a sweep), so effects-only instruments have
something to act on.

## Audition and immediate modes (Forms GL Box, 2026-11-07)

Two switches in the Instruments tab, before Same seed, both off by default (Forms GL Box only):
- **Audition:** the edited instrument's preview (the preview scene's frame; effects-only instruments
  over the GL underlay) plays under the header, alone on the stage: the other layers are silent and
  hidden while it is on, and the page loops with the clock so the header keeps crossing it. The
  sonification reads it as the header image. Every change to the instrument redraws it at once (one
  render in flight; the latest edit wins); it keeps its own seed, so only the edited parameter
  changes, and the user's preview seed is untouched. Switching it on draws a new seed. Off: the
  stage goes back to the playback, which kept running underneath.
- **Immediate:** an edit to an instrument redraws every page on the stage and waiting (every copy
  of a repeated page) that uses it, and swaps each in place: the images change without jumping. Only
  the changed parameters take new values; every other value the page drew stays (a change of
  structure, such as stack layers, linked picks, per-bar or the generator, draws that instrument's
  items anew, others untouched). A change to a scene's palette or scale redraws that scene's
  pages. A redrawn page overwrites its PNG and sidecar (when archiving; queued after the old ones).
  One batch at a time; edits during a batch are applied together when it ends.
- `formsLiveEdit.h` holds the patching (headless); `tests/formsGL/test_live_edit.cpp` checks it on
  a page of every scene: only the edited instrument's edited key changes, the patched request
  renders, a structural edit leaves the other items identical, the scene patch changes the palette
  only (30 pages, 0 failures); and every stack (92, two of them playing other presets): a structural
  edit replaces all of its items, including those of the presets it plays, and nothing else.
- Not covered: harmony or tempo edits of a scene (they would change the page's timing); an inner
  preset edited while a stack overrides that same parameter loses the stack's value in its pages.

## Tests of the player and the editor (headless)

- `tests/test_layers_live.cpp`: the real Transport, Header (beat repeats), layer-1 scroller, layer
  rings and engine, frame by frame with a fake renderer (openFrameworks stubbed in `tests/stub`):
  hard cuts, beat repeats, holes, pop-ins. `clang++ -std=c++17 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_layers_live.cpp && ./a.out <renderer copy> [minutes] [render s] [seed] [workers]`
- `tests/test_layers_live.cpp` also checks the transport feed (one jump, never back, tempo) and,
  with `LANE=1`, a scene lane (every layer-1 image inside a block has its scene). With `REPEAT=1`
  pages repeat (layer 1 x2 / x3, extra layers x2 / x3) with Forms Box's GO pacing: each page's
  copies in order, no fallback repeat, one request per page, and every extra-layer bar in layer 1's
  chords.
- `tests/test_transport_drive.cpp`: the feed against Oceanode's real transport (compiled in):
  follows within 0.01 beat, no step back during a beat repeat, holds on pause, releases.
- `tests/test_layer_fx.cpp src/formsBox/formsLayerFx.h`: the effects on the GPU against CPU
  references (blur profile and energy, edges, trail decay, nothing outside).
- `tests/test_banks.cpp <renderer copy>`: banks (legacy untouched, new / duplicate / move / rename /
  revert / delete in bank folders, read back).
- `tests/test_new_scene.sh <work dir>`: a scene built from scratch (presets, scene, setlist) with the
  engine calls the window uses, read back, requested and drawn by the renderer (render_requests.cpp,
  from a copy of `bin/data/Forms`); and the previews' Same seed / Reroll.

## Scales and chords from the Pitchclass catalog

Forms GL Box reads the shared catalog in `Pitchclass` (inspector; default
`Supercollider/Pitchclass`, in the app's data folder): `scales.txt` and `chords.txt`, one
`<index>, <name> <semitones...>;` per line. They join the scale library after the local scales
(`library/scales.json`):
- Catalog chords are named `chord <name>`.
- Local names win, so scenes keep their sound. A catalog entry whose name is taken by a different
  local scale gets ` (catalog)`, as with `arabian` and `diminished`.
- Catalog entries are read-only in the Library tab (shown in blue). Duplicate one to edit a local
  copy. Only local entries are saved.

The ESPECTRES scales that weren't in the catalog were appended to it (new indices only, existing
ones unchanged): `arabianHex`, `formsFa`, `formsMode`, `arabian3`, `m9add13pc` in `scales.txt` and
`m9add13` in `chords.txt`. The originals are backed up in `Pitchclass/backup_2026-11-05/`.

## Instruments tab layout

- **List:** one foldable section per generator (and Stack), coloured by family, with a preset
  count and a filter field.
- **Header:** family chip, name, Save / Revert / Duplicate / Delete, how many scenes use the
  preset, and the legacy method it came from.
- **Preview:** renders with a chosen scene's harmony. Tick "test image underneath" so effects have
  something to act on. "Add to <scene>" puts the preset in the selected scene.
- **Parameters:** in tabs, laid out as a table (label | vary | value | reset), so long labels are
  clipped instead of covering the sliders. Hover a label for its full text.
- The A/B comparison was removed.

## Speed (Forms GL Box, 2026-11-07)

Nothing renders differently: 552 renders (every instrument and the 35 scenes' requests, 1920x1080)
are bit-identical before and after, also drawn in parallel.
- **Worker pool:** requests are drawn by up to 8 threads (cores - 2); results reach the engine in
  the order they were asked for. 552 renders take 3.3 s instead of 19.5 s. Max Pending (inspector)
  limits how many layer-1 images are in flight; raise it to use more workers.
- **In memory:** the pixels go to the scrollers (and the header preview) directly as textures; no
  PNG is written and read back before an image can play.
- **Archive images** (Node tab, saved with the patch, on by default): the PNG and sidecar are
  written afterwards by an archive thread (sidecar `result.queueMs`, `result.archiveMs`). Off:
  nothing is written (Gallery, replay, panorama and prints need the files). Editor previews and
  thumbnails are always written first, since the editor reads them.
- **Timings** (Node tab): waiting for a worker, drawing, texture upload, archive (and its queue),
  asked -> ready.
- **Drawing:** round points use a per-width coverage table with inside / outside shortcuts, the
  straight-alpha conversion a lookup table, the spectral cell's envelope is computed once per
  column: about 19% less CPU, same pixels.
- **Replace at Playhead** (inspector, off by default, all layers): a new image replaces the one under
  the header as soon as it is ready, instead of scrolling in from the right edge. The beat under
  the header stays continuous; the image shows (wrapped) until the next one arrives.


## v1 backports from v2 (2026-11-07, GUI only)

Forms Box / Forms GL Box got v2's GUI and usability work. Output is unchanged: no request,
render or file format changed; anything that could change what plays is off by default.
- **Design system** (`formsTheme.h`): the approved mockups' colours, type scale, 4 px spacing,
  one control look, family colours with shapes (pitched square, percussive circle, texture
  diamond, effects triangle, stacks a bar). Pushed around the window only. System tab: **Classic
  look** brings back the old look (saved with the patch).
- **Undo / redo** (`formsUndo.h`; top bar, Cmd-Z / Cmd-Shift-Z): scenes, presets, setlist, library.
  A step is taken when an edit ends (a slider drag is one step). Undone edits come back unsaved;
  scenes / presets that appear or disappear are written / removed on disk at once.
  **Compare with saved**: per unsaved item, what differs from its file, and Revert.
- **Perform tab** (`formsPerform.h`, `formsEditorBackports.h`): scene pads (setlist entries and
  scenes) that queue the scene for the next image (dashed until it reaches the header), with an
  optional hard cut (Hard Cut set just for that change); 8 macro faders, each scaling one
  performance output (x1 = as the scene says, the default; right-click picks the output); 8
  snapshots (scene or setlist position, faders, the scene's performance settings), saved in the
  patch.
- **Switch now** (Perform tab, next to Hard cut; off by default, saved with the patch in
  "formsPerform"): a chosen scene (pad, setlist entry, snapshot, the Next menu, Play this scene,
  a lane row) replaces what plays at once, on every layer. Images of the old scene waiting to
  play are dropped and pending ones cancelled; the new scene's image is asked for on every layer
  (forced) and replaces the one under the playhead as soon as it is ready (Replace at Playhead for
  that image only, `Schedule::Layer::replaceNext`), then scrolling goes on as usual. The beat
  count stays continuous; a layer the new scene lacks empties. `Engine::switchNow`,
  `formsBox::switchNowRun`. **Render next now** (Play tab) also switches like this when the Next
  scene differs from the one playing (else it asks for the next image, as before).
  Off: nothing changes. Test: `clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_switch_now.cpp && [SWITCH=1] [REPEAT=1] ./a.out <renderer copy> [render s]`.
- **Assistant tab** (`formsAssistantV1.h`): new / modified instrument presets and scenes from text
  (ofxOceanodeAIAssistant, Requesty). The system prompt is written from v1's schema
  (generators.json, library, presets); answers are validated (one retry with the problems,
  rejected answers shown). A proposal is auditioned before anything changes: presets with
  Audition (Forms GL Box) or the preview, scenes with a preview render; Keep is one undo step,
  Discard leaves everything as it was. Inactive without a Requesty key.
- **Typed harmony** (Scenes tab, Harmony): `I vi IV V`, `I:2 V:2`. A frozen layer takes it as
  its roots per bar (transpose 0); other layers can take its degree chances (v1 draws degrees per
  bar by chance, there is no per-bar harmony for them). Nothing changes until a button is pressed.
- **Frame time** (System tab): frame time, render queue, the pipeline's queue / render / upload /
  archive times; drawn only when open.
- **Background colour** (Post-processing, After the sound > Image; `formsBackground.h`, `applyBackground`
  in `formsBox.h`): Output = post(composite) + background x (1 - coverage), coverage = the layers'
  alpha stacked "over", given the same stretch and header opacity as the image (the image fades
  into the background). Levels and the header line act on the image only; the background is added
  last. Default black: the pass is skipped, Output exactly as before. Output only: the sound reads
  the layer buffers, unchanged. Background R / G / B are publishable, saved with the patch.
- **Paper view** (same place, off by default): each Output pixel keeps its hue and saturation and
  its HSL lightness is inverted (L' = 1 - L: a bright blue line becomes deep blue, not orange),
  drawn by its coverage over the background (white while the background is black). Display
  only, after post; the sound is untouched. Off: Output as before.
  Test: `clang++ -std=c++17 -I src/formsBox tests/test_background.cpp && ./a.out`.
- **Edge feathers** (Post tab, under the background; off by default): Left / Right / Top /
  Bottom each have a width (fraction of the screen, 0 = off) and a curve (exponent after a
  smoothstep: 1 smooth, >1 harder, <1 softer); publishable as "Feather Left", "Feather Left
  Curve", ... and saved with the patch. The very last pass, after stretch, colour, levels, the
  header line and the background: the whole picture fades toward the background colour (white
  in paper view with a black background, black when none). All widths 0: the pass is skipped,
  Output bit-identical; the sound reads the layer buffers and never sees it.
  Maths in `src/formsBox/formsFeather.h`, shader in `forms::Post::feather` (`formsPost.h`).
  Another node reuses it with: `post.edges.width[e] / curve[e]` (e: 0 left, 1 right, 2 top,
  3 bottom; or `PostControls::apply`), then
  `tex = post.process(src, headerX, opacity, dt); /* own background */ tex = post.feather(*tex, bgColour);`
  (`feather` returns its input untouched when all widths are 0).
  Test: `clang++ -std=c++17 -I src/formsBox tests/test_feather.cpp && ./a.out`.

Test: `clang++ -std=c++17 -I src/formsBox -I <OF>/libs/json/include tests/test_v1_backports.cpp && ./a.out <renderer copy>`
(undo / redo incl. created scenes, compare, faders' identity, snapshots save / load, the
harmony parser, the assistant's validation).
