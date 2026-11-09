//
//  v2Perform.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Performance layer over Engine (ROADMAP 5.2 / 5.3):
//  - snapshots: the full playable state of an engine to / from JSON (restore then run = same events)
//  - Performer: scene slots with queued changes (now / bar / phrase / 4 bars / at a beat), hard cut or
//    crossfade of N beats (both scenes planned, gains per slot for the compositor), a setlist (lengths
//    in bars or minutes), transport (play / stop / seek), a lane registry, timeline actions applied
//    when the clock crosses them, and a record log of performed actions.
//  - templates: new scene from a template JSON.
//
//  Beats: the performer's clock is global; each slot runs its scene from its own origin
//  (local beat = global - origin), so an incoming scene starts at its beginning.
//
//  Header-only, plain C++17 + nlohmann::json.
//

#pragma once

#include <array>
#include <fstream>
#include <sstream>
#include "v2Engine.h"

namespace forms2 {

// ---------------------------------------------------------------- snapshot JSON
namespace perform_json {
inline json u64(uint64_t v){ return v; }
inline json toJ(const Phrase& p){ return {{"index", p.index}, {"start", p.start}, {"length", p.length}, {"seed", p.seed}}; }
inline Phrase phraseJ(const json& j){ Phrase p; p.index = j.value("index", 0); p.start = j.value("start", 0.0); p.length = j.value("length", 16.0); p.seed = j.value("seed", (uint64_t)0); return p; }
inline json toJ(const SeedLineage& l){ return {{"mode", (int)l.mode}, {"amount", l.amount}, {"anchor", l.anchor}, {"fresh", l.fresh}}; }
inline SeedLineage lineageJ(const json& j){
    SeedLineage l; l.mode = (VarMode)j.value("mode", 0); l.amount = j.value("amount", 0.0); l.anchor = j.value("anchor", (uint64_t)0);
    l.fresh = j.value("fresh", std::vector<uint64_t>{}); return l;
}
inline json toJ(const PhraseSeeds& s){ return {{"phrase", s.phrase}, {"rhythm", toJ(s.rhythm)}, {"pitch", toJ(s.pitch)}}; }
inline PhraseSeeds seedsJ(const json& j){ PhraseSeeds s; s.phrase = j.value("phrase", (uint64_t)0); s.rhythm = lineageJ(j["rhythm"]); s.pitch = lineageJ(j["pitch"]); return s; }
inline json toJ(const Event& e){
    return {{"id", e.id}, {"instance", e.instance}, {"layer", e.layer}, {"kind", (int)e.kind}, {"look", e.look},
            {"start", e.start}, {"length", e.length}, {"pitched", e.pitched}, {"degree", e.degree}, {"height", e.height},
            {"height2", e.height2}, {"level", e.level}, {"depth", e.depth}, {"colourRole", e.colourRole}, {"seed", e.seed},
            {"transient", e.transient}, {"p", e.p}};
}
inline Event eventJ(const json& j){
    Event e; e.id = j.value("id", (uint64_t)0); e.instance = j.value("instance", ""); e.layer = j.value("layer", 0);
    e.kind = (EventKind)j.value("kind", 0); e.look = j.value("look", ""); e.start = j.value("start", 0.0); e.length = j.value("length", 1.0);
    e.pitched = j.value("pitched", true); e.degree = j.value("degree", 0); e.height = j.value("height", 0.5); e.height2 = j.value("height2", 0.5);
    e.level = j.value("level", 1.0); e.depth = j.value("depth", 1.0); e.colourRole = j.value("colourRole", "neutral");
    e.seed = j.value("seed", (uint64_t)0); e.transient = j.value("transient", false);
    e.p = j.value("p", std::map<std::string, double>{}); return e;
}
} // namespace perform_json

inline json snapshotToJson(const EngineState& st){
    using namespace perform_json;
    json insts = json::array();
    for(auto& s : st.insts){
        json rr = json::array(); for(auto& r : s.rerolls) rr.push_back({r.first, r.second});
        json ph = json::array(); for(auto& p : s.phrases) ph.push_back(toJ(p));
        json pc = json::array(); for(auto& kv : s.planCount) pc.push_back({kv.first, kv.second});
        insts.push_back({{"id", s.id}, {"muted", s.muted}, {"solo", s.solo}, {"locked", s.locked},
                         {"lockSeeds", toJ(s.lockSeeds)}, {"lockFrom", s.lockFrom}, {"rerolls", rr},
                         {"clockBase", s.clockBase}, {"clockSrc", toJson(s.clockSrc)}, {"phrases", ph}, {"cursor", s.cursor},
                         {"planned", std::vector<int>(s.planned.begin(), s.planned.end())}, {"planCount", pc}});
    }
    json ev = json::array();
    for(int l = 0; l < 4; l++){ json a = json::array(); for(auto& s : st.events[l]) a.push_back({{"e", toJ(s.e)}, {"instance", s.instance}, {"phrase", s.phrase}}); ev.push_back(a); }
    return {{"format", "espectres.snapshot"}, {"formatVersion", 1}, {"scene", st.scene}, {"now", st.now},
            {"override", {{"hold", st.ov.hold}, {"jumpDegree", st.ov.jumpDegree}, {"holdBeat", st.holdBeat}, {"jumpBeat", st.jumpBeat}}},
            {"instances", insts}, {"events", ev}};
}
inline bool snapshotFromJson(const json& j, EngineState& st, std::string* err = nullptr){
    using namespace perform_json;
    try {
        st = EngineState{};
        st.scene = j.at("scene"); st.now = j.value("now", 0.0);
        const json& o = j.at("override");
        st.ov.hold = o.value("hold", false); st.ov.jumpDegree = o.value("jumpDegree", -1000);
        st.holdBeat = o.value("holdBeat", 0.0); st.jumpBeat = o.value("jumpBeat", 0.0);
        for(auto& s : j.at("instances")){
            InstanceState I;
            I.id = s.value("id", ""); I.muted = s.value("muted", false); I.solo = s.value("solo", false); I.locked = s.value("locked", false);
            I.lockSeeds = seedsJ(s.at("lockSeeds")); I.lockFrom = s.value("lockFrom", 0);
            for(auto& r : s.at("rerolls")) I.rerolls.push_back({r[0].get<int>(), r[1].get<uint64_t>()});
            I.clockBase = s.value("clockBase", 0); I.clockSrc = instanceFromJson(s.at("clockSrc"));
            for(auto& p : s.at("phrases")) I.phrases.push_back(phraseJ(p));
            I.cursor = s.value("cursor", 0);
            for(auto& k : s.at("planned")) I.planned.insert(k.get<int>());
            for(auto& kv : s.at("planCount")) I.planCount[kv[0].get<int>()] = kv[1].get<int>();
            st.insts.push_back(I);
        }
        const json& ev = j.at("events");
        for(int l = 0; l < 4 && l < (int)ev.size(); l++)
            for(auto& s : ev[(size_t)l]) st.events[l].push_back({eventJ(s.at("e")), s.value("instance", -1), s.value("phrase", 0)});
        return true;
    } catch(const std::exception& e){ if(err) *err = e.what(); return false; }
}
inline json snapshot(const Engine& e){ return snapshotToJson(e.captureState()); }
inline bool restoreSnapshot(Engine& e, const json& j, std::string* err = nullptr){
    EngineState st; if(!snapshotFromJson(j, st, err)) return false;
    if(!e.restoreState(st)){ if(err && !e.problems().empty()) *err = e.problems().front(); return false; }
    return true;
}

// ---------------------------------------------------------------- templates
// A new scene from a template: a copy with a new name and seed (seed 0 keeps the template's)
inline json sceneFromTemplate(const json& tmpl, const std::string& name, uint64_t seed = 0){
    json j = tmpl;
    j.erase("template");
    if(!name.empty()) j["name"] = name;
    if(seed) j["seed"] = seed;
    j["version"] = 1;
    return j;
}
inline bool loadTemplateFile(const std::string& path, json& out){
    std::ifstream f(path); if(!f) return false;
    std::stringstream ss; ss << f.rdbuf();
    try { out = json::parse(ss.str()); } catch(...){ return false; }
    return true;
}

// ---------------------------------------------------------------- timeline actions
// action: mute / unmute / solo / unsolo / lock / unlock / reroll (target = instance id),
// macro (target = name, value), hold / jump (value = degree) / clearHarmony,
// scene (value = setlist index, or data.scene a scene JSON; data.boundary, data.crossfade)
struct TimelineAction {
    Beat beat = 0;
    std::string action, target;
    double value = 0;
    json data = json::object();
};
inline json toJson(const TimelineAction& a){ return {{"beat", a.beat}, {"action", a.action}, {"target", a.target}, {"value", a.value}, {"data", a.data}}; }
inline TimelineAction timelineActionFromJson(const json& j){
    TimelineAction a; a.beat = j.value("beat", 0.0); a.action = j.value("action", ""); a.target = j.value("target", "");
    a.value = j.value("value", 0.0); a.data = j.contains("data") ? j["data"] : json::object(); return a;
}

enum class Boundary : int { Now = 0, Bar = 1, Phrase = 2, FourBars = 3, At = 4 };
inline const char* boundaryName(Boundary b){ static const char* n[] = {"now", "bar", "phrase", "4bars", "at"}; return n[(int)b]; }
inline Boundary boundaryFromName(const std::string& s){
    if(s == "bar") return Boundary::Bar; if(s == "phrase") return Boundary::Phrase;
    if(s == "4bars" || s == "fourBars") return Boundary::FourBars; if(s == "at") return Boundary::At;
    return Boundary::Now;
}

// One setlist entry: length in bars (of its own harmony) or minutes (at its bpm); 0 = until changed
struct SetEntry {
    json scene;
    double bars = 0, minutes = 0;
    double crossfadeBeats = 0;        // into the next entry
};

// ---------------------------------------------------------------- performer
class Performer {
public:
    struct Slot {
        std::unique_ptr<Engine> engine;
        Beat origin = 0;              // global beat of the scene's beat 0
        int setIndex = -1;
        Beat fadeStart = 0; double fadeBeats = 0; bool fadingIn = false, fadingOut = false;
        Beat endBeat = -1;            // setlist end (global), -1 none
    };
    struct Pending { bool active = false; json scene; int setIndex = -1; Beat at = 0; double crossfade = 0; };

