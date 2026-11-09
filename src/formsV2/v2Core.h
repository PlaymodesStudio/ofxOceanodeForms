//
//  v2Core.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The contract shared by every part of v2 (ROADMAP sections 5-6): units, seeds, harmony in scale
//  degrees, phrases, events, layer views, and the interfaces the schema, sequencer, modulation and
//  drawers implement. Plain C++17 and nlohmann::json only (no openFrameworks), so every part can be
//  built and tested headless. v1 (Forms Box / Forms GL Box, src/formsBox, src/formsGL) does not use
//  any of this and is not changed by it.
//
//  Units (ROADMAP 5.3): time in beats (double), pitch in scale degrees (int) or, for continuous
//  material (bands, glissandi, sweeps, textures, partials), height 0..1 from the bottom; colour by
//  role. A layer maps beats to pixels: x = headerX + (beat - nowBeat) * pxPerBeat * depth.
//

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace forms2 {

using json = nlohmann::json;
using Beat = double;

// ---------------------------------------------------------------- deterministic randomness
// splitmix64: small, fast, the same on every platform. Never std::random distributions (their
// output differs between standard libraries).
struct Rng {
    uint64_t s = 0x9E3779B97F4A7C15ull;
    explicit Rng(uint64_t seed = 0) : s(seed ^ 0x9E3779B97F4A7C15ull) {}
    uint64_t next(){
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    double uniform(){ return (next() >> 11) * (1.0 / 9007199254740992.0); }      // [0, 1)
    double uniform(double lo, double hi){ return lo + (hi - lo) * uniform(); }
    int below(int n){ return n <= 0 ? 0 : (int)(uniform() * n); }                   // [0, n)
    bool chance(double percent){ return uniform() * 100.0 < percent; }
};

// Nested seeds (ROADMAP 5.1 chance model): a child seed depends only on its parent and its own
// tag / index, so adding a random decision somewhere never shifts any other.
inline uint64_t mixSeed(uint64_t parent, uint64_t a, uint64_t b = 0){
    Rng r(parent ^ (a * 0xD6E8FEB86659FD93ull) ^ (b * 0xA0761D6478BD642Full));
    r.next();
    return r.next();
}
inline uint64_t hashTag(const std::string& s){   // FNV-1a
    uint64_t h = 1469598103934665603ull;
    for(unsigned char c : s){ h ^= c; h *= 1099511628211ull; }
    return h;
}
// Separate streams, so rhythm and pitch (and the rest) can be kept or varied independently
enum class Stream : uint64_t { Rhythm = 1, Pitch = 2, Look = 3, Mod = 4, Chance = 5, Phrase = 6 };
inline uint64_t streamSeed(uint64_t seed, Stream s){ return mixSeed(seed, (uint64_t)s, 0x5EED); }

// ---------------------------------------------------------------- harmony (ROADMAP 5.9)
// Scene-level, shared by every instrument; pitch never leaves the scale. Degrees are integers:
// 0 = the tonic, scale.size() = the tonic an octave up, negatives go down. Nothing in v2 transposes
// by semitones: chords, arps, walks and modulation move by degrees. The one exception is a step's
// keyShift: it moves the scale's tonic (the whole scale), so notes still stay inside the scale.

// Tuning: how a degree becomes a pitch. Equal temperament by default; a microtonal / Scala tuning
// gives each scale step in cents (steps.size() == scale size, period usually 1200 cents).
struct Tuning {
    std::vector<double> cents;        // empty = 12-TET from Scale::semis
    double periodCents = 1200;
};

struct Scale {
    std::string name;
    std::vector<int> semis{0, 2, 4, 5, 7, 9, 11};   // pitch set within the octave (12-TET)
    int tonic = 0;                    // semitone offset of the tonic (the key)
    Tuning tuning;
    int size() const { return std::max<int>(1, (int)(tuning.cents.empty() ? semis.size() : tuning.cents.size())); }
    // pitch of a degree in (fractional) semitones from the reference pitch
    double semitonesOf(int degree) const {
        int n = size(), o = (int)std::floor((double)degree / n), d = degree - o * n;
        if(!tuning.cents.empty()) return tonic + (o * tuning.periodCents + tuning.cents[(size_t)d]) / 100.0;
        return tonic + o * 12 + semis[(size_t)d];
    }
};

// Chord colour of a progression step, inside the scale (extra degree stacked on the triad)
enum class ChordColour : int { Triad = 0, Add7 = 1, Add9 = 2, Sus2 = 3, Sus4 = 4 };
// degree offsets from the chord root for a colour (stacks of scale steps: 0 2 4 = the scale's triad)
inline std::vector<int> chordDegrees(ChordColour c){
    switch(c){
        case ChordColour::Add7: return {0, 2, 4, 6};
        case ChordColour::Add9: return {0, 2, 4, 8};
        case ChordColour::Sus2: return {0, 1, 4};
        case ChordColour::Sus4: return {0, 3, 4};
        default: return {0, 2, 4};
    }
}

struct ProgressionStep {
    int degree = 0;                   // root degree (0-based: typed "1" = degree 0)
    double length = 4;                // beats (typed lengths are in bars: "6:2" = degree 5 for 8 beats)
    double lengthMax = -1;            // > length: random length in [length, lengthMax], drawn per pass
    ChordColour colour = ChordColour::Triad;
    int keyShift = 0;                 // semitones added to the section tonic for this step (typed "IV+2")
};

// Rules generator (optional source): probability of moving from each degree to each degree, a
// cadence forced at phrase / section ends, and a tension control (prefers far / unstable degrees)
struct HarmonyRules {
    std::vector<std::vector<double>> table;   // [from][to], scale-size square; empty = uniform
    int cadenceDegree = 0;            // forced at section ends (-1: none)
    double tension = 0.3;             // 0..1
    double stepBeats = 4;
};

// Time signature (ROADMAP 5.10): numerator / denominator and an optional grouping (7/8 = 2+2+3).
// Time is counted in beats of the denominator's unit; a bar is `num` beats. Strong beats are the
// first beat of each group.
struct Meter {
    int num = 4, den = 4;
    std::vector<int> grouping;        // empty = one group per beat (4/4: 1+1+1+1)
    double swing = 0;                 // 0..1: shuffle of even subdivisions
    double barBeats() const { return std::max(1, num); }
    // is the beat offset within the bar the start of a group (a strong beat)?
    bool strong(int beatInBar) const {
        if(grouping.empty()) return beatInBar == 0;
        int b = 0; for(int g : grouping){ if(beatInBar == b) return true; b += g; } return false;
    }
};

// A section: its own meter, scale, tonic and progression; sections follow each other (the last
// loops); changes are quantised to bars
struct HarmonySection {
    std::string name;
    Meter meter;
    Scale scale;
    std::string source = "typed";     // typed | timeline | rules
    std::vector<ProgressionStep> steps{ProgressionStep{}};
    HarmonyRules rules;
    double lengthBeats = 0;           // 0: one pass of its steps; else its length (steps cycle), whole bars
};

// What the harmony is at a beat
struct HarmonyAt {
    const Scale* scale = nullptr;
    int root = 0;                     // root degree
    ChordColour colour = ChordColour::Triad;
    int keyShift = 0;                 // step key shift: effective tonic = scale->tonic + keyShift
    double stepStart = 0, stepEnd = 4;
    int section = 0;
    // pitch of a degree in the step's key (semitones from the reference)
    double semitonesOf(int degree) const { return (scale ? scale->semitonesOf(degree) : 0) + keyShift; }
};

// Live override (Perform mode): hold the current chord, or jump to a degree
struct HarmonyOverride { bool hold = false; int jumpDegree = -1000; };

// How an instance follows the harmony (ROADMAP 5.9)
enum class Follow : int { Chord = 0, ScaleOnly = 1, Pedal = 2, Off = 3 };

// The harmony timeline: sections of steps, quantised to bars. Random step lengths and the rules
// source are resolved by the sequencer side (v2Harmony.h) into a concrete timeline per pass, from
// the scene seed; this struct is the authored description.
struct Harmony {
    std::vector<HarmonySection> sections{HarmonySection{}};
    HarmonyOverride override_;
    const Scale& scale(int section = 0) const { return sections[(size_t)std::clamp(section, 0, (int)sections.size() - 1)].scale; }
};

// Pitch space of the screen: semitones from the reference pitch to height 0..1. lowSemi at the
// bottom, highSemi at the top (v1's notegrid mapped the same way: one row per step).
struct PitchSpace {
    double lowSemi = -24, highSemi = 60;
    double heightOf(double semis) const { return (semis - lowSemi) / std::max(1e-9, highSemi - lowSemi); }
    double semisOf(double height) const { return lowSemi + height * (highSemi - lowSemi); }
};

// ---------------------------------------------------------------- phrases and events
// A phrase: the span an instance renders before it varies (ROADMAP 5).
struct Phrase {
    int index = 0;                    // 0, 1, 2 ... for this instance
    Beat start = 0, length = 16;
    uint64_t seed = 0;                // the phrase seed (keep / mutate / new act on it)
    Beat end() const { return start + length; }
};

// What an event looks like on screen; the drawers implement each kind (v2Draw*.h)
enum class EventKind : int {
    Note = 0,       // pitched note (with partials / additive / ribbon look)
    Hit = 1,        // percussive cell (kick, snare, noise band, block, gradient, taiko, speckle, dot...)
    Mass = 2,       // continuous line / glissando between two heights
    Sweep = 3,      // band whose bottom / top / level move from start to end
    Shape = 4,      // free geometry (line, rect, ellipse, triangle, curve, letter...)
    Effect = 5      // an effect slot on a layer bus (reverse, repeat, freeze, band, fade...)
};

struct Event {
    uint64_t id = 0;                  // unique within a scene run (instance, phrase, index)
    std::string instance;             // instance ID in the scene (ROADMAP 5.4: unique per scene)
    int layer = 0;                    // 0..3
    EventKind kind = EventKind::Note;
    std::string look;                 // the cell / look / effect name within its kind
    Beat start = 0, length = 1;       // beats
    // pitch: a note's scale degree (absolute, harmony already applied), or continuous heights
    bool pitched = true;
    int degree = 0;
    double height = 0.5, height2 = 0.5;   // continuous: from height to height2 (glissando / band top)
    double level = 1;                 // 0..1
    double depth = 1;                 // per-event depth: a multiplier on the layer's zoom (5.3)
    std::string colourRole = "neutral";
    uint64_t seed = 0;                // the event's own seed (per-event modulation, look details)
    bool transient = false;           // has a sharp attack (drum voices, sound transient path)
    std::map<std::string, double> p;  // look parameters after modulation (thickness, partials...)
    double param(const std::string& k, double def) const { auto f = p.find(k); return f == p.end() ? def : f->second; }
};

// ---------------------------------------------------------------- layers on screen
// How one layer maps beats to the screen. Every event of the layer is drawn at
// x = headerX + (beat - nowBeat) * pxPerBeat * depth, so it crosses the header exactly on its beat.
struct LayerView {
    int width = 1920, height = 1080;
    double headerX = 960;             // playhead position in px
    double nowBeat = 0;               // the beat under the playhead
    double pxPerBeat = 60;            // the layer's zoom (beats on screen = width / pxPerBeat)
    double xOf(Beat b, double depth = 1) const { return headerX + (b - nowBeat) * pxPerBeat * depth; }
    double yOf(double height01) const { return (1.0 - height01) * height; }   // 0 = bottom
    // beats ahead of the playhead that are visible at a depth (the look-ahead rule, 5.4)
    double beatsAhead(double depth = 1) const { return (width - headerX) / std::max(1e-9, pxPerBeat * depth); }
};

// ---------------------------------------------------------------- the schema (contract only;
// the registry's contents are in v2Schema.h)
// Modulation levels (ROADMAP 5.3): when a source is read and what it applies to
enum Level : unsigned { LevelEvent = 1, LevelPhrase = 2, LevelLayer = 4, LevelScene = 8 };

struct ParamDef {
    std::string key, label, group, help;
    std::string type = "float";       // float, int, bool, enum, intList, floatRange, intRange
    double min = 0, max = 1;
    json def;                         // default value
    std::vector<std::string> options; // enum
    unsigned levels = LevelPhrase;    // levels a modulation routing to it may use
    Level defaultLevel = LevelPhrase;
    bool main = false;                // on the instrument's main page (else "advanced")
    bool degrees = false;             // pitch in degrees: modulation is quantised to degrees
    std::string unit;                 // "beats", "degrees", "0-1", "%", "x"...
};

struct TypeDef {
    std::string name;                 // "bass", "melody", ... (ROADMAP 5.1: the 14 roles)
    std::string label, family;        // family: pitched, percussive, texture, effects
    EventKind kind = EventKind::Note;
    std::vector<ParamDef> params;     // look + type-specific parameters
    const ParamDef* param(const std::string& k) const { for(auto& p : params) if(p.key == k) return &p; return nullptr; }
};

// ---------------------------------------------------------------- instances and scenes (runtime)
// A modulation routing (one cell of the matrix)
struct Routing {
    std::string source;               // "lfo1", "random", "phraseEnv", "macro1", "follow:<id>:density", "lane:<name>"...
    std::string dest;                 // a parameter key: "look.thickness", "seq.density", "depth", "layer.zoom"...
    double amount = 0;                // -1..1 of the destination's range (or of [min, max] if set)
    Level level = LevelPhrase;
    double min = 0, max = 1;          // optional range override (used when hasRange)
    bool hasRange = false;
    double curve = 1;                 // 1 linear, >1 exponential
    bool invert = false;
    double smooth = 0;                // beats (per-layer routings glide)
    json sourceParams;                // e.g. {"shape":"sine","rateBeats":4} for an LFO
};

// An instance of an instrument in a scene: resolved parameters + routings (see v2Format.h for files)
struct Instance {
    std::string id;                   // unique in the scene
    std::string type;                 // TypeDef name
    std::string preset;               // preset it is based on (name, version) — informational
    int layer = 0;
    json params = json::object();     // look / type parameters (keys of TypeDef::params)
    json seq = json::object();        // sequencer settings (keys of the sequencer schema)
    json phrase = json::object();     // {"length":[8,16] or 16, "quant":1, "variation":{...}, "restartAtSections":false}
    Follow follow = Follow::Chord;    // how it follows the harmony (5.9)
    int pedalDegree = 0;              // Follow::Pedal
    double accent = 0;                // 0..1: louder / likelier on the meter's strong beats (5.10)
    double swing = -1;                // -1: the section's swing
    std::vector<Routing> routings;
    bool muted = false, locked = false;
};

struct LayerDef { double zoomBeats = 16; std::string blend = "lighten"; double gain = 1; };

struct Scene {
    std::string name;
    int version = 1;
    double bpm = 120;
    Harmony harmony;
    std::vector<LayerDef> layers{LayerDef{}};
    std::vector<Instance> instances;
    std::vector<Routing> routings;    // scene-level (macros -> knobs, followers)
    std::map<std::string, double> macros;
    uint64_t seed = 1;
};

} // namespace forms2
