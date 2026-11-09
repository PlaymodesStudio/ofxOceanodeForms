//
//  v2Draw.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Headless CPU reference renderer (ROADMAP 5.3-5.7, 6): every event is drawn every frame at
//  x = headerX + (beat - nowBeat) * pxPerBeat * depth, into one premultiplied RGBA float canvas per
//  layer; then layer-bus effects (in beat space), cover below, and the compositor (premultiplied,
//  lighten / add / normal with gain). column(x) is what the sonification reads.
//
//  The app draws on the GPU (v2DrawGL.h); this file is the reference the tests check and the
//  definition of what each look means. Plain C++17, no openFrameworks.
//
//  Look parameters are read from Event::p with the schema's defaults (v2Schema.h keys).
//  Modulation: two optional callbacks (Mod::drawn, Mod::animated). Drawn values vary along the
//  event (u = 0..1 within it), animated values are read at the frame's beat and apply to the whole
//  event. When absent the event's static values are used.
//
//  Enum parameters are stored in Event::p as the option index of the schema's list:
//  coverMode eraser 0 / black 1; coverLayers allBelow 0 / layerBelow 1 / all 2; glideShape
//  linear 0 / ease 1 / exp 2 / sCurve 3; filter fx shape steady 0 / rise 1 / fall 2 / swell 3 / lfo 4.
//  The cell / shape / effect name is Event::look. Depth: p["depth"] if set, else Event::depth.
//
//  Blob masks and effect scopes (5.12, v2Blob.h): an event with a blob mask, or an event / instance
//  targeted by an instance- or element-scope effect, is drawn into its own buffer, processed
//  (element effects, then its blob mask), then merged into its instance's buffer (instance inserts)
//  or the layer. Element echo / repeat are event copies made by the engine (skipped here).
//  Retuning (5.13): a note with p retuneStart / retuneBeats / retuneCurve / retuneFrom / retuneTo
//  (semitones) is drawn shifted from its old pitch to its new one over the morph.
//

#pragma once

#include "v2Core.h"
#include "v2Blob.h"
#include <array>
#include <cstdio>
#include <functional>
#include <tuple>

