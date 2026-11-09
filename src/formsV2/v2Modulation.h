//
//  v2Modulation.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The modulation routing matrix (ROADMAP 5.3): sources x destinations, each routing evaluated at
//  its level (event / phrase / layer / scene). Header-only, no openFrameworks.
//
//  Sources (Routing::source names, kind from the prefix or sourceParams "type"):
//    lfo*        {"shape":"sine|triangle|saw|square|smoothRandom", "rateBeats":4 | "ratePhrases":1,
//                 "phase":0, "reset":"phrase|free", "bipolar":false}
//    phraseEnv*  {"shape":"rise|fall|swell|steps|drawn", "steps":4, "points":[[x,y],...] or [y,...]}
//    noteEnv*    same shapes, over the event's own start..end
//    random*     {"scope":"event|beats|phrase|scene", "everyBeats":4, "smooth":false, "bipolar":false}
//    macro:<name> or macro* {"name":"..."}       ctx.macros, 0..1
//    follow:<instance>:<what> {"window":4}        ctx.follower(id, what, window, beat), 0..1
//    brightness                                    ctx.brightness(beat), 0..1
//    arc                                           ctx.arc(beat), 0..1
//    lane:<name>                                   ctx.lane(name, beat), 0..1
//    playhead    {"window":2, "shape":"approach|peak|ringout|distance"}  signed distance of the
//                event (start, or the drawn position) from the playhead; mainly render "animated"
//  Any source may add "render":"drawn|animated" (see evalDrawn / evalAnimated).
//
//  Each source gives s in 0..1 (or -1..1 when bipolar). Applying a routing:
//    s -> curve (sign(s) * |s|^curve) -> invert (unipolar 1-s, bipolar -s)
//    no range:  offset = amount * s * (def.max - def.min)
//    hasRange:  offset = amount * (lerp(min, max, s) - base)   (amount 1 = the value is in [min,max])
//  Offsets of every routing to a destination add up; the result is clamped to [def.min, def.max]
//  and rounded when def.degrees. Chance gates are not modulation (ROADMAP 5.4).
//

#pragma once

#include "v2Core.h"
#include <functional>
#include <string_view>
#include <utility>

