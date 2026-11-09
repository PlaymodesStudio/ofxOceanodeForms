//
//  v2Blob.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Blob masks and effect scopes (ROADMAP 5.12). Plain C++17, shared by the CPU reference
//  (v2Draw.h), the GPU path (v2DrawGL.h) and the engine (v2Engine.h).
//
//  A blob mask is evaluated in beat space (beat, height 0..1), so it moves with the content. Shapes:
//  an ellipse whose radius is deformed by periodic noise around its contour (irregularity, detail,
//  octaves), stretched and tapered on its later side (tail), feathered (softness), optionally
//  wobbling in time (render animated). Perlin placement thresholds a 2D noise field instead.
//
//  Event::p keys read (schema keys of v2Schema.h, enums as option index):
//    blob (0 off, 1 ghost, 2 fish, 3 microbe, 4 cloud, 5 tear, 6 custom), blobAmount, blobPlacement
//    (zone, partials, fullHeight, perlin, events), blobMode (in, out), blobScope (element, phrase,
//    general), blobCount, blobScale, blobSizeBeats, blobSizeHeight, blobAspect, blobRotation,
//    blobIrregularity, blobDetail, blobOctaves, blobTail, blobSoftness, blobZoneLow / High,
//    blobThreshold, blobWobble, blobWobbleRate.
//  Engine stamps (absent in synthetic events: element semantics from Event::seed):
//    blobSeedP / blobPhStart / blobPhLen   phrase seed (keep / mutate / new aware) and span
//    blobSeedG                             general (instance-wide, continuous) seed
//  Effect scope keys: fxScope (0 layer, 1 instance, 2 element, 3 earlier), fxTarget (hash of the target id).
//

#pragma once

#include "v2Core.h"
#include <array>