    std::vector<std::string> problems;

    // ---- scenes
    bool loadScene(const json& scene, Beat at = 0){ resetRun(); setlist_.clear(); base_ = scene; baseSet_ = -1; return start(at); }
    bool setSetlist(const std::vector<SetEntry>& entries, Beat at = 0){
        resetRun(); setlist_ = entries; baseSet_ = 0; base_ = json();
        return !entries.empty() && start(at);
    }
    const std::vector<SetEntry>& setlist() const { return setlist_; }

    // Queue a scene (or a setlist entry): switches at the boundary, a hard cut or a crossfade of N beats.
    // The boundary is taken from the current scene at the performer's clock. Returns the switch beat.
    Beat queueScene(const json& scene, Boundary when, double crossfadeBeats = 0, Beat at = 0){
        TimelineAction a; a.beat = now_; a.action = "scene"; a.value = -1;
        a.data = {{"scene", scene}, {"boundary", boundaryName(when)}, {"crossfade", crossfadeBeats}, {"at", at}};
        perform(a); return lastAt_;
    }
    Beat queueSetEntry(int index, Boundary when, double crossfadeBeats = 0, Beat at = 0){
        TimelineAction a; a.beat = now_; a.action = "scene"; a.value = index;
        a.data = {{"boundary", boundaryName(when)}, {"crossfade", crossfadeBeats}, {"at", at}};
        perform(a); return lastAt_;
    }
    const Pending& pending() const { return pending_; }
    // the next boundary of a kind in the current scene after the clock (global beat)
    Beat nextBoundary(Boundary b, Beat at = 0) const {
        if(slots_.empty() || b == Boundary::Now) return now_;
        if(b == Boundary::At) return std::max(now_, at);
        const Slot& s = slots_.back(); Engine& e = *s.engine;
        Beat L = now_ - s.origin;
        if(b == Boundary::Phrase){
            Beat best = -1;
            for(auto& in : e.scene().instances){
                if(e.muted(in.id)) continue;
                Phrase p = e.phrase(in.id, e.phraseIndexAt(in.id, L));
                best = std::max(best, p.end());
            }
            if(best > L + 1e-9) return best + s.origin;
            b = Boundary::Bar;
        }
        for(double w = 32; w < 1e6; w *= 2)
            for(Beat x : e.harmony().barStarts(L, L + w)){
                if(x <= L + 1e-9) continue;
                if(b == Boundary::FourBars && e.harmony().barIndex(x + 1e-9) % 4 != 0) continue;
                return x + s.origin;
            }
        return now_;
    }