namespace forms2 {

// Reference (slow) path of the modulation / engine, kept for the equivalence test
#define V2_HAS_SLOWPATH 1
inline bool& v2SlowPath(){ static bool b = false; return b; }

// ---------------------------------------------------------------- context supplied by the caller
struct ModContext {
    std::function<const ParamDef*(const std::string& dest)> paramDef;   // the schema lookup
    uint64_t sceneSeed = 1;
    Beat sceneStart = 0;
    Phrase phrase;                    // the current phrase (for layer-level and hook evaluation)
    Beat now = 0;                     // current beat (matrix view, hook)
    std::map<std::string, double> macros;
    std::function<double(const std::string& id, const std::string& what, double window, Beat at)> follower;
    std::function<double(Beat at)> brightness;
    std::function<double(Beat at)> arc;
    std::function<double(const std::string& lane, Beat at)> lane;
};

// Where a source is read
struct ModPoint {
    Beat t = 0;
    const Phrase* phrase = nullptr;
    const Event* event = nullptr;     // event-level / note envelopes
    uint64_t eventSeed = 0;
    bool hasEvent = false;
    bool drawn = false;               // t is a drawn position within the event
    double playhead = NAN;            // NAN: ctx.now
};

namespace mod {

inline double frac(double x){ return x - std::floor(x); }
inline double clamp01(double x){ return std::clamp(x, 0.0, 1.0); }
inline double jnum(const json& j, const char* k, double def){
    if(j.is_object()){ auto f = j.find(k); if(f != j.end() && f->is_number()) return f->get<double>(); }
    return def;
}
inline std::string jstr(const json& j, const char* k, const std::string& def){
    if(j.is_object()){ auto f = j.find(k); if(f != j.end() && f->is_string()) return f->get<std::string>(); }
    return def;
}
inline bool jbool(const json& j, const char* k, bool def){
    if(j.is_object()){ auto f = j.find(k); if(f != j.end() && f->is_boolean()) return f->get<bool>(); }
    return def;
}
inline bool starts(const std::string& s, const char* p){ return s.rfind(p, 0) == 0; }
inline double rand01(uint64_t seed){ Rng r(seed); return r.uniform(); }
inline double smoothstep(double a, double b, double x){ double u = 0.5 - 0.5 * std::cos(M_PI * clamp01(x)); return a + (b - a) * u; }

enum class Kind { Lfo, PhraseEnv, NoteEnv, Random, Macro, Follow, Brightness, Arc, Lane, Playhead, Unknown };

inline Kind kindOf(const Routing& r){
    std::string t = jstr(r.sourceParams, "type", "");
    const std::string& s = t.empty() ? r.source : t;
    if(starts(s, "lfo")) return Kind::Lfo;
    if(starts(s, "phraseEnv")) return Kind::PhraseEnv;
    if(starts(s, "noteEnv")) return Kind::NoteEnv;
    if(starts(s, "random")) return Kind::Random;
    if(starts(s, "macro")) return Kind::Macro;
    if(starts(s, "follow")) return Kind::Follow;
    if(starts(s, "brightness")) return Kind::Brightness;
    if(starts(s, "arc")) return Kind::Arc;
    if(starts(s, "lane")) return Kind::Lane;
    if(starts(s, "playhead")) return Kind::Playhead;
    return Kind::Unknown;
}

// LFO wave at phase 0..1, unipolar
inline double wave(const std::string& shape, double ph){
    if(shape == "triangle") return ph < 0.5 ? 2 * ph : 2 - 2 * ph;
    if(shape == "saw") return ph;
    if(shape == "square") return ph < 0.5 ? 1 : 0;
    return 0.5 + 0.5 * std::sin(2 * M_PI * ph);   // sine
}

// Envelope over x 0..1, unipolar
inline double envelope(const json& p, double x){
    x = clamp01(x);
    std::string shape = jstr(p, "shape", "rise");
    if(shape == "fall") return 1 - x;
    if(shape == "swell") return std::sin(M_PI * x);
    if(shape == "steps"){
        int n = std::max(1, (int)jnum(p, "steps", 4));
        if(n == 1) return 0;
        return std::min(n - 1, (int)(x * n)) / (double)(n - 1);
    }
    if(shape == "drawn"){
        std::vector<std::pair<double, double>> pts;
        if(p.is_object() && p.contains("points") && p["points"].is_array()){
            auto& a = p["points"];
            for(size_t i = 0; i < a.size(); i++){
                if(a[i].is_array() && a[i].size() >= 2) pts.push_back({a[i][0].get<double>(), a[i][1].get<double>()});
                else if(a[i].is_number()) pts.push_back({a.size() > 1 ? i / (double)(a.size() - 1) : 0.0, a[i].get<double>()});
            }
        }
        if(pts.empty()) return 0;
        std::sort(pts.begin(), pts.end());
        if(x <= pts.front().first) return clamp01(pts.front().second);
        for(size_t i = 1; i < pts.size(); i++) if(x <= pts[i].first){
            double w = pts[i].first - pts[i - 1].first;
            double u = w > 0 ? (x - pts[i - 1].first) / w : 1;
            return clamp01(pts[i - 1].second + (pts[i].second - pts[i - 1].second) * u);
        }
        return clamp01(pts.back().second);
    }
    return x;   // rise
}

} // namespace mod

// Raw source value at a point: 0..1 (or -1..1 when bipolar)
inline double sourceValueK(const Routing& r, mod::Kind kind, bool bipolar, uint64_t tag, const ModPoint& pt, const ModContext& ctx){
    using namespace mod;
    const json& p = r.sourceParams;
    static const Phrase dflt;
    const Phrase& ph = pt.phrase ? *pt.phrase : dflt;
    double s = 0;
    switch(kind){
        case Kind::Lfo: {
            bool free = jstr(p, "reset", "phrase") == "free";
            double period = p.is_object() && p.contains("ratePhrases") ? jnum(p, "ratePhrases", 1) * ph.length : jnum(p, "rateBeats", 4);
            period = std::max(1e-9, period);
            double tt = free ? pt.t : pt.t - ph.start;
            std::string shape = jstr(p, "shape", "sine");
            if(shape == "smoothRandom"){
                uint64_t base = mixSeed(free ? streamSeed(ctx.sceneSeed, Stream::Mod) : streamSeed(ph.seed, Stream::Mod), tag);
                double c = tt / period, k = std::floor(c);
                s = smoothstep(rand01(mixSeed(base, (uint64_t)(int64_t)k)), rand01(mixSeed(base, (uint64_t)(int64_t)(k + 1))), c - k);
            } else s = wave(shape, frac(tt / period + jnum(p, "phase", 0)));
            break;
        }
        case Kind::PhraseEnv: s = envelope(p, (pt.t - ph.start) / std::max(1e-9, ph.length)); break;
        case Kind::NoteEnv:
            s = pt.event ? envelope(p, (pt.t - pt.event->start) / std::max(1e-9, pt.event->length)) : envelope(p, 0);
            break;
        case Kind::Random: {
            std::string scope = jstr(p, "scope", "phrase");
            if(scope == "event"){
                uint64_t es = pt.event ? pt.event->seed : pt.eventSeed;
                s = rand01(mixSeed(streamSeed(es, Stream::Mod), tag));
            } else if(scope == "scene"){
                s = rand01(mixSeed(streamSeed(ctx.sceneSeed, Stream::Mod), tag));
            } else if(scope == "beats"){
                // groups restart at each phrase start (ROADMAP 5.4)
                double n = std::max(1e-9, jnum(p, "everyBeats", 4));
                uint64_t base = mixSeed(streamSeed(ph.seed, Stream::Mod), tag, 0xBEA7);
                double c = std::max(0.0, pt.t - ph.start) / n, k = std::floor(c);
                double a = rand01(mixSeed(base, (uint64_t)k));
                s = jbool(p, "smooth", false) ? smoothstep(a, rand01(mixSeed(base, (uint64_t)k + 1)), c - k) : a;
            } else {
                s = rand01(mixSeed(streamSeed(ph.seed, Stream::Mod), tag));
            }
            break;
        }
        case Kind::Macro: {
            std::string name = jstr(p, "name", starts(r.source, "macro:") ? r.source.substr(6) : r.source);
            auto f = ctx.macros.find(name); s = f == ctx.macros.end() ? 0 : f->second; break;
        }
        case Kind::Follow: {
            // follow:<id>:<what>
            std::string rest = starts(r.source, "follow:") ? r.source.substr(7) : "";
            auto c = rest.find(':');
            std::string id = jstr(p, "id", rest.substr(0, c)), what = jstr(p, "what", c == std::string::npos ? "density" : rest.substr(c + 1));
            s = ctx.follower ? ctx.follower(id, what, jnum(p, "window", 4), pt.t) : 0; break;
        }
        case Kind::Brightness: s = ctx.brightness ? ctx.brightness(pt.t) : 0; break;
        case Kind::Arc: s = ctx.arc ? ctx.arc(pt.t) : 0; break;
        case Kind::Lane: {
            std::string name = jstr(p, "name", starts(r.source, "lane:") ? r.source.substr(5) : r.source);
            s = ctx.lane ? ctx.lane(name, pt.t) : 0; break;
        }
        case Kind::Playhead: {
            // d > 0: still ahead of the playhead, d = 0: crossing, d < 0: passed
            double head = std::isnan(pt.playhead) ? ctx.now : pt.playhead;
            double pos = (pt.event && !pt.drawn) ? pt.event->start : pt.t;
            double d = pos - head, w = std::max(1e-9, jnum(p, "window", 2));
            std::string shape = jstr(p, "shape", "peak");
            if(shape == "approach") s = d > w ? 0 : d <= 0 ? 1 : 1 - d / w;      // ramp up to the crossing, then held
            else if(shape == "ringout") s = d > 0 ? 0 : std::max(0.0, 1 + d / w); // decay after the crossing
            else if(shape == "distance") s = 0.5 + d / (2 * w);                   // 0.5 at the crossing
            else s = std::max(0.0, 1 - std::fabs(d) / w);                         // peak at the crossing
            break;
        }
        default: return 0;
    }
    s = clamp01(s);
    return bipolar ? s * 2 - 1 : s;
}
inline double sourceValue(const Routing& r, const ModPoint& pt, const ModContext& ctx){
    return sourceValueK(r, mod::kindOf(r), mod::jbool(r.sourceParams, "bipolar", false), hashTag(r.source), pt, ctx);
}

// Shaped source (curve, invert)
inline double shapedB(const Routing& r, double s, bool bipolar){
    double c = r.curve > 0 ? r.curve : 1;
    s = (s < 0 ? -1 : 1) * std::pow(std::fabs(s), c);
    if(r.invert) s = bipolar ? -s : 1 - s;
    return s;
}
inline double shaped(const Routing& r, double s){ return shapedB(r, s, mod::jbool(r.sourceParams, "bipolar", false)); }

// Offset a routing adds to a destination
inline double routingOffsetB(const Routing& r, const ParamDef& def, double base, double s, bool bipolar){
    s = shapedB(r, s, bipolar);
    if(r.hasRange) return r.amount * (r.min + (r.max - r.min) * s - base);
    return r.amount * s * (def.max - def.min);
}
inline double routingOffset(const Routing& r, const ParamDef& def, double base, double s){
    s = shaped(r, s);
    if(r.hasRange) return r.amount * (r.min + (r.max - r.min) * s - base);
    return r.amount * s * (def.max - def.min);
}

inline double finishValue(const ParamDef& def, double v){
    v = std::clamp(v, std::min(def.min, def.max), std::max(def.min, def.max));
    if(def.degrees) v = std::round(v);
    return v;
}

inline const char* levelBadge(Level l){
    switch(l){ case LevelEvent: return "E"; case LevelPhrase: return "P"; case LevelLayer: return "L"; case LevelScene: return "S"; }
    return "?";
}

// A problem found by check()
struct ModIssue { std::string owner; size_t index = 0; std::string dest, message; };

// One row of the matrix view
struct MatrixRow { std::string owner, source, dest; double amount = 0; Level level = LevelPhrase; std::string badge; double source01 = 0, offset = 0; bool valid = true; };

// ---------------------------------------------------------------- the engine
class Modulation {
public:
    // owner "" = scene, "@layerN" = scene routing to layer N; dest is canonical (see canonicalDest),
    // srcDest the dest as written
    struct Entry { std::string owner; int layer = -1; size_t index = 0; Routing r; std::string srcDest;
                   // cached at push: source kind, bipolar, source tag, render animated, pure (time / seed only)
                   mod::Kind kind = mod::Kind::Unknown; bool bipolar = false, animated = false, pure = false; uint64_t tag = 0; };