namespace forms2 {
namespace blob {

// seeds through doubles (Event::p): 52 bits are exact
inline double seedToP(uint64_t s){ return (double)(s >> 12); }
inline uint64_t seedFromP(double d){ return (uint64_t)d; }
// target id hash stored in fxTarget
inline double targetHash(const std::string& id){ return id.empty() ? 0.0 : (double)((hashTag(id) & 0xFFFFFFFFull) | 1ull); }

inline double clamp01(double x){ return std::clamp(x, 0.0, 1.0); }
inline double smooth01(double t){ t = clamp01(t); return t * t * (3 - 2 * t); }
inline double h01(uint64_t seed, int64_t a, int64_t b = 0){ return (double)(mixSeed(seed, (uint64_t)a, (uint64_t)b) >> 11) * (1.0 / 9007199254740992.0); }

enum Placement { Zone = 0, Partials = 1, FullHeight = 2, Perlin = 3, OnEvents = 4 };
enum Scope { Element = 0, Phrase = 1, General = 2 };

struct Shape {
    double sizeBeats = 2, sizeHeight = 0.3, aspect = 1, rotation = 0;
    double irregularity = 0.3, detail = 2; int octaves = 2; double tail = 0, softness = 0.3;
};

inline const std::vector<std::string>& presetNames(){
    static const std::vector<std::string> n{"off", "ghost", "fish", "microbe", "cloud", "tear", "custom"};
    return n;
}
// a preset = a set of shape values; false for off / custom
inline bool presetShape(int idx, Shape& s){
    switch(idx){
        case 1: s = {2.0, 0.35, 0.8, 0, 0.25, 1.5, 2, 0.35, 0.6}; return true;    // ghost: soft, rounded, a little tail
        case 2: s = {1.5, 0.12, 1.0, 0, 0.1, 1.0, 1, 0.8, 0.2}; return true;      // fish: slim with a long tail
        case 3: s = {0.6, 0.12, 1.0, 0, 0.6, 7.0, 3, 0.0, 0.1}; return true;      // microbe: small and spiky
        case 4: s = {4.0, 0.4, 1.2, 0, 0.45, 2.5, 4, 0.1, 0.8}; return true;      // cloud: large, billowing, soft
        case 5: s = {0.5, 1.1, 1.0, 0, 0.35, 5.0, 3, 0.2, 0.15}; return true;     // tear: narrow, ragged, tall
        default: return false;
    }
}

struct Params {
    int preset = 0; Shape shape;
    double scale = 1; int placement = Zone, scope = Phrase, count = 3;
    bool maskOut = false; double amount = 1, zoneLow = 0, zoneHigh = 1, threshold = 0.55, wobble = 0, wobbleRate = 2;
};
inline bool enabled(const Event& e){ return e.param("blob", 0) >= 0.5; }
inline Params paramsOf(const Event& e){
    Params P;
    P.preset = (int)std::lround(e.param("blob", 0));
    if(!presetShape(P.preset, P.shape)){
        Shape& s = P.shape;
        s.sizeBeats = e.param("blobSizeBeats", 2); s.sizeHeight = e.param("blobSizeHeight", 0.3); s.aspect = e.param("blobAspect", 1);
        s.rotation = e.param("blobRotation", 0); s.irregularity = e.param("blobIrregularity", 0.3); s.detail = e.param("blobDetail", 2);
        s.octaves = (int)std::lround(e.param("blobOctaves", 2)); s.tail = e.param("blobTail", 0); s.softness = e.param("blobSoftness", 0.3);
    }
    P.scale = e.param("blobScale", 1);
    P.placement = std::clamp((int)std::lround(e.param("blobPlacement", 0)), 0, 4);
    P.scope = std::clamp((int)std::lround(e.param("blobScope", 1)), 0, 2);
    P.count = std::clamp((int)std::lround(e.param("blobCount", 3)), 1, 32);
    P.maskOut = e.param("blobMode", 0) >= 0.5;
    P.amount = clamp01(e.param("blobAmount", 1));
    P.zoneLow = e.param("blobZoneLow", 0); P.zoneHigh = e.param("blobZoneHigh", 1);
    P.threshold = e.param("blobThreshold", 0.55);
    P.wobble = clamp01(e.param("blobWobble", 0)); P.wobbleRate = std::max(0.05, e.param("blobWobbleRate", 2));
    return P;
}

// where the blobs of one event come from
struct Source {
    uint64_t elemSeed = 0, phraseSeed = 0, generalSeed = 0;
    double phStart = 0, phLen = 16;
    double evStart = 0, evLen = 1;    // drawn span of the event (beats, tail included)
    double evHeight = 0.5;            // centre height (events placement)
    std::vector<double> lines;        // fundamental + partial heights (partials placement)
};
inline Source sourceOf(const Event& e, double drawnLen){
    Source s;
    s.elemSeed = mixSeed(e.seed ^ 0xB10B, e.id);
    s.phraseSeed = e.p.count("blobSeedP") ? seedFromP(e.param("blobSeedP", 0)) : s.elemSeed;
    s.generalSeed = e.p.count("blobSeedG") ? seedFromP(e.param("blobSeedG", 0)) : mixSeed(hashTag(e.instance), 0x6E);
    s.phStart = e.p.count("blobPhStart") ? e.param("blobPhStart", 0) : e.start;
    s.phLen = e.p.count("blobPhLen") ? std::max(1e-3, e.param("blobPhLen", 16)) : std::max(1e-3, drawnLen);
    s.evStart = e.start; s.evLen = std::max(1e-3, drawnLen);
    s.evHeight = e.height;
    return s;
}

struct Blob { double cb = 0, ch = 0.5, rb = 1, rh = 0.1, rot = 0; uint64_t seed = 0; };

// periodic value noise around the contour, -1..1 (t 0..1 = one turn), time row for the wobble
inline double ringNoise(uint64_t seed, double t, double detail, int octaves, double row){
    double sum = 0, amp = 1, norm = 0;
    int64_t r0 = (int64_t)std::floor(row); double rf = smooth01(row - std::floor(row));
    for(int o = 0; o < std::max(1, octaves); o++){
        int N = std::max(2, (int)std::lround(detail * 2 * std::pow(2.0, o)));
        double x = t * N, f = std::floor(x), u = smooth01(x - f);
        int a = ((int)f % N + N) % N, b = (a + 1) % N;
        auto at = [&](int64_t rr){ return h01(seed, a + o * 997, rr) * (1 - u) + h01(seed, b + o * 997, rr) * u; };
        double v = at(r0) * (1 - rf) + at(r0 + 1) * rf;
        sum += amp * (v * 2 - 1); norm += amp; amp *= 0.5;
    }
    return sum / norm;
}
// smooth 2D value noise 0..1
inline double vnoise2(uint64_t seed, double x, double y){
    double fx = std::floor(x), fy = std::floor(y), u = smooth01(x - fx), v = smooth01(y - fy);
    int64_t ix = (int64_t)fx, iy = (int64_t)fy;
    double a = h01(seed, ix, iy), b = h01(seed, ix + 1, iy), c = h01(seed, ix, iy + 1), d = h01(seed, ix + 1, iy + 1);
    return (a * (1 - u) + b * u) * (1 - v) + (c * (1 - u) + d * u) * v;
}
inline double fbm2(uint64_t seed, double x, double y, int octaves){
    double s = 0, amp = 1, n = 0;
    for(int o = 0; o < std::max(1, octaves); o++){ s += amp * vnoise2(mixSeed(seed, (uint64_t)o + 1), x, y); n += amp; amp *= 0.5; x *= 2; y *= 2; }
    return s / n;
}

// The mask of one event (or one effect slot): built once per frame, evaluated per pixel
class Mask {
public:
    Params P;
    std::vector<Blob> blobs;
    uint64_t fieldSeed = 0;

