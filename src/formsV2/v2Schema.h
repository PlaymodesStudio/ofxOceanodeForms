//
//  v2Schema.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The single schema registry (ROADMAP 5.3 "one schema"): the 14 instrument types of 5.1 with their
//  look / type parameters, the event-level destinations every type shares (depth, size, brightness,
//  pitchFine, height), the layer and scene parameters, and the effect types of Time FX / Filter FX.
//  The editor, the renderer, the modulation matrix and the file format (v2Format.h) all read this.
//
//  Destination keys (what a Routing::dest may be, inside an instance):
//    look.<key>     a parameter of the instance's type (also accepted bare: "<key>")
//    depth, size, brightness, pitchFine, height    per-event destinations shared by every type
//    layer.<key>    the instance's layer (zoom, blend, gain)
//    scene.<key>    the scene (bpm, pitchBalance, macro1..macro8)
//    seq.<key>      the shared sequencer; owned by v2Sequencer.h, not defined here. A resolver can
//                   be installed with setSequencerSchema() so validation can check those keys too.
//
//  Levels follow 5.3: event depth / size / brightness: event (default), phrase, layer; layer zoom /
//  gain / effect amounts: layer or phrase; structure (look, cell, effect choice, modes): phrase or
//  scene only, so a pattern never changes mid-phrase.
//
//  Render option of a routing (requested 2026-11-07): "drawn" (default: baked into the event's shape
//  along its time, e.g. vibrato as a wavy line, tremolo as pulsing brightness) or "animated" (the
//  element moves / fades in real time every frame). Animated only makes sense for event-level
//  continuous destinations: level, pitchFine, depth, size, brightness, height (see allowsAnimated()).
//  It is stored and validated by v2Format.h.
//
//  Header-only, plain C++17 + nlohmann::json.
//

#pragma once

#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "v2Core.h"