namespace forms2 {
namespace draw {

// ---------------------------------------------------------------- canvas (premultiplied RGBA)
struct Canvas {
    int w = 0, h = 0;
    std::vector<float> px;            // r g b a, premultiplied
    void resize(int W, int H){ w = W; h = H; px.assign((size_t)W * H * 4, 0.f); }
    void clear(){ std::fill(px.begin(), px.end(), 0.f); }
    float* at(int x, int y){ return &px[((size_t)y * w + x) * 4]; }
    const float* at(int x, int y) const { return &px[((size_t)y * w + x) * 4]; }
    bool inside(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    // put a straight colour with coverage a: max (lighten) or add, premultiplied
    void put(int x, int y, const float c[3], float a, bool add){
        if(a <= 0 || !inside(x, y)) return;
        a = std::min(a, 1.f);
        float* p = at(x, y);
        for(int i = 0; i < 3; i++){ float v = c[i] * a; p[i] = add ? std::min(1.f, p[i] + v) : std::max(p[i], v); }
        p[3] = add ? std::min(1.f, p[3] + a) : std::max(p[3], a);
    }
};

// ---------------------------------------------------------------- helpers
inline double clamp01(double x){ return std::clamp(x, 0.0, 1.0); }
inline double lerp(double a, double b, double t){ return a + (b - a) * t; }
inline double smooth01(double t){ t = clamp01(t); return t * t * (3 - 2 * t); }
inline double hash01(uint64_t seed, int64_t a, int64_t b = 0){   // deterministic noise
    return (double)(mixSeed(seed, (uint64_t)a, (uint64_t)b) >> 11) * (1.0 / 9007199254740992.0);
}
// per-partial noise shared bit-exactly with the GPU (pHash in eventFrag): a 32-bit seed from the
// event (the float the instance carries) and a 32-bit integer mix on uints
inline uint32_t partialSeed(uint64_t seed){ float f = (float)hash01(seed, 1); return (uint32_t)(f * 16777216.0f); }
inline float partialHash(uint32_t s, uint32_t k){
    uint32_t h = s ^ (k * 0x9E3779B9u); h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16;
    return (float)(h & 0xFFFFFFu) * (1.0f / 16777216.0f);
}
// smooth 1D value noise
inline double vnoise(uint64_t seed, double x, int64_t ch = 0){
    double f = std::floor(x); double t = smooth01(x - f);
    return lerp(hash01(seed, (int64_t)f, ch), hash01(seed, (int64_t)f + 1, ch), t);
}
// colour roles; dark = the dark-ink palette for light backgrounds (5.18)
inline std::array<float, 3> roleColour(const std::string& r, bool dark = false){
    if(dark){
        if(r == "bass") return {0.42f, 0.16f, 0.05f};
        if(r == "accent") return {0.62f, 0.04f, 0.14f};
        if(r == "highlight") return {0.04f, 0.22f, 0.52f};
        if(r == "shadow") return {0.38f, 0.38f, 0.44f};
        if(r == "black") return {0.f, 0.f, 0.f};
        return {0.06f, 0.06f, 0.08f};
    }
    if(r == "bass") return {1.f, 0.55f, 0.25f};
    if(r == "accent") return {1.f, 0.3f, 0.4f};
    if(r == "highlight") return {0.6f, 0.9f, 1.f};
    if(r == "shadow") return {0.45f, 0.45f, 0.6f};
    if(r == "black") return {0.f, 0.f, 0.f};
    return {1.f, 1.f, 1.f};
}

// ---------------------------------------------------------------- modulation callbacks
struct Mod {
    // drawn: value of dest at position u (0..1) within the event; base = the static value
    std::function<double(const Event&, const std::string& dest, double u, double base)> drawn;
    // animated: value of dest at the frame's beat, for the whole event
    std::function<double(const Event&, const std::string& dest, Beat now, double base)> animated;
};

// ---------------------------------------------------------------- 3D view settings (5.14)
// Display only: the score canvases (and column()) stay flat; display() shows the 3D view.
struct ThreeD {
    bool perspective = false;                 // scene switch
    double amount = 1, vanishX = 0.6, vanishY = 0.5, fog = 0.5, dof = 0, camera = 0, lookAhead = 4;
    std::array<bool, 4> layerPersp{{false, false, false, false}};
    std::array<double, 4> planeZ{{0, 0, 0, 0}}, layerCamera{{0, 0, 0, 0}};
    bool on(int L) const { return perspective || layerPersp[(size_t)std::clamp(L, 0, 3)]; }
    bool any() const { return perspective || layerPersp[0] || layerPersp[1] || layerPersp[2] || layerPersp[3]; }
};
// read from a scene's "view3d" object: scene keys (schema sceneParams 3d group) and
// "layers": [{perspective, planeZ, camera}, ...]
inline ThreeD threeDFromJson(const json& j){
    ThreeD t;
    if(!j.is_object()) return t;
    auto num = [&](const json& o, const char* k, double d){ return o.contains(k) && o[k].is_number() ? o[k].get<double>() : d; };
    auto flag = [&](const json& o, const char* k){ return o.contains(k) && ((o[k].is_boolean() && o[k].get<bool>()) || (o[k].is_number() && o[k].get<double>() != 0)); };
    t.perspective = flag(j, "perspective");
    t.amount = num(j, "perspAmount", t.amount); t.vanishX = num(j, "vanishX", t.vanishX); t.vanishY = num(j, "vanishY", t.vanishY);
    t.fog = num(j, "fog", t.fog); t.dof = num(j, "dof", t.dof); t.camera = num(j, "camera", t.camera); t.lookAhead = num(j, "lookAhead", t.lookAhead);
    if(j.contains("layers") && j["layers"].is_array())
        for(size_t i = 0; i < std::min<size_t>(4, j["layers"].size()); i++){
            const json& l = j["layers"][i];
            if(!l.is_object()) continue;
            t.layerPersp[i] = flag(l, "perspective"); t.planeZ[i] = num(l, "planeZ", 0); t.layerCamera[i] = num(l, "camera", 0);
        }
    return t;
}

// ---------------------------------------------------------------- light backgrounds (5.18)
// Per layer: sonification reading mode, ink blend (overrides the layer's blend), alpha from
// brightness; scene: dark-ink palette. Read from a scene's "paper" object:
//   {"darkInk": true, "layers": [{"reading": "darkInk", "blend": "multiply", "alphaFromBrightness": false}, ...]}
enum Reading { ReadBrightness = 0, ReadCoverage = 1, ReadDarkInk = 2 };
struct Paper {
    bool darkInk = false;                                  // dark-ink palette for the colour roles
    bool hasBackground = false; std::array<float, 3> background{{0, 0, 0}};   // optional display background
    std::array<int, 4> reading{{0, 0, 0, 0}};
    std::array<std::string, 4> blend;                      // "" = the layer's own blend
    std::array<bool, 4> alphaFromBrightness{{false, false, false, false}};
    // the scene's own role colours (top-level "colours", e.g. a converted v1 palette); used on the
    // dark background only (the dark-ink palette keeps its own colours)
    std::map<std::string, std::array<float, 3>> colours;
    bool anyReading() const { for(int r : reading) if(r != ReadBrightness) return true; return false; }
    std::array<float, 3> colourOf(const std::string& role) const {
        if(!darkInk && !colours.empty()){ auto f = colours.find(role.empty() ? std::string("neutral") : role); if(f != colours.end()) return f->second; }
        return roleColour(role, darkInk);
    }
};
// a scene's "colours" object: {"accent": "#FFC000", "bass": [1, 0.5, 0.2], ...}; bad entries are skipped
inline std::map<std::string, std::array<float, 3>> coloursFromJson(const json& j){
    std::map<std::string, std::array<float, 3>> out;
    if(!j.is_object()) return out;
    for(auto it = j.begin(); it != j.end(); ++it){
        const json& v = it.value();
        if(v.is_string()){
            std::string h = v.get<std::string>(); if(!h.empty() && h[0] == '#') h = h.substr(1);
            if(h.size() != 6 || h.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos) continue;
            std::array<float, 3> c; for(int i = 0; i < 3; i++) c[(size_t)i] = std::stoi(h.substr((size_t)i * 2, 2), nullptr, 16) / 255.f;
            out[it.key()] = c;
        } else if(v.is_array() && v.size() == 3 && v[0].is_number() && v[1].is_number() && v[2].is_number()){
            out[it.key()] = {std::clamp(v[0].get<float>(), 0.f, 1.f), std::clamp(v[1].get<float>(), 0.f, 1.f), std::clamp(v[2].get<float>(), 0.f, 1.f)};
        }
    }
    return out;
}
inline const std::vector<std::string>& readingNames(){ static const std::vector<std::string> n{"brightness", "coverage", "darkInk"}; return n; }
inline const std::vector<std::string>& blendNames(){ static const std::vector<std::string> n{"lighten", "add", "normal", "multiply", "darken"}; return n; }
inline int blendIndex(const std::string& b){ auto& n = blendNames(); for(size_t i = 0; i < n.size(); i++) if(n[i] == b) return (int)i; return 0; }
inline Paper paperFromJson(const json& j){
    Paper p;
    if(!j.is_object()) return p;
    auto flag = [&](const json& o, const char* k){ return o.contains(k) && ((o[k].is_boolean() && o[k].get<bool>()) || (o[k].is_number() && o[k].get<double>() != 0)); };
    p.darkInk = flag(j, "darkInk");
    if(j.contains("background") && j["background"].is_array() && j["background"].size() == 3){
        p.hasBackground = true; for(int i = 0; i < 3; i++) p.background[(size_t)i] = j["background"][(size_t)i].is_number() ? j["background"][(size_t)i].get<float>() : 0.f;
    }
    if(j.contains("layers") && j["layers"].is_array())
        for(size_t i = 0; i < std::min<size_t>(4, j["layers"].size()); i++){
            const json& l = j["layers"][i];
            if(!l.is_object()) continue;
            if(l.contains("reading") && l["reading"].is_string()){ auto& n = readingNames(); for(size_t k = 0; k < n.size(); k++) if(n[k] == l["reading"].get<std::string>()) p.reading[i] = (int)k; }
            if(l.contains("blend") && l["blend"].is_string()) p.blend[i] = l["blend"].get<std::string>();
            p.alphaFromBrightness[i] = flag(l, "alphaFromBrightness");
        }
    return p;
}
inline json paperToJson(const Paper& p){
    json L = json::array();
    for(int i = 0; i < 4; i++) L.push_back({{"reading", readingNames()[(size_t)std::clamp(p.reading[(size_t)i], 0, 2)]}, {"blend", p.blend[(size_t)i]}, {"alphaFromBrightness", p.alphaFromBrightness[(size_t)i]}});
    json o{{"darkInk", p.darkInk}, {"layers", L}};
    if(p.hasBackground) o["background"] = {p.background[0], p.background[1], p.background[2]};
    return o;
}
// a premultiplied pixel as the compositor sees it: rgb x gain, alpha clamped, alpha from brightness
inline void layerPixel(const float* s, float g, bool afb, float o[4]){
    o[0] = s[0] * g; o[1] = s[1] * g; o[2] = s[2] * g; o[3] = std::min(1.f, s[3]);
    if(afb) o[3] = std::min(1.f, std::max({o[0], o[1], o[2]}));
}
// loudness of a premultiplied pixel: brightness max(r, g, b); coverage alpha; dark ink alpha x
// (1 - lightness of the straight colour). Fully transparent is 0 in every mode.
inline float readPixel(const float* p, int mode){
    if(mode == ReadCoverage) return std::clamp(p[3], 0.f, 1.f);
    if(mode == ReadDarkInk){
        float a = std::clamp(p[3], 0.f, 1.f); if(a <= 1e-6f) return 0.f;
        float mx = std::max({p[0], p[1], p[2]}) / a, mn = std::min({p[0], p[1], p[2]}) / a;
        float l = std::clamp(0.5f * (mx + mn), 0.f, 1.f);
        return a * (1 - l);
    }
    return std::min(1.f, std::max({p[0], p[1], p[2]}));
}
// premultiplied blend of s over o: 0 lighten 1 add 2 normal 3 multiply 4 darken
inline void blendPixel(float* o, const float* s, int mode){
    if(mode == 0){ for(int i = 0; i < 4; i++) o[i] = std::max(o[i], s[i]); return; }
    if(mode == 1){ for(int i = 0; i < 4; i++) o[i] = std::min(1.f, o[i] + s[i]); return; }
    float ks = 1 - s[3], kd = 1 - o[3];
    if(mode == 3) for(int i = 0; i < 3; i++) o[i] = s[i] * o[i] + s[i] * kd + o[i] * ks;
    else if(mode == 4) for(int i = 0; i < 3; i++) o[i] = std::min(s[i] * o[3], o[i] * s[3]) + s[i] * kd + o[i] * ks;
    else for(int i = 0; i < 3; i++) o[i] = s[i] + o[i] * ks;
    o[3] = s[3] + o[3] * ks;
}

// ---------------------------------------------------------------- v1 pitch grid
// v1 (notegrid rows over the image height, patch root MIDI 27): semitone p above the root sits at
// height (p + 1) / notegrid. v2 octave 0 = v1 octave 2, so v2 semitone s (tonic included) is v1 row
// s + 24: lowSemi = -25, highSemi = lowSemi + notegrid. The sound uses the same row layout.
inline PitchSpace v1PitchSpace(int notegrid = 100, double tonicShift = 0){
    PitchSpace s; s.lowSemi = tonicShift - 25; s.highSemi = s.lowSemi + std::max(12, notegrid); return s;
}

// ---------------------------------------------------------------- frame settings
struct Frame {
    int width = 1920, height = 1080;
    double headerX = 960;
    double nowBeat = 0;
    Scale scale;
    PitchSpace space = v1PitchSpace();   // v1's grid by default
    Mod mod;
    ThreeD td;                        // off by default: flat
    Paper paper;                      // reading modes, ink blends, dark-ink palette (5.18)
};

struct Stats { int drawn = 0, culled = 0, effects = 0, covers = 0, drawn3D = 0; };

// Per-event context while drawing: resolves values with modulation
struct Ctx {
    const Event& ev; const Frame& f; const LayerView& view;
    double depth = 1;
    // static value, then animated (whole event); drawn values added per position
    double base(const std::string& k, double def) const {
        double v = ev.param(k, def);
        if(f.mod.animated) v = f.mod.animated(ev, k, f.nowBeat, v);
        return v;
    }
    double at(const std::string& k, double staticV, double u) const {
        return f.mod.drawn ? f.mod.drawn(ev, k, clamp01(u), staticV) : staticV;
    }
    double levelBase() const {
        double v = ev.level;
        if(f.mod.animated) v = f.mod.animated(ev, "level", f.nowBeat, v);
        return v * base("brightness", 1);
    }
    double levelAt(double lb, double u) const {
        if(!f.mod.drawn) return lb;
        double l = f.mod.drawn(ev, "level", clamp01(u), ev.level) / std::max(1e-9, ev.level);
        double b = f.mod.drawn(ev, "brightness", clamp01(u), 1.0);
        return lb * (ev.level > 1e-9 ? l : 1) * b;
    }
    // column centre -> beat
    double beatOf(double xc) const { return view.nowBeat + (xc - view.headerX) / std::max(1e-9, view.pxPerBeat * depth); }
    double heightOfSemis(double s) const { return f.space.heightOf(s); }
    double yOf(double h) const { return view.yOf(h); }
};

// horizontal coverage of column x by span [x0, x1)
inline double colCover(int x, double x0, double x1){ return std::max(0.0, std::min<double>(x + 1, x1) - std::max<double>(x, x0)); }

// vertical soft line in one column: centre yc (px), half thickness ht, soft edge sf (px)
inline void vline(Canvas& c, int x, double yc, double ht, double sf, const float col[3], double a, bool add){
    if(a <= 1e-5) return;
    double scale = 1;
    if(ht < 0.5){ scale = ht / 0.5; ht = 0.5; }                 // thin lines dim instead of vanishing
    sf = std::max(0.6, sf);
    int y0 = (int)std::floor(yc - ht - sf), y1 = (int)std::ceil(yc + ht + sf);
    for(int y = std::max(0, y0); y <= std::min(c.h - 1, y1); y++){
        double d = std::fabs(y + 0.5 - yc);
        double cov = d <= ht ? 1 : std::max(0.0, 1 - (d - ht) / sf);
        c.put(x, y, col, (float)(a * cov * scale), add);
    }
}
// vertical band in one column, soft edges, per-row intensity function
template<class F>
inline void vband(Canvas& c, int x, double yTop, double yBot, double sf, double a, const float col[3], bool add, F rowGain){
    if(a <= 1e-5) return;
    if(yTop > yBot) std::swap(yTop, yBot);
    sf = std::max(0.5, sf);
    int y0 = (int)std::floor(yTop - sf), y1 = (int)std::ceil(yBot + sf);
    for(int y = std::max(0, y0); y <= std::min(c.h - 1, y1); y++){
        double yc = y + 0.5, cov = 1;
        if(yc < yTop) cov = 1 - (yTop - yc) / sf; else if(yc > yBot) cov = 1 - (yc - yBot) / sf;
        if(cov <= 0) continue;
        double g = rowGain(y, yc);
        c.put(x, y, col, (float)(a * cov * g), add);
    }
}

// ---------------------------------------------------------------- event extents
// beats the event draws over (some looks draw a tail after the event)
inline double tailBeats(const Event& e){
    switch(e.kind){
        case EventKind::Note: return e.param("release", 0.5) + e.param("legatoTail", 0) + e.param("trail", 0) + (e.p.count("ring") ? e.param("ring", 3) : 0);
        case EventKind::Hit: return std::max(0.0, e.param("width", e.length) - e.length);
        case EventKind::Shape: return std::max(0.0, e.param("shapeLength", e.length) - e.length);
        default: return 0;
    }
}
inline double eventDepth(const Event& e, const Frame& f){
    double d = e.p.count("depth") ? e.param("depth", 1) : e.depth;
    if(f.mod.animated) d = f.mod.animated(e, "depth", f.nowBeat, d);
    return std::max(0.01, d);
}

// retune morph (5.13): semitones to add to a note's drawn pitch at a beat
inline double retuneCurve(int c, double t){
    t = clamp01(t);
    switch(c){ case 0: return t; case 2: return (std::exp(3 * t) - 1) / (std::exp(3.0) - 1); case 3: return smooth01(smooth01(t)); default: return smooth01(t); }
}
inline double retuneOffset(const Event& e, double nowBeat){
    if(!e.p.count("retuneStart")) return 0;
    double from = e.param("retuneFrom", 0), to = e.param("retuneTo", 0), b = e.param("retuneBeats", 1);
    double t = b <= 0 ? 1 : (nowBeat - e.param("retuneStart", 0)) / b;
    return (from - to) * (1 - retuneCurve((int)std::lround(e.param("retuneCurve", 1)), t));
}

// ---------------------------------------------------------------- drawers
// ---------------------------------------------------------------- v1 techniques (ROADMAP 5.15)
// Ported from Forms GL Box (formsGLSketch.h: drawNote, drawSpectral, drawKlank, taikoSweep,
// gradblock_op, spectralGradient): power envelopes, partial fades, row noise.
// v1 envelope along a note: hold 0 / decay 1 / rise 2 / swell 3, raised to envPower (0 = flat)
inline double v1Env(int shape, double ph, double pw){
    ph = clamp01(ph);
    double e = 1;
    if(shape == 1) e = 1 - ph; else if(shape == 2) e = ph; else if(shape == 3) e = (1 - std::cos(ph * 2 * M_PI)) / 2;
    if(shape == 0 || pw <= 0) return 1;
    return std::pow(std::max(0.0, e), pw);
}
// a hit's envelope over its width: (1 - u)^k with k from decay (decay >= width: flat; width / (k + 1))
inline double hitEnv(double u, double width, double decay){
    double k = std::max(0.0, width / std::max(1e-6, decay) - 1);
    return k <= 0 ? 1 : std::pow(std::max(0.0, 1 - clamp01(u)), k);
}
// v1 band profile: 1 - (cos(2 pi v) + 1) / 2 across the band (v 0..1), raised to pw (0 = flat)
inline double bandProfile(double v, double pw){
    if(pw <= 0) return 1;
    return std::pow(std::max(0.0, (1 - std::cos(clamp01(v) * 2 * M_PI)) / 2), pw);
}
// Processing-like 1D noise: 4 octaves of smooth value noise, falloff 0.5 (mean ~0.47)
// smooth 2D value noise 0..1 (v1 noise masks)
inline double noise2(uint64_t seed, double x, double y){
    double fx = std::floor(x), fy = std::floor(y), tx = smooth01(x - fx), ty = smooth01(y - fy);
    int64_t ix = (int64_t)fx, iy = (int64_t)fy;
    double a = hash01(seed, ix, iy), b = hash01(seed, ix + 1, iy), c2 = hash01(seed, ix, iy + 1), d = hash01(seed, ix + 1, iy + 1);
    return lerp(lerp(a, b, tx), lerp(c2, d, tx), ty);
}
inline double fbm(uint64_t seed, double x, int64_t ch = 0){
    double s = 0, a = 0.5, f = 1;
    for(int o = 0; o < 4; o++){ s += a * vnoise(seed, x * f, ch * 8 + o); a *= 0.5; f *= 2; }
    return s;
}
// v1 spectral row noise: (noise * gain)^contrast per row (rows in 1080-px units), constant along time
inline double rowNoise(uint64_t seed, double row1080, double scale, double gain, double contrast){
    // v1 uses one octave of gradient noise: smooth, ~0.5 mean, narrow spread
    double n = 0.5 + 1.2 * (vnoise(seed, row1080 * scale + 17.3, 3) - 0.5);
    return std::min(1.0, std::pow(std::max(0.0, n * gain), contrast));
}

// Note (v1 drawNote): partials on the harmonic series (fundamental x j), each faded by
// (1 - j / n)^partialCut and partialDecay, the envelope along the note (envShape / envPower),
// hard 1-px-like lines with a thin anti-aliased edge; release tail after the note; detune voices
// stack lines; glide to p["glideTo"] (degree) or, unpitched, height -> height2. Looks: note, additive, ribbon.
inline void drawNote(Canvas& c, Ctx& k, double x0, double x1){
    const Event& e = k.ev;
    auto col = k.f.paper.colourOf(e.colourRole);
    std::string look = e.look.empty() ? "note" : e.look;
    bool additive = look == "additive", ribbon = look == "ribbon";
    double th = k.base("thickness", 2) * k.base("size", 1);
    int partials = (int)std::lround(k.base("partials", 8) * clamp01(k.base("partialCount", 1)));
    double pDecay = k.base("partialDecay", 0), pTilt = k.base("partialBright", 0), pCut = k.base("partialCut", 1);
    double spread = k.base("partialSpread", 1), thin = k.base("partialThin", 0);
    double inharm = k.base("inharmonic", 0);
    double attack = k.base("attack", 0), release = k.base("release", 0);
    int envShape = (int)std::lround(k.base("envShape", 0));   // absent: hold (schema default decay)
    double envPow = k.base("envPower", 1);
    double soft = k.base("softness", 0.1), shimmer = k.base("shimmer", 0);
    double detune = k.base("detune", 0), pDet = k.base("partialDetune", 0);
    int voices = std::clamp((int)std::lround(k.base("detuneVoices", 1)), 1, 3);
    double pChance = clamp01(k.base("partialChance", 1));
    double pEnvPow = k.base("partialEnvPower", 0);
    double kStack = std::clamp(k.base("alphaStack", 1), 1.0, 8.0);
    double sub = k.base("sub", 0), headAcc = k.base("headAccent", 0) + k.base("strikeFlash", 0);
    double len = e.length * k.base("noteLength", 1);
    double lb = k.levelBase();
    double fineStatic = k.base("pitchFine", 0), hOff = k.base("height", 0);
    bool glide = e.p.count("glideTo") > 0;
    bool timed = glide && e.p.count("glideStart") > 0;
    double semA = k.f.scale.semitonesOf(timed ? (int)std::lround(e.param("glideFrom", e.degree)) : e.degree);
    double semB = glide ? k.f.scale.semitonesOf((int)std::lround(e.param("glideTo", e.degree))) : semA;
    // key-change bends carry semitones (old scale -> new scale): preferred over degrees
    if(e.p.count("glideFromSemis")) semA = e.param("glideFromSemis", semA);
    if(e.p.count("glideToSemis")){ semB = e.param("glideToSemis", semB); glide = true; if(!e.p.count("glideFromSemis")) semA = k.f.scale.semitonesOf(e.degree); }
    timed = glide && e.p.count("glideStart") > 0;
    // tied continuation: no attack, accent or envelope restart, the line reads continuous
    bool tied = e.param("tied", 0) > 0.5;
    if(tied){ attack = 0; headAcc = 0; envShape = 0; }
    double rt = retuneOffset(e, k.f.nowBeat);
    semA += rt; semB += rt;
    double gStart = e.param("glideStart", e.start), gBeats = std::max(1e-6, e.param("glideBeats", len));
    int gCurve = std::clamp((int)std::lround(e.param("glideCurve", 1)), 0, 3);
    auto bend = [&](double t01){ return retuneCurve(gCurve, t01); };
    // per-partial amplitude (v1 hicut and the partial decay / tilt), computed once
    std::vector<double> pAmp((size_t)partials + 1, 1.0);
    for(int p = 1; p <= partials; p++){
        double ph1 = (p + 1.0) / (partials + 1.0);                   // v1: j / partials, j = p + 1
        double a = std::pow(std::max(0.0, 1 - ph1), std::max(0.0, pCut));
        a *= std::pow(std::max(0.0, 1 - pDecay), p) * std::exp(pTilt * 0.25 * p);
        if(additive) a = 0.5 * a + 0.5 * std::pow(std::max(0.0, 1 - ph1), 1.0);
        pAmp[(size_t)p] = std::min(1.0, a);
    }
    pAmp[0] = partials > 0 ? std::pow(std::max(0.0, 1 - 1.0 / (partials + 1.0)), std::max(0.0, pCut)) : 1;
    // per partial (fundamental included), fixed for the note: drawn or not (partialChance, v1's
    // random / thin partial modes) and a random flattening up to partialDetune (v1 random(partDet) rows)
    uint32_t ps32 = partialSeed(e.seed);
    std::vector<char> pOn((size_t)partials + 1, 1); std::vector<double> pFlat((size_t)partials + 1, 0.0);
    for(int p = 0; p <= partials; p++){
        if(pChance < 1 && partialHash(ps32, (uint32_t)p * 2u + 1u) >= pChance) pOn[(size_t)p] = 0;
        if(pDet > 0) pFlat[(size_t)p] = partialHash(ps32, (uint32_t)p * 2u + 2u) * pDet / 100.0;
    }
    for(int x = std::max(0, (int)std::floor(x0)); x <= std::min(c.w - 1, (int)std::ceil(x1)); x++){
        double cov = colCover(x, x0, x1); if(cov <= 0) continue;
        double t = k.beatOf(x + 0.5) - e.start;                   // beats into the note
        double u = clamp01(t / std::max(1e-9, len));
        double env = v1Env(envShape, u, envPow);
        if(attack > 0) env *= clamp01(t / attack);
        if(t > len) env = release > 0 ? v1Env(envShape, 1, envPow) * std::max(0.0, 1 - (t - len) / release) : 0;
        if(env <= 0) continue;
        double lvl = k.levelAt(lb, u) * env * cov;
        if(headAcc > 0) lvl *= 1 + headAcc * std::exp(-t * 8);
        if(shimmer > 0) lvl *= 1 - shimmer * hash01(e.seed, (int64_t)std::floor(t * 32), 7);
        double fine = k.at("pitchFine", fineStatic, u);
        double h0;
        if(e.pitched){
            double g = !glide ? 0 : timed ? bend((e.start + t - gStart) / gBeats) : smooth01(u);
            h0 = k.heightOfSemis(lerp(semA, semB, g) + fine / 100.0);
        } else {
            h0 = lerp(e.height, e.height2, smooth01(u)) + k.f.space.heightOf(fine / 100.0) - k.f.space.heightOf(0);
        }
        h0 += k.at("height", hOff, u);
        double thx = th * (k.f.mod.drawn ? k.at("size", 1, u) : 1);
        if(ribbon) thx *= 0.6 + 0.8 * std::sin(M_PI * clamp01(t / std::max(1e-9, len + release)));
        double semBase = k.f.space.semisOf(h0);
        auto drawAt = [&](double semis, double ht, double a){
            if(kStack > 1) a = 1 - std::pow(std::max(0.0, 1 - a), kStack);   // v1 overlapping paint
            double y = k.yOf(k.heightOfSemis(semis));
            if(y < -ht - 2 || y > c.h + ht + 2) return;
            double sf = soft * ht * 2 + 0.5;
            for(int v = 0; v < voices; v++){
                double off = voices == 1 ? 0 : (voices == 2 ? (v ? 0.5 : -0.5) : (v - 1.0));
                vline(c, x, k.yOf(k.heightOfSemis(semis + off * detune / 100.0)), ht, sf, col.data(), a, additive);
            }
        };
        // v1 lfo power: the drawn brightness gain g(u) (brightness routings) raised per partial
        double g = lb > 1e-9 ? clamp01(k.levelAt(lb, u) / lb) : 1;
        auto pGain = [&](int p){ return pEnvPow > 0 ? std::pow(g, (p + 1.0) / (partials + 1.0) * pEnvPow) : 1.0; };
        if(pOn[0]) drawAt(semBase - pFlat[0], thx * 0.5, lvl * pAmp[0] * pGain(0));
        if(sub > 0) drawAt(semBase - 12, thx * 0.5, lvl * sub);
        for(int p = 1; p <= partials; p++){
            double ratio = 1 + p * spread;
            ratio *= 1 + inharm * 0.02 * p * p;
            double ht = thx * 0.5 * std::max(0.15, 1 - thin * p / (partials + 1.0));
            double semP = semBase + 12 * std::log2(ratio);
            if(k.yOf(k.heightOfSemis(semP)) < -4) break;           // above the top
            if(!pOn[(size_t)p]) continue;
            drawAt(semP - pFlat[(size_t)p], ht, lvl * pAmp[(size_t)p] * pGain(p));
        }
    }
}

// Hit (v1 hit cells): kick / taiko (v1 taikoSweep: a floor-anchored wedge whose top falls), sine /
// ring, noiseBand / snare (v1 drawSpectral: row noise^contrast with a power envelope), burst (v1
// klank: flat band, power decay), block (v1 gradblock: cos profile^bandShape), gradient (v1
// spectralGradient: narrowing cos profile, power fade), speckle, dot / dotGrid, tick / line, lfoBand.
// Envelope over the width: (1 - u)^k, k = width / decay - 1 (decay >= width: flat).
inline void drawHit(Canvas& c, Ctx& k, double x0, double /*x1*/){
    const Event& e = k.ev;
    auto col = k.f.paper.colourOf(e.colourRole);
    std::string look = e.look.empty() ? "block" : e.look;
    double lo = e.param("heightLow", e.param("bandLow", std::min(e.height, e.height2)));
    double hi = e.param("heightHigh", e.param("bandHigh", std::max(e.height, e.height2)));
    if(hi - lo < 0.01){ lo -= 0.05; hi += 0.05; }
    double hOff = k.base("height", 0); lo += hOff; hi += hOff;
    double sz = k.base("size", 1), mid = (lo + hi) / 2;
    lo = mid + (lo - mid) * sz; hi = mid + (hi - mid) * sz;
    bool noisy = look == "noiseBand" || look == "snare";
    double width = k.base("width", e.length), decay = std::max(0.005, k.base("decay", 0.3));
    double soft = k.base("softness", 0.2), grain = k.base("grain", noisy ? 1 : 0);
    double sweep = k.base("sweep", 0.5), tone = k.base("tone", 0), dot = k.base("dotSize", 3) * sz;
    double contrast = k.base("contrast", 4), gsz = std::max(1.0, k.base("grainSize", 20)), shapeP = k.base("bandShape", 0);   // absent: flat (schema default 1 = v1 cos profile)
    double lb = k.levelBase();
    double ppb = k.view.pxPerBeat * k.depth;
    double xEnd = x0 + width * ppb;
    double yTop = k.yOf(hi), yBot = k.yOf(lo), bandPx = std::max(1.0, yBot - yTop);
    double px1080 = 1080.0 / std::max(1, k.view.height);            // rows in v1's 1080 units
    if(look == "tick" || look == "line"){
        double tx = x0;
        for(int x = std::max(0, (int)std::floor(tx)); x <= std::min(c.w - 1, (int)std::ceil(tx + std::max(1.0, dot * 0.5))); x++){
            double cov = colCover(x, tx, tx + std::max(1.0, dot * 0.5)); if(cov <= 0) continue;
            double t = std::max(0.0, k.beatOf(x + 0.5) - e.start);
            double u = clamp01(t / std::max(1e-9, width));
            vband(c, x, yTop, yBot, soft * 4 + 0.5, k.levelAt(lb, u) * cov, col.data(), false, [&](int, double){ return 1.0; });
        }
        return;
    }
    if(look == "dot" || look == "speckle" || look == "dotGrid"){
        Rng r(e.seed ^ 0xD07);
        int n = look == "dot" ? 1 : (look == "speckle" ? std::max(4, (int)(bandPx * width * ppb / 60 * (0.3 + grain))) : 0);
        std::vector<std::pair<double, double>> pts;
        double scatter = k.base("scatter", 0.3);
        if(look == "dot") pts.push_back({0, lerp(mid, lo + r.uniform() * (hi - lo), scatter)});
        else if(look == "speckle") for(int i = 0; i < n && i < 4000; i++) pts.push_back({r.uniform() * width, lo + r.uniform() * (hi - lo)});
        else {
            double step = std::max(dot * 3, 4.0);
            for(double yy = yTop; yy <= yBot; yy += step) for(double xx = 0; xx <= width * ppb; xx += step) pts.push_back({xx / ppb, 1 - yy / k.view.height});
        }
        for(size_t i = 0; i < pts.size(); i++){
            double cx = x0 + pts[i].first * ppb, cy = k.yOf(pts[i].second);
            double a = lb * hitEnv(pts[i].first / std::max(1e-9, width), width, decay) * (look == "speckle" ? hash01(e.seed, (int64_t)i, 3) : 1);
            double rad = std::max(0.6, dot * 0.5);
            for(int y = (int)std::floor(cy - rad - 1); y <= (int)std::ceil(cy + rad + 1); y++)
                for(int x = (int)std::floor(cx - rad - 1); x <= (int)std::ceil(cx + rad + 1); x++){
                    double d = std::hypot(x + 0.5 - cx, y + 0.5 - cy);
                    double cov = clamp01(rad + 0.5 - d);
                    c.put(x, y, col.data(), (float)(a * cov), false);
                }
        }
        return;
    }
    // power of the kick wedge (v1 kickPower): from the decay
    double kp = std::max(0.25, (width / std::max(1e-6, decay) - 1) / 2);
    for(int x = std::max(0, (int)std::floor(x0)); x <= std::min(c.w - 1, (int)std::ceil(xEnd)); x++){
        double cov = colCover(x, x0, xEnd); if(cov <= 0) continue;
        double t = k.beatOf(x + 0.5) - e.start;
        if(t < 0) continue;
        double u = clamp01(t / std::max(1e-9, width));
        double env = hitEnv(u, width, decay);
        double a = k.levelAt(lb, u) * cov;
        if(look == "kick" || look == "taiko"){
            // v1 taikoSweep: the top edge falls (u^0.25) by `sweep` of the band, brighter towards the floor
            double yEdge = yTop + std::pow(u, 0.25) * sweep * (yBot - yTop);
            double h2 = std::max(1.0, yBot - yEdge);
            vband(c, x, yEdge, yBot, 0.5, a * env, col.data(), false, [&](int, double yc){
                return std::pow(clamp01((yc - yEdge) / h2), kp / 2);
            });
        } else if(look == "sine" || look == "ring"){
            double fall = 1 - std::exp(-t / std::max(0.01, decay * (1.2 - sweep)));
            double hc = lerp(hi, lo, fall * sweep + (1 - sweep) * 0.5);
            double half = look == "ring" ? (hi - lo) * (0.08 + 0.25 * env) : (hi - lo) * 0.06;
            double ht = std::max(0.5, half * k.view.height);
            if(look == "ring"){ vline(c, x, k.yOf(hc + half), 1, 1, col.data(), a * env, false); vline(c, x, k.yOf(hc - half), 1, 1, col.data(), a * env, false); }
            else vline(c, x, k.yOf(hc), ht, soft * ht + 1, col.data(), a * env, false);
        } else if(noisy){
            double m2 = (yTop + yBot) / 2, hh = (yBot - yTop) / 2;
            vband(c, x, yTop, yBot, 0.5, a * env, col.data(), false, [&](int, double yc){
                double n = rowNoise(e.seed, (yc - yTop) * px1080, 1.0 / gsz, 1.33, contrast);
                double g = (1 - grain) + grain * n;
                if(tone > 0) g = std::max(g, tone * std::exp(-std::pow((yc - m2) / std::max(1.0, hh * 0.1), 2)));
                return g;
            });
        } else if(look == "burst"){
            // v1 klank: flat band, 0.75 x power decay
            vband(c, x, yTop, yBot, soft * 4 + 0.5, a * env * 0.75, col.data(), false, [&](int, double){ return 1.0; });
        } else if(look == "gradient"){
            // v1 spectralGradient: band narrows to half towards the end, cos profile, power fade
            double m2 = (yTop + yBot) / 2, hh = (yBot - yTop) / 2 * (1 - 0.5 * u);
            vband(c, x, m2 - hh, m2 + hh, 0.5, a * env, col.data(), false, [&](int, double yc){ return bandProfile((yc - (m2 - hh)) / std::max(1.0, 2 * hh), std::max(0.0, shapeP)); });
        } else if(look == "lfoBand"){
            double hc = mid + (hi - lo) * 0.4 * std::sin(2 * M_PI * t / std::max(0.05, width * 0.5));
            vline(c, x, k.yOf(hc), std::max(1.0, bandPx * 0.1), 2, col.data(), a * env, false);
        } else {   // block (v1 gradblock: cos profile^bandShape, grain as row noise)
            vband(c, x, yTop, yBot, soft * 8 + 0.5, a * env, col.data(), false, [&](int, double yc){
                double g = bandProfile((yc - yTop) / bandPx, shapeP);
                if(grain > 0) g *= (1 - grain) + grain * rowNoise(e.seed, (yc - yTop) * px1080, 1.0 / gsz, 1.33, contrast);
                return g;
            });
        }
    }
}

// glide shapes for Mass strands
// v1 portamentoPG sigmoid (Legacy.pde): tension 0 linear, toward 1 a hold, a step at the middle, a hold
inline double snapSigmoid(double t, double tension){
    double k = std::clamp(tension, 0.0, 0.999), n = 2 * clamp01(t) - 1;
    return (n * (1 + k) / (1 - k + 2 * k * std::fabs(n)) + 1) / 2;
}
inline double glideShape(const std::string& s, double t, double tension = 0.95){
    t = clamp01(t);
    if(s == "linear") return t;
    if(s == "snap") return snapSigmoid(t, tension);
    if(s == "exp") return (std::exp(3 * t) - 1) / (std::exp(3.0) - 1);
    if(s == "sCurve") return smooth01(smooth01(t));
    return smooth01(t);
}
// Mass: strands gliding between heights, with spread, wobble, fade
inline void drawMass(Canvas& c, Ctx& k, double x0, double x1){
    const Event& e = k.ev;
    auto col = k.f.paper.colourOf(e.colourRole);
    double hA = e.param("heightFrom", e.height), hB = e.param("heightTo", e.height2);
    int strands = std::clamp((int)std::lround(k.base("strands", 8)), 1, 64);
    double lw = k.base("lineWidth", 1.5) * k.base("size", 1) * e.param("widthScale", 1), spread = k.base("spread", 0.1);
    double wob = k.base("wobble", 0.1), fade = k.base("fade", 0), soft = k.base("softness", 0.1);
    double jit = k.base("strandJitter", 0.1), hOff = k.base("height", 0);
    static const char* gsNames[] = {"linear", "ease", "exp", "sCurve", "snap"};
    std::string gs = gsNames[std::clamp((int)e.param("glideShape", 1), 0, 4)];
    double gt = e.param("glideTension", 0.95);
    double lb = k.levelBase();
    for(int x = std::max(0, (int)std::floor(x0)); x <= std::min(c.w - 1, (int)std::ceil(x1)); x++){
        double cov = colCover(x, x0 + 0.5, x1 + 0.5); if(cov <= 0) continue;   // v1 strokes: ends on pixel centres
        double t = k.beatOf(x + 0.5) - e.start, u = clamp01(t / std::max(1e-9, e.length));
        double lvl = k.levelAt(lb, u) * cov;
        double hBase = lerp(hA, hB, glideShape(gs, u, gt)) + k.at("height", hOff, u);
        // v1 strokes: a sloped line covers lw * sqrt(1 + slope^2) px per column
        double du = 1.0 / std::max(1.0, x1 - x0);
        double dy = k.yOf(lerp(hA, hB, glideShape(gs, std::min(1.0, u + du), gt))) - k.yOf(lerp(hA, hB, glideShape(gs, std::max(0.0, u - du), gt)));
        double slopeW = std::sqrt(1 + 0.25 * dy * dy);
        for(int s = 0; s < strands; s++){
            double so = strands == 1 ? 0 : (s / (strands - 1.0) - 0.5);
            double js = jit * hash01(e.seed, s, 1) * 0.3;            // strand start offset (fraction)
            if(u < js) continue;
            double uf = clamp01((u - js) / std::max(1e-6, 1 - js));
            double f = fade > 0 ? std::min(clamp01(uf / fade), clamp01((1 - uf) / fade)) : 1;
            double w = wob * 0.05 * (vnoise(e.seed, t * 2 + s * 13.1, s) - 0.5) * 2;
            double h = hBase + so * spread + w;
            vline(c, x, k.yOf(h), lw * 0.5 * slopeW, soft * lw + 0.5, col.data(), lvl * f, false);   // v1: solid strokes
        }
    }
}

// Sweep / texture: band bottom / top / level moving from start to end; texture cells add noise
inline void drawSweep(Canvas& c, Ctx& k, double x0, double x1){
    const Event& e = k.ev;
    auto col = k.f.paper.colourOf(e.colourRole);
    bool texture = e.p.count("bandLow") || e.p.count("density") || e.look == "noise" || e.look == "cloud" || e.look == "grain" || e.look == "nebula" || e.look == "stripes";
    double bF, bT, tF, tT;
    if(texture){ bF = bT = e.param("bandLow", std::min(e.height, e.height2)); tF = tT = e.param("bandHigh", std::max(e.height, e.height2)); }
    else { bF = e.param("bottomFrom", e.height); bT = e.param("bottomTo", e.height); tF = e.param("topFrom", e.height2); tT = e.param("topTo", e.height2); }
    double lF = e.param("levelFrom", 1), lT = e.param("levelTo", 1), curve = std::max(0.1, e.param("curve", 1));
    double soft = k.base("softness", 0.4), fade = k.base("fade", texture ? 0.3 : 0), dens = k.base("density", 0.5);
    double gsz = std::max(0.5, k.base("grainSize", 6)), drift = k.base("drift", 0.2), contrast = k.base("contrast", 1);
    double shapeP = k.base("bandShape", 0), px1080 = 1080.0 / std::max(1, k.view.height);
    double hOff = k.base("height", 0), lb = k.levelBase();
    double ppb = k.view.pxPerBeat * k.depth;
    std::string cell = e.look.empty() ? "cloud" : e.look;
    for(int x = std::max(0, (int)std::floor(x0)); x <= std::min(c.w - 1, (int)std::ceil(x1)); x++){
        double cov = colCover(x, x0, x1); if(cov <= 0) continue;
        double t = k.beatOf(x + 0.5) - e.start, u = clamp01(t / std::max(1e-9, e.length));
        double uc = u;                                               // v1: heights linear, level^curve
        double ho = k.at("height", hOff, u);
        if(texture) ho += drift * 0.1 * (vnoise(e.seed, t * 0.5, 5) - 0.5);
        double b = lerp(bF, bT, uc) + ho, tp = lerp(tF, tT, uc) + ho;
        double f = fade > 0 ? std::min(clamp01(u / fade), clamp01((1 - u) / fade)) : 1;
        double a = k.levelAt(lb, u) * std::pow(std::max(0.0, lerp(lF, lT, uc)), curve) * f * cov;
        double yT = k.yOf(tp), yB = k.yOf(b);
        double sfp = soft * 0.25 * std::fabs(yB - yT) + 0.5;
        if(!texture){ vband(c, x, yT, yB, sfp, a, col.data(), false, [&](int, double yc){ return bandProfile((yB - yc) / std::max(1.0, yB - yT), shapeP); }); continue; }
        double xb = t * ppb / gsz;                                   // noise in event space (px / grain)
        vband(c, x, yT, yB, sfp, a, col.data(), false, [&](int, double yc){
            double yy = yc / gsz, n;
            if(cell == "stripes") n = 0.5 + 0.5 * std::sin(yy * 1.7);
            else if(cell == "noise") return rowNoise(e.seed, (yc - yT) * px1080, 1.0 / std::max(1.0, gsz * 3), 1.33, 4 * contrast);   // v1 spectral rows
            else if(cell == "grain") n = hash01(e.seed, (int64_t)std::floor(xb), (int64_t)std::floor(yy));
            else { // cloud / nebula: two octaves of smooth noise
                double n1 = lerp(vnoise(e.seed, xb * 0.3, (int64_t)std::floor(yy * 0.3)), vnoise(e.seed, xb * 0.3, (int64_t)std::floor(yy * 0.3) + 1), smooth01(yy * 0.3 - std::floor(yy * 0.3)));
                n = cell == "nebula" ? n1 * n1 * 1.5 : n1;
            }
            double g = clamp01((n - (1 - dens)) * 2 * contrast + 0.5 * dens);
            return g;
        });
    }
}

// Shape: line, rect, ellipse, triangle, curve (letter falls back to an outlined X); filled or
// stroked; rotation in screen px around the centre
inline void drawShape(Canvas& c, Ctx& k, double x0, double /*x1*/){
    const Event& e = k.ev;
    auto col = k.f.paper.colourOf(e.colourRole);
    std::string sh = e.look.empty() ? "rect" : e.look;
    double sz = k.base("size", 1);
    double lenPx = k.base("shapeLength", e.length) * k.view.pxPerBeat * k.depth;
    double hc = k.base("heightCenter", e.pitched ? k.f.space.heightOf(k.f.scale.semitonesOf(e.degree)) : e.height) + k.base("height", 0);
    double hs = k.base("heightSize", 0.2) * sz;
    bool filled = k.base("filled", 1) > 0.5 && sh != "line" && sh != "curve" && sh != "letter";
    double sw = k.base("strokeWidth", 2), rot = k.base("rotation", 0) * M_PI / 180, soft = k.base("softness", 0.1);
    double a = k.levelBase();
    double cx = x0 + lenPx / 2, cy = k.yOf(hc), hw = lenPx / 2, hh = hs * k.view.height / 2;
    double R = std::hypot(hw, hh) + sw + 2;
    double cr = std::cos(rot), sr = std::sin(rot);
    double sf = std::max(0.7, soft * 8);
    auto segD = [](double px, double py, double ax, double ay, double bx, double by){
        double dx = bx - ax, dy = by - ay; double t = clamp01(((px - ax) * dx + (py - ay) * dy) / std::max(1e-9, dx * dx + dy * dy));
        return std::hypot(px - ax - t * dx, py - ay - t * dy);
    };
    for(int y = std::max(0, (int)(cy - R)); y <= std::min(c.h - 1, (int)(cy + R)); y++)
        for(int x = std::max(0, (int)(cx - R)); x <= std::min(c.w - 1, (int)(cx + R)); x++){
            double dx = x + 0.5 - cx, dy = y + 0.5 - cy;
            double px = dx * cr + dy * sr, py = -dx * sr + dy * cr;   // into the shape's frame
            double d;                                                // signed distance (negative inside)
            if(sh == "ellipse"){ double r = std::hypot(px / std::max(1e-6, hw), py / std::max(1e-6, hh)); d = (r - 1) * std::min(hw, hh); }
            else if(sh == "triangle"){
                double s1 = segD(px, py, -hw, hh, hw, hh), s2 = segD(px, py, hw, hh, 0, -hh), s3 = segD(px, py, 0, -hh, -hw, hh);
                double m = std::min({s1, s2, s3});
                double ty = (py + hh) / std::max(1e-6, 2 * hh);           // inside test: |px| < hw * ty
                bool in = py <= hh && py >= -hh && std::fabs(px) <= hw * ty;
                d = in ? -m : m;
            } else if(sh == "line") d = segD(px, py, -hw, 0, hw, 0);
            else if(sh == "curve"){
                double tt = clamp01((px + hw) / std::max(1e-6, 2 * hw));
                d = (px < -hw || px > hw) ? 1e9 : std::fabs(py + hh * std::sin(M_PI * tt) - hh * 0.5) * 0.8;
            } else if(sh == "letter") d = std::min(segD(px, py, -hw, -hh, hw, hh), segD(px, py, -hw, hh, hw, -hh));
            else { double qx = std::fabs(px) - hw, qy = std::fabs(py) - hh; d = std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0); }
            double cov;
            if(filled) cov = clamp01(0.5 - d / sf);
            else cov = clamp01((sw * 0.5 + 0.5 - std::fabs(d)) / std::max(0.7, sf * 0.5));
            if(cov > 0) c.put(x, y, col.data(), (float)(a * cov), false);
        }
}

// ---------------------------------------------------------------- layer-bus effects (beat space)
// amount shape over the slot (u 0..1)
inline double amountShape(const std::string& s, double u, double rateFrac = 0.25){
    if(s == "rise") return u;
    if(s == "fall") return 1 - u;
    if(s == "swell") return std::sin(M_PI * u);
    if(s == "lfo") return 0.5 + 0.5 * std::sin(2 * M_PI * u / std::max(0.01, rateFrac));
    return 1;
}
// Operates on columns of the layer canvas inside the slot [start, end) at the layer's zoom.
// Columns outside the screen are not available: a remap that reads off screen reads black.
// blob mask over a slot: gain per pixel (beat space of the view)
inline void applyBlobCols(Canvas& c, const blob::Mask& m, const LayerView& v, int a0, int a1, double nowBeat){
    for(int x = std::max(0, a0); x <= std::min(c.w - 1, a1); x++){
        double beat = v.nowBeat + (x + 0.5 - v.headerX) / std::max(1e-9, v.pxPerBeat);
        for(int y = 0; y < c.h; y++){
            float* d = c.at(x, y);
            if(d[3] <= 0 && d[0] <= 0 && d[1] <= 0 && d[2] <= 0) continue;
            float g = (float)std::clamp(m.gain(beat, 1 - (y + 0.5) / c.h, v.pxPerBeat, c.h, nowBeat), 0.0, 1.0);
            for(int i = 0; i < 4; i++) d[i] *= g;
        }
    }
}
inline void applyEffectSpan(Canvas& c, const Event& e, const LayerView& v, double xs, double xe);
inline void applyEffect(Canvas& c, const Event& e, const LayerView& v){
    applyEffectSpan(c, e, v, v.xOf(e.start), v.xOf(e.start + e.length));
}
// the effect over columns [xs, xe) (a slot, or one event's span for element scope); v gives the
// beats -> px scale (at the event's depth for element scope)
inline void applyEffectSpan(Canvas& c, const Event& e, const LayerView& v, double xs, double xe){
    std::string fx = e.look;
    if(xe < 0 || xs >= c.w || xe - xs < 1) return;
    int a0 = (int)std::floor(xs), a1 = (int)std::ceil(xe) - 1;
    double slotPx = xe - xs;
    Canvas src = c;                                                   // read from a copy
    auto copyCol = [&](int dst, double sx, float gain){
        int s = (int)std::floor(sx);
        for(int y = 0; y < c.h; y++){
            float* d = c.at(dst, y);
            if(s < 0 || s >= c.w){ d[0] = d[1] = d[2] = d[3] = 0; continue; }
            const float* p = src.at(s, y);
            for(int i = 0; i < 4; i++) d[i] = p[i] * gain;
        }
    };
    if(fx == "blob"){
        blob::Mask m;
        double b0 = v.nowBeat + (xs - v.headerX) / v.pxPerBeat, b1 = v.nowBeat + (xe - v.headerX) / v.pxPerBeat;
        blob::Source so = blob::sourceOf(e, e.length);
        so.evStart = b0; so.evLen = std::max(1e-3, b1 - b0);
        if(!e.p.count("blobPhStart")){ so.phStart = b0; so.phLen = so.evLen; }
        m.build(blob::paramsOf(e), so, b0, b1);
        applyBlobCols(c, m, v, a0, a1, v.nowBeat);
        return;
    }
    if(fx == "echo"){
        int n = std::clamp((int)std::lround(e.param("echoCount", 3)), 1, 16);
        double dec = e.param("echoDecay", 0.5), sp = e.param("echoSpacing", 0.5) * v.pxPerBeat;
        for(int i = 1; i <= n; i++){
            float g = (float)std::pow(dec, i);
            for(int x = a0; x <= a1; x++){
                int dx = (int)std::lround(x + i * sp);
                if(x < 0 || x >= c.w || dx < 0 || dx >= c.w) continue;
                for(int y = 0; y < c.h; y++){ float* d = c.at(dx, y); const float* p = src.at(x, y); for(int k = 0; k < 4; k++) d[k] = std::max(d[k], p[k] * g); }
            }
        }
        return;
    }
    std::vector<int> perm;
    if(fx == "shuffle"){
        int n = std::clamp((int)std::lround(e.param("shuffleSlices", 4)), 2, 32);
        perm.resize((size_t)n); for(int i = 0; i < n; i++) perm[(size_t)i] = i;
        Rng r(e.seed ^ 0x5F); for(int i = n - 1; i > 0; i--) std::swap(perm[(size_t)i], perm[(size_t)r.below(i + 1)]);
    }
    static const char* shapeNames[] = {"steady", "rise", "fall", "swell", "lfo"};
    std::string shape = shapeNames[std::clamp((int)e.param("shape", 0), 0, 4)];
    double amount = e.param("amount", 1);
    double lo = e.param("bandLow", 0.2), hi = e.param("bandHigh", 0.8), feather = e.param("feather", 0.1);
    for(int x = std::max(0, a0); x <= std::min(c.w - 1, a1); x++){
        double lx = x + 0.5 - xs, u = clamp01(lx / slotPx);
        if(fx == "reverse") copyCol(x, xs + xe - (x + 0.5), 1);
        else if(fx == "repeat"){ int n = std::max(1, (int)std::lround(e.param("repeats", 2))); copyCol(x, xs + std::fmod(lx, slotPx / n), 1); }
        else if(fx == "stutter"){ int n = std::max(2, (int)std::lround(e.param("stutterDiv", 8))); copyCol(x, xs + std::fmod(lx, slotPx / n), 1); }
        else if(fx == "tile"){   // squeeze the slot n times: average the n source columns
            int n = std::max(1, (int)std::lround(e.param("repeats", 2)));
            double sx = xs + std::fmod(lx * n, slotPx) - 0.5 * n;
            for(int y = 0; y < c.h; y++){
                float acc[4] = {0, 0, 0, 0}; int cnt = 0;
                for(int k = 0; k < n; k++){ int s2 = (int)std::floor(sx + k + 0.5); if(s2 < 0 || s2 >= c.w) continue; const float* p = src.at(s2, y); for(int i = 0; i < 4; i++) acc[i] += p[i]; cnt++; }
                float* d = c.at(x, y); for(int i = 0; i < 4; i++) d[i] = cnt ? acc[i] / n : 0;
            }
        }
        else if(fx == "freeze") copyCol(x, xs, 1);
        else if(fx == "blank") copyCol(x, -1, 0);
        else if(fx == "shuffle"){ int n = (int)perm.size(); int i = std::min(n - 1, (int)(u * n)); copyCol(x, xs + (perm[(size_t)i] + (u * n - i)) * slotPx / n, 1); }
        else if(fx == "tears"){
            double amt = e.param("tearAmount", 0.5);
            for(int y = 0; y < c.h; y++){
                int band = y / 16;
                double off = (hash01(e.seed, band, 77) - 0.5) * 2 * amt * slotPx * 0.25;
                int s = (int)std::floor(x + off);
                float* d = c.at(x, y);
                if(s < 0 || s >= c.w){ d[0] = d[1] = d[2] = d[3] = 0; continue; }
                const float* p = src.at(s, y); for(int i = 0; i < 4; i++) d[i] = p[i];
            }
        }
        else {   // filter fx: a gain per pixel
            double amt = amount * amountShape(shape, u, e.param("gateRate", 0.5) / std::max(1e-6, e.length));
            for(int y = 0; y < c.h; y++){
                double h = 1 - (y + 0.5) / c.h, g = 1;
                auto inBand = [&]{ double f = std::max(1e-4, feather * 0.5); return std::min(smooth01((h - lo) / f + 0.5), smooth01((hi - h) / f + 0.5)); };
                if(fx == "band") g = 1 - amt * (1 - inBand());
                else if(fx == "notch") g = 1 - amt * inBand();
                else if(fx == "mask") g = 1 - amt * (e.param("maskInvert", 0) > 0.5 ? inBand() : 1 - inBand());
                else if(fx == "page") g = 1 - amt * (1 - std::pow(0.5 - 0.5 * std::cos(2 * M_PI * u), std::max(0.0, e.param("pagePower", 1))));
                else if(fx == "noise"){   // v1 noise mask: (noise x gain)^contrast, inverted / thresholded
                    double cell = std::max(1.0, e.param("noiseScale", 0.2) * c.h);
                    double nv = std::pow(clamp01(noise2(e.seed, lx / cell, y / cell) * e.param("noiseGain", 1)), e.param("noiseContrast", 1));
                    if(e.param("maskInvert", 0) > 0.5) nv = 1 - nv;
                    double th = e.param("noiseThreshold", 0); if(th > 0) nv = nv >= th ? 1 : 0;
                    g = 1 - amt + amt * nv;
                }
                else if(fx == "fadeIn") g = 1 - amt * (1 - u);
                else if(fx == "fadeOut") g = 1 - amt * u;
                else if(fx == "vignette"){ double vs = std::max(1e-3, e.param("vignetteSize", 0.3)); g = 1 - amt * (1 - smooth01(std::min(u, 1 - u) / vs)); }
                else if(fx == "gate"){
                    double rate = std::max(0.01, e.param("gateRate", 0.5)), duty = e.param("gateDuty", 0.5);
                    double ph = std::fmod(lx / v.pxPerBeat, rate) / rate;
                    g = ph < duty ? 1 : 1 - amt;
                }
                float* d = c.at(x, y);
                if(fx == "saturation"){
                    double dr = 1 + e.param("drive", 1) * amt;
                    for(int i = 0; i < 3; i++) d[i] = (float)std::min<double>(d[3], std::tanh(d[i] * dr) / std::tanh(dr) * std::max(1.0f, d[3]));
                    continue;
                }
                for(int i = 0; i < 4; i++) d[i] *= (float)g;
            }
        }
    }
}

// ---------------------------------------------------------------- cover below
struct CoverKey {
    double dilate, feather, release, amount; int mode, target;   // mode 0 eraser 1 black; target 0 allBelow 1 layerBelow 2 all
    bool operator<(const CoverKey& o) const { return std::tie(dilate, feather, release, amount, mode, target) < std::tie(o.dilate, o.feather, o.release, o.amount, o.mode, o.target); }
};
// separable max filter (dilate) and box blur (feather), on a w x h float mask
inline void dilateMask(std::vector<float>& m, int w, int h, int r){
    if(r <= 0) return;
    std::vector<float> t(m.size());
    for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){ float v = 0; for(int d = -r; d <= r; d++){ int xx = x + d; if(xx >= 0 && xx < w) v = std::max(v, m[(size_t)y * w + xx]); } t[(size_t)y * w + x] = v; }
    for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){ float v = 0; for(int d = -r; d <= r; d++){ int yy = y + d; if(yy >= 0 && yy < h) v = std::max(v, t[(size_t)yy * w + x]); } m[(size_t)y * w + x] = v; }
}
inline void blurMask(std::vector<float>& m, int w, int h, int r){
    if(r <= 0) return;
    std::vector<float> t(m.size());
    for(int pass = 0; pass < 2; pass++){
        for(int y = 0; y < h; y++){ double s = 0; int n = 0; for(int x = -r; x < w + r; x++){
            if(x + r < w){ s += m[(size_t)y * w + std::max(0, x + r)] * (x + r >= 0); n += (x + r >= 0); }
            if(x - r - 1 >= 0){ s -= m[(size_t)y * w + x - r - 1]; n--; }
            if(x >= 0 && x < w) t[(size_t)y * w + x] = (float)(s / std::max(1, n)); } }
        for(int x = 0; x < w; x++){ double s = 0; int n = 0; for(int y = -r; y < h + r; y++){
            if(y + r < h){ s += t[(size_t)std::max(0, y + r) * w + x] * (y + r >= 0); n += (y + r >= 0); }
            if(y - r - 1 >= 0){ s -= t[(size_t)(y - r - 1) * w + x]; n--; }
            if(y >= 0 && y < h) m[(size_t)y * w + x] = (float)(s / std::max(1, n)); } }
    }
}
// trail: the cover persists to the later side (right: later beats) and fades linearly over
// `release` beats
inline void trailMask(std::vector<float>& m, int w, int h, double releasePx){
    if(releasePx <= 0) return;
    float step = (float)(1.0 / releasePx);
    for(int y = 0; y < h; y++){ float v = 0; for(int x = 0; x < w; x++){ float& p = m[(size_t)y * w + x]; v = std::max(p, v - step); p = v; } }
}

