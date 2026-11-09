//
//  v2Engine.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The headless runtime of a v2 scene (ROADMAP 5.2-5.4, 6): loads and validates a scene, resolves the
//  harmony, runs one phrase clock per instance and plans phrases ahead of the playhead (look-ahead per
//  layer and minimum event depth), in Follow order, through the modulation hook. Keeps the planned
//  events per layer, ordered by start, and drops them once they are well behind the playhead.
//  Lock / reroll, mute / solo, macros, harmony override and live edits. Queries for the drawers and
//  the sound: visible events, drawn / animated modulation per event, harmony, grid, layer zoom.
//
//  Notes on the other parts (worked around here, see the report):
//  - scene routings "<id>:<dest>" and "layerN.<key>" are not understood by Modulation::setScene, so
//    the entries are built here: "<id>:<dest>" becomes a routing of that instance, "layerN.<key>" a
//    routing of layer N with dest "layer.<key>".
//  - v2Format keeps the render option in sourceParams "_render"; Modulation reads "render". Copied.
//  - "look.<key>" destinations are stored bare ("<key>"), since Modulation matches dest literally.
//
//  Blob masks / effect scopes (5.12): events of blob-masked instances get blobSeedP / blobPhStart /
//  blobPhLen (phrase seed from the rhythm lineage, so keep / mutate / new and lock apply) and
//  blobSeedG (instance-wide). Effects get fxTarget (hash of the target id). Element-scope echo /
//  repeat are stored as copies of the target's events (owned by the effect instance, so muting the
//  effect hides them; Event::instance stays the target's id); targets are planned first.
//  Retuning (5.13): scene keys retune (new | ahead | all), retuneBeats, retuneCurve. On a harmony
//  override (jump / clear) planned note events of chord-following instances move by the root change
//  and carry a morph (retuneStart / Beats / Curve / From / To, semitones) for the drawers. "new"
//  keeps the old behaviour (phrases not started are re-planned, nothing on screen changes).
//
//  Determinism: phrases and seeds depend only on the scene, its seed and the calls made (lock,
//  reroll, edits at given beats); never on the frame rate.
//
//  Header-only, plain C++17 + nlohmann::json.
//

#pragma once

#include <array>
#include <chrono>
#include <memory>
#include <unordered_map>
#include <set>
#include "v2Format.h"
#include "v2Harmony.h"
#include "v2Sequencer.h"
#include "v2Modulation.h"
#include "v2Blob.h"

namespace forms2 {

// installs the sequencer schema into the registry (seq.<key> lookups and validation)
inline void installSequencerSchema(){
    if(!sequencerSchema()) setSequencerSchema([](const std::string& k){ return sequencerParam(k); });
}

// A planned event with the instance (index in the scene) and phrase it belongs to
struct StoredEvent { Event e; int instance = -1; int phrase = 0; };
// An event visible in a layer view: x from its start to its end, at its own depth
struct VisibleEvent { const StoredEvent* ev = nullptr; double x0 = 0, x1 = 0; };
// A grid line: kind 0 section start, 1 bar, 2 group start (strong beat), 3 beat
struct GridLine { Beat beat = 0; int bar = 0; int kind = 1; int num = 4, den = 4; };

struct EngineStats {
    long plannedPhrases = 0;          // every planPhrase call (re-plans included)
    long replannedPhrases = 0;        // phrases planned again after an edit / lock / reroll
    int liveEvents[4] = {0, 0, 0, 0};
    double lastPlanMs = 0, totalPlanMs = 0, maxPlanMs = 0;
    long updates = 0;
};

// The full playable state of an engine (performance snapshots, v2Perform.h serialises it)
struct InstanceState {
    std::string id;
    bool muted = false, solo = false, locked = false;
    PhraseSeeds lockSeeds; int lockFrom = 0;
    std::vector<std::pair<int, uint64_t>> rerolls;
    int clockBase = 0; Instance clockSrc;     // phrase lineage after live phrase edits
    std::vector<Phrase> phrases;
    int cursor = 0;
    std::set<int> planned;
    std::map<int, int> planCount;
};
struct EngineState {
    json scene;                       // scene with live edits and current macros (override left out)
    Beat now = 0;
    HarmonyOverride ov; Beat holdBeat = 0, jumpBeat = 0;
    std::vector<InstanceState> insts;
    std::vector<StoredEvent> events[4];
};

struct EngineOptions {
    double ringOutBeats = 8;          // kept after an event ends (release, animated ring-out)
    double defaultWidth = 1920, defaultHeight = 1080, defaultHeaderX = 960;   // when no view is given
    bool replanOnLock = true;         // lock / reroll re-plan the phrases not started yet
    // look-ahead spreading (0 = off, the default: everything the view can show is planned at once).
    // Phrases starting beyond now + planHorizonBeats are planned at most farPhrasesPerUpdate per update.
    double planHorizonBeats = 0;
    int farPhrasesPerUpdate = 8;
};

using DrawnFn = std::function<double(const std::string& dest, double base, double u)>;
using AnimatedFn = std::function<double(const std::string& dest, double base, Beat nowBeat)>;

class Engine;
// Modulation reader of one planned event (Engine::modOf): its own copy of the event and phrase, so
// it stays valid after the store changes. Drawn values are memoised (they never change) when the
// instance's drawn routings read only time / seeds.
struct EventMod {
    Event ev; Phrase ph;
    double drawn(const std::string& dest, double base, double u) const;
    double animated(const std::string& dest, double base, Beat nowBeat) const;
    bool hasAnimated() const { return anim; }
private:
    friend class Engine;
    const Engine* en = nullptr; ModContext* ctx = nullptr;
    bool anim = false, pure = false; uint64_t gen = 0, epoch = 0; int inst = -1;
    struct Memo { double base, u, v; };
    mutable std::map<std::string, std::vector<Memo>, std::less<>> memo;
};

class Engine {
public:
    EngineOptions options;
    // screen pitch space the planner maps heights with (scene key "pitchSpace": {lowSemi, highSemi});
    // the drawers' Frame::space should match it
    PitchSpace pitchSpace;
    // external sources for the modulation (0..1); empty = 0
    std::function<double(Beat)> brightnessSource, arcSource;
    std::function<double(const std::string&, Beat)> laneSource;