    // ---- transport
    void play(){ playing_ = true; }
    void stop(){ playing_ = false; }
    bool playing() const { return playing_; }
    Beat position() const { return now_; }
    // advances the clock by dBeats when playing
    void tick(double dBeats){ if(playing_) update(now_ + dBeats); }
    // the frame: applies the queued changes and timeline actions crossed, then plans every slot.
    // A clock going backwards seeks.
    void update(Beat beat){
        if(beat < now_ - 1e-9){ seek(beat); return; }
        advanceTo(beat);
        planSlots(beat);
    }
    // Re-plans from a beat deterministically: the run restarts from the beginning (first scene or
    // setlist entry) and the timeline / setlist up to the beat is applied without planning.
    void seek(Beat beat){
        bool rec = recording_; recording_ = false;
        resetRun();
        start(startBeat_);
        if(beat > now_) advanceTo(beat);
        planSlots(beat);
        recording_ = rec;
    }

    // ---- slots (compositor)
    int slotCount() const { return (int)slots_.size(); }
    Engine& engine(int slot = -1){ return *slots_[(size_t)(slot < 0 ? (int)slots_.size() - 1 : slot)].engine; }
    const Slot& slot(int i) const { return slots_[(size_t)i]; }
    Beat localBeat(int slot, Beat global) const { return global - slots_[(size_t)slot].origin; }
    // gain of a slot (0..1) at the clock: crossfades ramp linearly
    double slotGain(int i) const {
        const Slot& s = slots_[(size_t)i];
        double u = s.fadeBeats > 0 ? std::clamp((now_ - s.fadeStart) / s.fadeBeats, 0.0, 1.0) : 1.0;
        if(s.fadingOut) return 1 - u;
        if(s.fadingIn) return u;
        return 1;
    }
    // per layer: scene layer gain x slot gain
    std::array<double, 4> layerGains(int i) const {
        std::array<double, 4> g{};
        const Scene& sc = slots_[(size_t)i].engine->scene();
        for(int l = 0; l < 4; l++) g[(size_t)l] = ((size_t)l < sc.layers.size() ? sc.layers[(size_t)l].gain : 1.0) * slotGain(i);
        return g;
    }
    int currentSetIndex() const { return slots_.empty() ? -1 : slots_.back().setIndex; }