// ---------------------------------------------------------------- the renderer
class Renderer {
public:
    Frame frame;
    std::vector<LayerDef> layers{LayerDef{}};
    Stats stats;

    static constexpr int kMaxLayers = 4;

    LayerView viewOf(int layer) const {
        LayerView v; v.width = frame.width; v.height = frame.height; v.headerX = frame.headerX; v.nowBeat = frame.nowBeat;
        double zb = layers.empty() ? 16 : layers[(size_t)std::clamp(layer, 0, (int)layers.size() - 1)].zoomBeats;
        v.pxPerBeat = frame.width / std::max(1e-6, zb);
        return v;
    }
    const Canvas& layer(int i) const { return canvases[(size_t)i]; }
    const Canvas& composite() const { return out; }

    // x span [x0, x1) of an event at its depth (including its look's tail)
    std::pair<double, double> spanOf(const Event& e) const {
        LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
        return {v.xOf(e.start, d), v.xOf(e.start + e.length + tailBeats(e), d)};
    }
    bool visible(const Event& e) const {
        auto s = spanOf(e);
        double margin = 64;   // soft edges, dots and rotated shapes
        return s.second >= -margin && s.first <= frame.width + margin;
    }

    void render(const std::vector<Event>& events){
        int nl = std::clamp((int)layers.size(), 1, kMaxLayers);
        canvases.resize((size_t)nl);
        for(auto& c : canvases){ if(c.w != frame.width || c.h != frame.height) c.resize(frame.width, frame.height); else c.clear(); }
        if(scratch.w != frame.width || scratch.h != frame.height) scratch.resize(frame.width, frame.height);
        if(group.w != frame.width || group.h != frame.height) group.resize(frame.width, frame.height);
        stats = Stats{};
        // transpose slots act on note events before drawing (event-level); scoped ones on targets only
        std::vector<const Event*> transposes, insts, elems;
        for(auto& e : events) if(e.kind == EventKind::Effect){
            int sc = blob::scopeOf(e);
            if(e.look == "transpose") transposes.push_back(&e);
            else if(sc == 1) insts.push_back(&e);
            else if(sc == 2 && !blob::isCopyEffect(e)) elems.push_back(&e);
        }
        for(int L = 0; L < nl; L++){
            // instances with their own buffer: blob-masked or targeted by an instance insert
            std::vector<std::string> own;
            auto isOwn = [&](const std::string& id){ return std::find(own.begin(), own.end(), id) != own.end(); };
            for(auto& e : events){
                if(e.kind == EventKind::Effect || std::clamp(e.layer, 0, nl - 1) != L) continue;
                bool o = false;
                for(auto f : insts) if(blob::targets(*f, e)) o = true;
                if(o && !isOwn(e.instance)) own.push_back(e.instance);
            }
            // effects on the instruments listed before them (scope "earlier"), in their order
            std::vector<const Event*> early;
            for(auto& e : events) if(blob::isEarlier(e) && e.look != "transpose" && std::clamp(e.layer, 0, nl - 1) == L) early.push_back(&e);
            std::stable_sort(early.begin(), early.end(), [](const Event* a, const Event* b){ return blob::drawOrderOf(*a) < blob::drawOrderOf(*b); });
            // instances straight into the layer, isolated ones grouped then merged; with early effects
            // the layer is drawn in stages by drawOrder (groups too) and each such effect applied between
            std::map<std::string, double> ownOrder;
            for(auto& e : events) if(e.kind != EventKind::Effect && isOwn(e.instance)) ownOrder.emplace(e.instance, blob::drawOrderOf(e));
            size_t nStage = early.size() + 1;
            for(size_t stage = 0; stage < nStage; stage++){
                double lo = stage > 0 ? blob::drawOrderOf(*early[stage - 1]) : -1e300;   // drawOrder range of this stage
                double hi = stage < early.size() ? blob::drawOrderOf(*early[stage]) : 1e300;
                for(int pass = -1; pass < (int)own.size(); pass++){
                    if(pass >= 0){ double o = ownOrder[own[(size_t)pass]]; if(o < lo || o >= hi) continue; group.clear(); }
                    Canvas& dst = pass < 0 ? canvases[(size_t)L] : group;
                    for(auto& e0 : events){
                        if(e0.kind == EventKind::Effect || std::clamp(e0.layer, 0, nl - 1) != L) continue;
                        if(pass < 0 ? isOwn(e0.instance) : e0.instance != own[(size_t)pass]) continue;
                        if(pass < 0){ double o = blob::drawOrderOf(e0); if(o < lo || o >= hi) continue; }
                        if(!visible(e0)){ stats.culled++; continue; }
                        const Event* ep = &e0; Event tmp;
                        for(auto t : transposes){
                            bool on = blob::scopeOf(*t) == 0 ? t->layer == e0.layer : blob::targets(*t, e0);
                            if(on && e0.pitched && e0.start >= t->start && e0.start < t->start + t->length){
                                if(ep != &tmp){ tmp = e0; ep = &tmp; }
                                tmp.degree += (int)std::lround(t->param("transposeDegrees", 2));
                            }
                        }
                        drawProcessed(dst, *ep, elems);
                        stats.drawn++;
                    }
                    if(pass >= 0){
                        for(auto f : insts) if(f->layer == L && (f->param("fxTarget", 0) == 0 || f->param("fxTarget", 0) == blob::targetHash(own[(size_t)pass]))){
                            applyEffect(group, *f, viewOf(L)); stats.effects++;
                        }
                        mergeInto(canvases[(size_t)L], group, 0, group.w - 1, false);
                    }
                }
                if(stage < early.size()){ applyEffect(canvases[(size_t)L], *early[stage], viewOf(L)); stats.effects++; }
            }
        }
        // 3D: keep the layers before bus effects / covers (their gain is carried to the display)
        disp3D = false;
        for(auto& e : events) if(is3D(e)){ disp3D = true; break; }
        if(disp3D){ before.resize((size_t)nl); for(int L = 0; L < nl; L++) before[(size_t)L] = canvases[(size_t)L]; }
        for(auto& e : events) if(e.kind == EventKind::Effect && e.look != "transpose" && blob::scopeOf(e) == 0){
            int L = std::clamp(e.layer, 0, nl - 1);
            applyEffect(canvases[(size_t)L], e, viewOf(L)); stats.effects++;
        }
        applyCovers(events, nl);
        compositeLayers(nl);
        if(disp3D) renderDisplay(events, transposes, elems, nl);
    }