    Engine(){ installSequencerSchema(); mod_.setCacheDefs(true); }
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    // ---------------------------------------------------------------- loading
    // A scene JSON. Sections may give "progression": "I vi IV V" (typed, parsed with the section's
    // meter and scale); it replaces their steps. Returns false (and problems()) on errors.
    bool loadJson(const json& j0){
        reset();
        try {
            json j = loadMigrated(j0);
            if(j.contains("harmony") && j["harmony"].contains("sections") && j["harmony"]["sections"].is_array()){
                auto& secs = j["harmony"]["sections"];
                for(size_t s = 0; s < secs.size(); s++){
                    auto& sec = secs[s];
                    if(!sec.contains("progression") || !sec["progression"].is_string()) continue;
                    Meter m = sec.contains("meter") ? meterFromJson(sec["meter"]) : Meter{};
                    Scale sc = sec.contains("scale") ? scaleFromJson(sec["scale"]) : Scale{};
                    std::vector<ProgressionStep> steps; std::string err;
                    if(!parseProgression(sec["progression"].get<std::string>(), m, steps, err, sc.size())){
                        errors_.push_back("harmony section " + std::to_string(s) + ": progression " + err);
                        continue;
                    }
                    json a = json::array(); for(auto& p : steps) a.push_back(toJson(p));
                    sec["steps"] = a;
                }
            }
            if(!errors_.empty()){ problems_ = errors_; return false; }
            int rm = 0; double rb = 2; int rc = 1;
            if(j.contains("retune")){
                std::string r = j["retune"].is_string() ? j["retune"].get<std::string>() : "?";
                if(r == "new") rm = 0; else if(r == "ahead") rm = 1; else if(r == "all") rm = 2;
                else errors_.push_back("retune: expects new / ahead / all");
            }
            if(j.contains("retuneBeats")){
                if(j["retuneBeats"].is_number() && j["retuneBeats"].get<double>() >= 0) rb = j["retuneBeats"].get<double>();
                else errors_.push_back("retuneBeats: expects a number >= 0");
            }
            if(j.contains("retuneCurve")){
                static const char* names[] = {"linear", "ease", "exp", "sCurve"};
                std::string c = j["retuneCurve"].is_string() ? j["retuneCurve"].get<std::string>() : "?"; rc = -1;
                for(int k = 0; k < 4; k++) if(c == names[k]) rc = k;
                if(rc < 0){ rc = 1; errors_.push_back("retuneCurve: expects linear / ease / exp / sCurve"); }
            }
            if(!errors_.empty()){ problems_ = errors_; return false; }
            PitchSpace ps;
            if(j.contains("pitchSpace") && j["pitchSpace"].is_object()){
                ps.lowSemi = j["pitchSpace"].value("lowSemi", ps.lowSemi); ps.highSemi = j["pitchSpace"].value("highSemi", ps.highSemi);
            }
            bool ok = load(sceneDocFromJson(j));
            setRetune(rm, rb, rc);
            pitchSpace = ps;
            return ok;
        } catch(const std::exception& e){
            errors_.push_back(std::string("scene JSON: ") + e.what());
            problems_ = errors_;
            return false;
        }
    }
    bool loadText(const std::string& text){
        json j;
        try { j = json::parse(text); }
        catch(const std::exception& e){ reset(); errors_.push_back(std::string("scene JSON: ") + e.what()); problems_ = errors_; return false; }
        return loadJson(j);
    }
    bool load(const SceneDoc& doc){
        reset();
        installSequencerSchema();
        errors_ = validate(doc);
        auto srcCheck = [&](const Routing& r, const std::string& where){
            Routing c = r;
            if(mod::kindOf(c) == mod::Kind::Unknown) errors_.push_back(where + ": unknown modulation source '" + r.source + "'");
        };
        for(auto& in : doc.scene.instances) for(auto& r : in.routings) srcCheck(r, "instance '" + in.id + "'");
        for(auto& r : doc.scene.routings) srcCheck(r, "scene routing");
        problems_ = errors_;
        if(!errors_.empty()) return false;
        doc_ = doc;
        scene_ = doc.scene;
        while(scene_.layers.size() < 4) scene_.layers.push_back(LayerDef{});
        harmony_.resolve(scene_.harmony, scene_.seed);
        sceneCtx_ = makeCtx(nullptr);
        insts_.clear();
        for(size_t i = 0; i < scene_.instances.size(); i++){
            insts_.emplace_back(new Inst());
            Inst& I = *insts_.back();
            I.in = scene_.instances[i];
            I.in.layer = std::clamp(I.in.layer, 0, 3);
            I.type = findType(I.in.type);
            I.muted = I.in.muted;
            I.ctx = makeCtx(I.type);
            resetClock(I, 0, 0);
        }
        // layers holding an effect scoped to the instruments listed before it (v1 cell order):
        // their events carry drawOrder (the instance's index) for the renderers' staged drawing
        for(auto& e : earlierOn_) e = false;
        for(auto& p : insts_) if(p->in.params.is_object() && p->in.params.value("fxScope", std::string()) == "earlier") earlierOn_[(size_t)p->in.layer] = true;
        rebuildModulation();
        rebuildGraph();
        for(size_t i = 0; i < insts_.size(); i++) if(insts_[i]->in.locked) setLocked(insts_[i]->in.id, true);
        ready_ = true;
        return true;
    }

    // Retuning on screen (5.13): mode 0 new, 1 ahead, 2 all; morph beats; curve linear 0 / ease 1 / exp 2 / sCurve 3
    void setRetune(int mode, double beats, int curve){ retuneMode_ = std::clamp(mode, 0, 2); retuneBeats_ = std::max(0.0, beats); retuneCurve_ = std::clamp(curve, 0, 3); }
    int retuneMode() const { return retuneMode_; }