    // Destination as matched: "look.<k>" == "<k>"
    static std::string canonicalDest(const std::string& d){ return mod::starts(d, "look.") ? d.substr(5) : d; }

    // Collects the scene's and every instance's routings. Scene routings may point at
    // "<instanceId>:<dest>" (owned by that instance) or "layerN.<k>" (layer N, dest "layer.<k>").
    void setScene(const Scene& sc){
        entries.clear(); smoothState.clear(); layerOf.clear(); byDest.clear(); defCache.clear();
        for(auto& in : sc.instances) layerOf[in.id] = in.layer;
        for(size_t i = 0; i < sc.routings.size(); i++) push("", -1, i, sc.routings[i]);
        for(auto& in : sc.instances)
            for(size_t i = 0; i < in.routings.size(); i++) push(in.id, in.layer, i, in.routings[i]);
    }
    void add(const std::string& owner, int layer, const Routing& r){
        if(!owner.empty() && owner[0] != '@' && layer >= 0) layerOf[owner] = layer;
        size_t n = 0; for(auto& e : entries) if(e.owner == owner) n++;
        push(owner, layer, n, r);
    }
    void clear(){ entries.clear(); smoothState.clear(); layerOf.clear(); byDest.clear(); defCache.clear(); }
    // Engine: the contexts' paramDef never change without clear(), so lookups may be cached
    void setCacheDefs(bool on){ cacheDefs = on; defCache.clear(); }
    // routings to a (canonical) destination, in entry order
    const std::vector<size_t>* entriesFor(std::string_view dest) const {
        if(dest.substr(0, 5) == "look.") dest.remove_prefix(5);
        auto f = byDest.find(dest); return f == byDest.end() ? nullptr : &f->second;
    }
    // value of a destination without routings: base clamped / rounded (base when unknown)
    double baseOnly(const std::string& dest, double base, const ModContext& ctx) const {
        const ParamDef* d = defOf(dest, ctx);
        return d ? finishValue(*d, base + 0.0) : base;
    }
    // do the drawn routings of an instance (and the scene) read only time / seeds (no external sources)?
    bool drawnPure(const std::string& owner) const {
        for(auto& e : entries) if((e.owner.empty() || e.owner == owner) && e.r.level == LevelLayer && !e.animated && !e.pure) return false;
        return true;
    }
    bool hasAnimatedFor(const std::string& owner) const {
        for(auto& e : entries) if((e.owner == owner || e.owner.empty()) && e.animated) return true;
        return false;
    }
    const std::vector<Entry>& all() const { return entries; }