    // ---------------------------------------------------------------- 3D view (5.14)
    // Display stage only. An event's placement maps a flat pixel q to V + (q + off - V) * s:
    // off moves its head from the flat position onto its path, s scales toward the vanishing
    // point V (perspective). Identity once the event's start has crossed the playhead, so every
    // element is flat at its beat; x stays right of the playhead before (paths and V never cross it).
    struct Place {
        double ox = 0, oy = 0, s = 1, vx = 0, vy = 0, gain = 1; int blur = 0;
        bool identity() const { return ox == 0 && oy == 0 && s == 1 && gain == 1 && blur == 0; }
        double mapX(double x) const { return vx + (x + ox - vx) * s; }
        double mapY(double y) const { return vy + (y + oy - vy) * s; }
    };
    static bool pathFlat(const Event& e){
        return std::lround(e.param("pathKind", 0)) == 0 && e.param("pathAngle", 0) == 0 && e.param("pathCurve", 0) == 0 && e.param("pathSpeed", 1) == 1;
    }
    bool is3D(const Event& e) const { return e.kind != EventKind::Effect && (frame.td.on(e.layer) || !pathFlat(e)); }
    Place placeOf(const Event& e) const {
        Place pl;
        if(!is3D(e)) return pl;
        LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
        double dpx = (e.start - frame.nowBeat) * v.pxPerBeat * d;     // flat distance ahead, px
        if(dpx <= 0) return pl;
        double W = frame.width, H = frame.height, hx = frame.headerX;
        double row = v.yOf(linesOf(e).front());
        double dp = dpx * std::max(0.01, e.param("pathSpeed", 1));
        int kind = std::clamp((int)std::lround(e.param("pathKind", 0)), 0, 5);
        double headX = hx + dp, headY = row;
        if(kind == 3){                                             // point: from an origin right of the playhead
            double Ox = hx + std::clamp(e.param("pathOriginX", 0.7), 0.02, 1.0) * (W - hx), Oy = (1 - e.param("pathOriginY", 0.5)) * H;
            double L = std::max(1.0, std::hypot(Ox - hx, Oy - row)), g = 1 - std::exp(-dp / L);
            headX = hx + (Ox - hx) * g; headY = row + (Oy - row) * g;
        } else if(kind == 4){                                      // spiral, shrinking onto the target
            double ph = e.param("pathTurns", 1.5) * 2 * M_PI * dp / W + e.param("pathAngle", 0) * M_PI / 180;
            headX = hx + dp * (0.6 + 0.4 * std::cos(ph)); headY = row - dp * 0.6 * std::sin(ph);
        } else {                                                   // straight / curved: right, top, bottom, curve
            double ang = std::clamp(e.param("pathAngle", 0) + (kind == 1 ? 60 : kind == 2 ? -60 : 0), -85.0, 85.0) * M_PI / 180;
            double c = e.param("pathCurve", 0); if(kind == 5 && c == 0) c = 1;
            headY = row - (std::tan(ang) * dp + c * dp * dp / W);
        }
        pl.ox = headX - (hx + dpx); pl.oy = headY - row;
        if(frame.td.on(e.layer)){
            const ThreeD& t = frame.td; size_t L = (size_t)std::clamp(e.layer, 0, 3);
            double zeff = std::max(0.05, 1 + t.planeZ[L] - t.camera - t.layerCamera[L]);   // plane distance from the camera
            double z = std::max(0.0, t.amount) * zeff * dpx / W;
            pl.s = 1 / (1 + z);
            pl.vx = hx + std::clamp(t.vanishX, 0.1, 1.0) * (W - hx); pl.vy = (1 - t.vanishY) * H;
            pl.gain = std::exp(-std::max(0.0, t.fog) * z);
            pl.blur = (int)std::lround(std::min(8.0, std::max(0.0, t.dof) * 8 * z));
        }
        return pl;
    }
    // visible in the 3D view (flat visibility for flat events)
    bool shown(const Event& e) const {
        if(!is3D(e)) return visible(e);
        Place pl = placeOf(e); auto sp = spanOf(e);
        double m = 64;
        return pl.mapX(sp.second + m) >= 0 && pl.mapX(sp.first - m) <= frame.width;
    }
    // what is shown (the 3D view when any event is placed in 3D, else the flat composite)
    const Canvas& display() const { return disp3D ? dispOut : out; }
    const Canvas& displayLayer(int i) const { return disp3D ? disp[(size_t)i] : canvases[(size_t)i]; }
    bool displayIs3D() const { return disp3D; }