namespace forms2 {

// level masks used below
static constexpr unsigned kEPL = LevelEvent | LevelPhrase | LevelLayer;   // event-ish visual values
static constexpr unsigned kEP  = LevelEvent | LevelPhrase;                // read when an event is made
static constexpr unsigned kPL  = LevelPhrase | LevelLayer;                // layer amounts, effect amounts
static constexpr unsigned kPS  = LevelPhrase | LevelScene;                // structure
static constexpr unsigned kSL  = LevelScene | LevelLayer;                 // scene values that may glide

namespace schema_detail {

// a numeric parameter
inline ParamDef num(const std::string& key, const std::string& label, double mn, double mx, json def,
                    const std::string& unit, unsigned levels, Level defLevel, bool main,
                    const std::string& group, const std::string& help, bool isInt = false){
    ParamDef p;
    p.key = key; p.label = label; p.group = group; p.help = help;
    p.type = isInt ? "int" : "float";
    p.min = mn; p.max = mx; p.def = def; p.unit = unit;
    p.levels = levels; p.defaultLevel = defLevel; p.main = main;
    return p;
}
inline ParamDef integer(const std::string& key, const std::string& label, int mn, int mx, int def,
                        const std::string& unit, unsigned levels, Level defLevel, bool main,
                        const std::string& group, const std::string& help){
    return num(key, label, mn, mx, def, unit, levels, defLevel, main, group, help, true);
}
// a choice: structure, so phrase or scene only
inline ParamDef choice(const std::string& key, const std::string& label, std::vector<std::string> opts,
                       const std::string& def, bool main, const std::string& group, const std::string& help){
    ParamDef p;
    p.key = key; p.label = label; p.group = group; p.help = help;
    p.type = "enum"; p.options = std::move(opts); p.def = def;
    p.min = 0; p.max = (double)p.options.size() - 1;
    p.levels = kPS; p.defaultLevel = LevelPhrase; p.main = main;
    return p;
}
inline ParamDef toggle(const std::string& key, const std::string& label, bool def, bool main,
                       const std::string& group, const std::string& help){
    ParamDef p;
    p.key = key; p.label = label; p.group = group; p.help = help;
    p.type = "bool"; p.def = def; p.min = 0; p.max = 1;
    p.levels = kPS; p.defaultLevel = LevelPhrase; p.main = main;
    return p;
}

using List = std::vector<ParamDef>;
inline void add(List& to, const List& from){ to.insert(to.end(), from.begin(), from.end()); }

// level + colour role: every drawing type (effects get level only, as their wet amount)
inline List commonMain(bool colour){
    List l{ num("level", "Level", 0, 1, 0.8, "0-1", kEPL, LevelEvent, true, "main",
                "How bright the instrument is drawn (and so how loud it sounds).") };
    if(colour) l.push_back(choice("colourRole", "Colour", {"bass", "accent", "neutral", "highlight", "shadow"},
                                  "neutral", true, "main", "Colour role from the scene palette."));
    return l;
}

// cover below (5.7): erase what lies under the instrument on the layers below
inline List cover(){
    return {
        toggle("coverEnable", "Cover below", false, false, "cover", "Erase the layers below where this instrument is drawn."),
        num("coverDilate", "Cover grow", 0, 32, 2, "px", kPL, LevelPhrase, false, "cover", "Grow the cover shape by this many pixels / rows."),
        num("coverFeather", "Cover feather", 0, 32, 4, "px", kPL, LevelPhrase, false, "cover", "Soften the cover edge."),
        num("coverRelease", "Cover release", 0, 8, 0.25, "beats", kPL, LevelPhrase, false, "cover", "The cover persists after the shape and fades over this time."),
        choice("coverMode", "Cover mode", {"eraser", "black"}, "eraser", false, "cover", "Eraser makes the layers below transparent; black paints them black."),
        num("coverAmount", "Cover amount", 0, 1, 1, "0-1", kPL, LevelPhrase, false, "cover", "How strongly the layers below are ducked."),
        choice("coverLayers", "Cover acts on", {"allBelow", "layerBelow", "all"}, "allBelow", false, "cover", "Which layers the cover ducks."),
    };
}

// blob masks (5.12): soft organic blobs that shape what the instrument draws, placed in beats
inline List blob(bool forEffect){
    List l{
        choice("blob", "Blob mask", {"off", "ghost", "fish", "microbe", "cloud", "tear", "custom"}, "off", true, "blob",
               "Mask the drawing with blobs; a preset sets the shape (custom uses the shape values below)."),
        num("blobAmount", "Blob amount", 0, 1, 1, "0-1", kPL, LevelPhrase, true, "blob", "How strongly the mask acts."),
        choice("blobPlacement", "Blob placement", {"zone", "partials", "fullHeight", "perlin", "events"}, "zone", true, "blob",
               "Random zone, along the partials, full height for a width in beats, Perlin field, or one per event."),
        choice("blobMode", "Blob mode", {"in", "out"}, "in", false, "blob", "In: show only inside the blobs. Out: blobs are holes."),
        choice("blobScope", "Blob scope", {"element", "phrase", "general"}, "phrase", false, "blob",
               "Per element (each event its own blobs), per phrase (one set per phrase), general (one continuous field)."),
        integer("blobCount", "Blob count", 1, 32, 3, "", kPS, LevelPhrase, false, "blob", "Blobs per element / phrase / 16 beats."),
        num("blobScale", "Blob scale", 0.1, 8, 1, "x", kEP, LevelPhrase, false, "blob", "Multiplier on the blob size (presets too)."),
        num("blobSizeBeats", "Blob length", 0.05, 32, 2, "beats", kEP, LevelPhrase, false, "blob", "Custom: blob length in beats."),
        num("blobSizeHeight", "Blob height", 0.01, 1.2, 0.3, "0-1", kEP, LevelPhrase, false, "blob", "Custom: blob height (fraction of the screen)."),
        num("blobAspect", "Blob aspect", 0.2, 5, 1, "x", kEP, LevelPhrase, false, "blob", "Custom: longer (>1) or taller (<1)."),
        num("blobRotation", "Blob rotation", -180, 180, 0, "deg", kEP, LevelPhrase, false, "blob", "Custom: rotation."),
        num("blobIrregularity", "Irregularity", 0, 1, 0.3, "0-1", kEP, LevelPhrase, false, "blob", "Custom: noise deformation of the contour."),
        num("blobDetail", "Detail", 0.5, 16, 2, "x", kEP, LevelPhrase, false, "blob", "Custom: noise frequency (smooth ghost to spiky microbe)."),
        integer("blobOctaves", "Detail octaves", 1, 5, 2, "", kPS, LevelPhrase, false, "blob", "Custom: noise octaves."),
        num("blobTail", "Tail", 0, 1, 0, "0-1", kEP, LevelPhrase, false, "blob", "Custom: asymmetric taper along time (fish, comet)."),
        num("blobSoftness", "Blob softness", 0, 1, 0.3, "0-1", kEP, LevelPhrase, false, "blob", "Custom: edge feather."),
        num("blobZoneLow", "Zone bottom", 0, 1, 0, "0-1", kPL, LevelPhrase, false, "blob", "Random zone: lowest blob centre."),
        num("blobZoneHigh", "Zone top", 0, 1, 1, "0-1", kPL, LevelPhrase, false, "blob", "Random zone: highest blob centre."),
        num("blobThreshold", "Field threshold", 0, 1, 0.55, "0-1", kPL, LevelPhrase, false, "blob", "Perlin field: higher = fewer, smaller islands."),
        num("blobWobble", "Wobble", 0, 1, 0, "0-1", kPL, LevelPhrase, false, "blob", "Animated: the contour wobbles as it travels."),
        num("blobWobbleRate", "Wobble rate", 0.05, 16, 2, "beats", kPL, LevelPhrase, false, "blob", "Animated: beats per wobble."),
    };
    if(forEffect) l[0].main = false;   // the effect choice "blob" is the switch there
    return l;
}

// synthesised drum voice (5.7), percussive types only
inline List drumVoice(double pitchStart, double pitchEnd, double decay, double noise){
    return {
        num("drumLevel", "Drum level", 0, 1, 0.8, "0-1", kEP, LevelEvent, false, "drum voice", "Level of the drum voice."),
        num("drumPitchStart", "Pitch start", 20, 12000, pitchStart, "Hz", kEP, LevelEvent, false, "drum voice", "Pitch at the attack."),
        num("drumPitchEnd", "Pitch end", 20, 12000, pitchEnd, "Hz", kEP, LevelEvent, false, "drum voice", "Pitch the sweep falls to."),
        num("drumDecay", "Drum decay", 0.005, 4, decay, "s", kEP, LevelEvent, false, "drum voice", "Amplitude decay time."),
        num("drumClick", "Click", 0, 1, 0.3, "0-1", kEP, LevelEvent, false, "drum voice", "Short click at the attack."),
        num("drumNoise", "Noise", 0, 1, noise, "0-1", kEP, LevelEvent, false, "drum voice", "Noise burst mixed in."),
        num("drumDrive", "Drive", 0, 1, 0.1, "0-1", kEP, LevelEvent, false, "drum voice", "Saturation."),
        num("scAmount", "Sidechain amount", 0, 1, 0.6, "0-1", kPL, LevelPhrase, false, "drum voice", "Spectral sidechain: how much the drum ducks the same frequencies."),
        num("scAttack", "Sidechain attack", 0.1, 100, 2, "ms", kPL, LevelPhrase, false, "drum voice", "Sidechain attack."),
        num("scRelease", "Sidechain release", 5, 2000, 120, "ms", kPL, LevelPhrase, false, "drum voice", "Sidechain release."),
    };
}
inline ParamDef drumVoiceMode(){
    return choice("drumVoice", "Drum voice", {"off", "mix", "sidechain"}, "off", true, "main",
                  "Off: heard only through the drawing. Mix: added on top. Sidechain: ducks the drawing's spectrum.");
}

// note look shared by the pitched types: 7 main parameters (look + 6) and an advanced set
inline List noteMain(const std::string& look, double thickness, int partials, double release){
    return {
        choice("look", "Look", {"note", "additive", "ribbon"}, look, true, "main", "Note with partials, additive stack or ribbon."),
        num("thickness", "Thickness", 1, 80, thickness, "px", kEP, LevelPhrase, true, "main", "Height of the note line."),
        integer("partials", "Partials", 0, 160, partials, "", kEP, LevelPhrase, true, "main", "Overtones drawn above the note (its timbre); per note with an event-level routing."),
        num("partialDecay", "Partial decay", 0, 1, 0, "0-1", kEP, LevelPhrase, true, "main", "How fast higher partials fade."),
        num("attack", "Attack", 0, 4, 0, "beats", kEP, LevelPhrase, true, "main", "Fade-in at the note start."),
        num("release", "Release", 0, 8, release, "beats", kEP, LevelPhrase, true, "main", "Tail after the note ends."),
        num("noteLength", "Note length", 0.05, 4, 1, "x", kEP, LevelPhrase, true, "main", "Multiplier on the sequencer's step length."),
    };
}
inline List noteAdvanced(){
    return {
        num("softness", "Edge softness", 0, 1, 0.1, "0-1", kEP, LevelPhrase, false, "look", "Blur of the note edge."),
        // v1 note shape (5.15): envelope along the note and the partial roll-off
        choice("envShape", "Envelope", {"hold", "decay", "rise", "swell"}, "decay", false, "look", "Brightness along the note: flat, fading out, fading in, or a swell."),
        num("envPower", "Envelope power", 0, 16, 1, "x", kEP, LevelPhrase, false, "look", "Curve of the envelope (0 = flat, 1 = linear, higher = sharper)."),
        num("partialCut", "Partial roll-off", 0, 8, 1, "x", kEP, LevelPhrase, false, "look", "Upper partials fade as (1 - n / partials)^this (0 = all equal)."),
        num("partialSpread", "Partial spread", 0.5, 2, 1, "x", kEP, LevelPhrase, false, "look", "Stretch of the partial series (1 = harmonic)."),
        num("partialThin", "Partial thinning", 0, 1, 0, "0-1", kEP, LevelPhrase, false, "look", "Higher partials drawn thinner."),
        num("shimmer", "Shimmer", 0, 1, 0, "0-1", kEPL, LevelPhrase, false, "look", "Grain flicker along the note."),
        num("partialEnvPower", "Partial envelope power", 0, 64, 0, "x", kEP, LevelPhrase, false, "look", "Higher partials fade faster along the note: the drawn brightness of partial n to the power 1 + n / partials x this (v1 lfo power)."),
        num("alphaStack", "Overlap gain", 1, 8, 1, "x", kEP, LevelPhrase, false, "look", "Brightness 1 - (1 - b)^this: v1 painted each note pixel several times (points wider than their spacing, the detune pair)."),
    };
}

inline List detuneParams(){
    // detuning: the one other exception to scale quantisation; the note stays on its degree, the
    // drawn lines / partials are doubled or thickened around it
    return {
        num("detune", "Detune", 0, 100, 0, "cents", kEPL, LevelPhrase, false, "detune", "Spread of stacked lines around the note."),
        integer("detuneVoices", "Detune voices", 1, 3, 1, "", kPS, LevelPhrase, false, "detune", "Single, double or triple lines per note / partial."),
        num("partialDetune", "Partial detune", 0, 200, 0, "cents", kEPL, LevelPhrase, false, "detune", "Random flattening of each partial (fundamental included), up to this, fixed per note (v1 partial detune)."),
        // musical animations (filter sweep / wah, spectral twinkle): usable with the animated render
        num("partialCount", "Partial count", 0, 1, 1, "0-1", kEPL, LevelPhrase, false, "partials", "Fraction of the type's partials drawn."),
        num("partialBright", "Partial tilt", -1, 1, 0, "-1..1", kEPL, LevelPhrase, false, "partials", "Brightness tilt of the partials: negative dark, positive bright."),
        num("partialChance", "Partial chance", 0, 1, 1, "0-1", kEP, LevelPhrase, false, "partials", "Chance each partial (fundamental included) is drawn, decided per note (v1 random / thin partial modes)."),
    };
}

// effect scope (5.12): layer bus (default), one instance's own drawing, or each of its events
inline List fxScope(){
    ParamDef t; t.key = "fxTarget"; t.label = "Target"; t.group = "scope"; t.type = "string"; t.def = "";
    t.levels = kPS; t.defaultLevel = LevelPhrase;
    t.help = "Instance id the instance / element scope acts on (empty: every instrument on the layer).";
    return {
        choice("fxScope", "Scope", {"layer", "instance", "element", "earlier"}, "layer", false, "scope",
               "Layer bus; an insert on one instance's drawing; each event on its own (echo / repeat as copies); or the layer bus as drawn by the instruments listed before this effect (v1 cell order)."),
        t,
    };
}

// v1 hit cell shape (5.15): row noise and band profile
inline List hitLook(){
    return {
        num("contrast", "Noise contrast", 0.25, 12, 4, "x", kEP, LevelPhrase, false, "look", "Noise rows raised to this power: higher = sparser bright streaks."),
        num("grainSize", "Noise size", 1, 200, 20, "px", kEP, LevelPhrase, false, "look", "Size of the noise rows (px at 1080)."),
        num("bandShape", "Band shape", 0, 8, 1, "x", kEP, LevelPhrase, false, "look", "Block / gradient profile across the band: 0 flat, 1 soft cosine, higher = thinner core."),
    };
}

inline TypeDef makeType(const std::string& name, const std::string& label, const std::string& family,
                        EventKind kind, List params){
    TypeDef t; t.name = name; t.label = label; t.family = family; t.kind = kind; t.params = std::move(params);
    return t;
}

inline TypeDef pitched(const std::string& name, const std::string& label, const std::string& look,
                       double thickness, int partials, double release, List extraMain, List extraAdv){
    List l = commonMain(true);
    add(l, noteMain(look, thickness, partials, release));
    add(l, extraMain);
    add(l, noteAdvanced());
    add(l, extraAdv);
    add(l, detuneParams());
    add(l, blob(false));
    if(name == "bells" || name == "chords") for(auto& p : l) if(p.key == "detune") p.main = true;
    add(l, cover());
    return makeType(name, label, "pitched", EventKind::Note, l);
}

inline TypeDef percussive(const std::string& name, const std::string& label, List main, List adv,
                          double pStart, double pEnd, double decay, double noise){
    List l = commonMain(true);
    add(l, main);
    l.push_back(drumVoiceMode());
    add(l, adv);
    add(l, drumVoice(pStart, pEnd, decay, noise));
    add(l, blob(false));
    add(l, cover());
    return makeType(name, label, "percussive", EventKind::Hit, l);
}

inline TypeDef textureType(const std::string& name, const std::string& label, EventKind kind, List main, List adv){
    List l = commonMain(true);
    add(l, main); add(l, adv); add(l, blob(false)); add(l, cover());
    return makeType(name, label, "texture", kind, l);
}

inline std::vector<TypeDef> buildTypes(){
    std::vector<TypeDef> t;
    // ---- pitched
    t.push_back(pitched("bass", "Bass", "note", 3, 8, 0,
        { num("sub", "Sub line", 0, 1, 0.3, "0-1", kEP, LevelPhrase, true, "main", "A softer line an octave below.") },
        { num("glideLook", "Glide curve", 0, 1, 0.5, "0-1", kPS, LevelPhrase, false, "look", "Shape of drawn glides between notes.") }));
    t.push_back(pitched("melody", "Melody", "note", 2, 12, 0,
        { num("legatoTail", "Legato tail", 0, 2, 0.1, "beats", kEP, LevelPhrase, true, "main", "Joins a note into the next.") },
        { num("headAccent", "Head accent", 0, 1, 0.2, "0-1", kEP, LevelEvent, false, "look", "Brighter note heads.") }));
    t.push_back(pitched("chords", "Chords", "additive", 2, 8, 0,
        { num("bloom", "Bloom", 0, 1, 0.2, "0-1", kEP, LevelPhrase, true, "main", "Held chords bloom into a pad.") },
        { num("voiceFade", "Upper voice fade", 0, 1, 0.2, "0-1", kEP, LevelPhrase, false, "look", "Upper chord voices drawn dimmer.") }));
    t.push_back(pitched("arpeggio", "Arpeggio", "note", 1.5, 8, 0,
        { num("trail", "Trail", 0, 4, 0.5, "beats", kEP, LevelPhrase, true, "main", "Each note leaves a fading trail.") },
        { num("stepAccent", "Step accent", 0, 1, 0.2, "0-1", kEP, LevelEvent, false, "look", "First note of each cycle brighter.") }));
    t.push_back(pitched("bells", "Bells", "additive", 1.5, 16, 0,
        { num("inharmonic", "Inharmonicity", 0, 1, 0.3, "0-1", kEP, LevelPhrase, true, "main", "Bell-like stretched partials."),
          num("ring", "Ring", 0.1, 16, 3, "beats", kEP, LevelPhrase, true, "main", "How long partials ring.") },
        { num("strikeFlash", "Strike flash", 0, 1, 0.3, "0-1", kEP, LevelEvent, false, "look", "Bright flash at the strike.") }));
    // ---- continuous masses (height, not degrees)
    t.push_back(textureType("mass", "Mass", EventKind::Mass,
        { num("lineWidth", "Line width", 0.5, 40, 1.5, "px", kEP, LevelPhrase, true, "main", "Width of each strand."),
          choice("glideShape", "Glide shape", {"linear", "ease", "exp", "sCurve", "snap"}, "ease", true, "main", "How a strand moves between heights (snap: holds, steps in the middle, holds; v1 glide)."),
          num("glideTension", "Glide tension", 0, 0.999, 0.95, "0-1", kEP, LevelPhrase, true, "main", "Snap shape only: 0 a straight glide, toward 1 a sharper step in the middle."),
          num("heightFrom", "Height from", 0, 1, 0.3, "0-1", kEPL, LevelPhrase, true, "main", "Start height."),
          num("heightTo", "Height to", 0, 1, 0.6, "0-1", kEPL, LevelPhrase, true, "main", "End height."),
          num("spread", "Spread", 0, 1, 0.1, "0-1", kEPL, LevelPhrase, true, "main", "Spread of strands around the line."),
          integer("strands", "Strands", 1, 64, 8, "", kPS, LevelPhrase, true, "main", "Number of strands."),
          num("wobble", "Wobble", 0, 1, 0.1, "0-1", kEPL, LevelPhrase, true, "main", "Irregularity of each strand."),
          num("fade", "Fade", 0, 1, 0, "0-1", kEP, LevelPhrase, true, "main", "Fade in and out at the ends.") },
        { num("softness", "Softness", 0, 1, 0.1, "0-1", kEP, LevelPhrase, false, "look", "Blur of the strands."),
          num("strandJitter", "Strand timing", 0, 1, 0.1, "0-1", kEP, LevelPhrase, false, "look", "Strands start at slightly different beats."),
          detuneParams()[0], detuneParams()[1], detuneParams()[2], detuneParams()[3], detuneParams()[4] }));
    // ---- percussive
    t.push_back(percussive("kick", "Kick",
        { choice("cell", "Cell", {"taiko", "block", "gradient", "sine", "ring"}, "taiko", true, "main", "How the hit is drawn."),
          num("width", "Width", 0.05, 8, 0.5, "beats", kEP, LevelEvent, true, "main", "Length of the drawn hit."),
          num("heightLow", "Bottom", 0, 1, 0.0, "0-1", kEP, LevelPhrase, true, "main", "Bottom of the hit."),
          num("heightHigh", "Top", 0, 1, 0.2, "0-1", kEP, LevelPhrase, true, "main", "Top of the hit."),
          num("decay", "Decay", 0.01, 4, 0.1, "beats", kEP, LevelEvent, true, "main", "Drawn fade after the attack."),
          num("sweep", "Pitch sweep", 0, 1, 1, "0-1", kEP, LevelEvent, true, "main", "Drawn downward sweep (heard as the pitch drop).") },
        { num("softness", "Softness", 0, 1, 0.2, "0-1", kEP, LevelPhrase, false, "look", "Blur of the hit."),
          num("grain", "Grain", 0, 1, 0, "0-1", kEP, LevelPhrase, false, "look", "Noise texture inside the hit."),
          hitLook()[0], hitLook()[1], hitLook()[2] },
        150, 45, 0.4, 0.05));
    t.push_back(percussive("snare", "Snare / Noise hit",
        { choice("cell", "Cell", {"noiseBand", "block", "speckle", "gradient", "burst"}, "noiseBand", true, "main", "How the hit is drawn."),
          num("bandLow", "Band bottom", 0, 1, 0.3, "0-1", kEP, LevelPhrase, true, "main", "Bottom of the noise band."),
          num("bandHigh", "Band top", 0, 1, 0.8, "0-1", kEP, LevelPhrase, true, "main", "Top of the noise band."),
          num("width", "Width", 0.05, 8, 0.2, "beats", kEP, LevelEvent, true, "main", "Length of the drawn hit."),
          num("decay", "Decay", 0.01, 4, 0.05, "beats", kEP, LevelEvent, true, "main", "Drawn fade after the attack."),
          num("grain", "Grain", 0, 1, 1, "0-1", kEP, LevelPhrase, true, "main", "Noise density inside the band."),
          num("tone", "Tone", 0, 1, 0.3, "0-1", kEP, LevelEvent, true, "main", "Tuned body under the noise.") },
        { num("softness", "Softness", 0, 1, 0.2, "0-1", kEP, LevelPhrase, false, "look", "Blur of the hit."),
          hitLook()[0], hitLook()[1], hitLook()[2] },
        220, 160, 0.2, 0.7));
    t.push_back(percussive("hats", "Hats / Ticks",
        { choice("cell", "Cell", {"dot", "tick", "speckle", "line", "block"}, "tick", true, "main", "How the hit is drawn."),
          num("heightLow", "Bottom", 0, 1, 0.75, "0-1", kEP, LevelPhrase, true, "main", "Lowest height of a hit."),
          num("heightHigh", "Top", 0, 1, 0.95, "0-1", kEP, LevelPhrase, true, "main", "Highest height of a hit."),
          num("width", "Width", 0.01, 1, 0.06, "beats", kEP, LevelEvent, true, "main", "Length of the drawn hit."),
          num("decay", "Decay", 0.005, 2, 0.08, "beats", kEP, LevelEvent, true, "main", "Drawn fade (closed short, open long)."),
          num("dotSize", "Dot size", 0.5, 40, 3, "px", kEP, LevelEvent, true, "main", "Size of dots / ticks."),
          num("scatter", "Scatter", 0, 1, 0.3, "0-1", kEP, LevelEvent, true, "main", "Random height inside the range per hit.") },
        { num("softness", "Softness", 0, 1, 0.1, "0-1", kEP, LevelPhrase, false, "look", "Blur of the hit."),
          num("metallic", "Metallic", 0, 1, 0.5, "0-1", kEP, LevelPhrase, false, "look", "Drawn ringing lines (FM-like).") },
        8000, 6000, 0.05, 1));
    // ---- textures
    t.push_back(textureType("texture", "Texture", EventKind::Sweep,
        { choice("cell", "Cell", {"noise", "cloud", "grain", "nebula", "stripes"}, "cloud", true, "main", "Kind of texture."),
          num("bandLow", "Band bottom", 0, 1, 0.2, "0-1", kEPL, LevelPhrase, true, "main", "Bottom of the texture band."),
          num("bandHigh", "Band top", 0, 1, 0.7, "0-1", kEPL, LevelPhrase, true, "main", "Top of the texture band."),
          num("density", "Density", 0, 1, 0.5, "0-1", kEPL, LevelPhrase, true, "main", "How filled the band is."),
          num("grainSize", "Grain size", 0.5, 64, 6, "px", kEP, LevelPhrase, true, "main", "Size of grains / clouds."),
          num("softness", "Softness", 0, 1, 0.5, "0-1", kEP, LevelPhrase, true, "main", "Blur."),
          num("drift", "Drift", 0, 1, 0.2, "0-1", kEPL, LevelPhrase, true, "main", "Slow vertical wander."),
          num("fade", "Fade", 0, 1, 0.3, "0-1", kEP, LevelPhrase, true, "main", "Fade in and out at the ends.") },
        { num("contrast", "Contrast", 0, 2, 1, "x", kEP, LevelPhrase, false, "look", "Contrast of the texture.") }));
    t.push_back(textureType("sweep", "Sweep", EventKind::Sweep,
        { choice("shape", "Shape", {"rise", "fall", "arch", "valley", "free"}, "rise", true, "main", "Overall movement."),
          num("bottomFrom", "Bottom from", 0, 1, 0.1, "0-1", kEP, LevelPhrase, true, "main", "Band bottom at the start."),
          num("bottomTo", "Bottom to", 0, 1, 0.5, "0-1", kEP, LevelPhrase, true, "main", "Band bottom at the end."),
          num("topFrom", "Top from", 0, 1, 0.2, "0-1", kEP, LevelPhrase, true, "main", "Band top at the start."),
          num("topTo", "Top to", 0, 1, 0.9, "0-1", kEP, LevelPhrase, true, "main", "Band top at the end."),
          num("levelFrom", "Level from", 0, 1, 0.2, "0-1", kEP, LevelPhrase, true, "main", "Level at the start."),
          num("levelTo", "Level to", 0, 1, 1, "0-1", kEP, LevelPhrase, true, "main", "Level at the end."),
          num("softness", "Softness", 0, 1, 0.4, "0-1", kEP, LevelPhrase, true, "main", "Blur of the band edges.") },
        { num("curve", "Curve", 0.1, 10, 1, "x", kEP, LevelPhrase, false, "look", "Level curve: 1 linear, >1 slow start."),
          num("bandShape", "Band shape", 0, 8, 0, "x", kEP, LevelPhrase, false, "look", "Profile across the band: 0 flat, 1 soft cosine, higher = thinner core.") }));
    t.push_back(textureType("shapes", "Shapes", EventKind::Shape,
        { choice("shape", "Shape", {"line", "rect", "ellipse", "triangle", "curve", "letter"}, "rect", true, "main", "Geometry."),
          toggle("filled", "Filled", true, true, "main", "Filled or outline."),
          num("strokeWidth", "Stroke", 0.5, 40, 2, "px", kEP, LevelPhrase, true, "main", "Outline width."),
          num("shapeLength", "Length", 0.05, 16, 1, "beats", kEP, LevelEvent, true, "main", "Length along time."),
          num("heightCenter", "Height", 0, 1, 0.5, "0-1", kEPL, LevelEvent, true, "main", "Vertical centre."),
          num("heightSize", "Size", 0, 1, 0.2, "0-1", kEPL, LevelEvent, true, "main", "Vertical size."),
          num("rotation", "Rotation", -180, 180, 0, "deg", kEPL, LevelEvent, true, "main", "Rotation."),
          num("softness", "Softness", 0, 1, 0.1, "0-1", kEP, LevelPhrase, true, "main", "Blur.") },
        { choice("letterSet", "Letters", {"latin", "digits", "symbols"}, "latin", false, "look", "Glyph set for the letter shape.") }));
    // ---- effects (layer bus, placed in bars / phrases; no screen-space effects)
    {
        List l = commonMain(false);
        add(l, {
            choice("effect", "Effect", {"reverse", "repeat", "freeze", "stutter", "shuffle", "echo", "transpose", "blank", "tears", "tile"},
                   "repeat", true, "main", "Event effect applied in each slot."),
            num("slotBeats", "Slot", 0.25, 64, 2, "beats", kPS, LevelPhrase, true, "main", "Length of each effect slot."),
            integer("repeats", "Repeats", 1, 16, 2, "", kEP, LevelEvent, true, "main", "Repeat / loop: copies per slot."),
            integer("stutterDiv", "Stutter division", 2, 64, 8, "", kEP, LevelEvent, true, "main", "Stutter: pieces per slot."),
            integer("echoCount", "Echoes", 1, 16, 3, "", kEP, LevelEvent, true, "main", "Echo throws: number of echoes."),
            num("echoDecay", "Echo decay", 0, 1, 0.5, "0-1", kEP, LevelEvent, true, "main", "Echo throws: level kept per echo."),
            integer("transposeDegrees", "Transpose", -14, 14, 2, "degrees", kEP, LevelEvent, true, "main", "Transpose by scale degrees."),
            num("tearAmount", "Tear amount", 0, 1, 0.5, "0-1", kPL, LevelLayer, true, "main", "Tears: displacement amount."),
            num("echoSpacing", "Echo spacing", 0.0625, 4, 0.5, "beats", kEP, LevelEvent, false, "effect", "Echo throws: beats between echoes."),
            integer("shuffleSlices", "Shuffle slices", 2, 32, 4, "", kEP, LevelEvent, false, "effect", "Sequence / shuffle: slices per slot."),
            num("freezeHold", "Freeze hold", 0.25, 16, 2, "beats", kEP, LevelEvent, false, "effect", "Freeze: how long the frozen column holds."),
        });
        add(l, fxScope());
        l[0].levels = kPL; l[0].defaultLevel = LevelLayer;   // wet amount is an effect amount
        for(auto& p : l) if(p.key == "transposeDegrees") p.degrees = true;
        t.push_back(makeType("timefx", "Time FX", "effects", EventKind::Effect, l));
    }
    {
        List l = commonMain(false);
        add(l, {
            choice("effect", "Effect", {"band", "notch", "fadeIn", "fadeOut", "vignette", "gate", "mask", "saturation", "blob", "noise", "page"},
                   "band", true, "main", "Continuous effect on the layer bus."),
            choice("shape", "Amount shape", {"steady", "rise", "fall", "swell", "lfo"}, "steady", true, "main", "Amount across the phrase."),
            num("amount", "Amount", 0, 1, 0.5, "0-1", kPL, LevelLayer, true, "main", "Effect amount."),
            num("bandLow", "Band bottom", 0, 1, 0.2, "0-1", kEPL, LevelLayer, true, "main", "Band / notch / mask bottom (per slot with an event-level routing)."),
            num("bandHigh", "Band top", 0, 1, 0.8, "0-1", kEPL, LevelLayer, true, "main", "Band / notch / mask top (per slot with an event-level routing)."),
            num("feather", "Feather", 0, 1, 0.1, "0-1", kPL, LevelLayer, true, "main", "Softness of edges."),
            num("gateRate", "Gate rate", 0.0625, 8, 0.5, "beats", kPL, LevelPhrase, true, "main", "Gate / tremolo period."),
            num("gateDuty", "Gate duty", 0.05, 0.95, 0.5, "0-1", kPL, LevelLayer, true, "main", "Gate open fraction."),
            num("drive", "Drive", 0, 4, 1, "x", kPL, LevelLayer, false, "effect", "Saturation drive."),
            num("vignetteSize", "Vignette size", 0, 1, 0.3, "0-1", kPL, LevelLayer, false, "effect", "Vignette: portion of each slot faded at its edges."),
            toggle("maskInvert", "Invert mask", false, false, "effect", "Keep outside the band instead of inside."),
            num("noiseScale", "Noise scale", 0.002, 1, 0.2, "0-1", kPL, LevelPhrase, false, "effect", "Noise: grain size (fraction of the height)."),
            num("noiseContrast", "Noise contrast", 0.1, 16, 1, "x", kPL, LevelPhrase, false, "effect", "Noise: power on the noise."),
            num("noiseGain", "Noise gain", 0, 4, 1, "x", kPL, LevelPhrase, false, "effect", "Noise: gain before the power."),
            num("noiseThreshold", "Noise threshold", 0, 1, 0, "0-1", kPL, LevelPhrase, false, "effect", "Noise: hard holes above this (0 = soft)."),
            num("pagePower", "Page power", 0, 16, 1, "x", kPL, LevelPhrase, false, "effect", "Page: raised-cosine window over the slot, to this power."),
        });
        add(l, fxScope());
        add(l, blob(true));
        l[0].levels = kPL; l[0].defaultLevel = LevelLayer;
        t.push_back(makeType("filterfx", "Filter FX", "effects", EventKind::Effect, l));
    }
    return t;
}

} // namespace schema_detail

// ---------------------------------------------------------------- registry
inline const std::vector<TypeDef>& instrumentTypes(){
    static const std::vector<TypeDef> types = schema_detail::buildTypes();
    return types;
}

inline const TypeDef* findType(const std::string& name){
    for(auto& t : instrumentTypes()) if(t.name == name) return &t;
    return nullptr;
}

// Per-event destinations shared by every type (5.3)
inline const std::vector<ParamDef>& eventParams(){
    using namespace schema_detail;
    static const std::vector<ParamDef> l{
        num("depth", "Depth", 0.05, 8, 1, "x", kEPL, LevelEvent, false, "event", "Multiplier on the layer's zoom for each event (it still crosses the playhead on its beat)."),
        num("size", "Size", 0.05, 8, 1, "x", kEPL, LevelEvent, false, "event", "Multiplier on the event's drawn size."),
        num("brightness", "Brightness", 0, 2, 1, "x", kEPL, LevelEvent, false, "event", "Multiplier on the event's level as drawn."),
        // the one exception to the degree rule: fine pitch around the note, the note stays on its degree
        num("pitchFine", "Fine pitch", -200, 200, 0, "cents", kEPL, LevelEvent, false, "event", "Fine pitch around the note (vibrato); the note stays on its degree."),
        num("height", "Height offset", -1, 1, 0, "0-1", kEPL, LevelEvent, false, "event", "Vertical offset of continuous material."),
        // 3D converging paths (5.14): how the element reaches the playhead; display only, the sound
        // reads score space. Right with angle 0 and no curve = flat.
        [](){ auto p = choice("pathKind", "Path", {"right", "top", "bottom", "point", "spiral", "curve"}, "right", false, "3d",
                              "Where the element comes from; it always ends at the playhead on its pitch row, on its beat.");
              p.levels = kEP | LevelScene; return p; }(),
        num("pathAngle", "Path angle", -85, 85, 0, "deg", kEPL, LevelPhrase, false, "3d", "Direction of a straight path (positive: from above)."),
        num("pathCurve", "Path curve", -4, 4, 0, "x", kEPL, LevelPhrase, false, "3d", "Bend of the path (curve uses 1 when 0)."),
        num("pathOriginX", "Path origin X", 0, 1, 0.7, "0-1", kEPL, LevelPhrase, false, "3d", "Point path: origin across the area right of the playhead."),
        num("pathOriginY", "Path origin Y", 0, 1, 0.5, "0-1", kEPL, LevelPhrase, false, "3d", "Point path: origin height (0 = bottom)."),
        num("pathTurns", "Path turns", 0, 8, 1.5, "turns", kEPL, LevelPhrase, false, "3d", "Spiral: turns per screen width of distance."),
        num("pathSpeed", "Path speed", 0.1, 4, 1, "x", kEPL, LevelPhrase, false, "3d", "Multiplier on the distance fed to the path."),
    };
    return l;
}

// Layer parameters (address as layer.<key>)
inline const std::vector<ParamDef>& layerParams(){
    using namespace schema_detail;
    static const std::vector<ParamDef> l{
        num("zoom", "Zoom", 0.5, 128, 16, "beats", kPL, LevelLayer, true, "layer", "Beats across the screen on this layer."),
        choice("blend", "Blend", {"lighten", "add", "normal"}, "lighten", true, "layer", "How the layer is composited."),
        num("gain", "Gain", 0, 2, 1, "x", kPL, LevelLayer, true, "layer", "Layer gain."),
        // light backgrounds (5.18): stored in the scene's "paper" object (v2Draw.h Paper), not in layers[]
        choice("reading", "Reading", {"brightness", "coverage", "darkInk"}, "brightness", false, "layer", "What sounds: brightness (v1), coverage (opacity, any colour) or dark ink (darker = louder). Transparent is silent."),
        choice("inkBlend", "Ink blend", {"layer", "lighten", "add", "normal", "multiply", "darken"}, "layer", false, "layer", "Overrides the blend; multiply / darken for dark inks on light backgrounds."),
        toggle("alphaFromBrightness", "Alpha from brightness", false, false, "layer", "Alpha = brightness, colour at full strength (as v1 stores its images)."),
        num("zoomEase", "Zoom easing", 0, 8, 1, "beats", kPS, LevelPhrase, false, "layer", "Time a zoom change takes."),
        // 3D (5.14): the layer as a plane at a depth; camera adds to the scene's dolly
        toggle("perspective", "Perspective", false, false, "3d", "Perspective on this layer (also on when the scene enables it)."),
        num("planeZ", "Plane depth", 0, 8, 0, "z", kPL | LevelScene, LevelLayer, false, "3d", "Depth of the layer plane (farther recedes faster)."),
        num("camera", "Camera dolly", -4, 4, 0, "z", kPL | LevelScene, LevelLayer, false, "3d", "Camera dolly for this layer, added to the scene's."),
    };
    return l;
}

// Scene parameters (address as scene.<key>)
inline const std::vector<ParamDef>& sceneParams(){
    using namespace schema_detail;
    static const std::vector<ParamDef> l = []{
        std::vector<ParamDef> v{
            num("bpm", "Tempo", 20, 300, 120, "bpm", kSL, LevelScene, true, "scene", "Tempo (the Oceanode transport wins when it drives the clock)."),
            num("pitchBalance", "Pitch balance", 0, 1, 0.5, "0-1", kSL, LevelScene, false, "scene", "0 flat, 1 full equal-loudness curve."),
            // 3D view (5.14), display only; off = flat
            toggle("perspective", "Perspective", false, false, "3d", "Per-event depth becomes z: far elements smaller, nearer the vanishing point, dimmer."),
            num("perspAmount", "Perspective amount", 0, 8, 1, "x", kSL, LevelScene, false, "3d", "How fast elements recede with distance ahead."),
            num("vanishX", "Vanishing X", 0, 1, 0.6, "0-1", kSL, LevelScene, false, "3d", "Vanishing point across the area right of the playhead."),
            num("vanishY", "Vanishing Y", 0, 1, 0.5, "0-1", kSL, LevelScene, false, "3d", "Vanishing point height (0 = bottom)."),
            num("fog", "Fog", 0, 4, 0.5, "x", kSL, LevelScene, false, "3d", "Dimming with distance."),
            num("dof", "Depth of field", 0, 1, 0, "0-1", kSL, LevelScene, false, "3d", "Blur with distance (0 = sharp)."),
            num("camera", "Camera dolly", -4, 4, 0, "z", kSL, LevelScene, false, "3d", "Camera dolly: positive moves toward the planes."),
            num("lookAhead", "3D look-ahead", 1, 8, 4, "x", kPS | LevelScene, LevelScene, false, "3d", "Look-ahead multiplier in perspective (far events converge)."),
        };
        for(int i = 1; i <= 8; ++i)
            v.push_back(num("macro" + std::to_string(i), "Macro " + std::to_string(i), 0, 1, 0, "0-1", kSL, LevelLayer, true, "macros", "Scene performance fader."));
        return v;
    }();
    return l;
}

// Effect types of Time FX / Filter FX (5.1), with the settings each one uses
struct EffectDef {
    std::string name, owner, family;          // owner: timefx | filterfx; family: event | continuous
    std::string help;
    std::vector<std::string> settings;        // keys of the owner type's params
};
inline const std::vector<EffectDef>& effectTypes(){
    static const std::vector<EffectDef> l{
        {"reverse", "timefx", "event", "Play the slot backwards (reads the look-ahead).", {"slotBeats"}},
        {"repeat", "timefx", "event", "Repeat / loop the start of the slot.", {"slotBeats", "repeats"}},
        {"freeze", "timefx", "event", "Hold one column across the slot.", {"slotBeats", "freezeHold"}},
        {"stutter", "timefx", "event", "Chop the slot into fast repeats.", {"slotBeats", "stutterDiv"}},
        {"shuffle", "timefx", "event", "Sequence / shuffle slices of the slot.", {"slotBeats", "shuffleSlices"}},
        {"echo", "timefx", "event", "Echo throws after the slot.", {"slotBeats", "echoCount", "echoDecay", "echoSpacing"}},
        {"transpose", "timefx", "event", "Transpose note events by scale degrees.", {"slotBeats", "transposeDegrees"}},
        {"blank", "timefx", "event", "Blank / drop the slot.", {"slotBeats"}},
        {"tears", "timefx", "event", "Tear and displace the slot.", {"slotBeats", "tearAmount"}},
        {"tile", "timefx", "event", "Squeeze the slot and play it n times (repeats).", {"slotBeats", "repeats"}},
        {"band", "filterfx", "continuous", "Keep a height band.", {"amount", "bandLow", "bandHigh", "feather", "shape"}},
        {"notch", "filterfx", "continuous", "Remove a height band.", {"amount", "bandLow", "bandHigh", "feather", "shape"}},
        {"fadeIn", "filterfx", "continuous", "Fade in across the phrase.", {"amount", "shape"}},
        {"fadeOut", "filterfx", "continuous", "Fade out across the phrase.", {"amount", "shape"}},
        {"vignette", "filterfx", "continuous", "Darken slot edges (in beats, not screen).", {"amount", "vignetteSize", "feather", "shape"}},
        {"gate", "filterfx", "continuous", "Gate / tremolo in beats.", {"amount", "gateRate", "gateDuty", "shape"}},
        {"mask", "filterfx", "continuous", "Mask a band.", {"amount", "bandLow", "bandHigh", "maskInvert", "shape"}},
        {"saturation", "filterfx", "continuous", "Saturate the layer.", {"amount", "drive", "shape"}},
        {"noise", "filterfx", "continuous", "Noise mask: holes from a 2D noise (invert keeps the dark part).", {"amount", "noiseScale", "noiseContrast", "noiseGain", "noiseThreshold", "maskInvert"}},
        {"page", "filterfx", "continuous", "Page window: fade in and out across the slot.", {"amount", "pagePower"}},
        {"blob", "filterfx", "continuous", "Blob mask over the slot (5.12).", {"blobAmount", "blobPlacement", "blobMode", "blobScope", "blobCount", "blobScale"}},
    };
    return l;
}

// ---------------------------------------------------------------- sequencer hook (seq.<key>)
using SeqResolver = std::function<const ParamDef*(const std::string&)>;
inline SeqResolver& sequencerSchema(){ static SeqResolver r; return r; }
inline void setSequencerSchema(SeqResolver r){ sequencerSchema() = std::move(r); }

inline const ParamDef* findIn(const std::vector<ParamDef>& l, const std::string& k){
    for(auto& p : l) if(p.key == k) return &p;
    return nullptr;
}
inline bool startsWith(const std::string& s, const std::string& pre){ return s.compare(0, pre.size(), pre) == 0; }

// Resolve a destination key for a type: look.<key> or bare <key>, shared event keys, layer.<key>
// (also layerN.<key>), scene.<key>, seq.<key> (only when a sequencer resolver is installed).
inline const ParamDef* findParam(const TypeDef* type, const std::string& key){
    if(startsWith(key, "look.")) return type ? type->param(key.substr(5)) : nullptr;
    if(startsWith(key, "scene.")) return findIn(sceneParams(), key.substr(6));
    if(startsWith(key, "seq.")) return sequencerSchema() ? sequencerSchema()(key.substr(4)) : nullptr;
    if(startsWith(key, "layer")){
        size_t dot = key.find('.');
        if(dot != std::string::npos){
            std::string mid = key.substr(5, dot - 5);
            bool ok = mid.empty() || (mid.size() == 1 && mid[0] >= '0' && mid[0] <= '3');
            if(ok) return findIn(layerParams(), key.substr(dot + 1));
        }
    }
    if(auto p = findIn(eventParams(), key)) return p;
    return type ? type->param(key) : nullptr;
}
inline const ParamDef* findParam(const std::string& type, const std::string& key){ return findParam(findType(type), key); }

// Every addressable destination of a type, for the modulation matrix (seq.* not included: owned by
// the sequencer schema)
inline std::vector<std::pair<std::string, const ParamDef*>> destinations(const std::string& typeName){
    std::vector<std::pair<std::string, const ParamDef*>> out;
    if(auto t = findType(typeName)) for(auto& p : t->params) out.push_back({"look." + p.key, &p});
    for(auto& p : eventParams()) out.push_back({p.key, &p});
    for(auto& p : layerParams()) out.push_back({"layer." + p.key, &p});
    for(auto& p : sceneParams()) out.push_back({"scene." + p.key, &p});
    return out;
}

// Destinations a routing may render "animated" (moving every frame) instead of "drawn"
inline bool allowsAnimated(const std::string& dest){
    static const char* keys[] = {"level", "look.level", "pitchFine", "depth", "size", "brightness", "height",
                                 "partialCount", "look.partialCount", "partialBright", "look.partialBright"};
    for(auto k : keys) if(dest == k) return true;
    return false;
}

inline const char* levelName(Level l){
    switch(l){ case LevelEvent: return "event"; case LevelLayer: return "layer"; case LevelScene: return "scene"; default: return "phrase"; }
}

} // namespace forms2