    // beatLo / beatHi: the beats the mask will be read over (general scope builds the cells there)
    void build(const Params& p, const Source& s, double beatLo, double beatHi){
        P = p; blobs.clear();
        const Shape& sh = P.shape;
        double rb0 = 0.5 * sh.sizeBeats * P.scale * std::sqrt(std::max(0.05, sh.aspect));
        double rh0 = 0.5 * sh.sizeHeight * P.scale / std::sqrt(std::max(0.05, sh.aspect));
        uint64_t scopeSeed = P.scope == Element ? s.elemSeed : (P.scope == Phrase ? s.phraseSeed : s.generalSeed);
        fieldSeed = scopeSeed;
        if(P.placement == Perlin) return;
        auto make = [&](uint64_t seed, double lo, double hi){
            Rng r(seed);
            Blob b; b.seed = mixSeed(seed, 0xC0);
            b.cb = r.uniform(lo, hi);
            double jit = 0.75 + 0.5 * r.uniform();
            b.rb = rb0 * jit; b.rh = rh0 * jit;
            double hz = r.uniform(std::min(P.zoneLow, P.zoneHigh), std::max(P.zoneLow, P.zoneHigh));
            b.rot = sh.rotation * M_PI / 180;
            if(P.placement == FullHeight){ b.ch = 0.5; b.rh = 0.58; b.rb = 0.5 * sh.sizeBeats * P.scale * jit; b.rot = 0; }
            else if(P.placement == Partials && !s.lines.empty()){
                // the phrase / field picks a height, snapped to this event's nearest line
                double best = s.lines[0];
                for(double l : s.lines) if(std::fabs(l - hz) < std::fabs(best - hz)) best = l;
                b.ch = best;
            } else b.ch = hz;
            blobs.push_back(b);
        };
        if(P.placement == OnEvents){
            Blob b; b.seed = mixSeed(scopeSeed, 0xE0);
            b.cb = s.evStart + s.evLen * 0.5; b.ch = s.evHeight;
            b.rb = rb0 + s.evLen * 0.5; b.rh = rh0; b.rot = sh.rotation * M_PI / 180;
            blobs.push_back(b);
            return;
        }
        if(P.scope == General){
            // cells of 16 / count beats, one blob each, in absolute beats (no seams at phrases)
            double C = 16.0 / P.count;
            double margin = (rb0 * 1.25 + 0.5 * sh.sizeBeats * P.scale) * (1 + 3 * sh.tail) + C;
            int64_t c0 = (int64_t)std::floor((beatLo - margin) / C), c1 = (int64_t)std::floor((beatHi + margin) / C);
            if(c1 - c0 > 4096) c0 = c1 - 4096;
            for(int64_t c = c0; c <= c1; c++) make(mixSeed(scopeSeed, 0xCE11, (uint64_t)c), c * C, (c + 1) * C);
            return;
        }
        double lo = P.scope == Element ? s.evStart : s.phStart;
        double hi = lo + (P.scope == Element ? s.evLen : s.phLen);
        for(int i = 0; i < P.count; i++) make(mixSeed(scopeSeed, 0xB1, (uint64_t)i), lo, hi);
    }