    // centre height of an event and the heights of its lines (fundamental + partials)
    std::vector<double> linesOf(const Event& e) const {
        std::vector<double> l;
        switch(e.kind){
            case EventKind::Note: {
                double semis = e.pitched ? frame.scale.semitonesOf(e.degree) : frame.space.semisOf(e.height);
                int partials = (int)std::lround(e.param("partials", 4));
                double spread = e.param("partialSpread", 1);
                for(int p = 0; p <= partials; p++) l.push_back(frame.space.heightOf(semis + 12 * std::log2(1 + p * spread)));
                break; }
            case EventKind::Hit: l.push_back(0.5 * (e.param("heightLow", e.param("bandLow", e.height)) + e.param("heightHigh", e.param("bandHigh", e.height2)))); break;
            case EventKind::Mass: l.push_back(0.5 * (e.param("heightFrom", e.height) + e.param("heightTo", e.height2))); break;
            case EventKind::Sweep: l.push_back(0.5 * (e.param("bandLow", e.param("bottomFrom", e.height)) + e.param("bandHigh", e.param("topTo", e.height2)))); break;
            case EventKind::Shape: l.push_back(e.param("heightCenter", e.pitched ? frame.space.heightOf(frame.scale.semitonesOf(e.degree)) : e.height)); break;
            default: l.push_back(e.height);
        }
        return l;
    }
    // the blob mask of an event, in its own beat space
    blob::Mask maskOf(const Event& e) const {
        double drawn = e.length + tailBeats(e);
        blob::Source so = blob::sourceOf(e, drawn);
        so.lines = linesOf(e); so.evHeight = so.lines.front();
        LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
        double b0 = v.nowBeat - v.headerX / (v.pxPerBeat * d), b1 = v.nowBeat + (v.width - v.headerX) / (v.pxPerBeat * d);
        blob::Mask m; m.build(blob::paramsOf(e), so, std::max(b0, e.start - 1), std::min(b1, e.start + drawn + 1));
        return m;
    }