    // Routings whose level the destination does not allow (or whose destination / source is unknown)
    std::vector<ModIssue> check(const ModContext& ctx) const {
        std::vector<ModIssue> out;
        for(auto& e : entries){
            const ParamDef* d = ctx.paramDef ? ctx.paramDef(e.r.dest) : nullptr;
            if(!d && e.srcDest != e.r.dest && ctx.paramDef) d = ctx.paramDef(e.srcDest);
            if(!d) out.push_back({e.owner, e.index, e.r.dest, "unknown destination"});
            else if(!(d->levels & e.r.level)) out.push_back({e.owner, e.index, e.r.dest, std::string("level ") + levelBadge(e.r.level) + " not allowed"});
            if(mod::kindOf(e.r) == mod::Kind::Unknown) out.push_back({e.owner, e.index, e.r.dest, "unknown source " + e.r.source});
        }
        return out;
    }

    // Per event: once at its creation (event seed, event start)
    double evalEvent(const std::string& dest, double base, const Event& ev, const Phrase& ph, const ModContext& ctx) const {
        ModPoint pt; pt.t = ev.start; pt.phrase = &ph; pt.event = &ev; pt.eventSeed = ev.seed; pt.hasEvent = true;
        return evalAt(dest, base, LevelEvent, [&](const Entry& e){ return e.owner.empty() || e.owner == ev.instance; }, pt, ctx);
    }
    // Per phrase: once at the phrase start
    double evalPhrase(const std::string& dest, double base, const std::string& instanceId, const Phrase& ph, const ModContext& ctx) const {
        ModPoint pt; pt.t = ph.start; pt.phrase = &ph;
        return evalAt(dest, base, LevelPhrase, [&](const Entry& e){ return e.owner.empty() || e.owner == instanceId; }, pt, ctx);
    }
    // Per scene: once at the scene start
    double evalScene(const std::string& dest, double base, const std::string& instanceId, const ModContext& ctx) const {
        Phrase ph; ph.start = ctx.sceneStart; ph.seed = ctx.sceneSeed;
        ModPoint pt; pt.t = ctx.sceneStart; pt.phrase = &ph;
        return evalAt(dest, base, LevelScene, [&](const Entry& e){ return e.owner.empty() || e.owner == instanceId; }, pt, ctx);
    }
    // Per layer: every frame, routings of the layer's instances and the scene; each routing with
    // smooth > 0 glides (one pole in beat time). ctx.phrase is the phrase used for phrase-relative sources.
    double evalLayer(const std::string& dest, double base, int layer, Beat nowBeat, const ModContext& ctx){
        if(v2SlowPath()) return evalLayerSlow(dest, base, layer, nowBeat, ctx);
        const ParamDef* d = defOf(dest, ctx);
        if(!d) return base;
        const std::vector<size_t>* idx = entriesFor(dest);
        if(!idx || !(d->levels & LevelLayer)) return finishValue(*d, base + 0.0);
        ModPoint pt; pt.t = nowBeat; pt.phrase = &ctx.phrase;
        double sum = 0;
        for(size_t i : *idx){
            auto& e = entries[i];
            if(e.r.level != LevelLayer) continue;
            if(!e.owner.empty() && layerOfEntry(e) != layer) continue;
            double off = routingOffsetB(e.r, *d, base, sourceValueK(e.r, e.kind, e.bipolar, e.tag, pt, ctx), e.bipolar);
            if(e.r.smooth > 0){
                auto key = std::make_pair(i, layer);
                auto f = smoothState.find(key);
                if(f == smoothState.end()) smoothState[key] = {off, nowBeat};
                else {
                    double dt = std::max(0.0, nowBeat - f->second.second);
                    double a = 1 - std::exp(-dt / e.r.smooth);
                    f->second.first += (off - f->second.first) * a;
                    f->second.second = nowBeat;
                    off = f->second.first;
                }
            }
            sum += off;
        }
        return finishValue(*d, base + sum);
    }
    // the reference loop (before the index)
    double evalLayerSlow(const std::string& dest, double base, int layer, Beat nowBeat, const ModContext& ctx){
        const ParamDef* d = defOfSlow(dest, ctx);
        if(!d) return base;
        ModPoint pt; pt.t = nowBeat; pt.phrase = &ctx.phrase;
        std::string cd = canonicalDest(dest);
        double sum = 0;
        for(size_t i = 0; i < entries.size(); i++){
            auto& e = entries[i];
            if(e.r.dest != cd || e.r.level != LevelLayer || !(d->levels & LevelLayer)) continue;
            if(!e.owner.empty() && layerOfEntry(e) != layer) continue;
            double off = routingOffset(e.r, *d, base, sourceValue(e.r, pt, ctx));
            if(e.r.smooth > 0){
                auto key = std::make_pair(i, layer);
                auto f = smoothState.find(key);
                if(f == smoothState.end()) smoothState[key] = {off, nowBeat};
                else {
                    double dt = std::max(0.0, nowBeat - f->second.second);
                    double a = 1 - std::exp(-dt / e.r.smooth);
                    f->second.first += (off - f->second.first) * a;
                    f->second.second = nowBeat;
                    off = f->second.first;
                }
            }
            sum += off;
        }
        return finishValue(*d, base + sum);
    }
    // Tremolo / vibrato render modes (sourceParams "render"): "drawn" (default) bakes the routing
    // into the event's shape, read along the event's own time; "animated" is read from the clock
    // each frame, so the element moves / fades as it travels. Both use layer-level routings of the
    // event's instance (and the scene), no smoothing. Fine pitch (semitones / cents) is a continuous
    // destination (degrees = false): the note itself stays on its degree.
    // drawn: u = position within the event 0..1
    double evalDrawn(const std::string& dest, double base, const Event& ev, const Phrase& ph, double u, const ModContext& ctx) const {
        ModPoint pt; pt.t = ev.start + mod::clamp01(u) * ev.length; pt.phrase = &ph; pt.event = &ev; pt.eventSeed = ev.seed; pt.hasEvent = true; pt.drawn = true;
        return evalAt(dest, base, LevelLayer, [&](const Entry& e){ return (e.owner.empty() || e.owner == ev.instance) && !e.animated; }, pt, ctx);
    }
    double evalDrawnBeats(const std::string& dest, double base, const Event& ev, const Phrase& ph, Beat fromStart, const ModContext& ctx) const {
        return evalDrawn(dest, base, ev, ph, fromStart / std::max(1e-9, ev.length), ctx);
    }
    // animated: read at the current beat
    double evalAnimated(const std::string& dest, double base, const Event& ev, const Phrase& ph, Beat nowBeat, const ModContext& ctx) const {
        ModPoint pt; pt.t = nowBeat; pt.phrase = &ph; pt.event = &ev; pt.eventSeed = ev.seed; pt.hasEvent = true; pt.playhead = nowBeat;
        return evalAt(dest, base, LevelLayer, [&](const Entry& e){ return (e.owner.empty() || e.owner == ev.instance) && e.animated; }, pt, ctx);
    }
    // "render" (set directly) or "_render" (where v2Format keeps it in memory)
    static bool isAnimated(const Routing& r){
        return mod::jstr(r.sourceParams, "render", "drawn") == "animated" || mod::jstr(r.sourceParams, "_render", "drawn") == "animated";
    }
    void resetSmoothing(){ smoothState.clear(); }