    // 0..1 inside; ppb = px per beat (at the event's depth), Hpx = view height
    double value(double beat, double h, double ppb, double Hpx, double nowBeat) const {
        const Shape& sh = P.shape;
        double row = P.wobble > 0 ? nowBeat / P.wobbleRate : 0;
        if(P.placement == Perlin){
            double sx = std::max(1e-3, sh.sizeBeats * P.scale), sy = std::max(1e-3, sh.sizeHeight * P.scale);
            double f = std::max(0.25, sh.detail * 0.5);
            double wx = sh.irregularity * (fbm2(fieldSeed ^ 0x77, beat / sx, h / sy, 2) - 0.5);
            double n = fbm2(fieldSeed, beat / sx * f + wx + row * 0.3 * P.wobble, h / sy * f, sh.octaves);
            double soft = std::max(0.02, sh.softness * 0.25);
            return smooth01((n - P.threshold) / soft + 0.5);
        }
        double m = 0;
        for(auto& b : blobs){
            m = std::max(m, blobValue(b, beat, h, ppb, Hpx, row));
            if(m >= 1) break;
        }
        return m;
    }
    double gain(double beat, double h, double ppb, double Hpx, double nowBeat) const {
        double m = value(beat, h, ppb, Hpx, nowBeat);
        return P.maskOut ? 1 - P.amount * m : 1 - P.amount * (1 - m);
    }

private:
    double blobValue(const Blob& b, double beat, double h, double ppb, double Hpx, double row) const {
        const Shape& sh = P.shape;
        double rx = std::max(0.5, b.rb * ppb), ry = std::max(0.5, b.rh * Hpx);
        double px = (beat - b.cb) * ppb, py = (h - b.ch) * Hpx;
        double c = std::cos(b.rot), s = std::sin(b.rot);
        double nx = (px * c + py * s) / rx, ny = (-px * s + py * c) / ry;
        double irr = sh.irregularity;
        if(nx > 0){                                                  // tail on the later side
            nx /= 1 + 3 * sh.tail;
            ny /= std::max(0.15, 1 - 0.85 * sh.tail * std::min(1.0, nx));
        }
        double r = std::hypot(nx, ny);
        double feather = std::max(sh.softness, 1.5 / std::min(rx, ry));
        if(r > (1 + 0.6 * irr) * (1 + feather) + 0.05) return 0;
        double t = (std::atan2(ny, nx) + M_PI) / (2 * M_PI);
        double n = ringNoise(b.seed, t, sh.detail, sh.octaves, 0);
        if(P.wobble > 0) n = n * (1 - P.wobble) + ringNoise(b.seed ^ 0xA11, t, sh.detail, sh.octaves, row) * P.wobble;
        double R = std::max(0.2, 1 + 0.6 * irr * n);
        double d = r / R;
        return 1 - smooth01((d - 1) / feather + 0.5);
    }
};

// ---------------------------------------------------------------- effect scopes
inline int scopeOf(const Event& fx){ return std::clamp((int)std::lround(fx.param("fxScope", 0)), 0, 3); }
// scope 3 "earlier": the layer bus as drawn by the instruments listed before the effect (v1 cell
// order). The engine stamps drawOrder (instance index) on every event of such a layer; the
// renderers draw the layer in stages and apply each such effect when its order is reached.
inline bool isEarlier(const Event& fx){ return fx.kind == EventKind::Effect && scopeOf(fx) == 3; }
inline double drawOrderOf(const Event& e){ return e.param("drawOrder", -1); }
// does a scoped effect act on this (non-effect) event?
inline bool targets(const Event& fx, const Event& e){
    if(e.kind == EventKind::Effect || e.layer != fx.layer || e.instance == fx.instance) return false;
    if(scopeOf(fx) == 3) return drawOrderOf(e) < drawOrderOf(fx);
    double t = fx.param("fxTarget", 0);
    return t == 0 || t == targetHash(e.instance);
}
// element-scope effects done as event copies (the engine stores them)
inline bool isCopyEffect(const Event& fx){ return fx.kind == EventKind::Effect && scopeOf(fx) == 2 && (fx.look == "echo" || fx.look == "repeat"); }

// Copies of the target events whose start lies in the effect's slot: echo = fading copies
// echoSpacing apart, repeat = back-to-back copies. Copies keep their instance (the target).
inline std::vector<Event> elementCopies(const Event& fx, const std::vector<Event>& events){
    std::vector<Event> out;
    if(!isCopyEffect(fx)) return out;
    for(auto& e : events){
        if(!targets(fx, e) || e.p.count("fxCopy") || e.start < fx.start - 1e-9 || e.start >= fx.start + fx.length) continue;
        bool echo = fx.look == "echo";
        int n = echo ? std::clamp((int)std::lround(fx.param("echoCount", 3)), 1, 16) : std::clamp((int)std::lround(fx.param("repeats", 2)), 1, 16) - 1;
        double sp = echo ? fx.param("echoSpacing", 0.5) : e.length * e.param("noteLength", 1);
        double dec = echo ? fx.param("echoDecay", 0.5) : 1;
        for(int i = 1; i <= n; i++){
            Event c = e;
            c.id = mixSeed(e.id, fx.id, (uint64_t)i);
            c.start = e.start + i * sp;
            c.level = e.level * std::pow(dec, i);
            c.p["fxCopy"] = i;
            out.push_back(c);
        }
    }
    return out;
}

} // namespace blob
} // namespace forms2