    // draw one event into a canvas (also used for cover masks)
    void drawEvent(Canvas& c, const Event& e) const {
        LayerView v = viewOf(e.layer);
        Ctx k{e, frame, v}; k.depth = eventDepth(e, frame);
        double x0 = v.xOf(e.start, k.depth), x1 = v.xOf(e.start + e.length + tailBeats(e), k.depth);
        switch(e.kind){
            case EventKind::Note: drawNote(c, k, x0, x1); break;
            case EventKind::Hit: drawHit(c, k, x0, x1); break;
            case EventKind::Mass: drawMass(c, k, x0, v.xOf(e.start + e.length, k.depth)); break;
            case EventKind::Sweep: drawSweep(c, k, x0, v.xOf(e.start + e.length, k.depth)); break;
            case EventKind::Shape: drawShape(c, k, x0, x1); break;
            default: break;
        }
    }

    // brightness per row (max of R, G, B) of the composite: what the sonification reads
    // With a non-brightness reading mode on any layer (5.18): max over layers of each layer's
    // reading (as the compositor sees the layer). The background never enters.
    std::vector<float> column(int x) const {
        std::vector<float> col((size_t)out.h, 0.f);
        if(x < 0 || x >= out.w) return col;
        if(frame.paper.anyReading()){
            for(int L = 0; L < (int)canvases.size(); L++){ auto c = layerColumn(L, x); for(size_t y = 0; y < col.size() && y < c.size(); y++) col[y] = std::max(col[y], c[y]); }
            return col;
        }
        for(int y = 0; y < out.h; y++){ const float* p = out.at(x, y); col[(size_t)y] = std::min(1.f, std::max({p[0], p[1], p[2]})); }
        return col;
    }
    // layer L's reading at column x (gain, alpha from brightness and the layer's reading mode)
    std::vector<float> layerColumn(int L, int x) const {
        if(L < 0 || L >= (int)canvases.size()) return {};
        const Canvas& c = canvases[(size_t)L];
        std::vector<float> col((size_t)c.h, 0.f);
        if(x < 0 || x >= c.w) return col;
        float g = (float)(L < (int)layers.size() ? layers[(size_t)L].gain : 1.0);
        int m = frame.paper.reading[(size_t)std::min(L, 3)]; bool afb = frame.paper.alphaFromBrightness[(size_t)std::min(L, 3)];
        for(int y = 0; y < c.h; y++){ float q[4]; layerPixel(c.at(x, y), g, afb, q); col[(size_t)y] = std::min(1.f, readPixel(q, m)); }
        return col;
    }
    // the blend a layer composites with (the paper override first)
    int blendOf(int L) const {
        const std::string& o = frame.paper.blend[(size_t)std::clamp(L, 0, 3)];
        return blendIndex(!o.empty() ? o : (L < (int)layers.size() ? layers[(size_t)L].blend : std::string("lighten")));
    }