    // ---- lanes (timeline lanes as modulation sources, global beats)
    void registerLane(const std::string& name, std::function<double(Beat)> fn){ lanes_[name] = std::move(fn); }
    void unregisterLane(const std::string& name){ lanes_.erase(name); }
    std::vector<std::string> laneNames() const { std::vector<std::string> v; for(auto& kv : lanes_) v.push_back(kv.first); return v; }
    // lanes the current scenes read and nobody registered
    std::vector<std::string> missingLanes() const {
        std::set<std::string> n;
        for(auto& s : slots_) for(auto& l : s.engine->laneNames()) if(!lanes_.count(l)) n.insert(l);
        return {n.begin(), n.end()};
    }

    // ---- timeline actions
    void setTimeline(std::vector<TimelineAction> t){
        std::stable_sort(t.begin(), t.end(), [](const TimelineAction& a, const TimelineAction& b){ return a.beat < b.beat; });
        timeline_ = std::move(t);
        tlCursor_ = 0; while(tlCursor_ < timeline_.size() && timeline_[tlCursor_].beat < now_ - 1e-9) tlCursor_++;
    }
    const std::vector<TimelineAction>& timeline() const { return timeline_; }

    // ---- performing (recorded when recording)
    void perform(const TimelineAction& a0){
        TimelineAction a = a0; a.beat = now_;
        apply(a);
        if(recording_) log_.push_back(a);
    }
    void mute(const std::string& id, bool on){ perform(act(on ? "mute" : "unmute", id)); }
    void solo(const std::string& id, bool on){ perform(act(on ? "solo" : "unsolo", id)); }
    void lock(const std::string& id, bool on){ perform(act(on ? "lock" : "unlock", id)); }
    void reroll(const std::string& id){ perform(act("reroll", id)); }
    void macro(const std::string& name, double v){ perform(act("macro", name, v)); }
    void hold(bool on){ perform(act(on ? "hold" : "unhold", "", 0)); }
    void jump(int degree){ perform(act("jump", "", degree)); }
    void clearHarmony(){ perform(act("clearHarmony", "")); }