    // The hook a sequencer calls: value(key, base, level, seed). For LevelEvent the seed is the
    // event's seed, read at ctx.now; LevelPhrase uses ctx.phrase (seed overrides its seed if non-zero).
    using Hook = std::function<double(const std::string& key, double base, Level level, uint64_t seed)>;
    Hook hook(const std::string& instanceId, int layer, const ModContext& ctx){
        HookAt h = hookAt(instanceId, layer, ctx);
        return [h](const std::string& key, double base, Level level, uint64_t seed){ return h(key, base, level, seed, NAN); };
    }
    // Same with the event's start beat (LevelEvent; NAN = ctx.now), so time-based sources read at it
    using HookAt = std::function<double(const std::string& key, double base, Level level, uint64_t seed, Beat start)>;
    HookAt hookAt(const std::string& instanceId, int layer, const ModContext& ctx){
        return [this, instanceId, layer, &ctx](const std::string& key, double base, Level level, uint64_t seed, Beat start) -> double {
            Phrase ph = ctx.phrase;
            switch(level){
                case LevelEvent: { Event ev; ev.instance = instanceId; ev.layer = layer; ev.start = std::isnan(start) ? ctx.now : start; ev.seed = seed; return evalEvent(key, base, ev, ph, ctx); }
                case LevelPhrase: if(seed) ph.seed = seed; return evalPhrase(key, base, instanceId, ph, ctx);
                case LevelLayer: return evalLayer(key, base, layer, ctx.now, ctx);
                case LevelScene: return evalScene(key, base, instanceId, ctx);
            }
            return base;
        };
    }