    // 8-bit RGB of what is shown (over black): the flat composite unless 3D is on
    std::vector<uint8_t> rgb8(const Canvas* c = nullptr) const {
        const Canvas& s = c ? *c : display();
        std::vector<uint8_t> o((size_t)s.w * s.h * 3);
        for(size_t i = 0; i < (size_t)s.w * s.h; i++) for(int k = 0; k < 3; k++) o[i * 3 + k] = (uint8_t)std::lround(std::clamp(s.px[i * 4 + k], 0.f, 1.f) * 255);
        return o;
    }
    bool writePPM(const std::string& path, const Canvas* c = nullptr) const {
        const Canvas& s = c ? *c : display();
        FILE* f = std::fopen(path.c_str(), "wb"); if(!f) return false;
        std::fprintf(f, "P6\n%d %d\n255\n", s.w, s.h);
        auto b = rgb8(&s); std::fwrite(b.data(), 1, b.size(), f); std::fclose(f); return true;
    }

private:
    std::vector<Canvas> canvases;
    Canvas out, scratch, group;
    std::vector<Canvas> before, disp;   // 3D: layers before bus effects; display layers
    Canvas dispOut;
    bool disp3D = false;

    // 3D display of every layer: 3D events warped from their processed buffer, carrying the
    // layer's effect / cover gain (score space) sampled at the source pixel
    void renderDisplay(const std::vector<Event>& events, const std::vector<const Event*>& transposes,
                       const std::vector<const Event*>& elems, int nl){
        int W = frame.width, H = frame.height;
        disp.resize((size_t)nl);
        std::vector<float> g((size_t)W * H);
        for(int L = 0; L < nl; L++){
            Canvas& D = disp[(size_t)L];
            bool has = false;
            for(auto& e : events) if(std::clamp(e.layer, 0, nl - 1) == L && is3D(e)){ has = true; break; }
            if(!has){ D = canvases[(size_t)L]; continue; }
            if(D.w != W || D.h != H) D.resize(W, H); else D.clear();
            const Canvas& A = canvases[(size_t)L]; const Canvas& B = before[(size_t)L];
            for(size_t i = 0; i < g.size(); i++){
                float b = std::max({B.px[i * 4], B.px[i * 4 + 1], B.px[i * 4 + 2], B.px[i * 4 + 3]});
                float a = std::max({A.px[i * 4], A.px[i * 4 + 1], A.px[i * 4 + 2], A.px[i * 4 + 3]});
                g[i] = b > 1e-4f ? std::clamp(a / b, 0.f, 2.f) : 1.f;
            }
            for(auto& e0 : events){
                if(e0.kind == EventKind::Effect || std::clamp(e0.layer, 0, nl - 1) != L) continue;
                Event e = e0;
                for(auto t : transposes){
                    bool on = blob::scopeOf(*t) == 0 ? t->layer == e0.layer : blob::targets(*t, e0);
                    if(on && e0.pitched && e0.start >= t->start && e0.start < t->start + t->length) e.degree += (int)std::lround(t->param("transposeDegrees", 2));
                }
                Place pl = placeOf(e);
                // far events: drawn shifted onto the canvas, the shift added back by the placement
                LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
                auto sp = spanOf(e);
                double shift = 0;
                if(sp.first > 0.75 * W){ shift = sp.first - (frame.headerX + 0.25 * (W - frame.headerX)); }
                Event te = e; te.start -= shift / std::max(1e-9, v.pxPerBeat * d);
                auto spt = spanOf(te);
                if(spt.second < -64 || spt.first > W + 64) continue;
                // destination bounds
                double m = 64 + (e.kind == EventKind::Shape ? e.param("heightSize", 0.2) * H + e.param("shapeLength", 1) * v.pxPerBeat * d : 0);
                int X0 = std::max(0, (int)std::floor(pl.mapX(spt.first - m + shift))), X1 = std::min(W - 1, (int)std::ceil(pl.mapX(spt.second + m + shift)));
                int Y0 = std::max(0, (int)std::floor(pl.mapY(-m))), Y1 = std::min(H - 1, (int)std::ceil(pl.mapY(H + m)));
                if(X0 > X1 || Y0 > Y1){ stats.culled++; continue; }
                auto cols = colsOf(te);
                processScratch(te, elems, cols);
                bool add = e.kind == EventKind::Mass || e.look == "additive";
                float gain = (float)pl.gain; int r = pl.blur;
                for(int Y = Y0; Y <= Y1; Y++) for(int X = X0; X <= X1; X++){
                    double sx = (X + 0.5 - pl.vx) / pl.s + pl.vx - pl.ox - shift, sy = (Y + 0.5 - pl.vy) / pl.s + pl.vy - pl.oy;
                    int ix = (int)std::floor(sx), iy = (int)std::floor(sy);
                    float c[4] = {0, 0, 0, 0};
                    if(r == 0){
                        if(!scratch.inside(ix, iy)) continue;
                        const float* q = scratch.at(ix, iy); for(int k = 0; k < 4; k++) c[k] = q[k];
                    } else {
                        int n = 0;
                        for(int yy = iy - r; yy <= iy + r; yy++) for(int xx = ix - r; xx <= ix + r; xx++){
                            n++; if(!scratch.inside(xx, yy)) continue;
                            const float* q = scratch.at(xx, yy); for(int k = 0; k < 4; k++) c[k] += q[k];
                        }
                        for(int k = 0; k < 4; k++) c[k] /= (float)n;
                    }
                    if(c[3] <= 0 && c[0] <= 0 && c[1] <= 0 && c[2] <= 0) continue;
                    int fx = (int)std::lround(ix + shift);
                    float gg = gain * (fx >= 0 && fx < W && iy >= 0 && iy < H ? g[(size_t)iy * W + fx] : 1.f);
                    float* o = D.at(X, Y);
                    for(int k = 0; k < 4; k++){ float vv = c[k] * gg; o[k] = add ? std::min(1.f, o[k] + vv) : std::max(o[k], vv); }
                }
                for(int y = 0; y < scratch.h; y++) std::fill(scratch.at(cols.first, y), scratch.at(cols.first, y) + (cols.second - cols.first + 1) * 4, 0.f);
                stats.drawn3D++;
            }
        }
        compositeInto(dispOut, disp, nl);
    }
    // an event with its element effects and blob mask, into the scratch buffer
    void processScratch(const Event& e, const std::vector<const Event*>& elems, std::pair<int, int> cols){
        drawEvent(scratch, e);
        LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
        LayerView vd = v; vd.pxPerBeat = v.pxPerBeat * d;
        for(auto f : elems) if(blob::targets(*f, e) && e.start >= f->start - 1e-9 && e.start < f->start + f->length){
            Event slot = *f; slot.start = e.start; slot.length = e.length + tailBeats(e);
            applyEffectSpan(scratch, slot, vd, vd.xOf(slot.start), vd.xOf(slot.start + slot.length));
        }
        if(blob::enabled(e)) applyBlobCols(scratch, maskOf(e), vd, cols.first, cols.second, frame.nowBeat);
    }

