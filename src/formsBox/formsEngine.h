//
//  formsEngine.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  What FormsBox does, without OF or GUI: scenes and setlist on disk, stepping through the
//  setlist, building render requests (drawn by Forms GL Box, src/formsGL), and the queue of
//  rendered images (FormsSchedule). The node only adds parameters, rendering and UI.
//

#pragma once

#include "formsCore.h"
#include "formsGenerators.h"
#include "formsPerformance.h"
#include <chrono>
#include <ctime>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <set>

class ofTexture;

namespace forms {

namespace fs = std::filesystem;

// One rendered (or pending) image and the musical values it was made with
struct Image {
    std::string id, png, json, scene, palette, scale;
    std::vector<formscover::Mask> covers;
    nlohmann::json layerCover = nlohmann::json::object();
    int layer = 1, notegrid = 100, snapshot = -1;
    double bpm = 120;                  // the scene tempo (may be a fraction)
    double bars = 8;                   // its length in bars (may be a fraction: quantBars)
    unsigned seed = 0;
    std::vector<int> roots, trans;     // per bar: degree index, transpose in semitones
    int root = -1;                     // the root (MIDI) the page was drawn against; -1 = its scene's root
    std::vector<float> scaleNotes;     // semitones of the scale (what Macro_3 output as actualScale)
    nlohmann::json perf = nlohmann::json::object();   // the scene's performance settings when it was requested
    bool preview = false;              // editor preview: shown in the editor, never scrolls in
    std::string previewOf;             // the preset it previews
    // Phase 3 (layers 2..4): where the image sits in absolute beats (layer 1 keeps the 1920 timing
    // of the clock and leaves this at -1), and how it composites
    double startBeat = -1;
    double cutBeat = -1;               // hard cut: the image stops here (a new scene starts), else -1
    double startBar = -1;              // the absolute bar where it starts (its harmony: from barIndex of it)
    bool kick = false;                 // its layer is the kick layer
    bool cancelled = false;            // a scene change made it obsolete before it was shown
    // Repetition (LayerDef::repeat): a request stands for `repeat` pages in a row; when it is
    // rendered it becomes that many copies (repeatIndex 0..repeat-1, each repeat = 1)
    int repeat = 1, repeatIndex = 0;
    // Phrase (LayerDef::phrase): a request stands for `slices` pages that show the rendered phrase
    // window by window (slice 0..slices-1 of the image's width); repeats repeat the whole phrase
    int slices = 1, slice = 0;
    // Layers 2 and up: a phrase that is not a whole number of pages lasts exactly its own length
    // (phraseBars, in bars; 0 = whole pages): the render covers the phrase alone and its last page
    // is shown only up to the phrase's end (pageBeats), where the next phrase starts
    double phraseBars = 0;
    double sliceUW() const { return slices > 1 ? (phraseBars > 0 ? bars / phraseBars : 1.0 / slices) : 1.0; }
    double sliceU0() const { return slices > 1 ? slice * sliceUW() : 0.0; }
    double phraseBeats() const { return 4.0 * (phraseBars > 0 ? phraseBars : std::max(1.0 / kBarGrid, bars) * std::max(1, slices)); }
    // where page k of the request (repeatIndex: 0 .. repeat x slices - 1) starts, in beats from the request's start
    double pageOffsetBeats(int k) const { const int n = std::max(1, slices); return (k / n) * phraseBeats() + (k % n) * 4.0 * std::max(1.0 / kBarGrid, bars); }
    // the beats this page plays: its length, or less for the last page of such a phrase
    double pageBeats() const {
        const double len = 4.0 * std::max(1.0 / kBarGrid, bars);
        if(phraseBars <= 0 || slices <= 1 || slice != slices - 1) return len;
        return std::max(0.0, std::min(len, 4.0 * phraseBars - slice * len));
    }
    double copiesUntil = -1;           // a scene change: copies that would start here or later are dropped
    double askedAt = -1;               // Engine::now when it was asked for (render time)
    std::string blend = "lighten";
    float gain = 1;
    // The rendered pixels, when the renderer is inside the app (Forms GL Box): straight-alpha RGBA,
    // 16 bits per channel, pw x ph. The scrollers upload them directly instead of loading the PNG (which may still be
    // being written, or not written at all when archiving is off). Shared by repeat copies.
    std::shared_ptr<const std::vector<uint16_t>> rgba;
    int pw = 0, ph = 0;
    // CPU fallback for the live scrollers. Exact GPU mode instead retains a native-size texture.
    std::shared_ptr<const std::vector<uint16_t>> stageRgba;
    int stageW = 0, stageH = 0;
    // Shared worker GL texture, published only after its upload fence signals.
    std::shared_ptr<ofTexture> texture;
};

// Timings of the image pipeline (ms), for the System tab: last value and a running mean
struct StageTime {
    double last = 0, mean = 0, peak = 0; int n = 0;
    void add(double ms){ last = ms; peak=std::max(peak,ms); n++; mean = n == 1 ? ms : mean + (ms - mean) * (n < 50 ? 1.0 / n : 0.02); }
};
struct StageTimer {
    StageTime& stage;
    std::chrono::steady_clock::time_point start=std::chrono::steady_clock::now();
    explicit StageTimer(StageTime& s):stage(s){}
    ~StageTimer(){stage.add(std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());}
};
struct PipelineStats {
    StageTime queue, render, archive, upload, total;   // waiting for a worker, drawing, PNG + sidecar, texture upload, asked -> on screen-ready
    StageTime player,request,delivery;                 // main-thread work; peaks expose occasional stalls
    int archiveBacklog = 0;
};
inline PipelineStats& pipelineStats(){ static PipelineStats s; return s; }

// Shared between FormsBox (writes) and LayerScroller (reads, and reports what is under the
// header). Both run on the main thread. One entry per layer; Phase 2 uses layer 0 only.
struct Schedule {
    struct Layer {
        std::deque<std::shared_ptr<Image>> ready;   // rendered, waiting to scroll in
        std::shared_ptr<Image> header;              // set by LayerScroller
        int shown = 0;                              // images that scrolled in
        int repeats = 0;                            // wraps with no ready image (image repeated)
        // Phase 3, layers 2..4: the images placed in absolute beats (written by the player)
        struct Slot {
            std::shared_ptr<Image> img; double start = 0, len = 32; bool repeat = false; bool seen = false;
            double cut = -1;                        // hard cut: shown only up to this beat
            double end() const { return cut >= 0 ? cut : start + len; }
        };
        std::deque<Slot> slots;
        double scheduledEnd = -1;                   // end beat of the last placed slot (-1: none)
        float phase = 0;                            // where the header is inside its image (0..1)
        int dropped = 0;                            // images that arrived after their time had passed
        int retimed = 0;                            // late images moved to the next free place (not popped in mid-view)
        // Switch now: this image replaces the one under the header when it arrives (once), even with
        // Replace at Playhead off; the scroller / ring clears it when it has done so
        std::shared_ptr<Image> replaceNext;
    };
    std::deque<Layer> layers = std::deque<Layer>(4); // grows without invalidating layer references
    double headerBeat = 0;                          // absolute beat under the header (from layer 1)
    Layer& layer(int i){ if((int)layers.size() <= i) layers.resize(i + 1); return layers[i]; }
};

inline json readJsonFile(const fs::path& p){
    std::ifstream f(p);
    if(!f) throw std::runtime_error("cannot read " + p.string());
    return json::parse(f);
}

// Write to a temporary name, then rename: readers never see half-written files
inline void writeJsonAtomic(const fs::path& p, const json& j){
    fs::create_directories(p.parent_path());
    fs::path tmp = p.parent_path() / ("." + p.filename().string() + ".tmp");
    {
        std::ofstream f(tmp);
        f << j.dump(1);
    }
    fs::rename(tmp, p);
}

inline std::string safeFileName(std::string s){
    for(char& c : s) if(!(isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.')) c = '_';
    return s;
}

class Engine {
public:
    // --- configuration
    fs::path rendererFolder;           // the data folder (bin/data/Forms by default)
    fs::path pitchclassFolder;         // shared scale / chord catalog (scales.txt, chords.txt), read-only
    int width = 1920, height = 1080;   // size of the images to request (any size: the renderer
                                       // draws other sizes than its canvas offscreen)
    int maxPending = 2;                // a GO beyond this many unanswered requests is skipped
    double pendingTimeout = 120;       // seconds: an unanswered request is given up after this

    // --- content
    std::map<std::string, Scene> scenes;   // by name
    std::map<std::string,fs::path> embeddedKicks;
    void prepareKick(const Scene& scene){
        if(!scene.kickSample)return;
        auto existing=embeddedKicks.find(scene.kickSample->id);
        if(existing!=embeddedKicks.end() && fs::is_regular_file(existing->second))return;
        embeddedKicks[scene.kickSample->id]=formskick::materialize(*scene.kickSample,rendererFolder/"samples"/"embedded");
    }
    fs::path kickFile(const Scene& scene) const {
        if(!scene.kickSample) return {};
        auto it=embeddedKicks.find(scene.kickSample->id);
        return it==embeddedKicks.end()?fs::path():it->second;
    }
    void importKick(Scene& scene,const fs::path& path){
        auto asset=formskick::importFile(path);
        auto cached=formskick::materialize(*asset,rendererFolder/"samples"/"embedded");
        embeddedKicks[asset->id]=cached;
        scene.kickSample=std::move(asset);
        scene.performance["kickSample"]=scene.kickSample->name;
        scene.performance["kickSynth"]=0;
    }
    std::vector<SetlistEntry> setlist;
    std::vector<ScaleDef> scaleLib;          // library/scales.json
    // The patch's Global Pitch node, as the Forms node reads it each frame (nothing here depends on
    // ofxOceanode): with scaleOn the scene's scale is replaced by these degrees on this root pitch
    // class, shift moves every bar's degree along the scale; transpose is a bend of the sound
    struct PitchFollow { bool scaleOn = false; std::vector<int> notes; int rootPc = 0; int shift = 0; bool transposeOn = false; float transpose = 0; };
    PitchFollow pitchFollow;
    // the scale a scene draws with: the Global Pitch scale when followed, else its own (name empty: none)
    ScaleDef effectiveScale(const Scene& s) const {
        if(pitchFollow.scaleOn && !pitchFollow.notes.empty()){ ScaleDef d; d.name = "Global Pitch"; d.notes = pitchFollow.notes; d.legacyIndex = -1; d.source = "Global Pitch"; return d; }
        const ScaleDef* sd = findScale(s.scale);
        return sd ? *sd : ScaleDef{"", {}, -1, ""};
    }
    // the root (MIDI) a scene sounds on: its own, moved to the Global Pitch root's pitch class when followed
    int effectiveRoot(const Scene& s) const {
        if(!pitchFollow.scaleOn) return s.root;
        int d = ((pitchFollow.rootPc - s.root % 12) % 12 + 12) % 12;   // nearest; a tritone goes up
        if(d > 6) d -= 12;
        return std::max(0, std::min(127, s.root + d));
    }
    // Global Pitch Shift: every bar's degree moves along the scale (a diatonic transpose)
    void applyPitchShift(std::vector<int>& roots) const { if(pitchFollow.scaleOn && pitchFollow.shift) for(auto& r : roots) r += pitchFollow.shift; }
    std::vector<PaletteDef> paletteLib;      // library/palettes.json
    std::vector<std::pair<std::string, std::string>> methods;   // name, family: legacy methods ("legacy/h"...), then presets ("cat/<category>")
    std::vector<GenDef> gens;                // parameterised generators (generators.json)
    std::map<std::string, Preset> presets;   // instrument presets, by ref "<gen>/<name>"
    std::string previewPng, previewId, previewRef;   // last finished editor preview (and its preset)
    int previewCount = 0;                    // bumps when a preview finishes
    std::deque<std::pair<std::string, std::string>> previewsDone;   // (tag, png) of finished previews, for the editor

    // --- state
    bool useSetlist = true;
    std::string manualScene;
    int setlistEntry = 0, imagesInEntry = 0;
    std::string session;
    int counter = 0;
    Rng rng;
    std::map<std::string, std::shared_ptr<Image>> pending;   // requested, not rendered yet
    std::map<std::string, std::chrono::steady_clock::time_point> pendingSince;
    std::shared_ptr<Schedule> schedule = std::make_shared<Schedule>();
    // Phase 3: one harmony timeline in absolute bars, shared by all layers. Layer 1 fills it as
    // before (makeHarmony per image, so a single-layer scene draws the same numbers); the other
    // layers slice it, filling gaps the same way.
    std::map<int, std::pair<int, int>> harmonyBars;   // absolute bar -> (degree, transpose)
    double layer1NextBar = 0;          // predicted start bar of the next layer-1 image
    int lastRequestRepeat = 1;         // pages the last layer-1 request stands for (GO pacing)
    // Layer 1's images in absolute bars, as requested (shifted when layer 1 drifts): which scene
    // plays at each bar, and where scenes change (and whether that change is a hard cut)
    // bars: what the entry spans (its image's bars x repeat); period: its image's bars
    struct PlanEntry { double startBar = 0, bars = 8, period = 0; std::string scene, id; bool hardCut = false; };
    // The scene lane (formsLane.h): the scene its blocks give layer 1's image starting at a bar
    // ("" = none: the setlist / manual scene), and whether the lane cuts into it
    std::function<std::string(int bar, bool& hardCut)> laneSceneAt;
    std::deque<PlanEntry> layer1Plan;
    bool hardCutManual = false;        // scene changes not from the setlist (manual): hard cut?

    // --- Replay (Gallery): a session played back from its archive, no renderer involved
    bool replaying = false;
    std::string replayName;
    std::deque<std::shared_ptr<Image>> replayAll1;                 // the whole plan (for looping)
    std::map<int, std::deque<std::shared_ptr<Image>>> replayAllX;
    std::deque<std::shared_ptr<Image>> replay1;                    // still to play
    std::map<int, std::deque<std::shared_ptr<Image>>> replayX;
    std::shared_ptr<Image> replayFirst;                            // its first layer-1 image
    double replayOrigin = std::numeric_limits<double>::quiet_NaN();// header beat where that one started
    double replayLoopBeats = 0;                                    // one pass of layer 1, in beats
    int replayLoop = 0;

    void startReplay(const std::string& name, const std::deque<std::shared_ptr<Image>>& l1, const std::map<int, std::deque<std::shared_ptr<Image>>>& extra){
        if(l1.empty()){ note("replay: " + name + " has no layer-1 images"); return; }
        replaying = true; replayName = name;
        playedImages.clear();
        replayAll1 = l1; replayAllX = extra;
        replayLoop = 0;
        replayLoopBeats = 0; for(auto& im : l1) replayLoopBeats += 4.0 * std::max(1.0 / kBarGrid, im->bars);
        replayX.clear();
        refillReplay();
        replayFirst = replay1.front();
        replayOrigin = std::numeric_limits<double>::quiet_NaN();
        for(auto& L : schedule->layers){ L.ready.clear(); L.slots.clear(); L.scheduledEnd = -1; }
        pending.clear(); pendingSince.clear();
        layer1Plan.clear(); layerNextBar.clear();
        for(int k = 0; k < 2 && !replay1.empty(); k++){ schedule->layers[0].ready.push_back(replay1.front()); replay1.pop_front(); }
        note("replaying " + name + " (" + std::to_string(l1.size()) + " images on layer 1)");
    }
    void stopReplay(){
        if(!replaying) return;
        replaying = false;
        replay1.clear(); replayX.clear();
        note("replay stopped");
    }
    // a fresh copy of the plan for the next pass (images on screen are never changed)
    void refillReplay(){
        replay1.clear();   // (the extra layers' queues keep what is not handed over yet: the next pass goes after it)
        for(auto& im : replayAll1) replay1.push_back(std::make_shared<Image>(*im));
        for(auto& [l, q] : replayAllX){
            for(auto& im : q){
                auto c = std::make_shared<Image>(*im);
                c->startBeat = im->startBeat + replayLoop * replayLoopBeats;   // + origin when it is known
                replayX[l].push_back(c);
            }
        }
    }
    // GO during a replay: the next archived image instead of a render
    void replayNext(){
        if(replay1.empty()){ replayLoop++; refillReplay(); }
        if(!replay1.empty()){ schedule->layers[0].ready.push_back(replay1.front()); replay1.pop_front(); }
    }
    // extra layers during a replay: hand over the images due within the next 64 beats
    void replayLayers(){
        if(std::isnan(replayOrigin)) return;
        double B = schedule->headerBeat;
        for(auto& [l, q] : replayX){
            auto& L = schedule->layer(l);
            while(!q.empty() && q.front()->startBeat + replayOrigin < B + 64){
                auto im = q.front(); q.pop_front();
                im->startBeat += replayOrigin;
                if(im->startBeat + im->bars * 4.0 > B) L.ready.push_back(im);
            }
        }
    }
    std::map<int, double> layerAskedAt;   // layers 2..4: Engine::now of their last request (turns when overdue)
    std::map<int, double> layerNextBar;   // layers 2..4: start bar of the next request
    int renderAheadBars = 2;           // a new layer starts this many bars after the header
    std::deque<std::string> log;
    std::deque<std::string> syncLog;   // Phase 3: each layer's position at every bar line (System tab)
    int errors = 0;
    int skipped = 0;                   // GOs skipped because the renderer was busy
    int lost = 0;                      // requests that got no answer before pendingTimeout
    int rendererCanvasW = 0, rendererCanvasH = 0;   // its default canvas (generators.json)

    fs::path scenesDir() const { return rendererFolder / "scenes"; }
    // --- scene banks: a bank is a folder under scenes/ ("" = scenes/ itself, the legacy scenes).
    // Scene names stay unique across banks (the setlist and the scene lane refer to them by name).
    std::string currentBank = "My scenes";   // where new scenes go (Forms Box saves it with the patch)
    // a bank's name is its folder's name (only characters a folder can't have are replaced)
    static std::string bankName(std::string b){
        for(char& c : b) if(c == '/' || c == '\\' || c == ':') c = '-';
        while(!b.empty() && (b[0] == '.' || b[0] == ' ')) b.erase(0, 1);
        return b;
    }
    fs::path scenePath(const Scene& s) const { return (s.bank.empty() ? scenesDir() : scenesDir() / bankName(s.bank)) / (safeFileName(s.name) + ".json"); }
    std::vector<std::string> banks() const {
        std::set<std::string> b{""};
        for(auto& [n, sc] : scenes) b.insert(sc.bank);
        std::error_code ec;
        if(fs::exists(scenesDir(), ec)) for(auto& e : fs::directory_iterator(scenesDir(), ec)) if(e.is_directory()) b.insert(e.path().filename().string());
        if(!currentBank.empty()) b.insert(currentBank);
        return std::vector<std::string>(b.begin(), b.end());
    }
    static std::string bankLabel(const std::string& b){ return b.empty() ? std::string("Legacy") : b; }
    // A new bank (an empty folder)
    // returns its name ("" if it could not be made)
    std::string createBank(const std::string& wanted){
        std::string b = bankName(wanted);
        if(b.empty()) return "";
        std::error_code ec; fs::create_directories(scenesDir() / b, ec);
        return ec ? std::string() : b;
    }
    // A scene to another bank (its file moves)
    bool moveSceneToBank(const std::string& name, const std::string& bank){
        auto it = scenes.find(name);
        if(it == scenes.end() || it->second.bank == bank) return false;
        std::error_code ec;
        fs::remove(scenePath(it->second), ec);
        it->second.bank = bank;
        saveScene(it->second);
        return true;
    }
    fs::path setlistPath() const { return rendererFolder / "setlists" / "current.json"; }
    fs::path libraryDir() const { return rendererFolder / "library"; }
    fs::path presetsDir() const { return rendererFolder / "presets"; }
    fs::path presetPath(const Preset& p) const { return presetsDir() / safeFileName(p.gen) / (safeFileName(p.name) + ".json"); }
    fs::path sessionDir() const { return rendererFolder / "sessions" / session; }
    // In-process renderers can hand the request directly to a worker. The external
    // renderer and headless callers retain the normal file-based protocol.
    std::function<void(const fs::path&,json)> requestSink;

    void note(const std::string& s){
        log.push_back(s);
        while(log.size() > 200) log.pop_front();
    }

    // New session: unique name (seconds), counter from 0, fresh queue, setlist from the start
    void newSession(unsigned seed = 0){
        auto t = std::time(nullptr);
        char buf[32];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d_%H%M%S", std::localtime(&t));
        session = buf;
        counter = 0;
        rng.seed(seed ? seed : (unsigned)std::chrono::high_resolution_clock::now().time_since_epoch().count());
        pending.clear();
        pendingSince.clear();
        skipped = lost = errors = 0;
        for(auto& l : schedule->layers){ l.ready.clear(); l.shown = 0; l.repeats = 0; l.slots.clear(); l.scheduledEnd = -1; l.dropped = 0; l.replaceNext.reset(); }
        playedImages.clear();
        schedule->headerBeat = 0;
        harmonyBars.clear();
        layer1NextBar = 0;
        layerNextBar.clear();
        layer1Plan.clear();
        setlistEntry = 0;
        imagesInEntry = 0;
        note("session " + session);
    }

    void resetSetlist(){
        setlistEntry = 0;
        imagesInEntry = 0;
        note("setlist reset");
    }

    // --- disk

    void loadAll(){
        scenes.clear();
        if(fs::exists(scenesDir())){
            // scenes/ (the legacy bank) and one level of bank folders
            auto loadFolder = [&](const fs::path& dir, const std::string& bank){
                for(auto& e : fs::directory_iterator(dir)){
                    if(e.path().extension() != ".json") continue;
                    try {
                        Scene s = readJsonFile(e.path()).get<Scene>();
                        if(s.name.empty()) s.name = e.path().stem().string();
                        s.bank = bank;
                        if(scenes.count(s.name)) note("scene " + s.name + " is in two banks (" + bankLabel(scenes[s.name].bank) + ", " + bankLabel(bank) + "): " + bankLabel(bank) + " kept");
                        scenes[s.name] = s;
                    } catch(std::exception& ex){ note("scene " + e.path().filename().string() + ": " + ex.what()); }
                }
            };
            loadFolder(scenesDir(), "");
            for(auto& e : fs::directory_iterator(scenesDir()))
                if(e.is_directory() && e.path().filename().string()[0] != '.') loadFolder(e.path(), e.path().filename().string());
        }
        for(auto& [name,scene]:scenes) try{ prepareKick(scene); }
            catch(const std::exception& e){note("kick sample in "+name+": "+e.what());}
        setlist.clear();
        if(fs::exists(setlistPath())){
            try { setlist = readJsonFile(setlistPath()).value("entries", std::vector<SetlistEntry>()); }
            catch(std::exception& ex){ note(std::string("setlist: ") + ex.what()); }
        }
        json gen;
        try { gen = readJsonFile(rendererFolder / "generators.json"); }
        catch(std::exception& ex){ note(std::string("generators.json: ") + ex.what()); }
        methods.clear();
        gens.clear();
        for(auto& g : gen.value("generators", json::array())){
            std::string fam = g.value("family", std::string());
            if(fam.rfind("legacy/", 0) == 0) methods.push_back({g.value("name", std::string()), fam});
            else gens.push_back(genDefFromJson(g));
        }
        // the process slot: the effects generator's parameters, appended to the other generators
        // (generators.json lists them once); a generator that still lists them keeps its own
        const GenDef* fx = nullptr;
        for(auto& g : gens) if(g.name == "effects") fx = &g;
        if(fx){
            std::vector<ParamDef> proc;
            for(auto& p : fx->params) if(p.key != "steps") proc.push_back(p);
            for(auto& g : gens){
                if(g.name == "effects" || g.param("processOn")) continue;
                for(auto& p : proc){ g.params.push_back(p); g.processKeys.push_back(p.key); }
            }
        }
        // Cover is a host feature, shared by every drawable instrument.
        auto coverDef = genDefFromJson({{"params", formscover::schema()}});
        for(auto& g : gens) if(g.name != "effects")
            for(auto& p : coverDef.params){
                auto it=std::find_if(g.params.begin(),g.params.end(),[&](const ParamDef& old){return old.key==p.key;});
                if(it==g.params.end()) g.params.push_back(p); else *it=p;
            }
        // Blocking: the switch of the black background, on every generator that can blank
        // ("blank what is underneath"), shown first in that parameter's group.
        for(auto& g : gens){
            auto cc=std::find_if(g.params.begin(),g.params.end(),[](const ParamDef& p){return p.key=="clearChance";});
            if(cc==g.params.end() || g.param("blocking")) continue;
            auto def=genDefFromJson({{"params",json::array({formscover::blockingSchema(cc->group)})}});
            g.params.insert(cc,def.params[0]);
        }
        loadPresets();
        if(gen.contains("canvas") && gen["canvas"].size() == 2){
            rendererCanvasW = gen["canvas"][0].get<int>();
            rendererCanvasH = gen["canvas"][1].get<int>();
        }
        loadLibrary(gen);
        // scenes saved with 1920 indices ("#n") get library names, and are saved that way
        for(auto& [name, sc] : scenes){
            bool changed = false;
            if(!sc.scale.empty() && sc.scale[0] == '#'){ sc.scale = scaleNameForLegacy(std::stoi(sc.scale.substr(1))); changed = true; }
            if(!sc.palette.empty() && sc.palette[0] == '#'){ sc.palette = paletteNameForLegacy(std::stoi(sc.palette.substr(1))); changed = true; }
            if(changed) saveScene(sc);
        }
        note("loaded " + std::to_string(scenes.size()) + " scenes, " + std::to_string(setlist.size()) + " setlist entries");
    }

    // Set by the node: a scene that plays a kick from the samples folder (by number or by name) gets that
    // sample embedded before it is written, so the scene keeps its kick when the folder moves or changes.
    std::function<void(Scene&)> embedFolderKick;
    void saveScene(const Scene& s){
        if(embedFolderKick && !s.kickSample){
            auto own = scenes.find(s.name);
            if(own != scenes.end() && &own->second == &s) embedFolderKick(own->second);
        }
        std::error_code ec; fs::create_directories(scenePath(s).parent_path(), ec);
        writeJsonAtomic(scenePath(s), json(s));
    }

    // Macro_8's router snapshots → each scene's performance settings, by the scene's Director slot
    // (Macro_8 was selected by that same number). Values a slot doesn't have come from slot 0
    // (Macro_8's state when the patch was saved). Returns the number of scenes updated.
    int importPerformance(const fs::path& presetFolder){
        json m = readJsonFile(presetFolder / "Macro_8.json");
        json snaps = m.value("Snapshots", json::object());
        auto valuesOf = [](const json& slot){
            json v = json::object();
            const json routers = slot.value("routerValues", json::object());   // named: items() of a temporary would dangle
            for(auto& [k, rv] : routers.items()){
                if(rv.value("type", std::string()) == "c" || !rv.contains("value") || rv["value"].is_null()) continue;
                v[k] = rv["value"];
            }
            return v;
        };
        json base = snaps.contains("0") ? valuesOf(snaps["0"]) : json::object();
        int n = 0;
        for(auto& [name, sc] : scenes){
            if(sc.snapshot < 0) continue;
            json v = base;
            std::string key = std::to_string(sc.snapshot);
            if(snaps.contains(key)){
                const json sv = valuesOf(snaps[key]);
                for(auto& [k, x] : sv.items()) v[k] = x;
            }
            else note("performance: " + name + " (slot " + key + ") had no Macro_8 snapshot; slot 0 values used");
            sc.performance = completePerformance(v);
            saveScene(sc);
            n++;
        }
        note("performance settings imported into " + std::to_string(n) + " scenes");
        return n;
    }

    // --- instrument presets

    const GenDef* findGen(const std::string& name) const {
        for(auto& g : gens) if(g.name == name) return &g;
        return nullptr;
    }

    void loadPresets(){
        presets.clear();
        if(fs::exists(presetsDir())){
            for(auto& d : fs::directory_iterator(presetsDir())){
                if(!d.is_directory()) continue;
                for(auto& e : fs::directory_iterator(d.path())){
                    if(e.path().extension() != ".json") continue;
                    try {
                        Preset p = readPresetFile(e.path());
                        if(p.gen.empty()) p.gen = d.path().filename().string();
                        if(p.name.empty()) p.name = e.path().stem().string();
                        if(presets.count(p.ref())) note("preset " + p.ref() + " defined twice; using " + e.path().filename().string());
                        presets[p.ref()] = p;
                    } catch(std::exception& ex){ note("preset " + e.path().filename().string() + ": " + ex.what()); }
                }
            }
        }
        refreshPresetMethods();
    }

    // presets appear in the method lists after the legacy methods
    void refreshPresetMethods(){
        methods.erase(std::remove_if(methods.begin(), methods.end(), [](auto& m){ return isPresetRef(m.first); }), methods.end());
        for(auto& [ref, p] : presets) methods.push_back({ref, "cat/" + presetCategory(p)});
    }

    // A preset file, with its "process" blocks merged into "params" (how presets are kept in memory)
    static Preset readPresetFile(const fs::path& path){
        json j = readJsonFile(path);
        mergeProcess(j);
        if(j.contains("layers")) for(auto& l : j["layers"]) mergeProcess(l);
        auto migrateCover=[](json& item){
            if(!item.contains("params") || !item["params"].is_object()) return;
            auto& p=item["params"];
            if(p.contains("coverDilate") && !p.contains("coverGrowHeight")) p["coverGrowHeight"]=p["coverDilate"];
        };
        migrateCover(j);
        if(j.contains("layers")) for(auto& l:j["layers"]) migrateCover(l);
        return j.get<Preset>();
    }
    // Files keep the process settings in "process"; in memory they sit in "params"
    static void mergeProcess(json& j){
        if(!j.is_object() || !j.contains("process") || !j["process"].is_object()) return;
        if(!j.contains("params") || !j["params"].is_object()) j["params"] = json::object();
        for(auto& [k, v] : j["process"].items()) j["params"][k] = v;
        j.erase("process");
    }
    void splitProcess(json& j) const {
        if(!j.is_object() || !j.contains("params") || !j["params"].is_object()) return;
        const GenDef* g = findGen(j.value("gen", std::string()));
        if(!g || g->processKeys.empty()) return;
        json proc = json::object(), own = json::object();
        for(auto& [k, v] : j["params"].items()) (g->isProcessKey(k) ? proc : own)[k] = v;
        j["params"] = own;
        if(!proc.empty()) j["process"] = proc;
    }
    json presetFile(const Preset& p) const {
        json j = p;
        splitProcess(j);
        if(j.contains("layers")) for(auto& l : j["layers"]) splitProcess(l);
        return j;
    }
    // A resolved item: its process values go to "process" (requests keep them apart too)
    json item(const GenDef& g, const std::string& ref, json params) const {
        json it{{"gen", g.name}, {"preset", ref}, {"params", params}};
        splitProcess(it);
        return it;
    }

    // ---- a scene's own version of its instruments (Scene::instruments) ----
    // While a scene is set here (OwnScope), every instrument looked up for a request is that scene's
    // version: the shared instrument with the scene's changes laid over it. Outside (the library,
    // its previews and thumbnails, auditions) the shared instrument is used.
    const Scene* ownScene = nullptr;
    mutable std::map<std::string, Preset> ownCache;
    struct OwnScope {
        Engine& e; const Scene* prev;
        OwnScope(Engine& en, const Scene* s) : e(en), prev(en.ownScene) { e.ownScene = s; e.ownCache.clear(); }
        ~OwnScope(){ e.ownScene = prev; e.ownCache.clear(); }
        OwnScope(const OwnScope&) = delete;
        OwnScope& operator=(const OwnScope&) = delete;
    };
    const Preset* presetFor(const std::string& ref) const {
        auto it = presets.find(ref);
        if(it == presets.end()) return nullptr;
        if(!ownScene || !ownScene->instruments.is_object()) return &it->second;
        auto d = ownScene->instruments.find(ref);
        if(d == ownScene->instruments.end()) return &it->second;
        auto c = ownCache.find(ref);
        if(c == ownCache.end()) c = ownCache.emplace(ref, presetWithOwn(it->second, *d)).first;
        return &c->second;
    }
    // the instrument as a scene plays it (the shared one where the scene has no version of its own)
    Preset effectivePreset(const Scene& s, const std::string& ref) const {
        auto it = presets.find(ref);
        if(it == presets.end()) return Preset();
        if(!s.instruments.is_object()) return it->second;
        auto d = s.instruments.find(ref);
        return d == s.instruments.end() ? it->second : presetWithOwn(it->second, *d);
    }

    void savePreset(const Preset& p){
        writeJsonAtomic(presetPath(p), presetFile(p));
        presets[p.ref()] = p;
        refreshPresetMethods();
    }

    void deletePreset(const std::string& ref){
        auto it = presets.find(ref);
        if(it == presets.end()) return;
        fs::remove(presetPath(it->second));
        presets.erase(it);
        refreshPresetMethods();
    }

    // Renames a preset; scenes that use it follow
    bool renamePreset(const std::string& ref, const std::string& newName){
        auto it = presets.find(ref);
        if(it == presets.end() || newName.empty() || newName.find('/') != std::string::npos) return false;
        Preset p = it->second;
        p.name = newName;
        if(presets.count(p.ref())) return false;
        fs::remove(presetPath(it->second));
        presets.erase(it);
        savePreset(p);
        for(auto& [n, sc] : scenes){
            bool changed = false;
            for(auto& l : sc.layers) for(auto& c : l.cells) if(c.item == ref){ c.item = p.ref(); changed = true; }
            for(auto& l:sc.layers)if(l.effectPreset==ref){l.effectPreset=p.ref();changed=true;}
            for(auto& g:sc.globalEffects)if(g==ref){g=p.ref();changed=true;}
            if(sc.instruments.is_object() && sc.instruments.contains(ref)){ sc.instruments[p.ref()] = sc.instruments[ref]; sc.instruments.erase(ref); changed = true; }   // the scene's own version follows
            if(changed) saveScene(sc);
        }
        // stacks that play it as a layer follow too
        for(auto& [r, sp] : presets){
            bool changed = false;
            for(auto& l : sp.layers) if(l.value("preset", std::string()) == ref){ l["preset"] = p.ref(); changed = true; }
            if(changed) savePreset(sp);
        }
        return true;
    }

    // Stacks that play this preset as a layer
    int stacksUsing(const std::string& ref) const {
        int n = 0;
        for(auto& [r, sp] : presets) for(auto& l : sp.layers) if(l.value("preset", std::string()) == ref){ n++; break; }
        return n;
    }

    // --- building a scene from scratch (the window's Scenes tab New, Instruments tab Add to scene)

    // A new empty scene, saved: one layer of 8 bars, the library's first palette and scale
    std::string createScene(const std::string& base = "NEW SCENE"){
        Scene s;
        s.name = base;
        for(int i = 2; scenes.count(s.name); i++) s.name = base + " " + std::to_string(i);
        s.bank = currentBank;
        s.bpm = 95;
        if(!paletteLib.empty()) s.palette = paletteLib[0].name;
        if(!scaleLib.empty()) s.scale = scaleLib[0].name;
        s.degreeWeights[0] = 1;
        s.transposeWeights[0] = 1;
        LayerDef l;
        l.bars = 8;
        l.rows = 64;
        l.groupProb.assign(7, 1.0f);
        s.layers.push_back(l);
        scenes[s.name] = s;
        saveScene(s);
        return s.name;
    }

    // The group most scenes give this item's family (the 1920 scenes group methods by family)
    int groupForItem(const std::string& m) const {
        // a legacy method's family letter; a preset's from its category (geometry counts as texture here)
        auto fam = [this](const std::string& x){
            if(isPresetRef(x)){ auto it = presets.find(x); std::string f = it != presets.end() ? categoryFamily(presetCategory(it->second)) : std::string("h"); return f == "g" ? std::string("t") : f; }
            return x.rfind("fx", 0) == 0 ? std::string("fx") : x.substr(0, 1);
        };
        std::string pre = fam(m);
        std::map<int, int> count;
        for(auto& [n, sc] : scenes) for(auto& l : sc.layers) for(auto& c : l.cells)
            if(!c.item.empty() && fam(c.item) == pre) count[c.group]++;
        int best = 0, bestN = -1;
        for(auto& [g, n] : count) if(n > bestN){ best = g; bestN = n; }
        return best;
    }

    // Adds an instrument (a preset or a legacy method) to a layer of a scene, in its first empty
    // cell. The first instrument of a layer plays on every image; the next ones at 50 % (edit the
    // cell's chance in the Scenes tab). Not saved: the scene is marked as edited by the caller.
    bool addItemToScene(Scene& s, int layer, const std::string& m){
        if(s.layers.empty()) return false;
        auto& l = s.layers[std::max(0, std::min(layer, (int)s.layers.size() - 1))];
        bool empty = true;
        for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(!l.cells[i].item.empty()) empty = false;
        Cell c; c.item = m; c.prob = empty ? 100 : 50; c.group = groupForItem(m);
        if((int)l.groupProb.size() <= c.group) l.groupProb.resize(c.group + 1, 1.0f);
        bool placed = false;
        for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(l.cells[i].item.empty()){ l.cells[i] = c; placed = true; break; }
        if(!placed){ l.cells.insert(l.cells.begin() + std::min<int>(l.rows, (int)l.cells.size()), c); if((int)l.cells.size() > l.rows) l.rows++; }
        return true;
    }

    std::string uniquePresetName(const std::string& gen, const std::string& base) const {
        std::string n = base;
        for(int i = 2; presets.count(gen + "/" + n); i++) n = base + " " + std::to_string(i);
        return n;
    }

    // --- library (scales, palettes)

    // Loads library/*.json; the first time, seeds it from the renderer's built-in tables
    // (generators.json), keeping their 1920 index, so existing scenes sound the same.
    void loadLibrary(const json& gen){
        scaleLib.clear();
        paletteLib.clear();
        try { scaleLib = readJsonFile(libraryDir() / "scales.json").value("scales", std::vector<ScaleDef>()); } catch(...) {}
        try { paletteLib = readJsonFile(libraryDir() / "palettes.json").value("palettes", std::vector<PaletteDef>()); } catch(...) {}
        bool seeded = false;
        bool hadLocal = !scaleLib.empty();
        if(scaleLib.empty() && gen.contains("scales")){
            auto names = gen.value("scaleNames", json::array());
            for(size_t i = 0; i < gen["scales"].size(); i++){
                ScaleDef d;
                d.name = i < names.size() ? names[i].get<std::string>() : "scale " + std::to_string(i);
                d.notes = gen["scales"][i].get<std::vector<int>>();
                d.legacyIndex = (int)i;
                scaleLib.push_back(d);
            }
            seeded = true;
        }
        if(paletteLib.empty() && gen.contains("palettes")){
            int i = 0;
            for(auto& p : gen["palettes"]){
                PaletteDef d;
                d.name = p.value("name", "palette " + std::to_string(i));
                d.colors = p.value("colors", std::vector<std::string>{"FFFFFF"});
                d.legacyIndex = i++;
                paletteLib.push_back(d);
            }
            seeded = true;
        }
        (void)hadLocal;
        addCatalog();
        if(seeded){
            saveLibrary();
            note("library created from the renderer's tables: " + std::to_string(scaleLib.size()) + " scales, " + std::to_string(paletteLib.size()) + " palettes");
        }
    }

    // The Pitchclass catalog ("<index>, <name> <notes...>;" per line): its scales and chords join the
    // library after the local ones. A local entry with the same name wins (scenes keep their sound);
    // a catalog entry whose name is taken by a different local scale gets " (catalog)".
    void addCatalog(){
        if(pitchclassFolder.empty()) return;
        int added = 0;
        auto read = [&](const char* file, bool chords){
            std::ifstream f(pitchclassFolder / file);
            if(!f) return;
            std::string line;
            while(std::getline(f, line)){
                auto comma = line.find(',');
                auto semi = line.find(';');
                if(comma == std::string::npos || semi == std::string::npos || semi < comma) continue;
                std::string idx = line.substr(0, comma);
                std::stringstream ss(line.substr(comma + 1, semi - comma - 1));
                std::string name;
                ss >> name;
                std::vector<int> notes;
                int n;
                while(ss >> n) notes.push_back(n);
                if(name.empty() || notes.empty()) continue;
                ScaleDef d;
                d.name = chords ? "chord " + name : name;
                d.notes = notes;
                d.source = std::string(chords ? "catalog chord #" : "catalog scale #") + idx;
                bool clash = false, same = false;
                for(auto& s : scaleLib) if(s.name == d.name){ clash = true; same = s.notes == d.notes; }
                if(same) continue;
                if(clash) d.name += " (catalog)";
                scaleLib.push_back(d);
                added++;
            }
        };
        read("scales.txt", false);
        read("chords.txt", true);
        if(added) note("Pitchclass catalog: " + std::to_string(added) + " scales and chords");
    }

    void saveLibrary(){
        std::vector<ScaleDef> local;
        for(auto& s : scaleLib) if(s.source.empty()) local.push_back(s);
        writeJsonAtomic(libraryDir() / "scales.json", json{{"scales", local}});
        writeJsonAtomic(libraryDir() / "palettes.json", json{{"palettes", paletteLib}});
    }

    const ScaleDef* findScale(const std::string& n) const {
        for(auto& d : scaleLib) if(d.name == n) return &d;
        return scaleLib.empty() ? nullptr : &scaleLib[0];
    }
    const PaletteDef* findPalette(const std::string& n) const {
        for(auto& d : paletteLib) if(d.name == n) return &d;
        return paletteLib.empty() ? nullptr : &paletteLib[0];
    }
    std::string scaleNameForLegacy(int i) const {
        for(auto& d : scaleLib) if(d.legacyIndex == i) return d.name;
        return "#" + std::to_string(i);
    }
    std::string paletteNameForLegacy(int i) const {
        for(auto& d : paletteLib) if(d.legacyIndex == i) return d.name;
        return "#" + std::to_string(i);
    }

    // --- scene and setlist editing

    float imageOpacity(const Image& im) const {
        auto it = scenes.find(im.scene);
        int layer = std::max(0, im.layer - 1);
        if(it != scenes.end() && layer < (int)it->second.layers.size()) return it->second.layers[layer].gain;
        return im.gain;
    }

    // The player owns additional images (including the scroller's loading slot).
    std::function<void(const std::string&, const std::string&)> sceneRenamed;

    bool renameScene(std::string from, std::string to){
        if(to.empty() || scenes.count(to) || !scenes.count(from)) return false;
        Scene s = scenes[from];
        auto oldPath = scenePath(s);
        s.name = to;
        saveScene(s);
        if(oldPath != scenePath(s)) fs::remove(oldPath);
        scenes.erase(from);
        scenes[to] = s;
        for(auto& e : setlist) if(e.scene == from) e.scene = to;
        if(manualScene == from) manualScene = to;
        for(auto& pe : layer1Plan) if(pe.scene == from) pe.scene = to;
        auto renameImage = [&](const std::shared_ptr<Image>& im){ if(im && im->scene == from) im->scene = to; };
        for(auto& [id, im] : pending) renameImage(im);
        for(auto& L : schedule->layers){
            renameImage(L.header); renameImage(L.replaceNext);
            for(auto& im : L.ready) renameImage(im);
            for(auto& sl : L.slots) renameImage(sl.img);
        }
        renameImage(lastImage); renameImage(replayFirst);
        for(auto& im : replayAll1) renameImage(im);
        for(auto& im : replay1) renameImage(im);
        for(auto& [l, q] : replayAllX) for(auto& im : q) renameImage(im);
        for(auto& [l, q] : replayX) for(auto& im : q) renameImage(im);
        if(previewSeedScene == from) previewSeedScene = to;
        if(sceneRenamed) sceneRenamed(from, to);
        saveSetlist();
        return true;
    }

    // Save As: the scene as it is now (unsaved edits included) becomes a new scene `to` in the same
    // bank; `from` goes back to its saved file (never saved: it keeps the edits). Empty = done.
    std::string saveSceneAs(const std::string& from, const std::string& to){
        if(!scenes.count(from)) return "the scene is gone";
        if(to.empty()) return "type a name";
        if(scenes.count(to)) return "a scene called '" + to + "' already exists";
        Scene s = scenes[from];
        s.name = to; s.snapshot = -1;
        if(fs::exists(scenePath(s))) return "the file " + scenePath(s).filename().string() + " already exists";
        scenes[to] = s;
        saveScene(s);
        if(fs::exists(scenePath(scenes[from]))) reloadScene(from);
        note("saved '" + from + "' as '" + to + "'");
        return "";
    }

    std::string duplicateScene(const std::string& from){
        if(!scenes.count(from)) return "";
        std::string to = from + " copy";
        for(int i = 2; scenes.count(to); i++) to = from + " copy " + std::to_string(i);
        Scene s = scenes[from];
        s.name = to;
        s.snapshot = -1;
        s.bank = currentBank;   // a copy is a new scene: the bank new scenes go to
        scenes[to] = s;
        saveScene(s);
        return to;
    }

    // Back to the saved file (undo unsaved edits)
    void reloadScene(const std::string& name){
        try {
            std::string bank = scenes.count(name) ? scenes[name].bank : std::string();
            Scene probe; probe.name = name; probe.bank = bank;
            Scene s = readJsonFile(scenePath(probe)).get<Scene>();
            s.name = name; s.bank = bank;
            prepareKick(s);
            scenes[name] = s;
        } catch(std::exception& e){ note("revert " + name + ": " + e.what()); }
    }

    // Library entries renamed: scenes that use them follow
    void renameLibraryRefs(bool isScale, const std::string& from, const std::string& to){
        for(auto& [n, sc] : scenes){
            std::string& ref = isScale ? sc.scale : sc.palette;
            if(ref == from){ ref = to; saveScene(sc); }
        }
    }

    void deleteScene(const std::string& name){
        auto it = scenes.find(name);
        if(it != scenes.end()) fs::remove(scenePath(it->second));
        scenes.erase(name);
    }

    void saveSetlist(){
        writeJsonAtomic(setlistPath(), json{{"name", "current"}, {"entries", setlist}});
    }

    int scenesUsing(const std::string& method) const {
        int n = 0;
        for(auto& [name, sc] : scenes){
            for(auto& l : sc.layers){
                bool used = l.effectPreset==method || std::find(sc.globalEffects.begin(), sc.globalEffects.end(), method) != sc.globalEffects.end();
                for(auto& c : l.cells) if(c.item == method && c.prob > 0) used = true;
                if(used){ n++; break; }
            }
        }
        return n;
    }

    // Import the 30 Director snapshots and the 1920 setlist from a 2--ESPECTRES_1920 preset folder
    int importPreset(const fs::path& presetFolder, const std::string& directorFile = "FORMS_P5_PNG_Director_1.json"){
        json node = readJsonFile(presetFolder / directorFile);
        auto bySlot = importDirector(node);
        for(auto& [slot, s] : bySlot) saveScene(s);
        auto csv = [&](const char* f){
            std::vector<float> v;
            try {
                json j = readJsonFile(presetFolder / f);
                std::stringstream ss(j.value("CSV_Input", std::string()));
                std::string tok;
                while(std::getline(ss, tok, ',')) if(!tok.empty()) v.push_back(std::stof(tok));
            } catch(...) {}
            return v;
        };
        auto slotsF = csv("CSV_to_Vector_10.json");
        auto durs = csv("CSV_to_Vector_11.json");
        std::vector<int> slots(slotsF.begin(), slotsF.end());
        auto entries = importSetlist(slots, durs, bySlot);
        writeJsonAtomic(setlistPath(), json{{"name", "espectres_1920"}, {"entries", entries}});
        loadAll();
        importPerformance(presetFolder);
        note("imported " + std::to_string(bySlot.size()) + " scenes and a " + std::to_string(entries.size()) + "-entry setlist");
        return (int)bySlot.size();
    }

    // --- rendering

    // The scene the next image will use (does not advance)
    const Scene* currentScene() const {
        std::string name = manualScene;
        if(useSetlist && !setlist.empty()) name = setlist[setlistEntry % setlist.size()].scene;
        auto it = scenes.find(name);
        return it != scenes.end() ? &it->second : nullptr;
    }

    void advanceSetlist(){
        if(!useSetlist || setlist.empty()) return;
        imagesInEntry++;
        if(imagesInEntry >= setlist[setlistEntry % setlist.size()].images){
            imagesInEntry = 0;
            setlistEntry = (setlistEntry + 1) % (int)setlist.size();
        }
    }

    // Requests nobody answered (renderer stopped, packet lost) stop blocking after a while
    void expirePending(){
        auto now = std::chrono::steady_clock::now();
        for(auto it = pendingSince.begin(); it != pendingSince.end();){
            if(std::chrono::duration<double>(now - it->second).count() > pendingTimeout){
                note("no answer for " + it->first + ", given up");
                forgetReplace(it->first);
                pending.erase(it->first);
                it = pendingSince.erase(it);
                lost++;
            }else ++it;
        }
    }

    // Builds and writes the next request. Returns its path ("" if there is no scene, or if the
    // renderer is still busy with maxPending requests: like 1920, where a /go during a render
    // was merged away, the image is skipped instead of queued, so what scrolls in stays current).
    // sceneOverride: render this scene now (editor preview) without moving the setlist.
    // previewItem: render only this preset (editor preview, its unsaved values); the image is shown
    // in the editor and never scrolls in.
    // layerIndex: 0 = layer 1 (the clock's timing); 1..3 = the extra layers (Phase 3), placed in
    // absolute beats at startBar (from requestLayer).
    std::string makeRequest(bool force = false, const std::string& sceneOverride = "", const Preset* previewItem = nullptr, int layerIndex = 0, double startBar = -1, const Scene* sceneFor = nullptr, double cutBar = -1){
        StageTimer requestTimer(pipelineStats().request);
        if(session.empty()) newSession();
        if(layerIndex == 0) lastRequestRepeat = 1;
        if(replaying && !previewItem && sceneOverride.empty() && layerIndex == 0){ replayNext(); return ""; }
        expirePending();
        if(!force){
            if((int)pending.size() >= maxPending){
                skipped++;
                return "";
            }
        }
        const Scene* s = currentScene();
        if(!sceneOverride.empty()){
            auto it = scenes.find(sceneOverride);
            s = it != scenes.end() ? &it->second : nullptr;
        }
        // the scene lane, where it has a block at the bar this image starts
        bool laneChose = false, laneCut = false;
        if(layerIndex == 0 && sceneOverride.empty() && !previewItem && !sceneFor && laneSceneAt && !replaying){
            std::string n = laneSceneAt(barIndex(layer1NextBar), laneCut);
            auto it = scenes.find(n);
            if(!n.empty() && it != scenes.end() && !it->second.layers.empty()){ s = &it->second; laneChose = true; }
        }
        if(!s || s->layers.empty()){
            note("no scene to render");
            return "";
        }
        if(sceneFor) s = sceneFor;
        if(layerIndex >= (int)s->layers.size()){ note("scene " + s->name + " has no layer " + std::to_string(layerIndex + 1)); return ""; }
        const LayerDef& l = s->layers[layerIndex];
        auto img = std::make_shared<Image>();
        char idbuf[16];
        snprintf(idbuf, sizeof(idbuf), "%04d", counter++);
        img->id = idbuf;
        img->scene = s->name;
        img->layer = layerIndex + 1;
        img->askedAt = now;
        img->blend = l.blend;
        img->gain = l.gain;
        img->kick = l.kick;
        img->bars = layerBars(l);
        img->repeat = (previewItem || !sceneOverride.empty()) ? 1 : std::max(1, l.repeat);
        // "Next image now" with Switch now: the image that replaces the one playing stands for one
        // more page, so after the rest of the current page it plays once in full (switchNow keepPages)
        if(layerIndex == 0 && extraRepeatNext > 0 && !previewItem && sceneOverride.empty()){ img->repeat += extraRepeatNext; extraRepeatNext = 0; }
        if(layerIndex > 0 && extraRepeatLayer > 0 && !previewItem && sceneOverride.empty()) img->repeat += extraRepeatLayer;   // switchNow: a phrase entered near its end
        // a phrase: one render for `slices` pages (editor renders draw the page alone, as before)
        img->slices = (previewItem || !sceneOverride.empty()) ? 1 : phraseSlices(l);
        double spanBars = layerBars(l) * img->slices;
        if(img->slices > 1 && layerIndex > 0){
            // layers 2 and up: exactly the phrase; a last page that is not whole is cut at its end
            spanBars = layerSpan(l, true);
            img->slices = std::max(1, (int)std::ceil(spanBars / layerBars(l) - 1e-9));
            if(spanBars < layerBars(l) * img->slices - 1e-9) img->phraseBars = spanBars;
        }
        const double span = spanBars;   // the bars this render covers
        img->bpm = s->bpm;
        img->notegrid = s->notegrid;
        img->palette = s->palette;
        const ScaleDef es = effectiveScale(*s);
        const ScaleDef* sd = es.name.empty() ? nullptr : &es;
        img->scale = sd ? sd->name : s->scale;
        img->root = effectiveRoot(*s);
        img->snapshot = s->snapshot;
        const bool frozen = l.fixed.is_object() && l.fixed.contains("items") && !previewItem;
        img->seed = frozen ? l.fixed.value("seed", 0u) : (rng() & 0x7fffffff);
        const PaletteDef* pd = findPalette(s->palette);
        if(sd) img->scaleNotes.assign(sd->notes.begin(), sd->notes.end());

        img->perf = completePerformance(s->performance);
        img->layerCover = l.cover;
        img->preview = previewItem != nullptr;
        if(previewItem) img->previewOf = previewItem->ref();
        std::vector<std::string> items;
        std::vector<float> itemGains;   // the cells' volume, per selected item
        const float dens = previewItem ? 0 : density;   // a previewed instrument always plays
        if(!frozen) items = selectItems(l, rng, &itemGains, dens);
        Harmony h;
        bool live = !previewItem && sceneOverride.empty();
        const int nb = barCount(span);   // harmony entries: one per bar it touches (the renderer's ceil(bars))
        if(frozen){
            // its own harmony; on the timeline too, so the other layers play along with it
            h.roots = l.fixed.value("roots", std::vector<int>(nb, 0));
            h.trans = l.fixed.value("trans", std::vector<int>(nb, 0));
            // longer than it was frozen (Bar Multiplier): its harmony goes round again
            auto fit = [nb](std::vector<int>& v){ const size_t n = v.size(); v.resize(nb, 0); if(n > 0) for(size_t k = n; k < v.size(); k++) v[k] = v[k % n]; };
            fit(h.roots); fit(h.trans);
        }
        if(!live){ if(!frozen) h = makeHarmony(*s, nb, rng); }   // editor renders: their own harmony, as before
        else{
            double bar = layerIndex == 0 ? layer1NextBar : startBar;
            if(frozen){
                auto pred = layerIndex == 0 ? std::vector<PlanEntry>() : predictedPlan(barIndex(bar + span) + 1);
                for(int b = 0; b < nb; b++) harmonyBars[layerIndex == 0 ? barIndex(bar + b) : harmonyBar(bar + b, &pred)] = {h.roots[b], h.trans[b]};
            }
            else h = harmonyAt(*s, bar, nb, layerIndex != 0);
            img->startBar = bar;
            if(layerIndex == 0){
                // the plan: a scene change here is a hard cut if the setlist entry (or, without
                // the setlist, the manual setting) says so
                PlanEntry pe; pe.startBar = bar; pe.bars = span * img->repeat; pe.period = span; pe.scene = s->name; pe.id = img->id;
                bool changes = layer1Plan.empty() || layer1Plan.back().scene != s->name;
                if(changes) pe.hardCut = laneChose ? laneCut : (useSetlist && !setlist.empty()) ? setlist[setlistEntry % setlist.size()].hardCut : hardCutManual;
                layer1Plan.push_back(pe);
                if(changes && layer1Plan.size() > 1) onSceneChange(pe.startBar, pe.scene, pe.hardCut);
                while(layer1Plan.size() > 64) layer1Plan.pop_front();
                layer1NextBar += span * img->repeat;
                lastRequestRepeat = img->repeat * img->slices;   // the GOs it stands for: every page
                if(img->slices > 1){
                    // a phrase's pages are short: the next phrase is asked for that many pages
                    // before this one ends, so it is rendered in time (else the last page repeats)
                    double pageSeconds = layerBars(l) * 4 * 60.0 / std::max(1.0, s->bpm);
                    int early = (int)std::ceil(1.5 * renderSeconds / std::max(0.05, pageSeconds)) + 1;
                    lastRequestRepeat = std::max(1, lastRequestRepeat - early);
                }
            }else{
                img->startBeat = bar * 4.0;
                if(cutBar > bar){ img->cutBeat = cutBar * 4.0; layerNextBar[layerIndex] = cutBar; }
                else layerNextBar[layerIndex] = bar + span * img->repeat;
            }
        }
        applyPitchShift(h.roots);
        img->roots = h.roots;
        img->trans = h.trans;

        json req{{"id", img->id}, {"layer", img->layer}, {"session", session}, {"seed", img->seed},
                 {"size", {width, height}}, {"scene", s->name}, {"snapshot", s->snapshot},
                 {"bars", barsJson(span)}, {"notegrid", s->notegrid}, {"bpm", barsJson(s->bpm)},
                 {"roots", h.roots}, {"trans", h.trans}, {"compat", s->compat}, {"bassDegree", s->bassDegree}, {"lineWeight", s->lineWeight}};
            if(s->hueDeviation > 0) req["hueDeviation"] = s->hueDeviation;
            if(s->satDeviation > 0) req["satDeviation"] = s->satDeviation;
            if(s->lightDeviation > 0) req["lightDeviation"] = s->lightDeviation;
        if(img->slices > 1){
            // the phrase is drawn `slices` views wide (at most 16384 px: past that the pages are
            // stretched); pixel sizes follow the view (viewWidth), not the canvas
            req["size"] = {std::min(16384, img->phraseBars > 0 ? (int)std::lround(width * span / layerBars(l)) : width * img->slices), height};
            req["viewWidth"] = width;
            req["pageBars"] = barsJson(layerBars(l));
            req["slices"] = img->slices;
        }
        if(barAdd != 0) req["barAdd"] = barAdd;
        if(dens != 0 && !frozen) req["chanceDensity"] = dens;   // for the record: the cells' chances were bent by this
        if(barMult != 1) req["barMult"] = barMult;   // "bars" / "pageBars" are the multiplied lengths (promote / freeze divide)
        // the scale and palette themselves travel with the request
        req["scale"] = sd ? sd->legacyIndex : -1;
        req["palette"] = pd ? pd->legacyIndex : -1;
        if(sd){ req["scaleName"] = sd->name; req["scaleNotes"] = sd->notes; }
        if(pd){ req["paletteName"] = pd->name; req["paletteColors"] = pd->colors; }
        req["performance"] = img->perf;
        req["layerCover"] = l.cover;   // archived in the sidecar
        if(!l.blocking) req["blocking"] = false;   // the layer's instruments draw no black background
        // presets resolve after the selection and the harmony, so scenes without presets draw the
        // same random numbers as before
        json jitems = json::array();
        itemBars = span;
        OwnScope ownScope(*this, previewItem ? nullptr : s);   // the scene's own versions of its instruments (a library preview: the shared one)
        auto genItem = [&](const Preset& pr){
            for(auto& it : presetItems(pr)) jitems.push_back(it);
        };
        if(previewItem) genItem(*previewItem);
        else if(frozen) jitems = l.fixed["items"];
        else for(size_t ii = 0; ii < items.size(); ii++){
            auto& it = items[ii];
            if(!isPresetRef(it)){ jitems.push_back({{"method", it}}); continue; }
            const Preset* pi = presetFor(it);
            if(!pi){ note("preset " + it + " not found"); continue; }
            size_t first = jitems.size();
            genItem(*pi);
            float g = ii < itemGains.size() ? itemGains[ii] : 1.f;
            if(g != 1) for(size_t k = first; k < jitems.size(); k++) jitems[k]["gain"] = g;   // the cell's volume (renderer)
        }
        req["items"] = jitems;
        applyLayerEffect(req,l,img->seed);
        applyGlobalEffects(req, *s, img->startBar >= 0 ? img->startBar : 0.0, span);

        fs::path p = sessionDir() / "requests" / (img->id + ".json");
        if(requestSink) requestSink(p,std::move(req)); else writeJsonAtomic(p,req);
        pending[img->id] = img;
        pendingSince[img->id] = std::chrono::steady_clock::now();
        if(sceneOverride.empty() && !previewItem && layerIndex == 0 && !laneChose) advanceSetlist();   // the lane's images don't count
        lastImage = img;
        note((previewItem ? "preview " + previewItem->ref() + " " : "request ") + img->id + " " + s->name + " (" + std::to_string(jitems.size()) + " items)");
        return p.string();
    }

    // A rendered request as the pages it stands for: `repeat` copies of the image one after the
    // other (same PNG and harmony; startBar, and startBeat for layers 2..4, moved by the image's
    // length each). Copies past a hard cut or a scene change (copiesUntil) are dropped; the one a
    // cut falls in is cut there.
    static std::vector<std::shared_ptr<Image>> expandRepeats(const std::shared_ptr<Image>& img){
        std::vector<std::shared_ptr<Image>> out;
        const int slices = std::max(1, img->slices);
        int n = std::max(1, img->repeat) * slices;   // a phrase: every page of it, then its repeats
        double len = 4.0 * std::max(1.0 / kBarGrid, img->bars);
        double cut = img->cutBeat, until = img->copiesUntil;
        const std::vector<int> roots = img->roots, trans = img->trans;   // the phrase's (per bar)
        for(int k = 0; k < n; k++){
            auto c = k == 0 ? img : std::make_shared<Image>(*img);
            c->repeat = 1; c->repeatIndex = k; c->copiesUntil = -1;
            if(slices > 1){
                // its window of the phrase, and the phrase's chords at its bars
                c->slice = k % slices;
                double from = c->slice * img->bars;
                int nb = barCount(img->bars);
                c->roots.assign(nb, 0); c->trans.assign(nb, 0);
                for(int j = 0; j < nb; j++){
                    int b = barIndex(from + j);
                    if(b < (int)roots.size()) c->roots[j] = roots[b];
                    if(b < (int)trans.size()) c->trans[j] = trans[b];
                }
            }
            const double off = img->phraseBars > 0 ? img->pageOffsetBeats(k) : k * len;   // (whole pages: as before)
            if(img->startBar >= 0) c->startBar = img->phraseBars > 0 ? img->startBar + off / 4.0 : img->startBar + k * img->bars;
            if(img->startBeat >= 0){
                double st = img->startBeat + off;
                if(k > 0 && ((cut >= 0 && st >= cut - 1e-6) || (until >= 0 && st >= until - 1e-6))) break;
                c->startBeat = st;
                c->cutBeat = (cut > st + 1e-6 && cut < st + len - 1e-6) ? cut : -1;
            }else if(k > 0 && until >= 0 && c->startBar * 4.0 >= until - 1e-6) break;
            out.push_back(c);
        }
        return out;
    }

    // Ready with its pixels in memory (Forms GL Box): the scrollers use them, the files follow
    void onReady(const std::string& id, const std::string& png, const std::string& jsonPath,
                 std::shared_ptr<const std::vector<uint16_t>> rgba, int pw, int ph){
        auto it = pending.find(id);
        if(it != pending.end()){ it->second->rgba = std::move(rgba); it->second->pw = pw; it->second->ph = ph; }
        onReady(id, png, jsonPath);
    }
    void onReady(const std::string& id, const std::string& png, const std::string& jsonPath){
        auto it = pending.find(id);
        if(it == pending.end()){ note("ready for unknown id " + id); return; }
        if(it->second->askedAt >= 0 && now > it->second->askedAt && !it->second->preview)
            pipelineStats().total.add((now - it->second->askedAt) * 1000.0);
        pendingSince.erase(id);
        // how long renders take (the extra layers ask that much ahead)
        if(it->second->askedAt >= 0 && now > it->second->askedAt && !it->second->preview)
            renderSeconds = 0.8 * renderSeconds + 0.2 * std::min(60.0, now - it->second->askedAt);
        it->second->png = png;
        it->second->json = jsonPath;
        if(it->second->preview){
            previewPng = png;
            previewId = id;
            previewRef = it->second->previewOf;
            previewCount++;
            previewsDone.push_back({it->second->previewOf, png});
            while(previewsDone.size() > 8) previewsDone.pop_front();
            pending.erase(it);
            note("preview ready " + id);
            return;
        }
        int layer = it->second->layer;
        if(it->second->cancelled){ pending.erase(it); note("ready " + id + " (cancelled by a scene change)"); return; }
        auto& Lq = schedule->layer(std::max(0, layer - 1));
        for(auto& c : expandRepeats(it->second)) Lq.ready.push_back(c);
        pending.erase(it);
        note("ready " + id + (layer > 1 ? " (layer " + std::to_string(layer) + ")" : ""));
    }

    // --- Gallery

    // A larger copy of an archived image (its sidecar), for print: the same recipe at k x the size,
    // in sessions/prints/. Returns the request path ("" on failure); it comes back as a preview.
    std::string makePrint(const json& side, int k){
        if(session.empty()) newSession();
        json req = side;
        req.erase("result");
        auto sz = side.value("size", std::vector<int>{width, height});
        if(sz.size() < 2) return "";
        char idbuf[32];
        snprintf(idbuf, sizeof(idbuf), "%sx%d_%04d", side.value("id", std::string("img")).c_str(), k, counter++);
        req["id"] = idbuf;
        req["session"] = "prints";
        req["size"] = {sz[0] * k, sz[1] * k};
        req["refSize"] = {sz[0], sz[1]};   // size-dependent sampling follows the original image
        fs::path p = sessionDir() / "requests" / (std::string(idbuf) + ".json");
        writeJsonAtomic(p, req);
        auto img = std::make_shared<Image>();
        img->id = idbuf; img->preview = true; img->previewOf = "print x" + std::to_string(k);
        img->layer = side.value("layer", 1); img->scene = side.value("scene", std::string());
        pending[img->id] = img;
        pendingSince[img->id] = std::chrono::steady_clock::now();
        note("print request " + std::string(idbuf) + " (" + std::to_string(sz[0] * k) + " x " + std::to_string(sz[1] * k) + ")");
        return p.string();
    }

    // A new scene from an archived image. freeze: one frozen layer that always draws exactly this
    // image; else: a normal scene with the image's instruments (each at 100%) to vary from.
    std::string sceneFromImage(const json& side, bool freeze){
        auto src = scenes.find(side.value("scene", std::string()));
        Scene s = src != scenes.end() ? src->second : Scene();
        std::string base = side.value("scene", std::string("image")) + (freeze ? " frozen " : " from ") + side.value("id", std::string());
        std::string name = base;
        for(int i = 2; scenes.count(name); i++) name = base + " " + std::to_string(i);
        s.name = name;
        s.snapshot = -1;
        s.bank = currentBank;   // a new scene: the bank new scenes go to
        int li = std::max(0, side.value("layer", 1) - 1);
        LayerDef l = (src != scenes.end() && li < (int)src->second.layers.size()) ? src->second.layers[li] : LayerDef();
        // a phrase's render: its page length and the phrase ("bars" is what was drawn)
        if(side.contains("pageBars")){ l.bars = barsFrom(side, "pageBars", l.bars); l.phrase = barsFrom(side, "bars", l.bars); }
        else{ l.bars = barsFrom(side, "bars", l.bars); l.phrase = 0; }
        // drawn with a Bar Multiplier: the scene keeps the length as written (the multiplier acts again when it plays)
        if(double m = side.value("barMult", 1.0), a = side.value("barAdd", 0); m > 0 && (m != 1 || a != 0)){
            const int n = phraseSlices(l);
            const bool wholePages = l.phrase >= l.bars * n - 1e-9;   // else: a phrase that ends inside its last page (Engine::layerSpan)
            const double drawn = l.phrase;
            if(l.bars - a > 0 && (a > 0 || l.bars > 1)) l.bars = quantBars((l.bars - a) / m); else l.bars = quantBars(l.bars / m);   // (a length held at the one-bar floor: the add is not known)
            if(l.phrase > 0) l.phrase = wholePages ? quantBars(l.bars * n) : quantBars(std::max(l.bars, (drawn - a > 0 ? drawn - a : drawn) / m));
        }
        l.z = -1;
        l.cells.clear();
        l.effectPreset=side.value("layerEffectPreset",std::string());
        if(freeze){
            l.fixed = json{{"seed", side.value("seed", 0u)}, {"items", side.value("items", json::array())},
                           {"roots", side.value("roots", json::array())}, {"trans", side.value("trans", json::array())},
                           {"from", side.value("session", std::string()) + "/" + side.value("id", std::string())}};
            l.fixed["layerEffectPreset"]=l.effectPreset;
            l.globalProb = 100;
        }else{
            l.fixed = json();
            std::set<std::string> seen;
            for(auto& it : side.value("items", json::array())){
                if(it.value("layerEffect",false))continue;
                std::string ref = it.value("preset", it.value("method", std::string()));
                if(ref.empty() || seen.count(ref)) continue;
                seen.insert(ref);
                Cell c; c.item = ref; c.prob = 100; c.group = 0;
                l.cells.push_back(c);
            }
            l.rows = std::max(1, (int)l.cells.size());
            l.globalProb = 100;
            l.groupProb.assign(7, 1.0f);
        }
        s.layers = {l};
        scenes[name] = s;
        saveScene(scenes[name]);
        note((freeze ? "froze " : "promoted ") + side.value("id", std::string()) + " as scene " + name);
        return name;
    }

    // The harmony of `bars` bars from absolute bar `start`: bars already on the timeline are
    // reused; if none are, it is made as for one image (makeHarmony) and stored.
    // Where layer 1 repeats a page, its bars repeat that page's harmony: they are read and stored at
    // the bar of the page's first copy, so every layer plays the same chords on every repeat.
    // b: a position in absolute bars (an image's start + k); returns the whole bar its chord is kept at
    int harmonyBar(double b, const std::vector<PlanEntry>* predicted = nullptr) const {
        auto fold = [&](const PlanEntry& pe){ return barIndex(pe.startBar + std::fmod(b - pe.startBar + 1e-9, pe.period) - 1e-9); };
        auto in = [&](const PlanEntry& pe){ return pe.startBar <= b + 1e-9 && b + 1e-9 < pe.startBar + pe.bars; };
        for(auto& pe : layer1Plan) if(in(pe)) return (pe.period > 0 && pe.period < pe.bars) ? fold(pe) : barIndex(b);
        if(predicted) for(auto& pe : *predicted) if(in(pe)) return (pe.period > 0 && pe.period < pe.bars) ? fold(pe) : barIndex(b);
        return barIndex(b);
    }
    // folded: the extra layers fold onto layer 1's repeated pages (planned or predicted); layer 1's
    // own new page does not (it starts where the plan ends)
    // start: absolute bars (may be a fraction); bars: harmony entries (barCount of the image)
    Harmony harmonyAt(const Scene& s, double start, int bars, bool folded = true){
        auto predicted = folded ? predictedPlan(barIndex(start) + bars + 1) : std::vector<PlanEntry>();
        std::vector<int> hb(bars);
        for(int i = 0; i < bars; i++) hb[i] = folded ? harmonyBar(start + i, &predicted) : barIndex(start + i);
        bool any = false;
        for(int i = 0; i < bars; i++) if(harmonyBars.count(hb[i])){ any = true; break; }
        Harmony h;
        if(!any){
            h = makeHarmony(s, bars, rng);
            for(int i = 0; i < bars; i++) if(!harmonyBars.count(hb[i])) harmonyBars[hb[i]] = {h.roots[i], h.trans[i]};
            // a bar folded onto one set just above (a page shorter than this image repeats): read it back
            for(int i = 0; i < bars; i++){ auto& v = harmonyBars[hb[i]]; h.roots[i] = v.first; h.trans[i] = v.second; }
        }else{
            Harmony fill;
            bool made = false;
            h.roots.assign(bars, 0); h.trans.assign(bars, 0);
            for(int i = 0; i < bars; i++){
                auto it = harmonyBars.find(hb[i]);
                if(it == harmonyBars.end()){
                    if(!made){ fill = makeHarmony(s, bars, rng); made = true; }
                    harmonyBars[hb[i]] = {fill.roots[i], fill.trans[i]};
                    it = harmonyBars.find(hb[i]);
                }
                h.roots[i] = it->second.first;
                h.trans[i] = it->second.second;
            }
        }
        // forget bars far behind the header
        int keepFrom = (int)std::floor(schedule->headerBeat / 4.0) - 256;
        if(!layer1Plan.empty()) keepFrom = std::min(keepFrom, barIndex(layer1Plan.front().startBar));   // a long repeat folds back to its first page
        while(!harmonyBars.empty() && harmonyBars.begin()->first < keepFrom) harmonyBars.erase(harmonyBars.begin());
        return h;
    }

    // Phase 3: keeps the extra layers of the current scene rendered ahead. Called every frame by
    // the player. Layer 1 has priority: nothing is asked while a layer-1 request is pending (the
    // renderer is one queue, and a late layer-1 image repeats on stage).
    // Returns the request's path ("" if none): the caller sends it to the renderer.
    // Two: the next image starts showing at the right edge long before the header reaches it.
    int aheadImages = 2;               // per extra layer: at most this many images beyond the one under the header, ...
    // ... and each asked for as soon as its deadline (the beat its left edge can first enter the
    // view: its start minus its length) is within two render times + 2 bars
    double now = 0;                    // seconds (set by the node every frame)
    double renderSeconds = 3;          // measured render time (smoothed)
    // How busy the renderers are with the scene at the header: the new images its layers need per
    // second (bpm / 60 / (4 x bars x repeat) each) over what the renderer makes (1 /
    // render time). Above about 1 they cannot keep up: after a hard cut an extra layer's new image
    // comes late and the layer is empty until it does (Repeat fixes it). 0: unknown.
    double renderLoad() const {
        const Scene* s = sceneAtHeader();
        if(!s) s = currentScene();
        if(!s || s->layers.empty() || renderSeconds <= 0) return 0;
        double perSec = 0;
        for(auto& l : s->layers) perSec += std::max(1.0, s->bpm) / 60.0 / (4.0 * std::max(1.0 / kBarGrid, layerSpan(l, &l != &s->layers[0])) * std::max(1, l.repeat));
        return perSec * renderSeconds;
    }
    double leadBeats() const {
        double bpm = 120;
        if(schedule && !schedule->layers.empty() && schedule->layers[0].header) bpm = std::max(1.0, schedule->layers[0].header->bpm);
        return 2 * renderSeconds * bpm / 60.0 + 8;
    }
    std::string requestLayers(){
        if(replaying){ replayLayers(); return ""; }
        if(session.empty() || layer1Plan.empty()) return "";
        bool anyLayers = currentScene() && currentScene()->layers.size() > 1;
        for(auto& pe : layer1Plan){ auto it = scenes.find(pe.scene); if(it != scenes.end() && it->second.layers.size() > 1){ anyLayers = true; break; } }
        if(!anyLayers) return "";
        for(auto& [id, img] : pending) if(img->layer == 1 && !img->preview && !img->cancelled) return "";
        // keep one place in the renderer queue for layer 1's next GO (a skipped GO repeats on stage)
        if((int)pending.size() >= maxPending - 1) return "";
        double B = schedule->headerBeat;
        // layer 1's plan, then what it will play next (predicted from the setlist)
        auto predicted = predictedPlan(std::ceil(B / 4.0) + renderAheadBars + 96);
        auto planOrPredicted = [&](double b) -> const PlanEntry* {
            if(const PlanEntry* pe = planAt(b)) if(b + 1e-9 < pe->startBar + pe->bars) return pe;
            for(auto& pe : predicted) if(pe.startBar <= b + 1e-9 && b + 1e-9 < pe.startBar + pe.bars) return &pe;
            return planAt(b);
        };
        // the layer whose next image is due soonest goes first; one request per frame
        int best = -1; double bestBar = 0, bestCut = -1, bestAsked = 0; bool bestWaiting = false;
        double bestDeadline = 0;
        const Scene* bestScene = nullptr;
        size_t layerCount=schedule->layers.size();
        for(auto& [name,scene]:scenes) layerCount=std::max(layerCount,scene.layers.size());
        for(int i = 1; i < (int)layerCount; i++){
            auto& L = schedule->layer(i);
            int waiting = 0;
            for(auto& [id, img] : pending) if(img->layer == i + 1 && !img->cancelled) waiting++;
            // (a phrase's pages count as one: they come from one request)
            int future = waiting;
            std::set<std::string> phrases;
            for(auto& im : L.ready){ if(im->slices <= 1) future++; else phrases.insert(im->id); }
            for(auto& sl : L.slots) if(!sl.repeat && sl.start + sl.len > B){ if(sl.img->slices <= 1) future++; else phrases.insert(sl.img->id); }
            future += (int)phrases.size();
            if(future > aheadImages + 2) continue;   // a cap, whatever the deadlines
            // The next start follows what is placed or asked for. A late restart
            // moves ahead by complete cycles from the scene origin.
            double earliest = std::ceil(B / 4.0) + renderAheadBars;
            // A newly enabled layer shares layer 1's scene origin. The ring may defer a
            // late page by whole cycles, but must not invent a two-bar phase offset.
            const PlanEntry* origin = planOrPredicted(B / 4.0);
            double originBar = origin ? origin->startBar : earliest;
            std::string runScene;
            if(origin) for(auto& pe : layer1Plan){
                if(pe.startBar > origin->startBar) break;
                if(pe.scene != runScene || pe.hardCut) originBar = pe.startBar;
                runScene = pe.scene;
            }
            double next = layerNextBar.count(i) ? layerNextBar[i] : originBar;
            if(L.scheduledEnd >= 0){
                // after what is placed: on the next bar line (pages of a fractional length: the 1/16-bar grid)
                double unit = 1;
                if(!L.slots.empty() && L.slots.back().img && L.slots.back().img->bars != std::floor(L.slots.back().img->bars)) unit = 1.0 / kBarGrid;
                next = std::max(next, std::ceil(L.scheduledEnd / 4.0 / unit - 1e-6) * unit);
            }
            if(next * 4.0 + 4.0 <= B){
                next = earliest;
                auto it = origin ? scenes.find(origin->scene) : scenes.end();
                if(it != scenes.end() && i < (int)it->second.layers.size()){
                    double cycle = std::max(1.0 / kBarGrid, layerSpan(it->second.layers[i], true));
                    next = originBar + std::ceil((earliest - originBar) / cycle - 1e-9) * cycle;
                }
            }
            // the scene at that bar is layer 1's (its plan); a layer the scene doesn't have is
            // silent until a planned scene has it again
            const Scene* s = nullptr;
            for(int guard = 0; guard < 64; guard++){
                const PlanEntry* pe = planOrPredicted(next);
                if(!pe){
                    // past the prediction too: assume the scene layer 1 asks for next (a different
                    // choice later is corrected by onSceneChange)
                    const Scene* cs = currentScene();
                    if(cs && i < (int)cs->layers.size()) s = cs;
                    break;
                }
                auto it = scenes.find(pe->scene);
                if(it != scenes.end() && i < (int)it->second.layers.size()){ s = &it->second; break; }
                next = pe->startBar + pe->bars;                  // skip to the next planned image
            }
            if(!s) continue;
            // on the layer's own grid (layerGrid): a start inherited from pages of another length, a
            // scene that began inside a bar, or a cut there, waits for its next line
            { double g = layerGrid(layerSpan(s->layers[i], true)); next = std::ceil(next / g - 1e-6) * g; }
            double bars = layerBars(s->layers[i]);
            double span = layerSpan(s->layers[i], true) * std::max(1, s->layers[i].repeat);   // the page (or phrase) and its repeats
            // not due yet: its deadline is further than the lead (one image ahead is always kept)
            // (its place enters the view at the rate of the image before it: after a longer one,
            // e.g. a scene change to shorter layers, that is more than its own length ahead)
            double viewBars = bars;
            if(!L.slots.empty() && L.slots.back().end() <= next * 4.0 + 1e-6) viewBars = std::max(viewBars, L.slots.back().len / 4.0);
            double deadline = next * 4.0 - viewBars * 4.0;
            if(future >= 1 && deadline - B > leadBeats()) continue;
            if(future > aheadImages && deadline - B > 0.5 * leadBeats()) continue;
            // a hard cut inside this image: it stops there and the next one starts at the cut
            double cut = -1;
            for(auto& pe : layer1Plan)
                if(pe.hardCut && pe.startBar > next && pe.startBar < next + span){ cut = pe.startBar; break; }
            if(cut < 0) for(auto& pe : predicted)
                if(pe.hardCut && pe.startBar > next && pe.startBar < next + span){ cut = pe.startBar; break; }
            // A layer with nothing coming (no image ready, asked for or placed ahead) goes before one
            // that has; then the soonest deadline; overdue layers take turns, the one asked longest
            // ago first. So a layer whose pages are too short for the renderer (it repeats them)
            // does not starve the others.
            double key = std::max(deadline, B);
            double asked = layerAskedAt.count(i) ? layerAskedAt[i] : -1e9;
            bool nothing = future == 0;
            bool better = best < 0 || (nothing && !bestWaiting) ||
                          (nothing == bestWaiting && (key < bestDeadline || (key == bestDeadline && asked < bestAsked)));
            if(better){ best = i; bestBar = next; bestCut = cut; bestScene = s; bestDeadline = key; bestAsked = asked; bestWaiting = nothing; }
        }
        if(best > 0){ layerAskedAt[best] = now; return makeRequest(false, "", nullptr, best, bestBar, bestScene, bestCut); }
        return "";
    }

    // ---- Switch now (Perform): the scene the next image uses (manual scene or setlist entry, set
    // by the caller) replaces what plays, now. Old images waiting (ready) go, pending ones are
    // cancelled; layer 1's plan and harmony restart at `bar` (where its image under the header
    // started); the new scene's image is asked for on every layer, forced, and each one replaces
    // the image under the header when it arrives (Schedule::Layer::replaceNext). Layer 1 also asks
    // for the following images (ahead - 1: 2 when the header is in the left image of the scroller,
    // whose right-hand copy gives way to the next one, else 1), so it scrolls on normally after. Returns the requests'
    // paths; goDebt: GOs to skip for layer 1's repeated pages (as Forms Box's request()).
    std::shared_ptr<Image> lastImage;  // the last image makeRequest asked for
    int extraRepeatNext = 0;           // extra pages the next layer-1 request stands for (once)
    int extraRepeatLayer = 0;          // switchNow: extra repeats of the phrase the layer request being made stands for
    // keepPages: the switch image plays this many more whole pages after the one it replaces
    // (Next image now on the same scene: 1, so the new image is not gone when the page ends)
    std::vector<std::string> switchNow(double bar, int& goDebt, int ahead = 2, int keepPages = 0){
        std::vector<std::string> out;
        goDebt = 0;
        if(replaying) return out;
        if(session.empty()) newSession();
        const Scene* s = currentScene();
        if(!s || s->layers.empty()) return out;
        for(auto& [id, im] : pending) if(!im->preview) im->cancelled = true;
        for(auto& L : schedule->layers){ L.ready.clear(); L.replaceNext.reset(); }
        // layer 1's plan from `bar` on (the image under the header may stand for more copies)
        while(!layer1Plan.empty() && layer1Plan.back().startBar >= bar) layer1Plan.pop_back();
        if(!layer1Plan.empty() && layer1Plan.back().startBar + layer1Plan.back().bars > bar) layer1Plan.back().bars = bar - layer1Plan.back().startBar;
        harmonyBars.erase(harmonyBars.lower_bound(barIndex(bar)), harmonyBars.end());   // the new scene's own chords
        layer1NextBar = bar;
        extraRepeatNext = std::max(0, keepPages);
        std::string p = makeRequest(true);
        extraRepeatNext = 0;
        if(p.empty() || !lastImage) return out;
        out.push_back(p);
        auto first = lastImage;
        int firstRepeat = lastRequestRepeat;
        schedule->layers[0].replaceNext = first;
        auto si = scenes.find(first->scene);   // the scene it got (a lane block may choose it)
        if(si == scenes.end()) return out;
        const Scene& to = si->second;
        // the extra layers: what is ahead of the header goes (unseen slots); a layer the scene
        // doesn't have goes entirely
        double B = schedule->headerBeat;
        for(int i = 1; i < (int)schedule->layers.size(); i++){
            auto& L = schedule->layers[i];
            for(auto it = L.slots.begin(); it != L.slots.end();){
                if(i >= (int)to.layers.size() || it->start > B) it = L.slots.erase(it); else ++it;
            }
            if(i >= (int)to.layers.size()) L.header.reset();
            L.scheduledEnd = L.slots.empty() ? -1 : L.slots.back().end();
            layerNextBar.erase(i);
        }
        for(int i = 1; i < (int)to.layers.size() && i < (int)schedule->layers.size(); i++){
            // it takes the place of the layer's image under the header (LayerRing): its bars from there
            auto& L = schedule->layers[i];
            double lb = std::ceil(B / 4.0 - 1e-9) - layerSpan(to.layers[i], true);
            for(auto& sl : L.slots) if(sl.start <= B && B < sl.end()){ lb = std::floor(sl.start / 4.0 * kBarGrid + 1e-9) / kBarGrid; break; }
            // a phrase keeps its place in the bar (the page under the header is one of its inner
            // pages, not its first); so does a page whose place is off the layer's own grid
            // (layerGrid): the line at or before the header. LayerRing puts it there.
            {
                double g = layerGrid(layerSpan(to.layers[i], true));
                double off = lb / g - std::floor(lb / g + 1e-6);
                if(phraseSlices(to.layers[i]) > 1 || off > 1e-6) lb = std::floor(B / 4.0 / g + 1e-9) * g;
                // a phrase longer than a bar: its cycle counted from where layer 1's image starts
                // (`bar`), when that is on the layer's grid, so the layers still end their phrase
                // together after the switch (the header lands inside the phrase, as it does inside
                // layer 1's image)
                if(phraseSlices(to.layers[i]) > 1){
                    const double span = layerSpan(to.layers[i], true);
                    const double anchored = bar + std::floor((B / 4.0 - bar) / span + 1e-9) * span;
                    const double ao = anchored / g - std::floor(anchored / g + 1e-6);
                    if(span > 1 + 1e-9 && anchored <= B / 4.0 + 1e-9 && ao <= 1e-6){
                        lb = anchored;
                        // what is left of the phrase is shorter than the view: the place after it is
                        // already on screen, where a new image may not appear. The switch image plays
                        // the phrase once more there (it arrives with it), so the layer keeps its cycle.
                        if(lb + span * std::max(1, to.layers[i].repeat) - B / 4.0 < layerBars(to.layers[i]) + leadBeats() / 4.0) extraRepeatLayer = 1;
                    }
                }
            }
            std::string q = makeRequest(true, "", nullptr, i, lb, &to, -1);
            extraRepeatLayer = 0;
            if(q.empty()) continue;
            out.push_back(q);
            schedule->layers[i].replaceNext = lastImage;
        }
        goDebt = std::max(0, firstRepeat - 1);
        for(int k = 1; k < ahead; k++){
            std::string n = makeRequest(true);
            if(n.empty()) break;
            out.push_back(n);
            goDebt += std::max(0, lastRequestRepeat - 1);
        }
        note("switch now to " + to.name + " at bar " + barsText(bar));
        syncLog.push_back("switch now to " + to.name + " at bar " + barsText(bar));
        return out;
    }
    // ---- Previous image: layer 1's image before the one under the header comes back in its place,
    // now (the same in-place swap as Switch now). The last few images layer 1 showed are kept
    // (trackPlayed, every frame); each press goes one further back. Only inside the scene that is
    // playing. It plays the rest of the page it replaces and one whole page; what was waiting to
    // come next on layer 1 is skipped; new images follow as usual.
    std::deque<std::shared_ptr<Image>> playedImages;   // layer 1: the distinct images it showed, oldest first
    void trackPlayed(){
        if(replaying) return;
        auto& L = schedule->layers[0];
        if(!L.header || L.header->preview || L.replaceNext) return;   // (a swap in flight: wait for it)
        if(!playedImages.empty() && playedImages.back()->id == L.header->id && playedImages.back()->png == L.header->png) return;   // its repeats, a phrase's pages, a recalled copy
        playedImages.push_back(L.header);
        while(playedImages.size() > 5) playedImages.pop_front();
    }
    bool canPrevious() const {
        if(replaying || playedImages.size() < 2) return false;
        auto& L = schedule->layers[0];
        return L.header && !L.replaceNext && playedImages[playedImages.size() - 2]->scene == L.header->scene;
    }
    bool previousImage(){
        if(!canPrevious()) return false;
        auto& L = schedule->layers[0];
        auto c = std::make_shared<Image>(*playedImages[playedImages.size() - 2]);
        // it takes the place (and the bars) of the page it replaces
        c->startBar = L.header->startBar; c->startBeat = L.header->startBeat;
        c->cancelled = false; c->cutBeat = -1; c->copiesUntil = -1; c->repeat = 1;
        playedImages.pop_back();
        // and one more whole page of it (as a page that repeats): without it the recovered image
        // would be gone as soon as the page it replaced ends
        auto again = std::make_shared<Image>(*c);
        again->repeatIndex = 1;
        if(again->startBar >= 0) again->startBar += std::max(1.0 / kBarGrid, c->bars);
        L.ready.push_front(again);
        L.ready.push_front(c);
        L.replaceNext = c;
        note("previous image: " + c->id + " back under the header");
        return true;
    }

    // a switch image that will never arrive (error, lost): nothing waits for it
    void forgetReplace(const std::string& id){
        for(auto& L : schedule->layers) if(L.replaceNext && L.replaceNext->id == id) L.replaceNext.reset();
    }

    // Layer 1 planned a scene change at `bar`. The extra layers may already have images asked for
    // or placed past it (they render ahead): those that start there or later with another scene
    // are cancelled (unless already on screen) and the layer asks again from `bar`; with a hard
    // cut, an image that runs across `bar` is cut there.
    void onSceneChange(double bar, const std::string& scene, bool hard){
        double cb = bar * 4.0;
        auto obsolete = [&](const std::shared_ptr<Image>& im){ return im->startBeat >= cb - 1e-6 && im->scene != scene; };
        for(auto& [id, im] : pending){
            if(im->layer < 2) continue;
            double span = im->bars * 4.0 * std::max(1, im->repeat);   // with its repeats
            if(obsolete(im)) im->cancelled = true;
            else if(im->startBeat < cb && im->startBeat + span > cb){
                if(hard){ if(im->cutBeat < 0 || im->cutBeat > cb) im->cutBeat = cb; }
                else if(im->scene != scene) im->copiesUntil = cb;   // its copies from cb on: the new scene's
            }
        }
        for(int i = 1; i < (int)schedule->layers.size(); i++){
            auto& L = schedule->layers[i];
            for(auto it = L.ready.begin(); it != L.ready.end();){
                if(obsolete(*it)){ it = L.ready.erase(it); continue; }
                if(hard && (*it)->startBeat < cb && (*it)->startBeat + (*it)->bars * 4.0 > cb) (*it)->cutBeat = cb;
                ++it;
            }
            for(auto it = L.slots.begin(); it != L.slots.end();){
                if(!it->seen && it->start >= cb - 1e-6 && (it->repeat || it->img->scene != scene)){ it = L.slots.erase(it); continue; }
                if(hard && it->start < cb && it->end() > cb){ it->cut = cb; if(!it->repeat) it->img->cutBeat = cb; }
                ++it;
            }
            L.scheduledEnd = L.slots.empty() ? -1 : L.slots.back().end();
            if(layerNextBar.count(i) && layerNextBar[i] > bar) layerNextBar[i] = bar;
        }
        syncLog.push_back(std::string(hard ? "hard cut" : "scene change") + " to " + scene + " at bar " + barsText(bar));
    }

    // Only effects (no drawing of its own): previews and thumbnails draw it over the underlay
    bool onlyEffects(const Preset& pr, int depth = 0) const {
        if(depth > 4) return false;
        if(!pr.isStack()) return pr.gen == "effects";
        if(pr.layers.empty()) return false;
        for(auto& l : pr.layers){
            if(l.contains("preset")){
                auto it = presets.find(l.value("preset", std::string()));
                if(it == presets.end() || !onlyEffects(it->second, depth + 1)) return false;
            }else if(l.value("gen", std::string()) != "effects") return false;
        }
        return true;
    }

    // The planned layer-1 image covering absolute bar b (or null if b is past the plan)
    // What layer 1 will play after its plan if nothing changes: the setlist (or the manual scene)
    // walked forward from where it is, image by image. The extra layers render ahead with it, so
    // a scene change (and a hard cut) further ahead than layer 1's own requests is known in time.
    std::vector<PlanEntry> predictedPlan(double untilBar) const {
        std::vector<PlanEntry> out;
        double bar = layer1NextBar;
        std::string prev = layer1Plan.empty() ? std::string() : layer1Plan.back().scene;
        int entry = setlistEntry, inEntry = imagesInEntry;
        bool sl = useSetlist && !setlist.empty();
        for(int guard = 0; guard < 128 && bar < untilBar; guard++){
            std::string name = sl ? setlist[entry % setlist.size()].scene : manualScene;
            bool cutFlag = sl ? setlist[entry % setlist.size()].hardCut : hardCutManual;
            bool fromLane = false;
            if(laneSceneAt){
                bool lc = false;
                std::string n = laneSceneAt(barIndex(bar), lc);
                auto li = scenes.find(n);
                if(!n.empty() && li != scenes.end() && !li->second.layers.empty()){ name = n; cutFlag = lc; fromLane = true; }
            }
            auto it = scenes.find(name);
            if(it == scenes.end() || it->second.layers.empty()) break;
            PlanEntry pe; pe.startBar = bar; pe.period = std::max(1.0 / kBarGrid, layerSpan(it->second.layers[0]));
            pe.bars = pe.period * std::max(1, it->second.layers[0].repeat); pe.scene = name;
            pe.hardCut = !prev.empty() && prev != name && cutFlag;
            out.push_back(pe);
            prev = name; bar += pe.bars;
            if(sl && !fromLane){
                inEntry++;
                if(inEntry >= std::max(1, setlist[entry % setlist.size()].images)){ inEntry = 0; entry = (entry + 1) % (int)setlist.size(); }
            }
        }
        return out;
    }
    // The first hard cut (planned or predicted) at a beat in (a, b], or -1
    double hardCutIn(double a, double b) const {
        double best = -1;
        auto look = [&](const PlanEntry& p){
            if(!p.hardCut) return;
            double c = p.startBar * 4.0;
            if(c > a + 1e-6 && c <= b + 1e-6 && (best < 0 || c < best)) best = c;
        };
        for(auto& p : layer1Plan) look(p);
        for(auto& p : predictedPlan(b / 4.0 + 1)) look(p);
        return best;
    }

    // The scene layer 1 plays (planned, or predicted from the setlist) at a beat; "" if unknown
    std::string sceneAtBeat(double beat) const {
        double b = beat / 4.0 + 1e-9;
        for(auto& pe : layer1Plan) if(pe.startBar <= b && b < pe.startBar + pe.bars) return pe.scene;
        for(auto& pe : predictedPlan(b + 1)) if(pe.startBar <= b && b < pe.startBar + pe.bars) return pe.scene;
        return "";
    }

    const PlanEntry* planAt(double b) const {
        b += 1e-9;
        for(auto& pe : layer1Plan) if(pe.startBar <= b && b < pe.startBar + pe.bars) return &pe;
        if(!layer1Plan.empty() && b < layer1Plan.front().startBar) return &layer1Plan.front();
        return nullptr;
    }

    // The scene playing at the header (layer 1's plan at the header bar)
    const Scene* sceneAtHeader() const {
        const PlanEntry* pe = planAt(schedule->headerBeat / 4.0);
        // past the end of the plan (Auto GO off, or the renderer late: layer 1 goes round on its
        // last image, and the plan only grows with its requests): that image's scene still plays.
        // Without this the extra layers stopped repeating and went away one by one.
        if(!pe){
            if(layer1Plan.empty() || replaying) return nullptr;
            const auto& h = schedule->layers[0].header;
            auto it = h && !h->preview ? scenes.find(h->scene) : scenes.end();
            return it != scenes.end() ? &it->second : nullptr;
        }
        auto it = scenes.find(pe->scene);
        return it != scenes.end() ? &it->second : nullptr;
    }

    // Layer 1's image under the header started at headerStart beats, but its harmony (and plan)
    // was made for img->startBar: after a repeat, a lost request or a renderer error they differ.
    // Shift what is still to come by the difference, so the next images line up again.
    void syncLayer1(const std::shared_ptr<Image>& img, double headerStart){
        if(replaying && img && replayFirst && img->id == replayFirst->id && img->png == replayFirst->png && std::isnan(replayOrigin)) replayOrigin = headerStart;
        if(!img || img->startBar < 0) return;
        // in whole bars, or in whole pages when its page is shorter than a bar
        const double unit = std::min(1.0, std::max(1.0 / kBarGrid, img->bars));
        double drift = std::round((headerStart / 4.0 - img->startBar) / unit) * unit;
        if(drift == 0) return;
        layer1NextBar += drift;
        double from = img->startBar;
        for(auto& pe : layer1Plan) if(pe.startBar >= from || pe.id == img->id) pe.startBar += drift;
        // what is still to come (its next copies too) moves with it, so it is not shifted twice
        for(auto& q : schedule->layers[0].ready) if(q != img && q->startBar >= from) q->startBar += drift;
        for(auto& [pid, q] : pending) if(q->layer <= 1 && !q->preview && q->startBar >= from) q->startBar += drift;
        img->startBar += drift;
        syncLog.push_back("layer 1 is " + barsText(drift) + " bars off its plan (repeat or lost image): the plan moves");
    }

    // A preset's items with values resolved for one image (several for a layered preset)
    // shared: values laid over every item's params (a stack's own linked pick, passed down)
    double itemBars = 8;               // bars of the image being resolved (per-bar choices)
    // Bar Multiplier (the node's; 1 = off): every layer's length times this, so a wide projection
    // keeps its picture proportions (3 screens wide: x3). Scenes keep the length as written; what
    // plays (requests, plan, harmony timeline) is layerBars / layerSpan. From the next image asked for.
    double barMult = 1;
    int barAdd = 0;                    // Bar Add (the node's): whole bars added after the multiplier (may be negative)
    float density = 0;                 // Density (the node's, -1..1): bends every cell's chance (densityProb); from the next image asked for
    double layerBars(const LayerDef& l) const {
        if(barMult == 1 && barAdd == 0) return l.bars;
        double m = l.bars * barMult;
        // taking bars away stops at one bar (a layer already shorter than that keeps its length)
        return quantBars(barAdd < 0 ? std::max(m + barAdd, std::min(1.0, m)) : m + barAdd);
    }
    // The page, or its whole phrase. Layer 1 plays whole pages (the phrase rounded up to them);
    // layers 2 and up (exact) play exactly the phrase: a last page that is not whole is cut short,
    // so layers of different page lengths with the same phrase end it together.
    double layerSpan(const LayerDef& l, bool exact = false) const {
        const int n = phraseSlices(l);
        const double whole = layerBars(l) * n;
        if(!exact || n <= 1 || l.phrase >= l.bars * n - 1e-9) return whole;   // (whole pages: as ever)
        // the phrase takes the Bar Multiplier and Bar Add as an image's length does, so it still
        // ends with a layer 1 of the same written length
        double p = l.phrase * barMult;
        p = quantBars(barAdd < 0 ? std::max(p + barAdd, std::min(1.0, p)) : p + barAdd);
        return std::min(whole, std::max(layerBars(l), p));
    }
    // The lines an extra layer may start on: the coarsest grid its own cycle (the page, or its
    // whole phrase) shares with the bar. A 1-bar phrase of 1/8-bar pages: bar lines; a 0.5-bar page:
    // half bars; 0.75 bars: beats; whole bars: bar lines. Counted from bar 0, so it never depends on
    // where a scene happened to start, what was under the header, or the lengths played before.
    static double layerGrid(double span){
        long u = std::max(1L, std::lround(span * kBarGrid)), g = (long)kBarGrid;
        while(u){ long r = g % u; g = u; u = r; }
        return g / kBarGrid;
    }
    json presetItems(const Preset& pr, int depth = 0, const json& shared = json::object(), int clipFrom = -1, int clipTo = -1){
        json out = json::array();
        if(depth > 4){ note("presets nested too deep at " + pr.ref() + " (a preset that includes itself?)"); return out; }
        // per-bar choices: resolve again for every perBar bars, each result clipped to them
        if(pr.perBar > 0 && clipFrom < 0){
            int bars = barCount(itemBars);
            for(int b = 0; b < bars; b += pr.perBar)
                for(auto& it : presetItems(pr, depth, shared, b, std::min(bars, b + pr.perBar))) out.push_back(it);
            return out;
        }
        int layerNo = 0;   // which stack layer an item comes from (for its shareKey)
        auto clip = [&](json item){
            if(clipFrom >= 0){ item["clipBars"] = {clipFrom, clipTo}; item["shareKey"] = pr.ref() + "#" + std::to_string(layerNo); }
            return item;
        };
        if(pr.isStack()){
            // the stack's own linked pick: one choice per image, shared by all its layers
            json sharedHere = shared;
            // A stack can cover as one instrument; all of its chosen child shapes share
            // the resolved cover. Use a separate RNG so cover variation never changes notes.
            bool hasCover=false;
            for(auto& entry:pr.params.items()) if(entry.key().rfind("cover",0)==0) hasCover=true;
            if(hasCover){
                auto definition=genDefFromJson({{"params",formscover::schema()}});
                Rng coverRng=rng;
                json cover=resolve(definition,pr,coverRng);
                if(formscover::read(cover).enabled)
                    for(auto& entry:cover.items()) if(!sharedHere.contains(entry.key())) sharedHere[entry.key()]=entry.value();
            }
            if(!pr.linked.empty()){ json pick = json::object(); applyLinked(pick, pr.linked, rng); for(auto& [k, v] : pick.items()) sharedHere[k] = v; }
            // blocking off on the stack switches it off in all its layers (on leaves each layer its own)
            if(!formscover::blocking(pr.params)) sharedHere["blocking"] = false;
            // which layers play in this image
            std::vector<bool> on(pr.layers.size(), true);
            if(pr.pick == "group" && !pr.layers.empty()){
                // groups in order of first appearance; a layer without a group is its own group
                std::vector<int> keys; std::vector<float> weights;
                auto keyOf = [&](size_t i){ return pr.layers[i].value("group", -1 - (int)i); };
                for(size_t i = 0; i < pr.layers.size(); i++){
                    int k = keyOf(i);
                    if(std::find(keys.begin(), keys.end(), k) == keys.end()){
                        keys.push_back(k);
                        weights.push_back(std::max(0.0f, pr.layers[i].value("weight", 1.0f)));
                    }
                }
                float total = 0;
                for(float w : weights) total += w;
                float r = uniform(rng, total), acc = 0;
                int chosen = keys.back();
                for(size_t j = 0; j < keys.size(); j++){ acc += weights[j]; if(r < acc){ chosen = keys[j]; break; } }
                for(size_t i = 0; i < on.size(); i++) on[i] = keyOf(i) == chosen;
            }else if(pr.pick == "one" && !pr.layers.empty()){
                float total = 0;
                for(auto& l : pr.layers) total += std::max(0.0f, l.value("weight", 1.0f));
                float r = uniform(rng, total), acc = 0;
                size_t chosen = pr.layers.size() - 1;
                for(size_t i = 0; i < pr.layers.size(); i++){
                    acc += std::max(0.0f, pr.layers[i].value("weight", 1.0f));
                    if(r < acc){ chosen = i; break; }
                }
                for(size_t i = 0; i < on.size(); i++) on[i] = i == chosen;
            }
            std::map<int, bool> groupRoll;
            for(size_t i = 0; i < pr.layers.size(); i++){
                const json& l = pr.layers[i];
                if(!on[i] || !l.contains("chance")) continue;
                // in "pick: group" mode the group is already chosen: chance rolls per layer
                if(pr.pick == "group"){ on[i] = uniform(rng, 100) < l.value("chance", 100.0f); continue; }
                float ch = l.value("chance", 100.0f);
                int grp = l.value("group", -1 - (int)i);   // no group: its own roll
                if(!groupRoll.count(grp)) groupRoll[grp] = uniform(rng, 100) < ch;
                on[i] = groupRoll[grp];
            }
            for(size_t i = 0; i < pr.layers.size(); i++){
                if(!on[i]) continue;
                layerNo = (int)i;
                auto& l = pr.layers[i];
                if(l.contains("preset")){
                    // a layer that plays another preset
                    std::string r = l.value("preset", std::string());
                    const Preset* pi = presetFor(r);
                    if(r == pr.ref()){ note("stack " + pr.ref() + " includes itself"); continue; }
                    if(!pi){ note("layer preset " + r + " not found (" + pr.ref() + ")"); continue; }
                    // order, as for plain layers: the layer's "params", then the stack's shared
                    // pick, then the layer's own "linked"
                    json sh = (l.contains("params") && l["params"].is_object()) ? l["params"] : json::object();
                    for(auto& [k, v] : sharedHere.items()) sh[k] = v;
                    if(l.contains("linked")){ json pick = json::object(); applyLinked(pick, l["linked"], rng); for(auto& [k, v] : pick.items()) sh[k] = v; }
                    for(auto& it : presetItems(*pi, depth + 1, sh, clipFrom, clipTo)) out.push_back(it);
                    continue;
                }
                Preset layer;
                layer.gen = l.value("gen", std::string());
                layer.params = l.value("params", json::object());
                for(auto& [k, v] : sharedHere.items()) layer.params[k] = v;
                layer.linked = l.value("linked", json::array());
                int lpb = std::max(0, l.value("perBar", 0));
                if(lpb > 0 && clipFrom < 0){
                    // this layer alone is redrawn per bar
                    const GenDef* lg = findGen(layer.gen);
                    if(!lg){ note("layer generator " + layer.gen + " unknown to the renderer (" + pr.ref() + ")"); continue; }
                    int bars = barCount(itemBars);
                    for(int b = 0; b < bars; b += lpb)
                    {
                        json it = item(*lg, pr.ref(), resolve(*lg, layer, rng));
                        it["clipBars"] = {b, std::min(bars, b + lpb)};
                        it["shareKey"] = pr.ref() + "#" + std::to_string(i);
                        out.push_back(it);
                    }
                    continue;
                }
                const GenDef* g = findGen(layer.gen);
                if(!g){ note("layer generator " + layer.gen + " unknown to the renderer (" + pr.ref() + ")"); continue; }
                out.push_back(clip(item(*g, pr.ref(), resolve(*g, layer, rng))));
            }
            return out;
        }
        const GenDef* g = findGen(pr.gen);
        if(!g){ note("generator " + pr.gen + " unknown to the renderer (" + pr.ref() + ")"); return out; }
        if(!shared.empty()){
            Preset withShared = pr;
            for(auto& [k, v] : shared.items()) withShared.params[k] = v;
            out.push_back(clip(item(*g, pr.ref(), resolve(*g, withShared, rng))));
            return out;
        }
        out.push_back(clip(item(*g, pr.ref(), resolve(*g, pr, rng))));
        return out;
    }

    // Editor previews with a scene's bars, harmony, scale and palette: one request per variant
    // (tag, items), all with the same seed and harmony, so an A/B differs only in what is compared.
    // They are shown in the editor and never scroll in.
    // Instruments tab previews: "Same seed" re-renders the last preview's image (its renderer seed,
    // harmony and the draws of the preset's ranges), so a parameter change is seen on the same
    // image; off (or Reroll), each preview is a new one
    bool previewLock = false;
    unsigned previewSeed = 0, previewItemsSeed = 0;
    std::string previewSeedScene;
    Harmony previewHarmony;
    bool previewSeedSet = false;
    // the seed to resolve a preset's ranges with, for the next preview
    unsigned nextPreviewItemsSeed(){
        if(!previewLock || !previewSeedSet) previewItemsSeed = rng() & 0x7fffffff;
        return previewItemsSeed;
    }
    // resolves a preset's items with a given seed, leaving the engine's own random stream as it was
    json presetItemsSeeded(const Preset& pr, unsigned seed){
        Rng saved = rng;
        rng.seed(seed);
        json items = json::array();
        for(auto& x : presetItems(pr)) items.push_back(x);
        rng = saved;
        return items;
    }

    // Resolve a whole-layer effect independently of instrument selection/harmony.
    // Tagged items can be replaced without deleting the same preset used in a cell.
    void applyLayerEffect(json& req,const LayerDef& layer,unsigned seed){
        json items=json::array();
        for(auto& it:req.value("items",json::array()))if(!it.value("layerEffect",false))items.push_back(it);
        const Preset* found=presetFor(layer.effectPreset);
        if(layer.fixed.is_object() && layer.fixed.value("layerEffectPreset",std::string())==layer.effectPreset){
            for(auto& it:layer.fixed.value("items",json::array()))if(it.value("layerEffect",false))items.push_back(it);
        }else if(found && onlyEffects(*found)){
            double savedBars=itemBars;itemBars=layerBars(layer);
            auto fx=presetItemsSeeded(*found,seed ^ 0x4c465830u);itemBars=savedBars;
            for(auto it:fx){it["layerEffect"]=true;items.push_back(std::move(it));}
        }
        req["items"]=std::move(items);req["layerEffectPreset"]=layer.effectPreset;
    }

    // The seed of a scene-wide effect at one bar of the timeline: the same for every layer
    static unsigned globalEffectSeed(const std::string& scene, const std::string& ref, int bar){
        uint32_t h = 2166136261u;
        for(unsigned char ch : scene + "|" + ref){ h ^= ch; h *= 16777619u; }
        h ^= (uint32_t)bar * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
        return h & 0x7fffffffu;
    }
    // The scene's effects on every layer: one resolution per bar of the timeline, clipped to that bar
    // of this image and drawn with that bar's own seed. An image of any length, starting anywhere on
    // the bar grid, gets the same effect at the same bars. Effects that act across bar lines (a long
    // echo, "last bar" processes, whole-image masks) are cut at each bar: use the per-bar ones.
    void applyGlobalEffects(json& req, const Scene& s, double startBar, double bars){
        json items = json::array();
        for(auto& it : req.value("items", json::array())) if(!it.value("globalEffect", false)) items.push_back(it);
        int nb = barCount(bars);
        // an image that starts inside a bar or is not a whole number of bars long cannot take a bar's
        // effect in step (the processes are anchored on the image's own bars): it gets none
        bool onGrid = std::fabs(startBar - std::round(startBar)) < 1e-6 && std::fabs(bars - std::round(bars)) < 1e-6;
        if(onGrid) for(auto& ref : s.globalEffects){
            const Preset* found = presetFor(ref);
            if(!found || !onlyEffects(*found)) continue;
            for(int b = 0; b < nb; b++){
                unsigned seed = globalEffectSeed(s.name, ref, barIndex(startBar + b));
                double savedBars = itemBars; itemBars = 1;
                auto fx = presetItemsSeeded(*found, seed);
                itemBars = savedBars;
                for(auto it : fx){
                    double c0 = 0, c1 = 1;
                    if(it.contains("clipBars") && it["clipBars"].is_array() && it["clipBars"].size() == 2){ c0 = it["clipBars"][0].get<double>(); c1 = it["clipBars"][1].get<double>(); }
                    it["clipBars"] = json::array({b + std::max(0.0, std::min(1.0, c0)), b + std::max(0.0, std::min(1.0, c1))});
                    it["seed"] = seed;
                    it["globalEffect"] = true;
                    items.push_back(std::move(it));
                }
            }
        }
        req["items"] = std::move(items);
    }
    // After a page's items are patched (Immediate), the scene-wide effects go back to the end, where
    // makeRequest puts them
    static void globalEffectsLast(json& req){
        json first = json::array(), last = json::array();
        for(auto& it : req.value("items", json::array())) (it.value("globalEffect", false) ? last : first).push_back(it);
        if(last.empty()) return;
        for(auto& it : last) first.push_back(it);
        req["items"] = std::move(first);
    }

    std::vector<std::string> makePreviews(const std::string& sceneName, const std::vector<std::pair<std::string, json>>& variants){
        std::vector<std::string> paths;
        if(session.empty()) newSession();
        auto sit = scenes.find(sceneName);
        if(sit == scenes.end() || sit->second.layers.empty()){ note("no scene to preview with"); return paths; }
        const Scene& s = sit->second;
        const LayerDef& l = s.layers[0];
        unsigned seed;
        Harmony h;
        if(previewLock && previewSeedSet && previewSeedScene == sceneName){ seed = previewSeed; h = previewHarmony; }
        else{
            seed = rng() & 0x7fffffff;
            h = makeHarmony(s, barCount(layerBars(l)), rng);
            previewSeed = seed; previewHarmony = h; previewSeedScene = sceneName; previewSeedSet = true;
        }
        const ScaleDef es = effectiveScale(s);
        const ScaleDef* sd = es.name.empty() ? nullptr : &es;
        const PaletteDef* pd = findPalette(s.palette);
        applyPitchShift(h.roots);
        for(auto& [tag, items] : variants){
            auto img = std::make_shared<Image>();
            char idbuf[16];
            snprintf(idbuf, sizeof(idbuf), "%04d", counter++);
            img->id = idbuf;
            img->scene = s.name;
            img->bars = layerBars(l);
            img->bpm = s.bpm;
            img->notegrid = s.notegrid;
            img->palette = s.palette;
            img->scale = sd ? sd->name : s.scale;
            img->root = effectiveRoot(s);
            img->snapshot = s.snapshot;
            img->seed = seed;
            img->roots = h.roots;
            img->trans = h.trans;
            img->preview = true;
            img->previewOf = tag;
            json req{{"id", img->id}, {"layer", 1}, {"session", session}, {"seed", seed},
                     {"size", {width, height}}, {"scene", s.name}, {"snapshot", s.snapshot},
                     {"bars", barsJson(layerBars(l))}, {"notegrid", s.notegrid}, {"bpm", barsJson(s.bpm)},
                     {"roots", h.roots}, {"trans", h.trans}, {"compat", s.compat}, {"preview", tag}, {"bassDegree", s.bassDegree}, {"lineWeight", s.lineWeight}};
            if(s.hueDeviation > 0) req["hueDeviation"] = s.hueDeviation;
            if(s.satDeviation > 0) req["satDeviation"] = s.satDeviation;
            if(s.lightDeviation > 0) req["lightDeviation"] = s.lightDeviation;
            if(!l.blocking) req["blocking"] = false;
            req["scale"] = sd ? sd->legacyIndex : -1;
            req["palette"] = pd ? pd->legacyIndex : -1;
            if(sd){ req["scaleName"] = sd->name; req["scaleNotes"] = sd->notes; }
            if(pd){ req["paletteName"] = pd->name; req["paletteColors"] = pd->colors; }
            req["items"] = items;
            fs::path p = sessionDir() / "requests" / (img->id + ".json");
            writeJsonAtomic(p, req);
            pending[img->id] = img;
            pendingSince[img->id] = std::chrono::steady_clock::now();
            paths.push_back(p.string());
            note("preview " + tag + " " + img->id + " (" + std::to_string(items.size()) + " items)");
        }
        return paths;
    }

    void onError(const std::string& id, const std::string& msg){
        if(!pending.count(id)){ note("late answer for " + id + " (sent twice): " + msg); return; }
        forgetReplace(id);
        pending.erase(id);
        pendingSince.erase(id);
        errors++;
        note("ERROR " + id + ": " + msg);
    }

    // Macro_3's outputs for the image under the header
    struct HeaderValues {
        int bpm = 120, notegrid = 100, root = 0, trans = 0, snapshot = -1;
        double bars = 8;
        std::vector<float> scale {0};
        bool valid = false;
    };
    HeaderValues headerValues(int barIn) const {
        HeaderValues v;
        auto& h = schedule->layers[0].header;
        if(!h) return v;
        v.valid = true;
        v.bpm = (int)std::lround(h->bpm);   // the "actualBPM" output stays whole (patch connections); BPM out carries the exact tempo
        v.bars = h->bars;
        v.notegrid = h->notegrid;
        v.snapshot = h->snapshot;
        if(!h->scaleNotes.empty()) v.scale = h->scaleNotes;
        // Vector Getter: index clamped to the vector
        auto at = [&](const std::vector<int>& vec){
            if(vec.empty()) return 0;
            int i = std::max(0, std::min(barIn, (int)vec.size() - 1));
            return vec[i];
        };
        v.root = at(h->roots);
        v.trans = at(h->trans);
        return v;
    }
};

} // namespace forms