    // Matrix view: rows for an instance (its routings + the scene's) or the scene ("" = scene only,
    // "*" = all), live values read at ctx.now in ctx.phrase
    std::vector<MatrixRow> matrix(const std::string& owner, const ModContext& ctx) const {
        std::vector<MatrixRow> out;
        ModPoint pt; pt.t = ctx.now; pt.phrase = &ctx.phrase;
        for(auto& e : entries){
            if(owner != "*" && !(e.owner.empty() || e.owner == owner)) continue;
            MatrixRow row{e.owner, e.r.source, e.r.dest, e.r.amount, e.r.level, levelBadge(e.r.level)};
            const ParamDef* d = ctx.paramDef ? ctx.paramDef(e.r.dest) : nullptr;
            row.valid = d && (d->levels & e.r.level) && mod::kindOf(e.r) != mod::Kind::Unknown;
            row.source01 = sourceValue(e.r, pt, ctx);
            if(d) row.offset = routingOffset(e.r, *d, 0, row.source01);
            out.push_back(row);
        }
        return out;
    }

private:
    std::vector<Entry> entries;
    std::map<std::pair<size_t, int>, std::pair<double, Beat>> smoothState;   // (entry, layer) -> (offset, beat)
    std::map<std::string, int> layerOf;   // instance id -> layer
    std::map<std::string, std::vector<size_t>, std::less<>> byDest;   // canonical dest -> entries
    bool cacheDefs = false;
    mutable std::map<const ModContext*, std::map<std::string, const ParamDef*, std::less<>>> defCache;