    // columns an event can touch
    std::pair<int, int> colsOf(const Event& e) const {
        auto sp = spanOf(e);
        double m = 64 + (e.kind == EventKind::Shape ? e.param("heightSize", 0.2) * frame.height + e.param("shapeLength", 1) * viewOf(e.layer).pxPerBeat * eventDepth(e, frame) : 0);
        return {std::max(0, (int)std::floor(sp.first - m)), std::min(frame.width - 1, (int)std::ceil(sp.second + m))};
    }
    // max (lighten) or add of src columns into dst
    static void mergeInto(Canvas& dst, const Canvas& src, int a0, int a1, bool add){
        for(int y = 0; y < dst.h; y++) for(int x = a0; x <= a1; x++){
            const float* s = src.at(x, y); if(s[3] <= 0 && s[0] <= 0 && s[1] <= 0 && s[2] <= 0) continue;
            float* d = dst.at(x, y);
            for(int i = 0; i < 4; i++) d[i] = add ? std::min(1.f, d[i] + s[i]) : std::max(d[i], s[i]);
        }
    }
    // draw one event; with a blob mask or element-scope effects through the scratch buffer
    void drawProcessed(Canvas& dst, const Event& e, const std::vector<const Event*>& elems){
        bool masked = blob::enabled(e);
        std::vector<const Event*> mine;
        for(auto f : elems) if(blob::targets(*f, e) && e.start >= f->start - 1e-9 && e.start < f->start + f->length) mine.push_back(f);
        if(!masked && mine.empty()){ drawEvent(dst, e); return; }
        auto cols = colsOf(e);
        drawEvent(scratch, e);
        LayerView v = viewOf(e.layer); double d = eventDepth(e, frame);
        LayerView vd = v; vd.pxPerBeat = v.pxPerBeat * d;
        for(auto f : mine){
            Event slot = *f; slot.start = e.start; slot.length = e.length + tailBeats(e);
            applyEffectSpan(scratch, slot, vd, vd.xOf(slot.start), vd.xOf(slot.start + slot.length));
            stats.effects++;
        }
        if(masked) applyBlobCols(scratch, maskOf(e), vd, cols.first, cols.second, frame.nowBeat);
        bool add = e.kind == EventKind::Mass || e.look == "additive";
        mergeInto(dst, scratch, cols.first, cols.second, add);
        for(int y = 0; y < scratch.h; y++) std::fill(scratch.at(cols.first, y), scratch.at(cols.first, y) + (cols.second - cols.first + 1) * 4, 0.f);
    }

    void applyCovers(const std::vector<Event>& events, int nl){
        // group cover events by (source layer, settings): one mask per group
        std::map<std::pair<int, CoverKey>, std::vector<const Event*>> groups;
        for(auto& e : events){
            if(e.kind == EventKind::Effect || e.param("coverEnable", 0) < 0.5 || !visible(e)) continue;
            CoverKey key{e.param("coverDilate", 2), e.param("coverFeather", 4), e.param("coverRelease", 0.25), e.param("coverAmount", 1),
                         (int)e.param("coverMode", 0), (int)e.param("coverLayers", 0)};
            groups[{std::clamp(e.layer, 0, nl - 1), key}].push_back(&e);
        }
        int W = frame.width, H = frame.height;
        Canvas tmp; tmp.resize(W, H);
        std::vector<float> m((size_t)W * H);
        for(auto& g : groups){
            int L = g.first.first; const CoverKey& ck = g.first.second;
            tmp.clear();
            for(auto e : g.second) drawEvent(tmp, *e);
            for(size_t i = 0; i < m.size(); i++) m[i] = tmp.px[i * 4 + 3] > 0.02f ? 1.f : tmp.px[i * 4 + 3] / 0.02f;
            dilateMask(m, W, H, (int)std::lround(ck.dilate));
            trailMask(m, W, H, ck.release * viewOf(L).pxPerBeat);
            blurMask(m, W, H, (int)std::lround(ck.feather * 0.5));
            for(int t = 0; t < nl; t++){
                bool on = ck.target == 2 ? t != L : (ck.target == 1 ? t == L - 1 : t < L);
                if(!on) continue;
                Canvas& c = canvases[(size_t)t];
                for(size_t i = 0; i < m.size(); i++){
                    float k = (float)(ck.amount * m[i]); if(k <= 0) continue;
                    float* p = &c.px[i * 4];
                    for(int j = 0; j < 3; j++) p[j] *= 1 - k;
                    p[3] = ck.mode == 1 ? p[3] + (1 - p[3]) * k : p[3] * (1 - k);   // black: opaque black
                }
            }
            stats.covers++;
        }
    }

    // premultiplied compositing bottom to top
    void compositeLayers(int nl){ compositeInto(out, canvases, nl); }
    void compositeInto(Canvas& out, const std::vector<Canvas>& canvases, int nl){
        if(out.w != frame.width || out.h != frame.height) out.resize(frame.width, frame.height); else out.clear();
        for(int L = 0; L < nl; L++){
            const LayerDef& d = layers[(size_t)L];
            float g = (float)d.gain;
            int mode = blendOf(L);
            bool afb = frame.paper.alphaFromBrightness[(size_t)std::min(L, 3)];
            const Canvas& c = canvases[(size_t)L];
            for(size_t i = 0; i < out.px.size(); i += 4){
                float s[4]; layerPixel(&c.px[i], g, afb, s);
                blendPixel(&out.px[i], s, mode);
            }
        }
    }
};

} // namespace draw
} // namespace forms2