    // ---- record log
    void setRecording(bool on){ recording_ = on; }
    bool recording() const { return recording_; }
    const std::vector<TimelineAction>& recorded() const { return log_; }
    void clearRecorded(){ log_.clear(); }
    // the log as timeline actions (sorted; scene changes keep their resolved switch beat as "at")
    std::vector<TimelineAction> recordedTimeline() const {
        std::vector<TimelineAction> t = log_;
        std::stable_sort(t.begin(), t.end(), [](const TimelineAction& a, const TimelineAction& b){ return a.beat < b.beat; });
        return t;
    }
    static json timelineToJson(const std::vector<TimelineAction>& t){ json a = json::array(); for(auto& x : t) a.push_back(toJson(x)); return a; }
    static std::vector<TimelineAction> timelineFromJson(const json& j){ std::vector<TimelineAction> t; if(j.is_array()) for(auto& x : j) t.push_back(timelineActionFromJson(x)); return t; }

    // ---- performance snapshot (all slots, pending change, clock)
    json snapshot() const {
        json sl = json::array();
        for(auto& s : slots_) sl.push_back({{"engine", snapshotToJson(s.engine->captureState())}, {"origin", s.origin}, {"setIndex", s.setIndex},
                                             {"fadeStart", s.fadeStart}, {"fadeBeats", s.fadeBeats}, {"fadingIn", s.fadingIn}, {"fadingOut", s.fadingOut}, {"endBeat", s.endBeat}});
        return {{"format", "espectres.performance"}, {"now", now_}, {"slots", sl},
                {"pending", {{"active", pending_.active}, {"scene", pending_.scene}, {"setIndex", pending_.setIndex}, {"at", pending_.at}, {"crossfade", pending_.crossfade}}},
                {"timelineCursor", tlCursor_}};
    }
    bool restore(const json& j){
        try {
            std::vector<Slot> ns;
            for(auto& s : j.at("slots")){
                Slot x; x.engine.reset(new Engine()); wire(x);
                std::string err;
                if(!restoreSnapshot(*x.engine, s.at("engine"), &err)){ problems = {"snapshot: " + err}; return false; }
                x.origin = s.value("origin", 0.0); x.setIndex = s.value("setIndex", -1);
                x.fadeStart = s.value("fadeStart", 0.0); x.fadeBeats = s.value("fadeBeats", 0.0);
                x.fadingIn = s.value("fadingIn", false); x.fadingOut = s.value("fadingOut", false); x.endBeat = s.value("endBeat", -1.0);
                ns.push_back(std::move(x));
            }
            slots_ = std::move(ns);
            for(auto& s : slots_) wire(s);
            now_ = j.value("now", 0.0);
            const json& p = j.at("pending");
            pending_.active = p.value("active", false); pending_.scene = p["scene"]; pending_.setIndex = p.value("setIndex", -1);
            pending_.at = p.value("at", 0.0); pending_.crossfade = p.value("crossfade", 0.0);
            tlCursor_ = j.value("timelineCursor", (size_t)0);
            return true;
        } catch(const std::exception& e){ problems = {std::string("snapshot: ") + e.what()}; return false; }
    }

private:
    std::vector<Slot> slots_;
    std::vector<SetEntry> setlist_;
    json base_; int baseSet_ = -1; Beat startBeat_ = 0;
    Pending pending_;
    std::vector<TimelineAction> timeline_, log_;
    size_t tlCursor_ = 0;
    std::map<std::string, std::function<double(Beat)>> lanes_;
    Beat now_ = 0, lastAt_ = 0;   // lastAt_: switch beat of the last scene action
    bool playing_ = false, recording_ = false;