    const ParamDef* defOf(const std::string& dest, const ModContext& ctx) const {
        if(!cacheDefs) return defOfSlow(dest, ctx);
        auto& m = defCache[&ctx];
        auto f = m.find(dest);
        if(f != m.end()) return f->second;
        const ParamDef* d = defOfSlow(dest, ctx);
        m.emplace(dest, d);
        return d;
    }
    static const ParamDef* defOfSlow(const std::string& dest, const ModContext& ctx){
        if(!ctx.paramDef) return nullptr;
        const ParamDef* d = ctx.paramDef(dest);
        if(!d && canonicalDest(dest) != dest) d = ctx.paramDef(canonicalDest(dest));
        return d;
    }
    // an "<id>:" routing added before its instance learns the layer late
    int layerOfEntry(const Entry& e) const {
        if(e.layer >= 0 || e.owner.empty() || e.owner[0] == '@') return e.layer;
        auto f = layerOf.find(e.owner); return f == layerOf.end() ? -1 : f->second;
    }
    // Normalises the dest forms of a scene routing (owner "")
    void push(std::string owner, int layer, size_t index, Routing r){
        std::string src = r.dest;
        if(owner.empty()){
            size_t c = r.dest.find(':');
            if(c != std::string::npos && c > 0){
                owner = r.dest.substr(0, c); r.dest = r.dest.substr(c + 1);
                auto f = layerOf.find(owner); layer = f == layerOf.end() ? -1 : f->second;
            } else if(r.dest.size() > 7 && mod::starts(r.dest, "layer") && std::isdigit((unsigned char)r.dest[5]) && r.dest[6] == '.'){
                layer = r.dest[5] - '0'; owner = "@layer" + std::to_string(layer);
                r.dest = "layer" + r.dest.substr(6);
            }
        }
        r.dest = canonicalDest(r.dest);
        Entry e{owner, layer, index, r, src};
        e.kind = mod::kindOf(r); e.bipolar = mod::jbool(r.sourceParams, "bipolar", false);
        e.animated = isAnimated(r); e.tag = hashTag(r.source);
        e.pure = e.kind == mod::Kind::Lfo || e.kind == mod::Kind::PhraseEnv || e.kind == mod::Kind::NoteEnv || e.kind == mod::Kind::Random;
        byDest[e.r.dest].push_back(entries.size());
        entries.push_back(std::move(e));
    }