    bool ready() const { return ready_; }
    // errors (refused to start) and warnings (Follow cycles...)
    const std::vector<std::string>& problems() const { return problems_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    const Scene& scene() const { return scene_; }
    const HarmonyTimeline& harmony() const { return harmony_; }
    const Modulation& modulation() const { return mod_; }

    // ---------------------------------------------------------------- the frame
    // Plans what the look-ahead needs and drops old events. views[layer] (optional): the layer views
    // drawn this frame; missing ones use viewFor() with the default size.
    void update(Beat nowBeat, const std::vector<LayerView>& views = {}){
        if(!ready_) return;
        auto t0 = std::chrono::steady_clock::now();
        now_ = nowBeat;
        for(int l = 0; l < 4; l++) views_[l] = (size_t)l < views.size() ? views[(size_t)l] : viewFor(l, nowBeat);
        int farBudget = options.farPhrasesPerUpdate;
        for(int i : order_){
            Inst& I = *insts_[(size_t)i];
            if(effMuted(i)) continue;
            double ahead = lookAheadBeats(views_[I.in.layer], I.minDepth);
            std::vector<int> stack;
            if(options.planHorizonBeats > 0 && ahead > options.planHorizonBeats){
                // near part now, the far part a few phrases per update
                cover(i, nowBeat, nowBeat + options.planHorizonBeats, true, stack);
                for(int k = indexAt(I, nowBeat); phraseAt(I, k).start < nowBeat + ahead && farBudget > 0; k++)
                    if(!I.planned.count(k)){ planOne(i, k, stack); farBudget--; }
            } else cover(i, nowBeat, nowBeat + ahead, true, stack);
        }
        dropOld();
        double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        stats_.lastPlanMs = ms; stats_.totalPlanMs += ms; stats_.maxPlanMs = std::max(stats_.maxPlanMs, ms);
        stats_.updates++;
        for(int l = 0; l < 4; l++) stats_.liveEvents[l] = (int)store_[l].size();
    }

    // ---------------------------------------------------------------- performance
    // Lock: the phrases after the current one repeat its variation (seeds). Unlock: back to the
    // instance's variation settings.
    void setLocked(const std::string& id, bool on){
        int i = indexOf(id); if(i < 0) return;
        Inst& I = *insts_[(size_t)i];
        int cur = indexAt(I, now_);
        if(on){ I.lockSeeds = seedsFor(I, cur); I.lockFrom = cur + 1; }
        I.locked = on; I.in.locked = on;
        if(options.replanOnLock) dropFrom(i, cur + 1);
    }
    bool locked(const std::string& id) const { int i = indexOf(id); return i >= 0 && insts_[(size_t)i]->locked; }
    // Reroll: the next phrase uses a fresh seed (and stays locked if locked)
    void reroll(const std::string& id){
        int i = indexOf(id); if(i < 0) return;
        Inst& I = *insts_[(size_t)i];
        int cur = indexAt(I, now_);
        I.rerolls.push_back({cur + 1, mixSeed(scene_.seed, hashTag("reroll:" + id), (uint64_t)I.rerolls.size() + 1)});
        if(I.locked){ I.locked = false; I.lockSeeds = seedsFor(I, cur + 1); I.locked = true; I.lockFrom = cur + 1; }
        if(options.replanOnLock) dropFrom(i, cur + 1);
    }
    // Mute drops the phrases not started yet and hides the instance from the queries
    void setMuted(const std::string& id, bool on){
        int i = indexOf(id); if(i < 0) return;
        bool was = effMuted(i);
        insts_[(size_t)i]->muted = on;
        if(!was && effMuted(i)) dropFrom(i, indexAt(*insts_[(size_t)i], now_) + 1);
    }
    void setSolo(const std::string& id, bool on){
        int i = indexOf(id); if(i < 0) return;
        std::vector<bool> was; for(size_t k = 0; k < insts_.size(); k++) was.push_back(effMuted((int)k));
        insts_[(size_t)i]->solo = on;
        for(size_t k = 0; k < insts_.size(); k++)
            if(!was[k] && effMuted((int)k)) dropFrom((int)k, indexAt(*insts_[k], now_) + 1);
    }
    bool muted(const std::string& id) const { int i = indexOf(id); return i >= 0 && effMuted(i); }
    // Macros ("macro1".."macro8" or any name a routing reads), 0..1; reach phrases planned from now
    void setMacro(const std::string& name, double v){
        scene_.macros[name] = v;
        sceneCtx_->macros[name] = v;
        for(auto& I : insts_) I->ctx->macros[name] = v;
    }
    double macro(const std::string& name) const { auto f = scene_.macros.find(name); return f == scene_.macros.end() ? 0 : f->second; }
    // Harmony override (hold / jump) from a beat; replanFuture re-plans the phrases not started yet
    void setHarmonyOverride(const HarmonyOverride& o, Beat atBeat, bool replanFuture = true){
        if(o.hold && !ov_.hold) ovHold_ = atBeat;
        if(o.jumpDegree != ov_.jumpDegree) ovJump_ = atBeat;
        ov_ = o;
        if(retuneMode_ != 0){ retune([&]{ harmony_.setOverride(o, atBeat); }); return; }
        if(anyGlideTranspose()){ retune([&]{ harmony_.setOverride(o, atBeat); }); }
        else harmony_.setOverride(o, atBeat);
        if(replanFuture) for(size_t i = 0; i < insts_.size(); i++) dropFrom((int)i, indexAt(*insts_[i], now_) + 1);
    }
    void clearHarmonyOverride(bool replanFuture = true){
        if(retuneMode_ != 0){ retune([&]{ harmony_.clearOverride(); }); ov_ = HarmonyOverride{}; return; }
        if(anyGlideTranspose()) retune([&]{ harmony_.clearOverride(); });
        else harmony_.clearOverride();
        ov_ = HarmonyOverride{};
        if(replanFuture) for(size_t i = 0; i < insts_.size(); i++) dropFrom((int)i, indexAt(*insts_[i], now_) + 1);
    }

    // ---------------------------------------------------------------- live edits
    // Replace an instance's settings (same id). Phrases not started yet are re-planned; the current
    // phrase stays unless replanCurrent. Mute / lock state of the engine is kept. Returns false (and
    // problems()) if the new settings are invalid.
    bool setInstance(const Instance& next, bool replanCurrent = false){
        int i = indexOf(next.id);
        if(i < 0){ problems_ = {"no instance '" + next.id + "'"}; return false; }
        std::vector<std::string> errs;
        validateInstance(next, nullptr, errs);
        for(auto& r : next.routings) if(mod::kindOf(r) == mod::Kind::Unknown) errs.push_back("instance '" + next.id + "': unknown modulation source '" + r.source + "'");
        if(!errs.empty()){ problems_ = errs; return false; }
        Inst& I = *insts_[(size_t)i];
        int cur = indexAt(I, now_);
        bool phraseChanged = next.phrase != I.in.phrase;
        bool keepMuted = I.muted, keepLocked = I.locked;
        I.in = next;
        I.in.layer = std::clamp(I.in.layer, 0, 3);
        I.in.muted = keepMuted; I.in.locked = keepLocked;
        if(I.type != findType(I.in.type)){ I.type = findType(I.in.type); I.ctx->paramDef = paramDefFor(I.type); }
        scene_.instances[(size_t)i] = I.in;
        dropFrom(i, replanCurrent ? cur : cur + 1);
        if(phraseChanged){
            phraseAt(I, cur);
            I.phrases.resize((size_t)cur + 1);
            resetClock(I, I.phrases.back().end(), cur + 1);
        }
        rebuildModulation();
        rebuildGraph();
        return true;
    }
    const Instance* instance(const std::string& id) const { int i = indexOf(id); return i < 0 ? nullptr : &insts_[(size_t)i]->in; }

    // ---------------------------------------------------------------- queries
    // Planned events of a layer, ordered by start (muted instances included; see visible())
    const std::vector<StoredEvent>& events(int layer) const { return store_[(size_t)std::clamp(layer, 0, 3)]; }
    // Events of a layer visible in a view (x at each event's own depth), muted / unsoloed left out
    std::vector<VisibleEvent> visible(int layer, const LayerView& v) const {
        std::vector<VisibleEvent> out;
        for(auto& s : store_[(size_t)std::clamp(layer, 0, 3)]){
            if(effMuted(s.instance)) continue;
            double tail = s.e.param("release", 0);
            double x0 = v.xOf(s.e.start, s.e.depth), x1 = v.xOf(s.e.start + s.e.length + tail, s.e.depth);
            if(x1 < 0 || x0 > v.width) continue;
            out.push_back({&s, x0, x1});
        }
        return out;
    }
    // Same without allocating: appends to out (pointers valid until the next update / edit)
    void visibleInto(int layer, const LayerView& v, std::vector<VisibleEvent>& out) const {
        for(auto& s : store_[(size_t)std::clamp(layer, 0, 3)]){
            if(effMuted(s.instance)) continue;
            double tail = s.e.param("release", 0);
            double x0 = v.xOf(s.e.start, s.e.depth), x1 = v.xOf(s.e.start + s.e.length + tail, s.e.depth);
            if(x1 < 0 || x0 > v.width) continue;
            out.push_back({&s, x0, x1});
        }
    }
    // The phrase an event was planned in
    Phrase phraseOf(const StoredEvent& s){ return phraseAt(*insts_[(size_t)s.instance], s.phrase); }
    // Modulation reader of an event, shared per event id (cached until the event changes: re-plan,
    // retune, edits). Valid while the engine lives and the scene is not reloaded.
    std::shared_ptr<const EventMod> modOf(const StoredEvent& s){
        if(v2SlowPath()) return makeMod(s);
        auto f = modCache_.find(s.e.id);
        if(f != modCache_.end()){
            const EventMod& m = *f->second;
            if(m.epoch == epoch_ && m.gen == modGen_ && m.inst == s.instance && sameEvent(m.ev, s.e)) return f->second;
        }
        if(modCache_.size() > 4096 + 4 * storedCount()) modCache_.clear();
        auto m = makeMod(s);
        modCache_[s.e.id] = m;
        return m;
    }
    // Drawn modulation along an event (u = 0..1 of its length) and animated modulation at a beat;
    // closures over the engine (valid while it lives and the scene is not reloaded). Cheap to copy
    // (one shared pointer).
    DrawnFn drawnFn(const StoredEvent& s){
        std::shared_ptr<const EventMod> m = modOf(s);
        return [m](const std::string& dest, double base, double u){ return m->drawn(dest, base, u); };
    }
    AnimatedFn animatedFn(const StoredEvent& s){
        std::shared_ptr<const EventMod> m = modOf(s);
        return [m](const std::string& dest, double base, Beat nowBeat){ return m->animated(dest, base, nowBeat); };
    }
    // does an instance have routings rendered animated (the drawer re-reads them every frame)?
    // Without, animated reads give finishValue(base): the base clamped to the param's range.
    bool hasAnimated(const std::string& id) const {
        int i = indexOf(id);
        if(i >= 0 && !v2SlowPath()) return insts_[(size_t)i]->hasAnim;
        return mod_.hasAnimatedFor(id);
    }

    HarmonyAt harmonyAt(Beat b) const { return harmony_.at(b); }
    const Meter& meterAt(Beat b) const { return harmony_.meterAt(b); }
    // Section starts, bars, group starts and beats in [a, b)
    std::vector<GridLine> grid(Beat a, Beat b) const {
        std::vector<GridLine> out;
        auto secs = harmony_.sectionBoundaries(a, b);
        for(Beat bar : harmony_.barStarts(a, b)){
            const Meter& m = harmony_.meterAt(bar + 1e-9);
            int idx = harmony_.barIndex(bar + 1e-9);
            bool sec = false; for(Beat s : secs) if(std::fabs(s - bar) < 1e-6) sec = true;
            out.push_back({bar, idx, sec ? 0 : 1, m.num, m.den});
            double bb = harmony_.barBeatsAt(bar + 1e-9);
            for(int k = 1; k < (int)std::lround(bb); k++){
                Beat t = bar + k;
                if(t < a - 1e-9 || t >= b) continue;
                out.push_back({t, idx, m.strong(k) ? 2 : 3, m.num, m.den});
            }
        }
        std::sort(out.begin(), out.end(), [](const GridLine& x, const GridLine& y){ return x.beat < y.beat; });
        return out;
    }
    // Beats across the screen on a layer, modulated at LevelLayer (phrase-relative sources use the
    // current phrase of the layer's first unmuted instance)
    double layerZoom(int layer, Beat nowBeat){
        layer = std::clamp(layer, 0, 3);
        double base = scene_.layers.size() > (size_t)layer ? scene_.layers[(size_t)layer].zoomBeats : 16;
        if(!sceneCtx_) return base;
        sceneCtx_->now = nowBeat;
        sceneCtx_->phrase = Phrase{};
        sceneCtx_->phrase.seed = scene_.seed;
        for(size_t i = 0; i < insts_.size(); i++)
            if(insts_[i]->in.layer == layer && !effMuted((int)i)){ sceneCtx_->phrase = phraseAt(*insts_[i], indexAt(*insts_[i], nowBeat)); break; }
        return mod_.evalLayer("layer.zoom", base, layer, nowBeat, *sceneCtx_);
    }
    // a layer parameter (layer.<key>) with its layer routings at a beat (e.g. 3D camera / planeZ)
    double layerParam(const std::string& key, double base, int layer, Beat nowBeat){
        if(!sceneCtx_) return base;
        layer = std::clamp(layer, 0, 3);
        sceneCtx_->now = nowBeat;
        sceneCtx_->phrase = Phrase{};
        sceneCtx_->phrase.seed = scene_.seed;
        for(size_t i = 0; i < insts_.size(); i++)
            if(insts_[i]->in.layer == layer && !effMuted((int)i)){ sceneCtx_->phrase = phraseAt(*insts_[i], indexAt(*insts_[i], nowBeat)); break; }
        return mod_.evalLayer("layer." + key, base, layer, nowBeat, *sceneCtx_);
    }
    LayerView viewFor(int layer, Beat nowBeat, double width = -1, double height = -1, double headerX = -1){
        LayerView v;
        v.width = (int)(width > 0 ? width : options.defaultWidth);
        v.height = (int)(height > 0 ? height : options.defaultHeight);
        v.headerX = headerX >= 0 ? headerX : options.defaultHeaderX;
        v.nowBeat = nowBeat;
        v.pxPerBeat = v.width / std::max(1e-6, layerZoom(layer, nowBeat));
        return v;
    }

    // ---------------------------------------------------------------- inspection
    const EngineStats& stats() const { return stats_; }
    // planning order (Follow targets first) and the Follow cycles found (ids)
    std::vector<std::string> planOrder() const { std::vector<std::string> o; for(int i : order_) o.push_back(insts_[(size_t)i]->in.id); return o; }
    const std::vector<std::vector<std::string>>& cycles() const { return cycles_; }
    // smallest event depth an instance can produce (from its depth routings), used for its look-ahead
    double minDepth(const std::string& id) const { int i = indexOf(id); return i < 0 ? 1 : insts_[(size_t)i]->minDepth; }
    // end of the furthest phrase planned (and still current) for an instance
    double plannedUntil(const std::string& id){
        int i = indexOf(id); if(i < 0) return 0;
        Inst& I = *insts_[(size_t)i];
        double u = -1e18;
        for(int k : I.planned) u = std::max(u, phraseAt(I, k).end());
        return u;
    }
    // how many times each phrase of an instance was planned (index -> count)
    std::map<int, int> planCounts(const std::string& id) const { int i = indexOf(id); return i < 0 ? std::map<int, int>{} : insts_[(size_t)i]->planCount; }
    std::set<int> plannedPhrases(const std::string& id) const { int i = indexOf(id); return i < 0 ? std::set<int>{} : insts_[(size_t)i]->planned; }
    Phrase phrase(const std::string& id, int k){ int i = indexOf(id); return i < 0 ? Phrase{} : phraseAt(*insts_[(size_t)i], k); }
    int phraseIndexAt(const std::string& id, Beat b){ int i = indexOf(id); return i < 0 ? 0 : indexAt(*insts_[(size_t)i], b); }
    // the events of one instance (all layers), ordered by start
    std::vector<const StoredEvent*> eventsOf(const std::string& id) const {
        std::vector<const StoredEvent*> out; int i = indexOf(id); if(i < 0) return out;
        for(auto& s : store_[(size_t)insts_[(size_t)i]->in.layer]) if(s.instance == i) out.push_back(&s);
        return out;
    }
    Beat now() const { return now_; }
    // moves the playhead without planning (timeline actions applied at their own beat)
    void setNow(Beat b){ now_ = b; }
    const HarmonyOverride& harmonyOverride() const { return ov_; }
    // lane names read by the scene's routings ("lane:<name>" or sourceParams "name")
    std::vector<std::string> laneNames() const {
        std::set<std::string> n;
        for(auto& e : mod_.all()) if(mod::kindOf(e.r) == mod::Kind::Lane){
            std::string d = startsWith(e.r.source, "lane:") ? e.r.source.substr(5) : e.r.source;
            n.insert(e.r.sourceParams.is_object() && e.r.sourceParams.contains("name") && e.r.sourceParams["name"].is_string() ? e.r.sourceParams["name"].get<std::string>() : d);
        }
        return {n.begin(), n.end()};
    }

    // ---------------------------------------------------------------- snapshots
    EngineState captureState() const {
        EngineState st;
        SceneDoc d = doc_; d.scene = scene_; d.scene.harmony.override_ = HarmonyOverride{};
        st.scene = toJson(d);
        st.now = now_; st.ov = ov_; st.holdBeat = ovHold_; st.jumpBeat = ovJump_;
        for(auto& p : insts_){
            const Inst& I = *p;
            InstanceState s;
            s.id = I.in.id; s.muted = I.muted; s.solo = I.solo; s.locked = I.locked;
            s.lockSeeds = I.lockSeeds; s.lockFrom = I.lockFrom; s.rerolls = I.rerolls;
            s.clockBase = I.clockBase; s.clockSrc = I.clockSrc; s.phrases = I.phrases; s.cursor = I.cursor;
            s.planned = I.planned; s.planCount = I.planCount;
            st.insts.push_back(s);
        }
        for(int l = 0; l < 4; l++) st.events[l] = store_[l];
        return st;
    }
    // Loads the state's scene and puts every instance back as captured; running from st.now gives
    // the same events as the captured engine (modulation smoothing, draw-only, restarts)
    bool restoreState(const EngineState& st){
        if(!loadJson(st.scene)) return false;
        if(st.insts.size() != insts_.size()){ problems_ = {"snapshot: instance count differs from its scene"}; ready_ = false; return false; }
        now_ = st.now;
        harmony_.clearOverride(); ov_ = HarmonyOverride{};
        if(st.ov.hold) harmony_.setOverride({true, -1000}, st.holdBeat);
        harmony_.setOverride(st.ov, st.jumpBeat);
        ov_ = st.ov; ovHold_ = st.holdBeat; ovJump_ = st.jumpBeat;
        for(size_t i = 0; i < insts_.size(); i++){
            Inst& I = *insts_[i]; const InstanceState& s = st.insts[i];
            if(s.id != I.in.id){ problems_ = {"snapshot: instance '" + s.id + "' out of order"}; ready_ = false; return false; }
            I.muted = s.muted; I.solo = s.solo; I.locked = s.locked; I.in.locked = s.locked;
            I.lockSeeds = s.lockSeeds; I.lockFrom = s.lockFrom; I.rerolls = s.rerolls;
            I.clockSrc = s.clockSrc; I.clock.reset(new PhraseClock(I.clockSrc, harmony_, scene_.seed)); I.clockBase = s.clockBase;
            I.phrases = s.phrases; I.cursor = s.cursor; I.planned = s.planned; I.planCount = s.planCount;
        }
        for(int l = 0; l < 4; l++) store_[l] = st.events[l];
        touch();
        return true;
    }

private:
    struct Inst {
        Instance in;
        const TypeDef* type = nullptr;
        std::unique_ptr<PhraseClock> clock;
        int clockBase = 0;                // global index of the clock's phrase 0
        Instance clockSrc;                // the instance the clock reads (kept alive)
        std::vector<Phrase> phrases;      // global phrases, laid out on demand
        int cursor = 0;
        std::set<int> planned;
        std::map<int, int> planCount;
        bool muted = false, solo = false, locked = false;
        PhraseSeeds lockSeeds; int lockFrom = 0;
        std::vector<std::pair<int, uint64_t>> rerolls;   // (from phrase, scene seed to use)
        std::unique_ptr<ModContext> ctx;
        double minDepth = 1;
        std::vector<int> deps;            // instances this one reads (Follow, follow:<id> sources)
        std::set<std::string> dests;      // routed destinations (look params filled per event)
        bool hasAnim = false;             // has routings rendered animated
        bool drawnPure = true;            // drawn routings read only time / seeds
    };
    friend struct EventMod;
    std::unordered_map<uint64_t, std::shared_ptr<const EventMod>> modCache_;
    uint64_t epoch_ = 1, modGen_ = 1;     // stored events changed in place / modulation rebuilt
    size_t storedCount() const { return store_[0].size() + store_[1].size() + store_[2].size() + store_[3].size(); }
    static bool sameEvent(const Event& a, const Event& b){
        return a.start == b.start && a.length == b.length && a.seed == b.seed && a.degree == b.degree && a.kind == b.kind && a.instance == b.instance && a.p.size() == b.p.size();
    }
    std::shared_ptr<const EventMod> makeMod(const StoredEvent& s){
        auto m = std::make_shared<EventMod>();
        Inst& I = *insts_[(size_t)s.instance];
        m->ev = s.e; m->ph = phraseOf(s); m->en = this; m->ctx = I.ctx.get();
        m->anim = I.hasAnim; m->pure = I.drawnPure; m->gen = modGen_; m->epoch = epoch_; m->inst = s.instance;
        return m;
    }
    void touch(){ epoch_++; modCache_.clear(); }
    // "look.<k>" -> "<k>" without a copy when there is no prefix
    static const std::string& destRef(const std::string& d, std::string& tmp){
        if(!startsWith(d, "look.")) return d;
        tmp = d.substr(5); return tmp;
    }
    double evDrawn(const EventMod& m, const std::string& dest, double base, double u) const {
        std::string tmp;
        if(v2SlowPath()) return mod_.evalDrawn(destKey(dest), base, m.ev, m.ph, u, *m.ctx);
        if(!m.pure || m.gen != modGen_) return mod_.evalDrawn(destRef(dest, tmp), base, m.ev, m.ph, u, *m.ctx);
        auto f = m.memo.find(dest);
        if(f != m.memo.end()) for(auto& x : f->second) if(x.base == base && x.u == u) return x.v;
        double v = mod_.evalDrawn(destRef(dest, tmp), base, m.ev, m.ph, u, *m.ctx);
        if(f == m.memo.end()) f = m.memo.emplace(dest, std::vector<EventMod::Memo>{}).first;
        if(f->second.size() < 64) f->second.push_back({base, u, v});
        return v;
    }
    double evAnimated(const EventMod& m, const std::string& dest, double base, Beat nowBeat) const {
        std::string tmp;
        if(v2SlowPath()) return mod_.evalAnimated(destKey(dest), base, m.ev, m.ph, nowBeat, *m.ctx);
        // no animated routing (at the same modulation): the loop would add nothing
        if(!m.anim && m.gen == modGen_) return mod_.baseOnly(destRef(dest, tmp), base, *m.ctx);
        return mod_.evalAnimated(destRef(dest, tmp), base, m.ev, m.ph, nowBeat, *m.ctx);
    }

    SceneDoc doc_;
    Scene scene_;
    HarmonyTimeline harmony_;
    Modulation mod_;
    std::unique_ptr<ModContext> sceneCtx_;
    std::vector<std::unique_ptr<Inst>> insts_;
    std::vector<int> order_;
    std::vector<std::vector<std::string>> cycles_;
    std::vector<StoredEvent> store_[4];
    LayerView views_[4];
    std::vector<std::string> errors_, warnings_, problems_;
    EngineStats stats_;
    Beat now_ = 0;
    bool ready_ = false;
    std::array<bool, 4> earlierOn_{};
    int retuneMode_ = 0; double retuneBeats_ = 2; int retuneCurve_ = 1;
    HarmonyOverride ov_; Beat ovHold_ = 0, ovJump_ = 0;   // for snapshots

    // instances with glide on change for transpositions (5.16)
    bool anyGlideTranspose() const {
        for(auto& I : insts_){ GlideOpts g = glideOptsOf(I->in); if(g.enabled(GlideTranspose) && I->in.follow == Follow::Chord) return true; }
        return false;
    }
    // applies a harmony change and morphs the planned notes it moves (ahead: not yet past the playhead).
    // Glide on change (transposition on): notes sounding at the playhead bend (tie) or re-attack from
    // the old pitch with the instance's glide time / curve (-1 time: the scene's retuneBeats / curve);
    // the rest follow the scene retune mode ("new": only new phrases).
    void retune(const std::function<void()>& change){
        struct Old { StoredEvent* s; int root; double semis; bool glide; };
        std::vector<Old> olds;
        std::vector<GlideOpts> gopts;
        for(auto& I : insts_) gopts.push_back(glideOptsOf(I->in));
        for(auto& st : store_) for(auto& s : st){
            Event& e = s.e;
            if(e.kind != EventKind::Note || !e.pitched) continue;
            int owner = indexOf(e.instance); if(owner < 0 || insts_[(size_t)owner]->in.follow != Follow::Chord) continue;
            bool sounding = e.start <= now_ && e.start + e.length > now_ + 1e-9;
            bool glide = sounding && gopts[(size_t)owner].enabled(GlideTranspose);
            if(!glide){
                if(retuneMode_ == 0) continue;
                if(retuneMode_ == 1 && e.start < now_) continue;
            }
            HarmonyAt h = harmony_.at(std::max(e.start, now_));   // all: passed notes follow the change at the playhead
            double semis = h.semitonesOf(e.degree) + draw_retune(e, now_);
            olds.push_back({&s, h.root, semis, glide});
        }
        change();
        touch();
        std::vector<StoredEvent> add;
        for(auto& o : olds){
            Event& e = o.s->e;
            HarmonyAt h = harmony_.at(std::max(e.start, now_));
            int d = h.root - o.root;
            double toSemis = h.semitonesOf(e.degree + d);
            bool keyMoved = d == 0 && std::fabs(toSemis - o.semis + draw_retune(e, now_)) > 1e-9;   // key shift only
            if(d == 0 && !keyMoved) continue;
            if(keyMoved){
                // same degree, new tonic: bend / retune in semitones
                if(o.glide){
                    const GlideOpts& g = gopts[(size_t)o.s->instance];
                    double beats = g.time >= 0 ? g.time : retuneBeats_;
                    int curve = g.time >= 0 ? g.curve : retuneCurve_;
                    int from = glidedetail::endDegree(e);
                    auto semisBend = [&](Event& x){ glidedetail::writeBend(x, from, from, now_, beats, curve, GlideTranspose); x.p["glideFromSemis"] = o.semis - draw_retune(e, now_); x.p["glideToSemis"] = toSemis; };
                    if(e.start >= now_ - 1e-9 || (g.tie && !glidedetail::hasBend(e))){ semisBend(e); e.p.erase("glide"); continue; }
                    Event f = glidedetail::splitAt(e, now_, g.tie);
                    semisBend(f);
                    add.push_back({f, o.s->instance, o.s->phrase});
                    continue;
                }
                for(const char* k : {"glideFromSemis", "glideToSemis"}) if(e.p.count(k)) e.p[k] += toSemis - (o.semis - draw_retune(e, now_));   // stamped key-shift pitch moves too
                e.p["retuneFrom"] = o.semis; e.p["retuneTo"] = toSemis;
                e.p["retuneStart"] = now_; e.p["retuneBeats"] = retuneBeats_; e.p["retuneCurve"] = retuneCurve_;
                continue;
            }
            if(o.glide){
                const GlideOpts& g = gopts[(size_t)o.s->instance];
                double beats = g.time >= 0 ? g.time : retuneBeats_;
                int curve = g.time >= 0 ? g.curve : retuneCurve_;
                int from = glidedetail::endDegree(e);
                bool stamped = e.p.count("glideToSemis") > 0;   // key-shift pitch in semitones: bend those too
                auto wb = [&](Event& x){
                    glidedetail::writeBend(x, from, from + d, now_, beats, curve, GlideTranspose);
                    if(stamped){ x.p["glideFromSemis"] = o.semis - draw_retune(e, now_); x.p["glideToSemis"] = h.semitonesOf(from + d); }
                };
                if(g.tie && !glidedetail::hasBend(e) && e.start < now_){ wb(e); continue; }
                if(e.start >= now_ - 1e-9){ wb(e); e.p.erase("glide"); continue; }
                Event f = glidedetail::splitAt(e, now_, g.tie);
                wb(f);
                add.push_back({f, o.s->instance, o.s->phrase});
                continue;
            }
            double moved = h.semitonesOf(e.degree + d) - (o.semis - draw_retune(e, now_));
            e.degree += d;
            for(const char* k : {"glideTo", "glideFrom"}) if(e.p.count(k)) e.p[k] += d;
            for(const char* k : {"glideFromSemis", "glideToSemis"}) if(e.p.count(k)) e.p[k] += moved;   // stamped key-shift pitch
            e.p["retuneFrom"] = o.semis;
            e.p["retuneTo"] = h.semitonesOf(e.degree);
            e.p["retuneStart"] = now_; e.p["retuneBeats"] = retuneBeats_; e.p["retuneCurve"] = retuneCurve_;
        }
        if(!add.empty()){
            for(auto& a : add) store_[(size_t)a.e.layer].push_back(a);
            for(auto& st : store_) std::stable_sort(st.begin(), st.end(), [](const StoredEvent& x, const StoredEvent& y){
                if(x.e.start != y.e.start) return x.e.start < y.e.start;
                if(x.instance != y.instance) return x.instance < y.instance;
                return x.e.id < y.e.id;
            });
        }
    }
    // the morph offset still to go (same formula as draw::retuneOffset)
    static double draw_retune(const Event& e, Beat now){
        if(!e.p.count("retuneStart")) return 0;
        double b = e.param("retuneBeats", 1), t = b <= 0 ? 1 : std::clamp((now - e.param("retuneStart", 0)) / b, 0.0, 1.0);
        int c = (int)std::lround(e.param("retuneCurve", 1));
        auto sm = [](double x){ return x * x * (3 - 2 * x); };
        double k = c == 0 ? t : c == 2 ? (std::exp(3 * t) - 1) / (std::exp(3.0) - 1) : c == 3 ? sm(sm(t)) : sm(t);
        return (e.param("retuneFrom", 0) - e.param("retuneTo", 0)) * (1 - k);
    }

    void reset(){
        ready_ = false; insts_.clear(); order_.clear(); cycles_.clear(); mod_.clear();
        for(auto& s : store_) s.clear();
        touch();
        errors_.clear(); warnings_.clear(); problems_.clear(); stats_ = EngineStats{}; now_ = 0;
        ov_ = HarmonyOverride{}; ovHold_ = ovJump_ = 0;
    }
    int indexOf(const std::string& id) const { for(size_t i = 0; i < insts_.size(); i++) if(insts_[i]->in.id == id) return (int)i; return -1; }
    bool anySolo() const { for(auto& I : insts_) if(I->solo) return true; return false; }
    bool effMuted(int i) const { const Inst& I = *insts_[(size_t)i]; return I.muted || (anySolo() && !I.solo); }

    std::function<const ParamDef*(const std::string&)> paramDefFor(const TypeDef* t){
        return [t](const std::string& d){ return findParam(t, d); };
    }
    std::unique_ptr<ModContext> makeCtx(const TypeDef* t){
        std::unique_ptr<ModContext> c(new ModContext());
        c->paramDef = paramDefFor(t);
        c->sceneSeed = scene_.seed;
        c->macros = scene_.macros;
        c->follower = [this](const std::string& id, const std::string& what, double window, Beat at){ return followerValue(id, what, window, at); };
        c->brightness = [this](Beat b){ return brightnessSource ? brightnessSource(b) : 0.0; };
        c->arc = [this](Beat b){ return arcSource ? arcSource(b) : 0.0; };
        c->lane = [this](const std::string& n, Beat b){ return laneSource ? laneSource(n, b) : 0.0; };
        return c;
    }
    static std::string destKey(const std::string& d){ return startsWith(d, "look.") ? d.substr(5) : d; }

    // follower source: activity of another instance in [at - window, at)
    double followerValue(const std::string& id, const std::string& what, double window, Beat at) const {
        int i = indexOf(id); if(i < 0) return 0;
        window = std::max(1e-6, window);
        int n = 0; double lv = 0, h = 0;
        for(auto& s : store_[(size_t)insts_[(size_t)i]->in.layer]){
            if(s.instance != i || s.e.start < at - window || s.e.start >= at) continue;
            n++; lv += s.e.level; h += s.e.height;
        }
        if(what == "level") return n ? lv / n : 0;
        if(what == "pitch" || what == "height") return n ? h / n : 0;
        return std::clamp(n / (window * 4), 0.0, 1.0);   // density: 4 events per beat = 1
    }

    // ---------------------------------------------------------------- phrases
    void resetClock(Inst& I, Beat offset, int base){
        I.clockSrc = I.in;
        if(base > 0){
            if(!I.clockSrc.phrase.is_object()) I.clockSrc.phrase = json::object();
            I.clockSrc.phrase["offset"] = offset;
        }
        I.clock.reset(new PhraseClock(I.clockSrc, harmony_, scene_.seed));
        I.clockBase = base;
        I.cursor = std::min(I.cursor, std::max(0, base - 1));
    }
    Phrase phraseAt(Inst& I, int k){
        k = std::max(0, k);
        while((int)I.phrases.size() <= k){
            int g = (int)I.phrases.size();
            Phrase p = I.clock->phrase(g - I.clockBase);
            p.index = g;
            p.seed = basePhraseSeed(scene_.seed, I.in.id, g);
            I.phrases.push_back(p);
        }
        return I.phrases[(size_t)k];
    }
    int indexAt(Inst& I, Beat b){
        if(b < phraseAt(I, 0).start) return 0;
        int k = std::max(0, I.cursor);
        while(k > 0 && phraseAt(I, k).start > b) k--;
        while(phraseAt(I, k).end() <= b) k++;
        I.cursor = k;
        return k;
    }
    PhraseSeeds seedsFor(Inst& I, int k){
        if(I.locked && k >= I.lockFrom) return I.lockSeeds;
        uint64_t s = scene_.seed;
        for(auto& r : I.rerolls) if(k >= r.first) s = r.second;
        return phraseSeeds(s, I.in.id, k, variationOf(I.in));
    }

    // plans the phrases of instance i overlapping [a, until) (and the next one when atLeastNext)
    void cover(int i, Beat a, Beat until, bool atLeastNext, std::vector<int>& stack){
        Inst& I = *insts_[(size_t)i];
        int k0 = indexAt(I, a);
        if(atLeastNext) until = std::max(until, phraseAt(I, k0).end() + 1e-6);
        for(int k = k0; phraseAt(I, k).start < until; k++) planOne(i, k, stack);
    }

    void planOne(int i, int k, std::vector<int>& stack){
        Inst& I = *insts_[(size_t)i];
        if(I.planned.count(k)) return;
        Phrase ph = phraseAt(I, k);
        PhraseSeeds seeds = seedsFor(I, k);
        ph.seed = seeds.phrase;
        // Follow targets first, through this phrase's end
        stack.push_back(i);
        for(int d : I.deps){
            if(std::find(stack.begin(), stack.end(), d) != stack.end()) continue;   // cycle: back edge skipped
            if(effMuted(d)) continue;
            cover(d, ph.start, ph.end(), false, stack);
        }
        stack.pop_back();
        if(I.planned.count(k)) return;    // planned meanwhile (cycle)
        std::vector<Event> followed;
        bool useFollowed = false;
        std::string fid = I.in.seq.is_object() && I.in.seq.contains("followId") && I.in.seq["followId"].is_string() ? I.in.seq["followId"].get<std::string>() : "";
        int f = fid.empty() ? -1 : indexOf(fid);
        if(f >= 0){
            useFollowed = true;
            for(auto& s : store_[(size_t)insts_[(size_t)f]->in.layer])
                if(s.instance == f && s.e.start >= ph.start - 1e-9 && s.e.start < ph.end()) followed.push_back(s.e);
        }
        ModContext& ctx = *I.ctx;
        ctx.phrase = ph; ctx.now = ph.start;
        PlanContext pc;
        pc.harmony = &harmony_;
        pc.sceneSeed = scene_.seed;
        pc.pitch = pitchSpace;
        pc.mod = mod_.hook(I.in.id, I.in.layer, ctx);
        pc.modAt = mod_.hookAt(I.in.id, I.in.layer, ctx);   // step beats for time-based sources
        Instance in = I.in; in.muted = false;
        // glide on change (5.16): the previous phrase's notes, when planned
        std::vector<Event> prevEvs;
        if(k > 0 && I.planned.count(k - 1) && glideOptsOf(in).on){
            for(auto& s : store_[(size_t)I.in.layer]) if(s.instance == i && s.phrase == k - 1) prevEvs.push_back(s.e);
            pc.prevPhrase = &prevEvs;
        }
        std::vector<Event> evs = planPhrase(in, ph, pc, useFollowed ? &followed : nullptr, &seeds);
        // per-event depth (phrase then event level, at the event's own start) and look parameters
        double depthP = mod_.evalPhrase("depth", 1.0, I.in.id, ph, ctx);
        std::map<std::string, double> lookP;
        if(I.type) for(auto& p : I.type->params){
            // enums reach the drawers as the index of their option, booleans as 0 / 1
            // (Event::p holds numbers; e.g. filterfx shape, coverMode, glideShape)
            if(p.type == "enum" || p.type == "bool"){
                const json* v = (I.in.params.is_object() && I.in.params.contains(p.key)) ? &I.in.params[p.key] : &p.def;
                double idx = 0;
                if(p.type == "bool") idx = (v->is_boolean() && v->get<bool>()) || (v->is_number() && v->get<double>() != 0) ? 1 : 0;
                else if(v->is_number()) idx = v->get<double>();
                else if(v->is_string()){
                    auto f = std::find(p.options.begin(), p.options.end(), v->get<std::string>());
                    idx = f == p.options.end() ? 0 : (double)(f - p.options.begin());
                }
                lookP[p.key] = idx;
                continue;
            }
            if(p.type != "float" && p.type != "int") continue;
            // glide tension (snap shape only): stamped when set or routed (renderers default 0.95)
            if(p.key == "glideTension" && !(I.in.params.is_object() && I.in.params.contains(p.key)) && !I.dests.count(p.key)) continue;
            double base = p.def.is_number() ? p.def.get<double>() : 0;
            if(I.in.params.is_object() && I.in.params.contains(p.key) && I.in.params[p.key].is_number()) base = I.in.params[p.key].get<double>();
            if(I.dests.count(p.key) && (p.levels & LevelPhrase)) base = mod_.evalPhrase(p.key, base, I.in.id, ph, ctx);
            lookP[p.key] = base;
        }
        // effect scope target and blob stamps (5.12)
        if(I.in.params.is_object() && I.in.params.contains("fxTarget") && I.in.params["fxTarget"].is_string())
            lookP["fxTarget"] = blob::targetHash(I.in.params["fxTarget"].get<std::string>());
        if(earlierOn_[(size_t)I.in.layer]) lookP["drawOrder"] = (double)i;
        bool blobFx = I.type && I.type->name == "filterfx" && I.in.params.is_object() && I.in.params.value("effect", std::string()) == "blob";
        if(lookP.count("blob") && (lookP["blob"] >= 0.5 || blobFx)){
            lookP["blobSeedP"] = blob::seedToP(seeds.rhythm.get(tagKey("blob")));
            lookP["blobPhStart"] = ph.start; lookP["blobPhLen"] = ph.length;
            lookP["blobSeedG"] = blob::seedToP(mixSeed(instanceSeed(scene_.seed, I.in.id), 0xB10B));
        }
        static const char* evKeys[] = {"size", "brightness", "pitchFine", "height"};
        std::vector<StoredEvent> add;
        for(auto& e : evs){
            e.depth = mod_.evalEvent("depth", depthP, e, ph, ctx);
            for(auto& kv : lookP){
                // mass heights: the planner's (band / harmonics) unless the instance sets or routes them
                if(e.kind == EventKind::Mass && (kv.first == "heightFrom" || kv.first == "heightTo")){
                    bool own = (I.in.params.is_object() && I.in.params.contains(kv.first)) || I.dests.count(kv.first);
                    if(!own || e.p.count("harmonic")) continue;
                }
                double v = kv.second;
                if(I.dests.count(kv.first)) v = mod_.evalEvent(kv.first, v, e, ph, ctx);
                e.p[kv.first] = v;
                // the look's Level (the instrument's brightness) scales the event level the sequencer
                // gave it (velocity, accents), relative to the sequencer's default level, so that with
                // the sequencer at its default the drawn level is the look level. Before, the look
                // level was stamped but never read: every instrument drew at the sequencer's 0.8.
                if(kv.first == "level"){
                    static const double seqDef = [](){ const ParamDef* d = sequencerParam("level"); return d && d->def.is_number() ? std::max(0.05, d->def.get<double>()) : 0.8; }();
                    e.level = std::clamp(e.level * v / seqDef, 0.0, 1.0);
                }
            }
            for(auto k2 : evKeys) if(I.dests.count(k2)){
                const ParamDef* d = findParam(I.type, k2);
                double b = d && d->def.is_number() ? d->def.get<double>() : 0;
                b = mod_.evalPhrase(k2, b, I.in.id, ph, ctx);
                e.p[k2] = mod_.evalEvent(k2, b, e, ph, ctx);
            }
            // 3D paths (5.14): only when the instance sets or routes them (flat events unchanged)
            static const char* pathKeys[] = {"pathKind", "pathAngle", "pathCurve", "pathOriginX", "pathOriginY", "pathTurns", "pathSpeed"};
            for(auto k2 : pathKeys){
                bool set = I.in.params.is_object() && I.in.params.contains(k2);
                if(!set && !I.dests.count(k2)) continue;
                const ParamDef* d = findParam(nullptr, k2);
                double b = d && d->def.is_number() ? d->def.get<double>() : 0;
                if(set){
                    const json& v = I.in.params[k2];
                    if(v.is_number()) b = v.get<double>();
                    else if(v.is_string() && d){ auto f = std::find(d->options.begin(), d->options.end(), v.get<std::string>()); if(f != d->options.end()) b = (double)(f - d->options.begin()); }
                }
                if(I.dests.count(k2)){ b = mod_.evalPhrase(k2, b, I.in.id, ph, ctx); b = mod_.evalEvent(k2, b, e, ph, ctx); }
                if(d && d->type == "enum") b = std::clamp(std::round(b), d->min, d->max);
                e.p[k2] = b;
            }
            add.push_back({e, i, k});
        }
        // element-scope echo / repeat: copies of the target's events (targets planned first)
        std::vector<StoredEvent> copies;
        for(auto& a : add) if(blob::isCopyEffect(a.e)){
            std::vector<Event> tg;
            for(auto& s2 : store_[(size_t)I.in.layer]) if(s2.instance != i) tg.push_back(s2.e);
            for(auto& c : blob::elementCopies(a.e, tg)) copies.push_back({c, i, k});
        }
        auto byStart = [](const StoredEvent& x, const StoredEvent& y){
            if(x.e.start != y.e.start) return x.e.start < y.e.start;
            if(x.instance != y.instance) return x.instance < y.instance;
            return x.e.id < y.e.id;
        };
        auto& st = store_[(size_t)I.in.layer];
        st.insert(st.end(), add.begin(), add.end());
        for(auto& c : copies) st.push_back(c);
        std::stable_sort(st.begin(), st.end(), byStart);
        I.planned.insert(k);
        if(I.planCount[k]++ > 0) stats_.replannedPhrases++;
        stats_.plannedPhrases++;
    }

    // forget phrases >= k of instance i (their events go; they are planned again when needed)
    void dropFrom(int i, int k){
        touch();
        Inst& I = *insts_[(size_t)i];
        auto& st = store_[(size_t)I.in.layer];
        st.erase(std::remove_if(st.begin(), st.end(), [&](const StoredEvent& s){ return s.instance == i && s.phrase >= k; }), st.end());
        for(auto it = I.planned.begin(); it != I.planned.end();) if(*it >= k) it = I.planned.erase(it); else ++it;
        // a layer change leaves events on the old layer: clear them too
        for(int l = 0; l < 4; l++) if(l != I.in.layer){
            auto& o = store_[l];
            o.erase(std::remove_if(o.begin(), o.end(), [&](const StoredEvent& s){ return s.instance == i && s.phrase >= k; }), o.end());
        }
        // element-scope copy effects reading this instance re-plan too (their copies are stale)
        if(dropDepth_ < 4) for(size_t j = 0; j < insts_.size(); j++){
            if((int)j == i || !copiesFrom((int)j, i)) continue;
            dropDepth_++;
            dropFrom((int)j, indexAt(*insts_[j], phraseAt(I, k).start));
            dropDepth_--;
        }
    }

    int dropDepth_ = 0;
    // does instance j make element copies of instance i's events?
    bool copiesFrom(int j, int i) const {
        const Instance& in = insts_[(size_t)j]->in;
        if(!in.params.is_object() || in.params.value("fxScope", std::string()) != "element") return false;
        std::string fx = in.params.value("effect", std::string());
        if(fx != "echo" && fx != "repeat") return false;
        std::string t = in.params.value("fxTarget", std::string());
        return in.layer == insts_[(size_t)i]->in.layer && (t.empty() || t == insts_[(size_t)i]->in.id);
    }

    // events whose end (+ ring-out) is behind everything the layer can still show
    void dropOld(){
        for(int l = 0; l < 4; l++){
            double md = 1;
            for(size_t i = 0; i < insts_.size(); i++) if(insts_[i]->in.layer == l) md = std::min(md, insts_[i]->minDepth);
            const LayerView& v = views_[l];
            double behind = v.headerX / std::max(1e-9, v.pxPerBeat * std::max(0.05, md));
            Beat limit = now_ - behind - options.ringOutBeats;
            auto& st = store_[l];
            st.erase(std::remove_if(st.begin(), st.end(), [&](const StoredEvent& s){
                return s.e.start + s.e.length + s.e.param("release", 0) < limit; }), st.end());
        }
    }

    // ---------------------------------------------------------------- modulation entries, graph
    Routing normalized(Routing r, const TypeDef* t){
        if(r.sourceParams.is_object() && r.sourceParams.contains("_render")) r.sourceParams["render"] = r.sourceParams["_render"];
        if(startsWith(r.dest, "look.") && t && t->param(r.dest.substr(5))) r.dest = r.dest.substr(5);
        return r;
    }
    void rebuildModulation(){
        mod_.clear();
        for(auto& I : insts_) for(auto& r : I->in.routings) mod_.add(I->in.id, I->in.layer, normalized(r, I->type));
        for(auto& r : scene_.routings){
            size_t c = r.dest.find(':');
            if(c != std::string::npos){
                int i = indexOf(r.dest.substr(0, c)); if(i < 0) continue;
                Routing s = r; s.dest = r.dest.substr(c + 1);
                mod_.add(insts_[(size_t)i]->in.id, insts_[(size_t)i]->in.layer, normalized(s, insts_[(size_t)i]->type));
            } else if(startsWith(r.dest, "layer") && r.dest.size() > 6 && r.dest[5] >= '0' && r.dest[5] <= '3' && r.dest[6] == '.'){
                int l = r.dest[5] - '0';
                Routing s = normalized(r, nullptr); s.dest = "layer" + r.dest.substr(6);
                mod_.add("@layer" + std::to_string(l), l, s);
            } else mod_.add("", -1, normalized(r, nullptr));
        }
        for(auto& I : insts_){
            I->dests.clear();
            for(auto& e : mod_.all()) if(e.owner == I->in.id || e.owner.empty()) I->dests.insert(e.r.dest);
            I->minDepth = depthFloor(I->in.id);
            I->hasAnim = mod_.hasAnimatedFor(I->in.id);
            I->drawnPure = mod_.drawnPure(I->in.id);
        }
        modGen_++; touch();
        mod_.resetSmoothing();
    }
    // the smallest depth the depth routings of an instance (and the scene) can give
    double depthFloor(const std::string& id) const {
        const ParamDef* d = findParam(nullptr, "depth");
        double lo = d ? d->min : 0.05, hi = d ? d->max : 8, sum = 0;
        for(auto& e : mod_.all()){
            if(e.r.dest != "depth" || !(e.owner == id || e.owner.empty())) continue;
            double smin = mod::jbool(e.r.sourceParams, "bipolar", false) ? -1 : 0;
            auto off = [&](double s){ return e.r.hasRange ? e.r.amount * (e.r.min + (e.r.max - e.r.min) * s - 1.0) : e.r.amount * s * (hi - lo); };
            sum += std::min({off(smin), off(1.0), 0.0});
        }
        return std::clamp(1.0 + sum, std::max(0.05, lo), hi);
    }
    void rebuildGraph(){
        int n = (int)insts_.size();
        for(int i = 0; i < n; i++){
            Inst& I = *insts_[(size_t)i];
            std::set<int> d;
            if(I.in.seq.is_object() && I.in.seq.contains("followId") && I.in.seq["followId"].is_string()){
                int f = indexOf(I.in.seq["followId"].get<std::string>());
                if(f >= 0 && f != i) d.insert(f);
            }
            for(int j = 0; j < n; j++) if(j != i && copiesFrom(i, j)) d.insert(j);   // copy targets first
            for(auto& e : mod_.all()){
                if(e.owner != I.in.id || !startsWith(e.r.source, "follow:")) continue;
                std::string rest = e.r.source.substr(7);
                int f = indexOf(rest.substr(0, rest.find(':')));
                if(f >= 0 && f != i) d.insert(f);
            }
            I.deps.assign(d.begin(), d.end());
        }
        // order: depth-first, targets before followers; cycles reported
        order_.clear(); cycles_.clear();
        std::vector<int> state((size_t)n, 0), path;
        std::function<void(int)> visit = [&](int i){
            state[(size_t)i] = 1; path.push_back(i);
            for(int d : insts_[(size_t)i]->deps){
                if(state[(size_t)d] == 1){
                    std::vector<std::string> cyc;
                    for(size_t p = std::find(path.begin(), path.end(), d) - path.begin(); p < path.size(); p++) cyc.push_back(insts_[(size_t)path[p]]->in.id);
                    cycles_.push_back(cyc);
                } else if(state[(size_t)d] == 0) visit(d);
            }
            path.pop_back(); state[(size_t)i] = 2; order_.push_back(i);
        };
        for(int i = 0; i < n; i++) if(state[(size_t)i] == 0) visit(i);
        warnings_.clear();
        for(auto& c : cycles_){
            std::string s; for(auto& id : c) s += id + " -> "; s += c.front();
            warnings_.push_back("Follow cycle: " + s + " (planned without the back link)");
        }
    }
};

inline double EventMod::drawn(const std::string& dest, double base, double u) const { return en->evDrawn(*this, dest, base, u); }
inline double EventMod::animated(const std::string& dest, double base, Beat nowBeat) const { return en->evAnimated(*this, dest, base, nowBeat); }

} // namespace forms2