    static TimelineAction act(const std::string& a, const std::string& t, double v = 0){ TimelineAction x; x.action = a; x.target = t; x.value = v; return x; }

    void resetRun(){ slots_.clear(); pending_ = Pending{}; tlCursor_ = 0; now_ = 0; problems.clear(); }
    bool start(Beat at){
        startBeat_ = at; now_ = at;
        bool ok = baseSet_ >= 0 ? addSlot(setlist_[(size_t)baseSet_].scene, baseSet_, at) : addSlot(base_, -1, at);
        while(tlCursor_ < timeline_.size() && timeline_[tlCursor_].beat < at - 1e-9) tlCursor_++;
        return ok;
    }
    void wire(Slot& s){
        Beat origin = s.origin;
        s.engine->laneSource = [this, origin](const std::string& n, Beat b){ auto f = lanes_.find(n); return f == lanes_.end() || !f->second ? 0.0 : f->second(b + origin); };
    }
    bool addSlot(const json& scene, int setIndex, Beat origin){
        Slot s; s.engine.reset(new Engine()); s.origin = origin; s.setIndex = setIndex;
        wire(s);
        if(!s.engine->loadJson(scene)){ problems = s.engine->problems(); return false; }
        s.engine->setNow(0);
        if(setIndex >= 0 && (size_t)setIndex < setlist_.size()){
            const SetEntry& en = setlist_[(size_t)setIndex];
            if(en.minutes > 0) s.endBeat = origin + en.minutes * s.engine->scene().bpm;
            else if(en.bars > 0){
                int n = (int)std::lround(en.bars);
                for(double w = 64; w < 1e7 && s.endBeat < 0; w *= 2){
                    auto bs = s.engine->harmony().barStarts(0, w);
                    if((int)bs.size() > n) s.endBeat = origin + bs[(size_t)n];
                }
            }
        }
        slots_.push_back(std::move(s));
        return true;
    }