    template<class Pred>
    double evalAt(const std::string& dest, double base, Level level, Pred owns, const ModPoint& pt, const ModContext& ctx) const {
        if(v2SlowPath()) return evalAtSlow(dest, base, level, owns, pt, ctx);
        const ParamDef* d = defOf(dest, ctx);
        if(!d) return base;
        const std::vector<size_t>* idx = entriesFor(dest);
        if(!(d->levels & level)) return finishValue(*d, base);
        if(!idx) return finishValue(*d, base + 0.0);   // no routing: base (as the loop gives)
        double sum = 0;
        for(size_t i : *idx){
            auto& e = entries[i];
            if(e.r.level != level || !owns(e)) continue;
            sum += routingOffsetB(e.r, *d, base, sourceValueK(e.r, e.kind, e.bipolar, e.tag, pt, ctx), e.bipolar);
        }
        return finishValue(*d, base + sum);
    }
    // the reference loop (before the index)
    template<class Pred>
    double evalAtSlow(const std::string& dest, double base, Level level, Pred owns, const ModPoint& pt, const ModContext& ctx) const {
        const ParamDef* d = defOfSlow(dest, ctx);
        if(!d) return base;
        if(!(d->levels & level)) return finishValue(*d, base);
        std::string cd = canonicalDest(dest);
        double sum = 0;
        for(auto& e : entries){
            if(e.r.dest != cd || e.r.level != level || !owns(e)) continue;
            sum += routingOffset(e.r, *d, base, sourceValue(e.r, pt, ctx));
        }
        return finishValue(*d, base + sum);
    }
};

} // namespace forms2