    // applies everything due in (now_, beat]: queued switches, fade ends, setlist ends, timeline actions
    void advanceTo(Beat beat){
        for(int guard = 0; guard < 10000; guard++){
            // next due item: timeline action, pending switch, or a setlist end (queued automatically)
            Beat tl = tlCursor_ < timeline_.size() ? timeline_[tlCursor_].beat : 1e300;
            if(!pending_.active && !slots_.empty() && slots_.back().endBeat >= 0){
                int nx = slots_.back().setIndex + 1;
                if((size_t)nx < setlist_.size()){
                    pending_ = Pending{true, json(), nx, slots_.back().endBeat, setlist_[(size_t)slots_.back().setIndex].crossfadeBeats};
                }
            }
            Beat sw = pending_.active ? pending_.at : 1e300;
            Beat fe = 1e300;
            for(auto& s : slots_) if(s.fadingOut) fe = std::min(fe, s.fadeStart + s.fadeBeats);
            Beat t = std::min({tl, sw, fe});
            if(t > beat + 1e-9) break;
            setClock(std::max(now_, t));
            if(fe <= t && fe <= sw && fe <= tl){       // a faded-out slot leaves
                for(size_t i = 0; i < slots_.size();) if(slots_[i].fadingOut && slots_[i].fadeStart + slots_[i].fadeBeats <= t + 1e-9) slots_.erase(slots_.begin() + (long)i); else i++;
                for(auto& s : slots_) if(s.fadingIn && s.fadeStart + s.fadeBeats <= t + 1e-9) s.fadingIn = false;
            } else if(sw <= tl) doSwitch();
            else { TimelineAction a = timeline_[tlCursor_++]; bool rec = recording_; recording_ = false; apply(a); recording_ = rec; }
        }
        setClock(beat);
    }
    void setClock(Beat b){ now_ = b; for(auto& s : slots_) s.engine->setNow(b - s.origin); }
    void planSlots(Beat beat){ now_ = beat; for(auto& s : slots_) s.engine->update(beat - s.origin); }

    void doSwitch(){
        Pending p = pending_; pending_ = Pending{};
        json scene = p.setIndex >= 0 && (size_t)p.setIndex < setlist_.size() ? setlist_[(size_t)p.setIndex].scene : p.scene;
        if(p.crossfade <= 0){
            std::vector<Slot> keep; slots_.swap(keep);
            if(!addSlot(scene, p.setIndex, p.at)){ slots_.swap(keep); return; }
            return;
        }
        // older fades end now; the current slot fades out, the new one in
        for(size_t i = 0; i + 1 < slots_.size();) slots_.erase(slots_.begin() + (long)i);
        if(!addSlot(scene, p.setIndex, p.at)) return;
        Slot& out = slots_[slots_.size() - 2]; Slot& in = slots_.back();
        out.fadingOut = true; out.fadingIn = false; out.fadeStart = p.at; out.fadeBeats = p.crossfade; out.endBeat = -1;
        in.fadingIn = true; in.fadeStart = p.at; in.fadeBeats = p.crossfade;
    }

    void apply(const TimelineAction& a){
        if(a.action == "scene"){
            Boundary b = boundaryFromName(a.data.value("boundary", "now"));
            Pending p; p.active = true;
            p.setIndex = (int)a.value;
            if(a.data.contains("scene")) { p.scene = a.data["scene"]; p.setIndex = -1; }
            p.crossfade = a.data.value("crossfade", 0.0);
            p.at = nextBoundary(b, a.data.value("at", 0.0));
            lastAt_ = p.at;
            if(p.setIndex < 0 && !p.scene.is_object()) return;
            pending_ = p;
            if(p.at <= now_ + 1e-9) doSwitch();
            return;
        }
        if(slots_.empty()) return;
        Engine& e = *slots_.back().engine;
        Beat L = now_ - slots_.back().origin;
        if(a.action == "mute") e.setMuted(a.target, true);
        else if(a.action == "unmute") e.setMuted(a.target, false);
        else if(a.action == "solo") e.setSolo(a.target, true);
        else if(a.action == "unsolo") e.setSolo(a.target, false);
        else if(a.action == "lock") e.setLocked(a.target, true);
        else if(a.action == "unlock") e.setLocked(a.target, false);
        else if(a.action == "reroll") e.reroll(a.target);
        else if(a.action == "macro") e.setMacro(a.target, a.value);
        else if(a.action == "hold" || a.action == "unhold"){ HarmonyOverride o = e.harmonyOverride(); o.hold = a.action == "hold"; e.setHarmonyOverride(o, L); }
        else if(a.action == "jump"){ HarmonyOverride o = e.harmonyOverride(); o.jumpDegree = (int)a.value; e.setHarmonyOverride(o, L); }
        else if(a.action == "clearHarmony") e.clearHarmonyOverride();
    }
};

} // namespace forms2
