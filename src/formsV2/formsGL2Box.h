//
//  formsGL2Box.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box), the vertical slice (ROADMAP 5.8 step 5)
//
//  One v2 scene played end to end inside Oceanode:
//    - clock: Oceanode's transport (play / pause, position, tempo) when "Follow Transport" is on and
//      the node has a transport; else an internal clock (Play, BPM, Speed). The beat under the
//      playhead drives Engine::update() every frame. A jump back in time reloads the scene (the
//      engine plans forward only; seeds make the reload reproducible).
//    - rendering: the visible events of the 4 layers, drawn every frame by the GPU path
//      (v2DrawGL.h) or by the CPU reference (v2Draw.h, at 1/4 size, uploaded as a texture), with
//      the engine's drawn / animated modulation as the drawers' Mod callbacks.
//    - outputs: Output (texture), Column (brightness per row under the playhead), Beat, Bar, Root
//      (harmony root degree), Kick (1 on frames where a kick event crosses the playhead), and Audio
//      when built with SuperCollider (v1's forms::SoundEngine reused read-only: the column as a
//      512-partial bank on layer 1, kicks from the KICKS sample folder).
//    - window (formsGL2Window section below): scene, transport, instances (mute / solo / lock /
//      reroll), macros, harmony (hold / jump), stats, reload, save as, AI assistant with undo / redo.
//    - assistant answers are auditioned first (the engine plays the candidate): Keep commits it as
//      one undoable step, Discard restores the previous scene text.
//    - "Compare GPU/CPU" renders the current frame both ways at 320 x 180 and shows the mean
//      absolute difference per channel (and both images).
//
//    - playability (v2Perform.h Performer): the node drives scenes through a Performer. Scene pads
//      (the scene folder) and templates, queued changes at now / bar / phrase / 4 bars with a hard
//      cut or a crossfade (both slots composited, event levels x slot gain), setlist, 8 snapshots
//      (saved in the patch), timeline record / list / play, seek when the transport jumps.
//    - sound: "Sound" inspector choice v1 SuperCollider bank / v2 engine / off. v2: an ofSoundStream
//      owned by the node (v2Audio.h): columns, events and the clock go to the audio callback
//      through lock-free queues; the v2 SoundEngine runs in the callback.
//
//  v1 is untouched: v1 headers are only included (formsSound.h for the sonification).
//

#pragma once

#include "ofMain.h"
#include "ofxOceanodeNodeModel.h"
#include "ofxOceanodeShared.h"
#include "ofxOceanodeAIAssistant.h"
#include "../formsBox/formsSound.h"
#include "../formsBox/formsPost.h"
#include "v2Engine.h"
#include "v2Draw.h"
#include "v2DrawGL.h"
#include "v2Assistant.h"
#include "v2Perform.h"
#include "v2Audio.h"
#include "v2GuiMain.h"
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <future>
#include <optional>
#include <sstream>
#include <unordered_map>

class formsGL2Box : public ofxOceanodeNodeModel {
public:
    formsGL2Box() : ofxOceanodeNodeModel("Forms GL2 Box") {}
#ifdef FORMS_HAS_SC
    explicit formsGL2Box(std::vector<serverManager*> servers) : formsGL2Box() { scServers = std::move(servers); }
#endif
    ~formsGL2Box() override {}

    std::string getWindowTitle() override { return "Forms GL2 Box " + ofToString(getNumIdentifier()); }
    ofParameter<bool>* getWindowToggle() override { return &showWindow; }

    // the example scene of the addon (tests/v2/scenes/example_v2.json)
    static std::string exampleScenePath(){
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::path p = fs::path(__FILE__).parent_path() / ".." / ".." / "tests" / "v2" / "scenes" / "example_v2.json";
        fs::path c = fs::weakly_canonical(p, ec);
        if(!ec && fs::exists(c)) return c.string();
        return "/Users/santiagovilanova/Documents/OF/of_playmodes/openFrameworks/addons/ofxOceanodeForms/tests/v2/scenes/example_v2.json";
    }

    void setup() override {
        addParameter(showWindow.set("Show", false));
        addParameter(play.set("Play", true));
        addParameter(bpm.set("BPM", 120, 1, 999));
        addParameter(speed.set("Speed", 1, -4, 4));
        for(int i = 0; i < 8; i++){
            macros[i].set("macro" + ofToString(i + 1), 0, 0, 1);
            addParameter(macros[i]);
            listeners.push(macros[i].newListener([this, i](float& v){
                for(int k = 0; k < perf.slotCount(); k++) perf.engine(k).setMacro("macro" + ofToString(i + 1), v);
                mods.clear();
            }));
        }
        addParameter(reload.set("Reload"));
        listeners.push(reload.newListener([this](){ pendingReload = true; pendingRestart = true; }));
        addParameter(volume.set("Volume", 1, 0, 4));

        addOutputParameter(output.set("Output", nullptr));
        addOutputParameter(columnOut.set("Column", {0.0f}, {0.0f}, {1.0f}));
        addOutputParameter(beatOut.set("Beat", 0, 0, 1e7));
        addOutputParameter(barOut.set("Bar", 0, 0, 1e6));
        addOutputParameter(rootOut.set("Root", 0, -64, 64));
        addOutputParameter(kickOut.set("Kick", 0, 0, 1));
#ifdef FORMS_HAS_SC
        sound = std::make_shared<forms::SoundEngine>(scServers);
        addOutputParameter(audioOut.set("Audio", nodePort(0, sound.get())));
#endif

        // inspector
        addInspectorParameter(sceneFile.set("Scene File", exampleScenePath()));
        addInspectorParameter(cpuRenderer.set("CPU Renderer", false));
        addInspectorParameter(outW.set("Output Width", 1920, 16, 8192));
        addInspectorParameter(outH.set("Output Height", 1080, 16, 8192));
        addInspectorParameter(playheadX.set("Playhead X", 0.5f, 0.f, 1.f));
        addInspectorParameter(bgColour.set("Background", ofFloatColor(0, 0, 0, 1)));   // display only, never sounds
        addInspectorParameter(followTransport.set("Follow Transport", true));
        // 0 v1 SuperCollider bank, 1 v2 engine, 2 off
        addInspectorParameter(soundMode.set("Sound (0 v1 SC, 1 v2, 2 off)", 1, 0, 2));
        listeners.push(sceneFile.newListener([this](std::string&){ if(!suppressFileReload){ pendingReload = true; pendingRestart = true; } }));

        setupAssistant();
        pendingReload = true;
    }

    // ---------------------------------------------------------------- presets
    void presetSave(ofJson& j) override {
        j["v2SceneFile"] = sceneFile.get();
        j["v2CpuRenderer"] = cpuRenderer.get();
        j["v2FollowTransport"] = followTransport.get();
        j["v2PlayheadX"] = playheadX.get();
        j["v2Background"] = {bgColour.get().r, bgColour.get().g, bgColour.get().b};
        j["v2Paper"] = forms2::draw::paperToJson(paper);
        j["v2Post"] = postToJson();
        j["v2Width"] = outW.get(); j["v2Height"] = outH.get();
        std::vector<float> m; for(auto& p : macros) m.push_back(p.get());
        j["v2Macros"] = m;
        // an AI edit not saved as a file yet travels with the patch
        if(auditioning){ if(auditionPrevDirty && !auditionPrev.empty()) j["v2SceneText"] = auditionPrev; }
        else if(sceneDirty && !currentText.empty()) j["v2SceneText"] = currentText;
        ai.save(j);
        // performance: snapshots, setlist, timeline, sound settings
        forms2::json sn = forms2::json::array();
        for(auto& x : snaps) sn.push_back(x);
        j["v2Snapshots"] = sn;
        forms2::json sl = forms2::json::array();
        for(auto& e : setUi) sl.push_back({{"name", e.name}, {"scene", e.scene}, {"bars", e.bars}, {"minutes", e.minutes}, {"crossfade", e.crossfade}});
        j["v2Setlist"] = sl;
        j["v2Timeline"] = forms2::Performer::timelineToJson(perf.timeline());
        j["v2Queue"] = {{"boundary", queueBoundary}, {"crossfade", queueCrossfade}};
        j["v2Sound"] = {{"mode", soundMode.get()}, {"ifft", sndIfft}, {"balance", sndBalance}, {"drums", sndDrums}, {"device", sndDevice}};
    }
    void presetRecallAfterSettingParameters(ofJson& j) override {
        suppressFileReload = true;
        if(j.contains("v2SceneFile") && j["v2SceneFile"].is_string()) sceneFile = j["v2SceneFile"].get<std::string>();
        if(j.contains("v2CpuRenderer") && j["v2CpuRenderer"].is_boolean()) cpuRenderer = j["v2CpuRenderer"].get<bool>();
        if(j.contains("v2FollowTransport") && j["v2FollowTransport"].is_boolean()) followTransport = j["v2FollowTransport"].get<bool>();
        if(j.contains("v2PlayheadX") && j["v2PlayheadX"].is_number()) playheadX = j["v2PlayheadX"].get<float>();
        if(j.contains("v2Background") && j["v2Background"].is_array() && j["v2Background"].size() == 3)
            bgColour = ofFloatColor(j["v2Background"][0].get<float>(), j["v2Background"][1].get<float>(), j["v2Background"][2].get<float>(), 1);
        if(j.contains("v2Paper")) paper = forms2::draw::paperFromJson(j["v2Paper"]);
        if(j.contains("v2Post")) postFromJson(j["v2Post"]);
        if(j.contains("v2Width") && j["v2Width"].is_number()) outW = j["v2Width"].get<int>();
        if(j.contains("v2Height") && j["v2Height"].is_number()) outH = j["v2Height"].get<int>();
        if(j.contains("v2Macros") && j["v2Macros"].is_array())
            for(size_t i = 0; i < 8 && i < j["v2Macros"].size(); i++) if(j["v2Macros"][i].is_number()) macros[i] = j["v2Macros"][i].get<float>();
        suppressFileReload = false;
        recalledText.clear();
        endAudition();
        if(j.contains("v2SceneText") && j["v2SceneText"].is_string()) recalledText = j["v2SceneText"].get<std::string>();
        ai.load(j);
        for(auto& x : snaps) x = forms2::json();
        if(j.contains("v2Snapshots") && j["v2Snapshots"].is_array())
            for(size_t i = 0; i < snaps.size() && i < j["v2Snapshots"].size(); i++) snaps[i] = j["v2Snapshots"][i];
        setUi.clear();
        if(j.contains("v2Setlist") && j["v2Setlist"].is_array())
            for(auto& e : j["v2Setlist"]){
                SetUi u; u.name = e.value("name", std::string("scene")); u.scene = e.value("scene", forms2::json());
                u.bars = e.value("bars", 0.f); u.minutes = e.value("minutes", 0.f); u.crossfade = e.value("crossfade", 0.f);
                if(u.scene.is_object()) setUi.push_back(u);
            }
        recalledTimeline = j.contains("v2Timeline") ? j["v2Timeline"] : forms2::json::array();
        haveRecalledTimeline = true;
        if(j.contains("v2Queue")){ queueBoundary = j["v2Queue"].value("boundary", 1); queueCrossfade = j["v2Queue"].value("crossfade", 0.f); }
        if(j.contains("v2Sound")){
            auto& q = j["v2Sound"];
            if(q.contains("mode") && q["mode"].is_number()) soundMode = q["mode"].get<int>();
            sndIfft = q.value("ifft", false); sndBalance = q.value("balance", 0.f); sndDrums = q.value("drums", false);
            std::string dev = q.value("device", std::string());
            if(dev != sndDevice){ sndDevice = dev; audioReopen = true; }
            balanceFromScene = false;
        }
        macrosFromScene = false;   // the patch's macro values win over the scene's
        pendingReload = true; pendingRestart = true;
    }

    // ---------------------------------------------------------------- frame
    void update(ofEventArgs&) override {
        auto t0 = std::chrono::steady_clock::now();
        ai.update();
        pollAssistant();
        if(pendingReload){
            pendingReload = false;
            if(!recalledText.empty()){ std::string t = recalledText; recalledText.clear(); loadScene(t, true); sceneDirty = true; }
            else loadFromFile();
            if(pendingRestart){ internalBeat = 0; prevBeat = -1e9; }
            pendingRestart = false;
            if(haveRecalledTimeline){ haveRecalledTimeline = false; perf.setTimeline(forms2::Performer::timelineFromJson(recalledTimeline)); }
        }
        double beat = clockStep();
        bool jumped = prevBeat > -1e8 && (beat < prevBeat - 0.25 || beat > prevBeat + 4);
        if(beat < prevBeat - 0.25 && E().ready()){
            // a jump back (seek, loop): with a setlist or a timeline the performer re-runs from its
            // start; else the current scene is planned again from there (same seeds, same result)
            if(!perf.setlist().empty() || !perf.timeline().empty()){ perf.seek(beat); lastCur = &E(); applyLiveState(); mods.clear(); }
            else loadScene(currentText, false);
        } else if(beat < perf.position()) beat = perf.position();   // small backward wobble: hold
        if(jumped) audioSeek = true;
        if(E().ready() || perf.slotCount() > 0){
            auto tp = Clk::now();
            perf.update(beat);
            tAcc[T_PERF] = msSince(tp);
            if(perf.slotCount() > 0 && &E() != lastCur) onSceneSwitched();
        }
        renderFrame(beat);
        prevBeat = beat;
        frameMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        tAcc[T_TOTAL] = ofGetLastFrameTime() * 1000.0;    // whole app frame (all nodes, drawing, ImGui)
        tAcc[T_SOUND] = soundMs;
        commitTimings();
    }

    void draw(ofEventArgs&) override {
        if(showWindow.get()) drawWindow();
    }

private:
    // ---------------------------------------------------------------- parameters
    ofParameter<bool> showWindow, play, cpuRenderer, followTransport;
    ofParameter<int> soundMode;
    ofParameter<float> bpm, speed, playheadX;
    ofParameter<ofFloatColor> bgColour;
    ofParameter<float> macros[8];
    ofParameter<void> reload;
    ofParameter<std::string> sceneFile;
    ofParameter<int> outW, outH;
    ofParameter<ofTexture*> output;
    ofParameter<std::vector<float>> columnOut;
    ofParameter<float> beatOut, barOut, rootOut, kickOut;
    ofEventListeners listeners;
    ofParameter<float> volume;
#ifdef FORMS_HAS_SC
    std::vector<serverManager*> scServers;
    std::shared_ptr<forms::SoundEngine> sound;
    ofParameter<nodePort> audioOut;
    forms::SoundFrame sframe;
    forms::SoundSettings ssettings;
    int soundArraysVersion = 0;
#endif

    // ---------------------------------------------------------------- state
    // scenes run in the performer's slots; E() is the current (last) slot's engine
    forms2::Performer perf;
    forms2::Engine emptyEngine;
    const forms2::Engine* lastCur = nullptr;
    double curOrigin = 0;                    // global beat of the current slot's beat 0
    forms2::Engine& E(){ return perf.slotCount() ? perf.engine() : emptyEngine; }
    const forms2::Engine& E() const { return perf.slotCount() ? const_cast<forms2::Performer&>(perf).engine() : emptyEngine; }
    // perform
    struct SetUi { std::string name; forms2::json scene; float bars = 8, minutes = 0, crossfade = 0; };
    std::vector<SetUi> setUi;
    std::array<forms2::json, 8> snaps;
    forms2::json recalledTimeline;
    bool haveRecalledTimeline = false;
    int queueBoundary = 1;                   // forms2::Boundary: now / bar / phrase / 4 bars
    float queueCrossfade = 0;                // beats; 0 = hard cut
    std::string queuedText;                  // text of the scene queued from a pad / template
    bool queuedDirty = false;
    std::string queuedFile;
    std::vector<std::string> padFiles, templateFiles;
    bool padsScanned = false;
    int templateIdx = 0;
    char newName[128] = "new_scene";
    std::string performStatus;
    // v2 sound
    std::unique_ptr<forms2::audio::AudioOut> audio;
    bool sndIfft = false, sndDrums = false, audioReopen = false, audioSeek = true, audioFailed = false, balanceFromScene = true;
    float sndBalance = 0.f;
    // v1-match scene keys (root MIDI, note grid, scale quantise, reverb send, kick sample)
    int sndRoot = 27, sndGrid = 100, sndKickSelect = 11;
    float sndQuant = 0, sndReverb = 0.1119f;
    std::string sndKickSample, kickLoaded;
    std::array<std::vector<float>, 4> layerCols;   // per-layer brightness at the playhead, row 0 = top
    std::array<forms2::snd::V1Layer, 4> sentLayer;
    int engKickLayer = -1;
    // 3D view (5.14): off by default; a scene's "view3d" object sets it on load
    forms2::draw::ThreeD td3;
    forms2::draw::Paper paper;        // reading modes, ink blends, alpha from brightness, dark inks (5.18)
    forms::Post post;                 // v1's post-processing of the shown output (stretch, line, HSV, levels, feathers)
    bool scene3D = false;             // the scene has a view3d object (plan farther ahead)
    std::string sndDevice;
    double evSentUntil = -1e9;
    float meterL = 0, meterR = 0;
    std::vector<std::string> deviceNames;
    forms2::drawgl::GLRenderer gl;
    forms2::draw::Renderer cpu;
    int glW = 0, glH = 0;
    ofTexture cpuTex;
    ofPixels cpuPix;
    std::string currentText, recalledText, loadedName;
    std::vector<std::string> loadErrors;
    bool sceneDirty = false, pendingReload = false, pendingRestart = false, suppressFileReload = false, macrosFromScene = true;
    double internalBeat = 0, prevBeat = -1e9, nowBeat = 0;
    bool usingTransport = false, lastPlay = true;
    float lastBpm = -1;
    std::map<std::string, bool> muteMap, soloMap;
    // modulation readers of the visible events by salted id (persistent; pruned when it grows)
    struct ModRef { std::shared_ptr<const forms2::EventMod> m; double org = 0; };
    std::unordered_map<uint64_t, ModRef> mods;
    std::vector<forms2::VisibleEvent> visBuf;
    double frameMs = 0, renderMs = 0, soundMs = 0;
    // frame-time breakdown: rolling average / max over the last 2 s
    struct Rolling {
        std::deque<std::pair<double, double>> v;     // (time s, value)
        void add(double t, double x){ v.push_back({t, x}); while(!v.empty() && v.front().first < t - 2.0) v.pop_front(); }
        double avg() const { double s = 0; for(auto& p : v) s += p.second; return v.empty() ? 0 : s / v.size(); }
        double max() const { double m = 0; for(auto& p : v) m = std::max(m, p.second); return m; }
    };
    enum { T_PERF, T_ENGINE, T_VISIBLE, T_PREP, T_GPU_CPU, T_GPU, T_COLUMN, T_CPUR, T_SOUND, T_AUDIO, T_TOTAL,
           T_G_LAYERS, T_G_COVERS, T_G_COMP, T_G_FLAT, T_N };   // T_G_*: per-pass GPU time (v2DrawGL timers)
    Rolling timing[T_N];
    double tAcc[T_N] = {};                           // this frame's values (ms; audio in %)
    int visPerLayer[4] = {0, 0, 0, 0};
    std::array<float, 120> frameHist{}; int histPos = 0;
    bool statsOpen = false;                          // fine timing (GL queries) only while visible
    bool glQueryOk = false;                          // the renderer's timer queries work
    using Clk = std::chrono::steady_clock;
    static double msSince(Clk::time_point t){ return std::chrono::duration<double, std::milli>(Clk::now() - t).count(); }
    void commitTimings(){
        double t = ofGetElapsedTimef();
        tAcc[T_AUDIO] = audio ? audio->bridge.load.load() * 100.0 : 0;
        for(int i = 0; i < T_N; i++){ if((i == T_GPU || i >= T_G_LAYERS) && !(glQueryOk && statsOpen)) continue; timing[i].add(t, tAcc[i]); }
        frameHist[(size_t)histPos] = (float)tAcc[T_TOTAL]; histPos = (histPos + 1) % (int)frameHist.size();
        double gpu[5] = {tAcc[T_GPU], tAcc[T_G_LAYERS], tAcc[T_G_COVERS], tAcc[T_G_COMP], tAcc[T_G_FLAT]};
        for(auto& x : tAcc) x = 0;
        tAcc[T_GPU] = gpu[0];                            // the query results arrive a few frames late
        for(int k = 0; k < 4; k++) tAcc[T_G_LAYERS + k] = gpu[k + 1];
    }
    int drawnEvents = 0;
    std::string fileStatus;
    // window
    int selected = -1;
    bool holdOn = false;
    int jumpDeg = -1000;
    char saveName[128] = "my_scene";
    // undo / redo: one history shared by the v2 GUI edits and the assistant (v2GuiModel.h)
    forms2::gui::Gui v2gui;
    forms2::gui::GuiHost v2host;

    // ---------------------------------------------------------------- loading
    static bool readFile(const std::string& path, std::string& out){
        std::ifstream f(path, std::ios::binary);
        if(!f) return false;
        std::stringstream ss; ss << f.rdbuf(); out = ss.str();
        return true;
    }
    void loadFromFile(){
        std::string t;
        if(!readFile(sceneFile.get(), t)){
            loadErrors = {"cannot read " + sceneFile.get()};
            fileStatus = loadErrors.front();
            return;
        }
        macrosFromScene = true;
        endAudition();
        if(loadScene(t, true)) sceneDirty = false;
        v2gui.cx.history.clear();
    }
    // loads a scene text into the engine; keeps the text even when it does not load (so Fix can work)
    bool loadScene(const std::string& text, bool takeBpm){
        mods.clear();
        currentText = text;
        forms2::json j;
        try { j = forms2::json::parse(text); }
        catch(const std::exception& e){
            perf.loadScene(forms2::json());   // clears the slots
            loadErrors = {std::string("scene JSON: ") + e.what()};
            fileStatus = "scene not loaded (1 problem)"; lastCur = nullptr; return false;
        }
        // an in-place reload keeps the current slot's origin (0 on restart or after a jump before it)
        double at = perf.slotCount() && !pendingRestart ? perf.slot(perf.slotCount() - 1).origin : 0;
        if(at > nowBeat) at = 0;
        bool ok = perf.loadScene(j, at);
        lastCur = perf.slotCount() ? &E() : nullptr;
        applyViewOptions();
        loadErrors = ok ? E().problems() : perf.problems;
        if(!ok){ fileStatus = "scene not loaded (" + ofToString(loadErrors.size()) + " problems)"; return false; }
        loadedName = E().scene().name;
        fileStatus = "loaded";
        if(takeBpm && !(usingTransport)) bpm = (float)E().scene().bpm;
        // macros: the scene's values on a file load; the sliders are pushed to the engine always
        if(macrosFromScene){
            for(int i = 0; i < 8; i++){
                auto f = E().scene().macros.find("macro" + ofToString(i + 1));
                if(f != E().scene().macros.end()) macros[i] = (float)f->second;
            }
        }
        macrosFromScene = true;
        applyLiveState();
        sceneSoundSettings(j);
        scene3DSettings(j); scenePaperSettings(j); sceneColourSettings(j);
#ifdef FORMS_HAS_SC
        soundArraysVersion++;
#endif
        return true;
    }
    void applyViewOptions(){
        for(int i = 0; i < perf.slotCount(); i++){
            auto& o = perf.engine(i).options;
            o.defaultWidth = std::max(16, outW.get()); o.defaultHeight = std::max(16, outH.get());
            o.defaultHeaderX = playheadX * o.defaultWidth;
        }
    }
    // performance state survives reloads and switches (not lock: it depends on the phrase it was set in)
    void applyLiveState(){
        if(!perf.slotCount()) return;
        forms2::Engine& e = E();
        for(int i = 0; i < 8; i++) e.setMacro("macro" + ofToString(i + 1), macros[i].get());
        for(auto& kv : muteMap) e.setMuted(kv.first, kv.second);
        for(auto& kv : soloMap) e.setSolo(kv.first, kv.second);
        if(holdOn || jumpDeg > -1000) e.setHarmonyOverride({holdOn, jumpDeg}, nowBeat - perf.slot(perf.slotCount() - 1).origin, false);
    }
    // the scene's 3D view settings (top-level "view3d"); absent = flat
    // the scene's light-background settings (top-level "paper", 5.18); absent = defaults
    void scenePaperSettings(const forms2::json& j){
        if(!(j.is_object() && j.contains("paper"))) return;   // absent: keep the patch's settings
        paper = forms2::draw::paperFromJson(j["paper"]);
        if(paper.hasBackground) bgColour = ofFloatColor(paper.background[0], paper.background[1], paper.background[2], 1);
    }
    // the scene's role colours (top-level "colours", e.g. a converted v1 palette); absent = built-in
    std::map<std::string, std::array<float, 3>> sceneColours;
    void sceneColourSettings(const forms2::json& j){
        sceneColours = j.is_object() && j.contains("colours") ? forms2::draw::coloursFromJson(j["colours"]) : std::map<std::string, std::array<float, 3>>{};
    }
    void scene3DSettings(const forms2::json& j){
        scene3D = j.is_object() && j.contains("view3d") && j["view3d"].is_object();
        td3 = scene3D ? forms2::draw::threeDFromJson(j["view3d"]) : forms2::draw::ThreeD{};
    }
    // the scene's pitch balance (top level, or in "scene" / "sound") sets the slider
    void sceneSoundSettings(const forms2::json& j){
        // v1-match keys: top level, "scene" or "sound"; absent = patch 8's values
        auto num = [&](const char* key, double def){
            for(const char* k : {"", "scene", "sound"}){
                const forms2::json* o = &j;
                if(*k){ if(!j.is_object() || !j.contains(k) || !j[k].is_object()) continue; o = &j[k]; }
                if(o->is_object() && o->contains(key) && (*o)[key].is_number()) return (*o)[key].get<double>();
            }
            return def;
        };
        sndRoot = (int)std::lround(num("root", 27));
        sndGrid = std::max(1, (int)std::lround(num("notegrid", 100)));
        sndQuant = (float)num("scaleQuant", 0);
        sndReverb = (float)num("reverbAmt", 0.1119);
        sndKickSelect = (int)std::lround(num("kickSelect", 11));
        sndKickSample.clear();
        for(const char* k : {"", "scene", "sound"}){
            const forms2::json* o = &j;
            if(*k){ if(!j.is_object() || !j.contains(k) || !j[k].is_object()) continue; o = &j[k]; }
            if(o->is_object() && o->contains("kickSample") && (*o)["kickSample"].is_string()){ sndKickSample = (*o)["kickSample"].get<std::string>(); break; }
        }
        engKickLayer = -1;                   // kick layer / sample again
        if(!balanceFromScene){ balanceFromScene = true; return; }
        sndBalance = (float)num("pitchBalance", 0);
    }
    // the performer switched to another scene (queued change, setlist, snapshot)
    void onSceneSwitched(){
        lastCur = &E();
        mods.clear();
        applyViewOptions();
        if(!queuedText.empty()){ currentText = queuedText; sceneDirty = queuedDirty; queuedText.clear(); }
        else { currentText = E().captureState().scene.dump(2); sceneDirty = true; }
        if(!queuedFile.empty()){ suppressFileReload = true; sceneFile = queuedFile; suppressFileReload = false; queuedFile.clear(); }
        loadedName = E().scene().name;
        loadErrors.clear();
        fileStatus = "switched to " + loadedName;
        endAudition();
        v2gui.cx.history.clear();
        selected = -1;
        applyLiveState();
        try { auto jj = forms2::json::parse(currentText); sceneSoundSettings(jj); scene3DSettings(jj); scenePaperSettings(jj); sceneColourSettings(jj); } catch(...){}
        if(audio) audio->bridge.resetLoudness();
#ifdef FORMS_HAS_SC
        soundArraysVersion++;
#endif
    }

    // ---------------------------------------------------------------- clock
    double clockStep(){
        auto tr = getTransport();
        usingTransport = followTransport.get() && tr != nullptr;
        float dt = (float)ofGetLastFrameTime();
        if(usingTransport){
            auto st = getTransportState();
            if(play.get() != lastPlay){ if(play.get()) tr->play(); else tr->pause(); }
            else if(st.isPlaying != play.get()) play = st.isPlaying;
            lastPlay = play.get();
            if(lastBpm >= 0 && std::fabs(bpm.get() - lastBpm) > 1e-4) tr->setBpm(bpm.get());
            else if(std::fabs(st.bpm - bpm.get()) > 1e-4) bpm = st.bpm;
            lastBpm = bpm.get();
            internalBeat = st.beatPosition;
            nowBeat = st.beatPosition;
        } else {
            lastPlay = play.get(); lastBpm = -1;
            if(play.get()) internalBeat += bpm.get() / 60.0 * std::min(0.25f, dt) * speed.get();
            nowBeat = internalBeat;
        }
        return nowBeat;
    }

    // ---------------------------------------------------------------- rendering
    // ids of events from different slots must not collide (modulation readers, sound)
    static uint64_t slotSalt(double origin){ return origin == 0 ? 0 : std::hash<double>{}(origin) * 0x9E3779B97F4A7C15ull; }
    // the visible events of every slot in global beats, levels x slot gain (crossfade compositing)
    std::vector<forms2::Event> visibleEvents(double beat, int W, int H, double hx){
        std::vector<forms2::Event> evs;
        for(auto& c : visPerLayer) c = 0;
        for(int i = 0; i < perf.slotCount(); i++){
            forms2::Engine& en = perf.engine(i);
            if(!en.ready()) continue;
            double org = perf.slot(i).origin, lb = beat - org, g = perf.slotGain(i);
            if(g <= 0) continue;
            uint64_t salt = slotSalt(org);
            std::vector<forms2::LayerView> views;
            for(int l = 0; l < 4; l++) views.push_back(en.viewFor(l, lb, W, H, hx));
            if(scene3D || td3.any()) for(auto& v : views) v.width = (int)(W * std::max(1.0, td3.lookAhead));   // far events converge
            auto te = Clk::now();
            en.update(lb, views);
            tAcc[T_ENGINE] += msSince(te);
            for(int l = 0; l < 4; l++){
                auto tv = Clk::now();
                visBuf.clear();
                en.visibleInto(l, views[(size_t)l], visBuf);
                tAcc[T_VISIBLE] += msSince(tv);
                visPerLayer[l] += (int)visBuf.size();
                auto tq = Clk::now();
                for(auto& ve : visBuf){
                    const forms2::StoredEvent& s = *ve.ev;
                    evs.push_back(s.e);
                    forms2::Event& e = evs.back();
                    e.id = s.e.id ^ salt; e.start += org; e.level *= g;
                    mods[e.id] = {en.modOf(s), org};
                }
                tAcc[T_PREP] += msSince(tq);
            }
        }
        if(mods.size() > evs.size() * 2 + 1024){   // drop readers of events no longer visible
            std::unordered_map<uint64_t, ModRef> keep;
            for(auto& e : evs){ auto f = mods.find(e.id); if(f != mods.end()) keep.insert(*f); }
            mods.swap(keep);
        }
        return evs;
    }
    forms2::draw::Frame makeFrame(double beat, int w, int h, double headerX){
        forms2::draw::Frame f;
        f.width = w; f.height = h; f.headerX = headerX; f.nowBeat = beat;
        auto ha = E().harmonyAt(beat - curOrigin);
        if(ha.scale) f.scale = *ha.scale;
        f.td = td3;
        f.paper = paper; f.paper.colours = sceneColours;
        if(td3.any() && E().ready()) for(int l = 0; l < 4; l++){   // layer routings of camera / plane depth
            f.td.layerCamera[(size_t)l] = E().layerParam("camera", td3.layerCamera[(size_t)l], l, beat - curOrigin);
            f.td.planeZ[(size_t)l] = E().layerParam("planeZ", td3.planeZ[(size_t)l], l, beat - curOrigin);
        }
        f.mod.drawn = [this](const forms2::Event& e, const std::string& dest, double u, double base){
            auto it = mods.find(e.id);
            return it == mods.end() || !it->second.m ? base : it->second.m->drawn(dest, base, u);
        };
        f.mod.animated = [this](const forms2::Event& e, const std::string& dest, forms2::Beat now, double base){
            auto it = mods.find(e.id);
            return it == mods.end() || !it->second.m ? base : it->second.m->animated(dest, base, now - it->second.org);
        };
        return f;
    }
    static bool isKick(const forms2::Engine& en, const forms2::StoredEvent& s){
        if(s.e.kind != forms2::EventKind::Hit) return false;
        if(s.e.look == "kick" || s.e.look == "taiko") return true;
        auto* in = en.instance(s.e.instance);
        return in && in->type == "kick";
    }

    void renderFrame(double beat){
        int W = std::max(16, outW.get()), H = std::max(16, outH.get());
        double hx = playheadX.get() * W;
        bool kick = false;
        std::vector<float> col;
        for(auto& c : layerCols) c.clear();
        curOrigin = perf.slotCount() ? perf.slot(perf.slotCount() - 1).origin : 0;
        if(E().ready()){
            double lb = beat - curOrigin;
            std::vector<forms2::LayerDef> layers = E().scene().layers;
            layers.resize(4);
            for(int l = 0; l < 4; l++) layers[(size_t)l].zoomBeats = E().layerZoom(l, lb);
            auto evs = visibleEvents(beat, W, H, hx);
            // kicks crossing the playhead since the last frame (every audible slot)
            if(beat > prevBeat && beat - prevBeat < 4)
                for(int i = 0; i < perf.slotCount() && !kick; i++){
                    forms2::Engine& en = perf.engine(i);
                    double org = perf.slot(i).origin;
                    if(perf.slotGain(i) <= 0.05) continue;
                    for(int l = 0; l < 4 && !kick; l++)
                        for(auto& s : en.events(l)){
                            if(s.e.start + org > beat) break;
                            if(s.e.start + org > prevBeat && isKick(en, s) && !en.muted(s.e.instance)){ kick = true; break; }
                        }
                }
            drawnEvents = (int)evs.size();
            auto r0 = std::chrono::steady_clock::now();
            if(!cpuRenderer.get()){
                if(glW != W || glH != H){ gl.setup(W, H); glW = W; glH = H; }
                gl.timing = statsOpen;                    // per-pass timer queries inside the renderer
                gl.setBackground(bgColour.get().r, bgColour.get().g, bgColour.get().b);
                gl.render(evs, makeFrame(beat, W, H, hx), layers);
                glQueryOk = gl.timersOk();
                if(glQueryOk && statsOpen){
                    tAcc[T_GPU] = gl.gpuMs(-1);
                    for(int k = 0; k < 4; k++) tAcc[T_G_LAYERS + k] = gl.gpuMs(k);
                }
                tAcc[T_GPU_CPU] = msSince(r0);
                glBlendEquation(GL_FUNC_ADD);
                ofEnableBlendMode(OF_BLENDMODE_ALPHA);
                {   // shown output: v1's post (stretch, line, HSV / levels), the background, then edge feathers
                    const ofTexture* t = post.process(gl.output().getTexture(), (float)hx, 1.f, (float)ofGetLastFrameTime());
                    bool on = post.enabled && post.ok;
                    t = &gl.showOver(*t, on && post.stretch ? (float)hx : -1.f, 1.f).getTexture();
                    t = post.feather(*t, bgColour.get());
                    output = const_cast<ofTexture*>(t);       // the column reads the flat score, never this
                }
                auto tc = Clk::now();
                col = gl.column((int)std::lround(hx));
                if(soundMode.get() != 2) glLayerColumns((int)std::lround(hx), col, layers);
                tAcc[T_COLUMN] = msSince(tc);
            } else {
                int cw = std::max(16, W / 4), ch = std::max(16, H / 4);
                cpu.frame = makeFrame(beat, cw, ch, hx * cw / W);
                cpu.layers = layers;
                cpu.render(evs);
                auto rgb = forms2::drawgl::rgb8OverBackground(cpu.display(), bgColour.get().r, bgColour.get().g, bgColour.get().b);
                cpuPix.setFromPixels(rgb.data(), cw, ch, OF_PIXELS_RGB);
                if(!cpuTex.isAllocated() || (int)cpuTex.getWidth() != cw || (int)cpuTex.getHeight() != ch) cpuTex.allocate(cpuPix);
                cpuTex.loadData(cpuPix);
                output = &cpuTex;
                {   // CPU reference: post on the image already over the background (approximate order)
                    const ofTexture* t = post.process(cpuTex, (float)(hx * cw / W), 1.f, (float)ofGetLastFrameTime());
                    output = const_cast<ofTexture*>(post.feather(*t, bgColour.get()));
                }
                col = cpu.column((int)std::lround(hx * cw / W));
                int cx = (int)std::lround(hx * cw / W);
                for(int l = 0; l < 4; l++){       // each layer's own canvas, as v1 reads one buffer per layer
                    const auto& c = cpu.layer(l);
                    auto& lc = layerCols[(size_t)l];
                    lc.assign((size_t)c.h, 0.f);
                    if(cx < 0 || cx >= c.w) continue;
                    float g = (float)layers[(size_t)l].gain;
                    (void)g; lc = cpu.layerColumn(l, cx);   // with the layer's reading mode (5.18)
                }
                tAcc[T_CPUR] = msSince(r0);
            }
            renderMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - r0).count();
            if(pendingCompare){ pendingCompare = false; compareGpuCpu(evs, layers, beat, hx / W); }
            beatOut = (float)beat;
            barOut = (float)E().harmony().barIndex(lb);
            rootOut = (float)E().harmonyAt(lb).root;
        }
        if(col.empty()) col.assign(1, 0.f);
        columnOut = col;
        kickOut = kick ? 1.f : 0.f;
        int mode = soundMode.get();
        auto ts = Clk::now();
        runSound(col, kick && mode == 0, beat);
        runSoundV2(col, beat, mode == 1);
        soundMs = msSince(ts);
    }

    // ---------------------------------------------------------------- GPU / CPU check (debug)
    forms2::drawgl::GLRenderer glCmp;
    ofTexture cmpTexCpu, cmpTexGpu;
    bool pendingCompare = false;
    std::string cmpResult;
    static constexpr int kCmpW = 320, kCmpH = 180;

    // renders this frame's events at kCmpW x kCmpH with both renderers; mean |GPU - CPU| per channel
    void compareGpuCpu(const std::vector<forms2::Event>& evs, const std::vector<forms2::LayerDef>& layers, double beat, double hxFrac){
        auto f = makeFrame(beat, kCmpW, kCmpH, hxFrac * kCmpW);
        forms2::draw::Renderer ref;
        ref.frame = f; ref.layers = layers;
        ref.render(evs);
        const auto& c = ref.display();
        if(glCmp.width() != kCmpW || glCmp.height() != kCmpH) glCmp.setup(kCmpW, kCmpH);
        glCmp.render(evs, f, layers);
        glBlendEquation(GL_FUNC_ADD);
        ofEnableBlendMode(OF_BLENDMODE_ALPHA);
        auto g = glCmp.readRGBA();
        size_t n = (size_t)kCmpW * kCmpH;
        if(c.px.size() != n * 4 || g.size() != n * 4){ cmpResult = "comparison failed (size mismatch)"; return; }
        double sum[4] = {0, 0, 0, 0}, mx = 0; size_t big = 0, litC = 0, litG = 0;
        std::vector<uint8_t> a8(n * 3), b8(n * 3);
        for(size_t i = 0; i < n; i++){
            double pd = 0;
            for(int k = 0; k < 4; k++){
                double cv = std::clamp(c.px[i * 4 + k], 0.f, 1.f), gv = std::clamp(g[i * 4 + k], 0.f, 1.f);
                double d = std::fabs(cv - gv); sum[k] += d; pd = std::max(pd, k < 3 ? d : 0.0);
                if(k < 3){ a8[i * 3 + k] = (uint8_t)std::lround(cv * 255); b8[i * 3 + k] = (uint8_t)std::lround(gv * 255); }
            }
            mx = std::max(mx, pd);
            if(pd > 0.1) big++;
            if(std::max({c.px[i * 4], c.px[i * 4 + 1], c.px[i * 4 + 2]}) > 0.05f) litC++;
            if(std::max({g[i * 4], g[i * 4 + 1], g[i * 4 + 2]}) > 0.05f) litG++;
        }
        char buf[400];
        std::snprintf(buf, sizeof buf, "beat %.2f, %d events, %dx%d: mean |GPU-CPU| R %.4f G %.4f B %.4f A %.4f   max %.3f   pixels off by >0.1: %.2f%%   lit pixels CPU %.1f%% GPU %.1f%%",
                      beat, (int)evs.size(), kCmpW, kCmpH, sum[0] / n, sum[1] / n, sum[2] / n, sum[3] / n, mx, 100.0 * big / n, 100.0 * litC / n, 100.0 * litG / n);
        cmpResult = buf;
        ofPixels pa, pb;
        pa.setFromPixels(a8.data(), kCmpW, kCmpH, OF_PIXELS_RGB); pb.setFromPixels(b8.data(), kCmpW, kCmpH, OF_PIXELS_RGB);
        if(!cmpTexCpu.isAllocated()) cmpTexCpu.allocate(pa);
        if(!cmpTexGpu.isAllocated()) cmpTexGpu.allocate(pb);
        cmpTexCpu.loadData(pa); cmpTexGpu.loadData(pb);
    }

    // ---------------------------------------------------------------- per-layer columns (GPU)
    // each layer's FBO column at x (sync read), row 0 = top. Orientation checked against the
    // composite: if the flipped layers match it better, they are flipped.
    void glLayerColumns(int x, const std::vector<float>& comp, const std::vector<forms2::LayerDef>& layers){
        int H = (int)comp.size();
        std::vector<float> rgba;
        GLint prevRead = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        for(int l = 0; l < 4; l++){
            auto& lc = layerCols[(size_t)l];
            ofFbo& f = gl.layer(l);
            if(!f.isAllocated() || (int)f.getHeight() != H){ lc.clear(); continue; }
            int xx = std::clamp(x, 0, (int)f.getWidth() - 1);
            rgba.assign((size_t)H * 4, 0.f);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, f.getId());
            glReadBuffer(GL_COLOR_ATTACHMENT0);
            glReadPixels(xx, 0, 1, H, GL_RGBA, GL_FLOAT, rgba.data());
            float g = (float)layers[(size_t)l].gain;
            lc.assign((size_t)H, 0.f);
            int rm = paper.reading[(size_t)l]; bool afb = paper.alphaFromBrightness[(size_t)l];
            for(int y = 0; y < H; y++){ float q[4]; forms2::draw::layerPixel(&rgba[(size_t)y * 4], g, afb, q); lc[(size_t)y] = std::min(1.f, paper.anyReading() ? forms2::draw::readPixel(q, rm) : std::max({q[0], q[1], q[2]})); }
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        // orientation check (the layer FBOs may be stored the other way up from the score target)
        double same = 0, flip = 0;
        for(int y = 0; y < H; y++){
            float a = 0, b = 0;
            for(auto& lc : layerCols) if((int)lc.size() == H){ a = std::max(a, lc[(size_t)y]); b = std::max(b, lc[(size_t)(H - 1 - y)]); }
            same += std::fabs(a - comp[(size_t)y]); flip += std::fabs(b - comp[(size_t)y]);
        }
        if(flip < same * 0.5) for(auto& lc : layerCols) std::reverse(lc.begin(), lc.end());
        bool any = false; for(auto& lc : layerCols) any = any || !lc.empty();
        if(!any) layerCols[0] = comp;   // no layer FBOs: the composite as layer 0
    }

    // the column of layer l (row 0 = top); empty if none
    const std::vector<float>& layerColumn(int l, const std::vector<float>& comp) const {
        static const std::vector<float> none;
        const auto& c = layerCols[(size_t)l];
        if(!c.empty()) return c;
        return l == 0 ? comp : none;
    }
    // harmony at the playhead in v1 terms: grid transpose (the key) and the scale's semitones
    void v1Harmony(int& trans, std::vector<float>& scale) const {
        trans = 0; scale.clear();
        if(!E().ready()) return;
        auto ha = E().harmonyAt(nowBeat - curOrigin);
        if(!ha.scale) return;
        trans = ha.scale->tonic;
        for(int s : ha.scale->semis) scale.push_back((float)s);
    }
    // the first layer with kick events (0 if none); scanned once per engine / scene load
    mutable const forms2::Engine* kickScanEng = nullptr; mutable int kickScanLayer = 0;
    int kickLayerOf() const {
        if(!E().ready()) return 0;
        if(kickScanEng == &E() && engKickLayer >= 0) return kickScanLayer;
        kickScanEng = &E(); kickScanLayer = 0;
        for(int l = 0; l < 4 && !kickScanLayer; l++) for(auto& s : E().events(l)) if(isKick(E(), s)){ kickScanLayer = l; break; }
        return kickScanLayer;
    }
    // kick sample files as v1 lists them (sorted paths)
    static std::vector<std::string> kickFiles(){
        std::vector<std::string> v;
        namespace fs = std::filesystem;
        std::error_code ec;
        std::string folder = ofToDataPath("Forms/samples/KICKS", true);
        if(fs::is_directory(folder, ec))
            for(auto& e : fs::directory_iterator(folder, ec)){
                auto ext = ofToLower(e.path().extension().string());
                if(ext == ".wav" || ext == ".aif" || ext == ".aiff") v.push_back(e.path().string());
            }
        std::sort(v.begin(), v.end());
        return v;
    }
    // v1's kickIndexFor: the scene's kickSample name, else kickSelect (default 11)
    int kickIndex(const std::vector<std::string>& files) const {
        if(files.empty()) return -1;
        if(!sndKickSample.empty())
            for(int i = 0; i < (int)files.size(); i++) if(ofFilePath::getFileName(files[(size_t)i]) == sndKickSample) return i;
        return std::clamp(sndKickSelect, 0, (int)files.size() - 1);
    }

    // ---------------------------------------------------------------- sound (v1's engine, read-only)
    // v1's partial layout / pitches / gains per layer (formsBox.h runSound), one bank per layer
#ifdef FORMS_HAS_SC
    std::array<forms::PartialLayout, 4> v1Lay;
    std::array<int, 4> v1LayH{{-1, -1, -1, -1}}, v1LayGrid{{-1, -1, -1, -1}};
    std::array<std::string, 4> v1Key;
#endif
    void runSound(const std::vector<float>& col, bool kick, double beat){
#ifdef FORMS_HAS_SC
        if(!sound) return;
        sound->setKickFolder(ofToDataPath("Forms/samples/KICKS", true));
        sound->poll();
        bool on = soundMode.get() == 0 && E().ready() && play.get();
        int trans; std::vector<float> scale; v1Harmony(trans, scale);
        std::vector<unsigned char> px;
        for(int l = 0; l < forms::SOUND_LAYERS && l < 4; l++){
            auto& L = sframe.layers[l];
            const auto& c = layerColumn(l, col);
            int h = (int)c.size();
            L.active = on && h > 1;
            L.level = 1;
            L.send = ofClamp(sndReverb, 0, 1);
            if(!L.active){ L.amps.clear(); continue; }
            auto& lay = v1Lay[(size_t)l];
            if(lay.rel.empty() || v1LayH[(size_t)l] != h || v1LayGrid[(size_t)l] != sndGrid){
                v1LayH[(size_t)l] = h; v1LayGrid[(size_t)l] = sndGrid;
                lay = forms::partialLayout(h, sndGrid, h);
                L.pitch.clear();
            }
            px.assign((size_t)h * 4, 0);
            for(int y = 0; y < h; y++){ unsigned char b = (unsigned char)std::lround(std::clamp(c[(size_t)y], 0.f, 1.f) * 255); px[(size_t)y * 4] = px[(size_t)y * 4 + 1] = px[(size_t)y * 4 + 2] = b; }
            forms::columnAmps(px.data(), h, 1.0f, lay, L.amps);
            std::string key = ofToString(sndRoot) + "/" + ofToString(trans) + "/" + ofToString(sndQuant) + "/" + ofToString(scale.size());
            for(float s : scale) key += "," + ofToString(s);
            if(L.pitch.empty() || key != v1Key[(size_t)l] || L.arraysVersion != soundArraysVersion){
                v1Key[(size_t)l] = key;
                L.pitch = forms::partialPitches(sndRoot, lay, trans, scale, sndQuant);
                forms::partialGains(L.pitch, l, ssettings, L.gl, L.gr);
                L.duckw = forms::duckWeights(L.pitch, ssettings);
                L.arraysVersion = soundArraysVersion;
            }
        }
        sframe.bend = -ssettings.detune * 0.5f;   // detuned pair centred on the pitch (no clock bend in v2)
        sframe.detune = ssettings.detune;
        sframe.bankGain = ssettings.bankGain;
        sframe.lag = ssettings.lag;
        sframe.kickAmp = 0.16f;
        sframe.fxGain = 2;
        sframe.volume = volume.get();
        int ki = kickIndex(sound->kicks());
        sframe.kickSample = ki;
        if(kick && ki >= 0){ sframe.kickCount++; sframe.kickRate = std::pow(2.0f, (trans + ssettings.kickTune) / 12.0f); }
        sound->send(sframe);
#else
        (void)col; (void)kick; (void)beat;
#endif
    }

    // ---------------------------------------------------------------- sound (v2 engine, own audio output)
    // a layer's v1-match mapping from the scene keys and the harmony at the playhead
    forms2::snd::V1Layer layerCfg(int l, int h) const {
        forms2::snd::V1Layer c = forms2::snd::SoundSettings{}.layers[l];   // v1 defaults (pan, spread, level)
        c.root = sndRoot; c.notegrid = sndGrid; c.imageH = h;
        std::vector<float> sc; v1Harmony(c.trans, sc);
        c.scaleN = std::min<int>(12, (int)sc.size());
        for(int i = 0; i < c.scaleN; i++) c.scale[i] = sc[(size_t)i];
        c.scaleQuant = sndQuant;
        c.send = ofClamp(sndReverb, 0, 1);
        return c;
    }
    static bool sameLayer(const forms2::snd::V1Layer& a, const forms2::snd::V1Layer& b){
        if(a.on != b.on || a.notegrid != b.notegrid || a.root != b.root || a.imageH != b.imageH || a.trans != b.trans || a.scaleQuant != b.scaleQuant
           || a.scaleN != b.scaleN || a.level != b.level || a.send != b.send || a.pan != b.pan || a.spread != b.spread) return false;
        for(int i = 0; i < a.scaleN; i++) if(a.scale[i] != b.scale[i]) return false;
        return true;
    }
    int columnH(const std::vector<float>& col) const {
        for(auto& c : layerCols) if(!c.empty()) return (int)c.size();
        return std::max(1, (int)col.size());
    }
    forms2::snd::SoundEngine* makeSoundEngine(int h){
        auto* e = new forms2::snd::SoundEngine();      // defaults: the v1 match
        if(sndIfft){ e->s.engine = forms2::snd::EngineKind::Ifft; e->s.v1Match = false; }
        e->s.rows = h;
        e->s.pitchBalance = sndBalance;
        e->s.fxGain = 2;                                // SpectrumGain
        e->s.kickAmp = 0.16f;                           // kickVol
        engKickLayer = kickLayerOf();
        e->s.kickLayer = engKickLayer;
        for(int l = 0; l < forms2::snd::v1::kLayers; l++){ sentLayer[(size_t)l] = layerCfg(l, h); e->s.layers[l] = sentLayer[(size_t)l]; }
        if(E().ready()){ auto ha = E().harmonyAt(nowBeat - curOrigin); if(ha.scale) e->setScale(*ha.scale); }
        kickLoaded.clear();                              // the sample again for this engine
        return e;
    }
    // the scene's kick sample to the running engine (once per engine / file)
    void sendKickSample(forms2::audio::Bridge& br){
        auto files = kickFiles();
        int ki = kickIndex(files);
        if(ki < 0 || files[(size_t)ki] == kickLoaded) return;
        std::vector<float> L, R; double sr = 48000;
        kickLoaded = files[(size_t)ki];
        if(forms2::snd::v1::loadWav(kickLoaded, L, R, sr)) br.setKickSample(std::move(L), std::move(R), sr);
    }
    void runSoundV2(const std::vector<float>& col, double beat, bool on){
        if(!on){
            if(audio){ audio->close(); audio.reset(); }
            audioFailed = false;
            return;
        }
        if(audioReopen && audio){ audio->close(); audio.reset(); audioFailed = false; }
        audioReopen = false;
        int H = columnH(col);
        if(!audio && !audioFailed){
            audio = std::make_unique<forms2::audio::AudioOut>();
            audio->bridge.installEngine(makeSoundEngine(H));
            if(!audio->open(sndDevice)){
                audioFailed = true;
                performStatus = audio->status;
                audio.reset();
#ifdef FORMS_HAS_SC
                soundMode = 0;           // no output device: back to the v1 bank
#endif
                return;
            }
            audioSeek = true;
        }
        if(!audio) return;
        auto& br = audio->bridge;
        br.collect();
        br.volume = volume.get();
        br.pitchBalance = sndBalance;
        br.bend = 0.f;                   // no clock pitch bend in v2
        br.ampMul = 1.f;                 // no head opacity / post levels in v2
        // a jump, a new scene's kick layer or another column height: a fresh engine
        if(audioSeek || engKickLayer < 0 || engKickLayer != kickLayerOf() || sentLayer[0].imageH != H){
            audioSeek = false;
            br.installEngine(makeSoundEngine(H));
            evSentUntil = beat;
        }
        sendKickSample(br);
        // harmony / settings changes: per layer, keeps the phases
        for(int l = 0; l < 4; l++){
            auto c = layerCfg(l, H);
            if(!sameLayer(c, sentLayer[(size_t)l]) && br.setLayer(l, c)) sentLayer[(size_t)l] = c;
        }
        bool playing = play.get() && E().ready();
        double rate = bpm.get() / 60.0 * (usingTransport ? 1.0 : speed.get());
        double frameBeats = beat > prevBeat && beat - prevBeat < 1 ? beat - prevBeat : rate / 60.0;
        // every layer's full column, flipped so row 0 = bottom (v1 sums one bank per layer)
        static std::vector<float> rows;
        for(int l = 0; l < 4; l++){
            const auto& c = layerColumn(l, col);
            int h = (int)c.size();
            if(h < 1) continue;
            rows.assign((size_t)h, 0.f);
            if(playing) for(int r = 0; r < h; r++) rows[(size_t)r] = c[(size_t)(h - 1 - r)];
            br.pushColumn(beat, rows.data(), h, l);
        }
        br.publishClock(beat - frameBeats, rate / br.sampleRate, playing && rate > 0);
        // events near the playhead (drum / note voices), each sent once, in global beats
        double until = beat + 0.25;
        if(playing && until > evSentUntil){
            for(int i = 0; i < perf.slotCount(); i++){
                forms2::Engine& en = perf.engine(i);
                double org = perf.slot(i).origin, g = perf.slotGain(i);
                if(g <= 0.01) continue;
                uint64_t salt = slotSalt(org);
                for(int l = 0; l < 4; l++)
                    for(auto& s : en.events(l)){
                        double st = s.e.start + org;
                        if(st >= until) break;
                        if(st < evSentUntil) continue;
                        bool drum = s.e.kind == forms2::EventKind::Hit || s.e.transient;
                        bool note = s.e.kind == forms2::EventKind::Note && s.e.param("noteVoice", 0) > 0.5;
                        if(!(drum ? sndDrums : note) || en.muted(s.e.instance)) continue;
                        forms2::Event e = s.e; e.id ^= salt; e.start = st; e.level *= g;
                        br.pushEvent(e);
                    }
            }
            evSentUntil = until;
        }
        meterL = std::max(br.peakL.load(), meterL * 0.9f);
        meterR = std::max(br.peakR.load(), meterR * 0.9f);
    }

    // ---------------------------------------------------------------- AI assistant (ROADMAP 5.11)
    ofxOceanodeAIAssistant ai;
    int aiModeIdx = 3;                       // forms2::AssistMode
    char aiPrompt[2048] = {};
    std::future<ofxOceanodeAIAssistant::TextResult> aiFuture;
    bool aiBusy = false, aiRetried = false;
    forms2::AssistRequest aiReq;
    std::string aiTargetId;                  // Modify of a selected instance: its id
    std::string aiStatus, aiExplanation, aiLastAnswer;
    std::vector<std::string> aiProblems;
    bool aiKey = false;

    static bool aiKeyPresent(){
        std::ifstream f(ofToDataPath("config/requesty.md"));
        std::string k = ofxOceanodeRequesty::parseKey(f);
        if(k.empty()){ const char* e = std::getenv("REQUESTY_API_KEY"); if(e) k = ofxOceanodeRequesty::trim(e); }
        return !k.empty();
    }

    void setupAssistant(){
        ofxOceanodeAIAssistant::Spec s;
        s.title = "Forms GL2 Box assistant (Requesty)";
        s.subtitle = [this](){ return std::string("Mode: ") + forms2::assistModeName((forms2::AssistMode)aiModeIdx) + " (choose it in the node window)"; };
        s.generateHint = "Describe the instrument, kit or scene, or the change you want.";
        s.fixHint = "Repairs the current scene (or the selected instance).";
        s.generateButton = "Send";
        s.fixButton = "Fix";
        s.generateModeLabel = "Selected mode";
        s.fixModeLabel = "Fix current";
        s.pendingHint = "The answer is checked by the v2 validator and auditioned: Keep or Discard it in the node window.";
        s.readyMessage = "Answer received: see the Forms GL2 Box window";
        s.systemPrompt = [](){ return forms2::systemPrompt(); };
        s.buildPrompt = [this](const std::string& user, bool fix){
            aiReq = makeRequest(fix ? forms2::AssistMode::Fix : (forms2::AssistMode)aiModeIdx, user);
            aiRetried = false;
            return aiReq.user;
        };
        s.apply = [this](const std::string& answer){
            handleAnswer(answer);
            return std::string();
        };
        ai.setup(s);
        aiKey = aiKeyPresent();
    }

    // the object the request is about: the selected instance as an instrument preset (Modify /
    // Explain), else the scene text
    forms2::AssistRequest makeRequest(forms2::AssistMode mode, const std::string& user){
        using M = forms2::AssistMode;
        aiTargetId.clear();
        std::string cur;
        if(mode == M::Modify || mode == M::Fix || mode == M::Explain){
            const forms2::Instance* in = selectedInstance();
            if(in && mode != M::Fix){
                aiTargetId = in->id;
                cur = forms2::toJson(forms2::presetFromInstance(*in, nullptr, in->id)).dump(2);
            } else cur = currentText;
        }
        return forms2::buildRequest(mode, user, cur);
    }
    const forms2::Instance* selectedInstance() const {
        if(!E().ready() || selected < 0 || selected >= (int)E().scene().instances.size()) return nullptr;
        return &E().scene().instances[(size_t)selected];
    }

    std::vector<std::string> aiRejected;     // shown by the v2 GUI (not hidden)
    void sendRequest(const forms2::AssistRequest& r, bool retry){
        if(!retry) aiRejected.clear();
        aiReq = r;
        aiRetried = retry;
        try {
            aiFuture = ai.generateText(r.system, r.user, r.expectsJson() ? 16000 : 1500);
            aiBusy = true;
            aiStatus = retry ? "The answer had problems; asking once more..." : "Waiting for the answer...";
        } catch(const std::exception& e){
            aiBusy = false; aiStatus = e.what();
        }
    }
    void pollAssistant(){
        if(!aiBusy || !aiFuture.valid()) return;
        if(aiFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        auto res = aiFuture.get();
        aiBusy = false;
        if(!res.error.empty()){ aiStatus = "Error: " + res.error; return; }
        handleAnswer(res.text);
    }
    void handleAnswer(const std::string& answer){
        aiLastAnswer = answer;
        if(!aiReq.expectsJson()){ aiExplanation = answer; aiStatus = "Explanation received."; return; }
        forms2::json j;
        aiProblems = forms2::checkAnswerText(answer, aiReq.kind, j);
        if(aiProblems.empty()){
            if(auditioning) discardAudition();   // a newer answer replaces the candidate
            aiProblems = applyAnswer(j);
        }
        if(aiProblems.empty()){ aiStatus = "Auditioning the answer (" + aiReq.kind + "): Keep or Discard."; return; }
        if(!aiRetried){
            aiRejected.push_back("First answer rejected: \u201c" + (aiProblems.empty() ? std::string("problems") : aiProblems.front()) + "\u201d \u2014 asked again automatically");
            sendRequest(forms2::retryRequest(aiReq, aiProblems, answer), true); return;
        }
        aiRejected.push_back("Answer rejected again: \u201c" + (aiProblems.empty() ? std::string("problems") : aiProblems.front()) + "\u201d");
        aiStatus = "Not applied: the answer still has " + ofToString(aiProblems.size()) + " problems.";
    }

    // applies a checked answer as one undoable step; returns problems (nothing changed then)
    std::vector<std::string> applyAnswer(const forms2::json& j){
        std::string kind = aiReq.kind;
        try {
            if(kind == "scene") return audition(j.dump(2));
            forms2::json cur = forms2::json::parse(currentText);
            if(!cur.contains("instances") || !cur["instances"].is_array()) cur["instances"] = forms2::json::array();
            if(kind == "kit"){
                forms2::SceneDoc d = forms2::sceneDocFromJson(cur);
                auto added = forms2::dropKit(d, forms2::kitFromJson(j));
                for(auto& id : added) for(auto& in : d.scene.instances) if(in.id == id){
                    forms2::json e = forms2::toJson(in);
                    auto o = d.origins.find(id); if(o != d.origins.end()) e["origin"] = forms2::toJson(o->second);
                    cur["instances"].push_back(e);
                }
                return audition(cur.dump(2));
            }
            if(kind == "instrument"){
                forms2::Preset p = forms2::presetFromJson(j);
                forms2::InstanceOrigin origin;
                if(!aiTargetId.empty()){
                    // the selected instance, edited live (same id, layer and harmony settings)
                    for(auto& e : cur["instances"]) if(e.value("id", std::string()) == aiTargetId){
                        forms2::Instance old = forms2::instanceFromJson(e);
                        forms2::Instance ni = forms2::instanceFromPreset(p, aiTargetId, origin);
                        ni.layer = old.layer; ni.follow = old.follow; ni.pedalDegree = old.pedalDegree;
                        ni.accent = old.accent; ni.swing = old.swing;
                        std::string prev = currentText;
                        bool prevDirty = sceneDirty;
                        e = forms2::toJson(ni);
                        if(!E().ready() || !E().setInstance(ni)) return E().problems().empty() ? std::vector<std::string>{"could not apply the instance"} : E().problems();
                        mods.clear();
                        currentText = cur.dump(2);
                        sceneDirty = true;
                        beginAudition(prev, prevDirty, old);
                        return {};
                    }
                }
                // a new instance on the layer with the fewest instances
                int counts[4] = {0, 0, 0, 0};
                std::set<std::string> ids;
                for(auto& e : cur["instances"]){ counts[std::clamp(e.value("layer", 0), 0, 3)]++; ids.insert(e.value("id", std::string())); }
                int layer = 0; for(int l = 1; l < 4; l++) if(counts[l] < counts[layer]) layer = l;
                std::string base = p.type.empty() ? "inst" : p.type, id = base; int n = 2;
                while(ids.count(id)) id = base + "_" + ofToString(n++);
                forms2::Instance ni = forms2::instanceFromPreset(p, id, origin);
                ni.layer = layer;
                cur["instances"].push_back(forms2::toJson(ni));
                return audition(cur.dump(2));
            }
        } catch(const std::exception& e){
            return {std::string("could not apply: ") + e.what()};
        }
        return {"unknown answer kind '" + kind + "'"};
    }
    // ---- audition: the candidate plays; Keep commits it (one undo step), Discard restores
    bool auditioning = false, auditionPrevDirty = false;
    std::string auditionPrev, auditionKind;
    std::optional<forms2::Instance> auditionOldInst;   // a live instance edit: the instance before it

    // loads a candidate scene for audition; on failure the previous scene comes back
    std::vector<std::string> audition(const std::string& text){
        std::string prev = currentText;
        bool prevDirty = sceneDirty;
        macrosFromScene = false;
        if(loadScene(text, false)){ sceneDirty = true; beginAudition(prev, prevDirty, std::nullopt); return {}; }
        auto probs = loadErrors;
        macrosFromScene = false;
        loadScene(prev, false);
        sceneDirty = prevDirty;
        return probs.empty() ? std::vector<std::string>{"the scene did not load"} : probs;
    }
    void beginAudition(const std::string& prev, bool prevDirty, std::optional<forms2::Instance> oldInst){
        auditioning = true; auditionPrev = prev; auditionPrevDirty = prevDirty;
        auditionKind = aiReq.kind; auditionOldInst = std::move(oldInst);
    }
    void endAudition(){ auditioning = false; auditionPrev.clear(); auditionOldInst.reset(); abOld = false; abCandidate.clear(); }
    // A / B: hear the scene before the candidate, then the candidate again
    bool abOld = false;
    std::string abCandidate;
    void auditionAB(){
        if(!auditioning) return;
        macrosFromScene = false;
        if(!abOld){ abCandidate = currentText; loadScene(auditionPrev, false); abOld = true; }
        else { loadScene(abCandidate, false); abOld = false; }
        sceneDirty = true;
    }
    void auditionRestoreCandidate(){
        if(!abOld) return;
        macrosFromScene = false; loadScene(abCandidate, false); abOld = false;
    }
    void keepAudition(){
        if(!auditioning) return;
        auditionRestoreCandidate();
        v2gui.pushUndo(auditionPrev, "Assistant: " + auditionKind); sceneDirty = true;
        aiStatus = "Kept (" + auditionKind + "). Undo to go back.";
        endAudition();
    }
    void discardAudition(){
        if(!auditioning) return;
        auditionRestoreCandidate();
        std::string prev = auditionPrev;
        bool restored = false;
        if(auditionOldInst && E().ready() && E().setInstance(*auditionOldInst)){ currentText = prev; mods.clear(); restored = true; }
        if(!restored){ macrosFromScene = false; loadScene(prev, false); }
        sceneDirty = auditionPrevDirty;
        aiStatus = "Discarded: back to the previous scene.";
        endAudition();
    }
    void undo(){ fillHost(); v2gui.undo(); }
    void redo(){ fillHost(); v2gui.redo(); }

    // "Save scene as": next to the scene file, never over an existing file (the example included)
    void saveSceneAs(){
        namespace fs = std::filesystem;
        std::string name = saveName;
        for(auto& c : name) if(c == '/' || c == '\\' || c == ':') c = '_';
        if(name.empty()) name = "scene";
        if(ofToLower(ofFilePath::getFileExt(name)) == "json") name = name.substr(0, name.size() - 5);
        fs::path dir = fs::path(sceneFile.get()).parent_path();
        if(dir.empty()) dir = fs::path(exampleScenePath()).parent_path();
        fs::path p = dir / (name + ".json");
        int n = 2;
        while(fs::exists(p)) p = dir / (name + "_" + ofToString(n++) + ".json");
        std::string text = currentText;
        try { forms2::json j = forms2::json::parse(currentText); if(E().ready()) j["name"] = j.value("name", name); text = j.dump(2); } catch(...){}
        std::ofstream f(p.string(), std::ios::binary);
        if(!f){ fileStatus = "could not write " + p.string(); return; }
        f << text;
        f.close();
        suppressFileReload = true;
        sceneFile = p.string();
        suppressFileReload = false;
        sceneDirty = false;
        fileStatus = "saved " + p.filename().string();
    }

    // ---------------------------------------------------------------- formsGL2Window
    static void sectionTitle(const char* t){
        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1.f, 1.f));
        ImGui::TextUnformatted(t);
        ImGui::PopStyleColor();
        ImGui::Separator();
    }
    static bool toggleButton(const char* label, bool on, ImVec4 onColour){
        if(on){ ImGui::PushStyleColor(ImGuiCol_Button, onColour); ImGui::PushStyleColor(ImGuiCol_ButtonHovered, onColour); }
        bool c = ImGui::SmallButton(label);
        if(on) ImGui::PopStyleColor(2);
        return c;
    }

    void drawWindow(){
        float z = ofxOceanodeShared::getZoomLevel();
        ImGui::SetNextWindowSize(ImVec2(1440 * z, 900 * z), ImGuiCond_FirstUseEver);
        bool open = showWindow.get();
        if(!ImGui::Begin((getWindowTitle() + "###formsGL2_" + ofToString(getNumIdentifier())).c_str(), &open)){
            ImGui::End();
            if(!open) showWindow = false;
            return;
        }
        if(!open){ showWindow = false; ImGui::End(); return; }
        if(!v2gui.classic){
            statsOpen = false;               // set again if the Stats section is drawn in Setup
            fillHost();
            // the assistant's Modify / Explain act on the GUI's selected instrument
            selected = -1;
            if(E().ready()){
                auto& insts = E().scene().instances;
                for(size_t i = 0; i < insts.size(); i++) if(insts[i].id == v2gui.cx.sel.instance) selected = (int)i;
            }
            v2gui.draw(v2host, ofGetElapsedTimef());
            ImGui::End();
            return;
        }
        if(ImGui::Button("New view")) v2gui.classic = false;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Back to the v2 GUI (arrangement, instrument, collection, modulation, perform).");
        drawAuditionBanner(z);

        // ---- scene
        sectionTitle("Scene");
        ImGui::Text("%s%s", loadedName.empty() ? "(no scene)" : loadedName.c_str(), sceneDirty ? "  (edited, not saved)" : "");
        ImGui::TextDisabled("%s", sceneFile.get().c_str());
        if(!fileStatus.empty()) ImGui::TextDisabled("%s", fileStatus.c_str());
        for(auto& p : loadErrors) ImGui::TextColored(ImVec4(1, 0.45f, 0.4f, 1), "problem: %s", p.c_str());
        for(auto& w : E().warnings()) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "warning: %s", w.c_str());
        if(ImGui::Button("Reload")){ pendingReload = true; pendingRestart = true; }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(160 * z);
        ImGui::InputText("##saveas", saveName, sizeof saveName);
        ImGui::SameLine();
        if(ImGui::Button("Save scene as...")) saveSceneAs();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Writes <name>.json next to the scene file (never over an existing file).");

        // ---- transport
        sectionTitle("Transport");
        bool p = play.get();
        if(ImGui::Button(p ? "Pause" : "Play", ImVec2(70 * z, 0))) play = !p;
        ImGui::SameLine();
        float b = bpm.get();
        ImGui::SetNextItemWidth(110 * z);
        if(ImGui::DragFloat("BPM", &b, 0.1f, 1, 999, "%.1f")) bpm = b;
        ImGui::SameLine();
        if(!usingTransport){
            float s = speed.get();
            ImGui::SetNextItemWidth(90 * z);
            if(ImGui::DragFloat("Speed", &s, 0.01f, -4, 4, "%.2f")) speed = s;
            ImGui::SameLine();
            if(ImGui::SmallButton("Restart")) pendingReload = pendingRestart = true;
        }
        int bar = E().ready() ? E().harmony().barIndex(nowBeat - curOrigin) : 0;
        ImGui::Text("beat %.2f   bar %d   clock: %s", nowBeat, bar + 1,
                    usingTransport ? "Oceanode transport" : (followTransport.get() ? "internal (no transport)" : "internal"));

        // ---- instances
        sectionTitle("Instances");
        if(E().ready() && ImGui::BeginTable("##inst", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)){
            ImGui::TableSetupColumn("id", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("type"); ImGui::TableSetupColumn("layer");
            ImGui::TableSetupColumn("play"); ImGui::TableSetupColumn("events"); ImGui::TableSetupColumn("");
            ImGui::TableHeadersRow();
            auto& insts = E().scene().instances;
            for(int i = 0; i < (int)insts.size(); i++){
                const std::string id = insts[(size_t)i].id;
                ImGui::PushID(i);
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                if(ImGui::Selectable(id.c_str(), selected == i, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) selected = selected == i ? -1 : i;
                ImGui::TableNextColumn(); ImGui::TextUnformatted(insts[(size_t)i].type.c_str());
                ImGui::TableNextColumn(); ImGui::Text("%d", insts[(size_t)i].layer + 1);
                ImGui::TableNextColumn();
                bool m = muteMap[id], s = soloMap[id], l = E().locked(id);
                if(toggleButton("M", m, ImVec4(0.8f, 0.3f, 0.2f, 1))){ muteMap[id] = !m; perf.mute(id, !m); mods.clear(); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Mute (from the next phrase)");
                ImGui::SameLine();
                if(toggleButton("S", s, ImVec4(0.8f, 0.7f, 0.1f, 1))){ soloMap[id] = !s; perf.solo(id, !s); mods.clear(); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Solo");
                ImGui::SameLine();
                if(toggleButton("L", l, ImVec4(0.2f, 0.5f, 0.9f, 1))){ perf.lock(id, !l); mods.clear(); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Lock: the next phrases repeat this one");
                ImGui::SameLine();
                if(ImGui::SmallButton("R")){ perf.reroll(id); mods.clear(); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Reroll: a new variation at the next phrase");
                ImGui::TableNextColumn(); ImGui::Text("%d", (int)E().eventsOf(id).size());
                ImGui::TableNextColumn(); ImGui::TextDisabled("%s", E().muted(id) ? "muted" : "");
                ImGui::PopID();
            }
            ImGui::EndTable();
            ImGui::TextDisabled(selected >= 0 ? "Selected: the assistant's Modify / Explain act on it." : "Click a row to select an instance for the assistant.");
        }

        // ---- macros
        sectionTitle("Macros");
        for(int i = 0; i < 8; i++){
            ImGui::PushID(100 + i);
            float v = macros[i].get();
            ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x * 0.5f - 60 * z);
            if(ImGui::SliderFloat(("macro " + ofToString(i + 1)).c_str(), &v, 0, 1)) macros[i] = v;
            if(i % 2 == 0) ImGui::SameLine();
            ImGui::PopID();
        }

        // ---- harmony
        sectionTitle("Harmony");
        if(E().ready()){
            double lb = nowBeat - curOrigin;
            auto ha = E().harmonyAt(lb);
            const auto& sec = E().harmony().sectionDef(lb);
            const auto& m = E().meterAt(lb);
            ImGui::Text("section %s   meter %d/%d   root degree %d   chord %s", sec.name.empty() ? "-" : sec.name.c_str(), m.num, m.den, ha.root + 1, forms2::colourName(ha.colour));
            if(ImGui::Checkbox("Hold chord", &holdOn)){ perf.hold(holdOn); mods.clear(); }
            ImGui::SameLine();
            ImGui::TextUnformatted("Jump to:");
            int n = ha.scale ? ha.scale->size() : 7;
            for(int d = 0; d < n; d++){
                ImGui::SameLine();
                ImGui::PushID(200 + d);
                if(toggleButton(ofToString(d + 1).c_str(), jumpDeg == d, ImVec4(0.3f, 0.6f, 0.3f, 1))){
                    jumpDeg = jumpDeg == d ? -1000 : d;
                    perf.jump(jumpDeg); mods.clear();
                }
                ImGui::PopID();
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Clear")){ holdOn = false; jumpDeg = -1000; perf.clearHarmony(); mods.clear(); }
        }

        draw3D(z);
        drawPaperPost(z);
        drawStats(z);

        drawPerform(z);
        drawSound(z);
        drawAssistant(z);
        ImGui::End();
    }

    // ---- 3D view (5.14)
    void draw3D(float z){
        // ---- 3D view (5.14)
        ImGui::Spacing();
        if(ImGui::CollapsingHeader("3D")){
            auto sl = [&](const char* n, double& v, float mn, float mx){
                float f = (float)v; ImGui::SetNextItemWidth(160 * z);
                if(ImGui::SliderFloat(n, &f, mn, mx, "%.2f")) v = f;
            };
            ImGui::Checkbox("perspective", &td3.perspective);
            sl("amount", td3.amount, 0, 8); sl("vanishing x", td3.vanishX, 0, 1); sl("vanishing y", td3.vanishY, 0, 1);
            sl("fog", td3.fog, 0, 4); sl("depth of field (CPU)", td3.dof, 0, 1); sl("camera dolly", td3.camera, -4, 4);
            sl("look-ahead", td3.lookAhead, 1, 8);
            for(int l = 0; l < 4; l++){
                ImGui::PushID(3300 + l);
                bool b = td3.layerPersp[(size_t)l];
                if(ImGui::Checkbox(("layer " + ofToString(l)).c_str(), &b)) td3.layerPersp[(size_t)l] = b;
                ImGui::SameLine(); sl("plane z", td3.planeZ[(size_t)l], 0, 8);
                ImGui::SameLine(); sl("camera", td3.layerCamera[(size_t)l], -4, 4);
                ImGui::PopID();
            }
            if(ImGui::SmallButton("Flat (reset)")) td3 = forms2::draw::ThreeD{};
            ImGui::TextDisabled("Paths are instance / event values (pathKind, pathAngle, pathCurve, pathOrigin, pathTurns, pathSpeed).");
            ImGui::TextDisabled("Display only: the sound reads the flat score.");
        }

    }
    // ---- light backgrounds (5.18) and the output post (v1's Post)
    void drawPaperPost(float z){
        ImGui::Spacing();
        if(ImGui::CollapsingHeader("Layers: reading / blend")){
            static const char* rd[] = {"brightness", "coverage", "dark ink"};
            static const char* bl[] = {"(layer)", "lighten", "add", "normal", "multiply", "darken"};
            for(int l = 0; l < 4; l++){
                ImGui::PushID(l + 900);
                ImGui::Text("L%d", l + 1); ImGui::SameLine();
                ImGui::SetNextItemWidth(110 * z); ImGui::Combo("reading", &paper.reading[(size_t)l], rd, 3);
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("What sounds: brightness (v1), coverage (opacity, any colour), dark ink (darker = louder).\nTransparent is always silent; the background never sounds.");
                ImGui::SameLine();
                int b = paper.blend[(size_t)l].empty() ? 0 : forms2::draw::blendIndex(paper.blend[(size_t)l]) + 1;
                ImGui::SetNextItemWidth(100 * z);
                if(ImGui::Combo("blend", &b, bl, 6)) paper.blend[(size_t)l] = b == 0 ? "" : bl[b];
                ImGui::SameLine();
                bool a = paper.alphaFromBrightness[(size_t)l];
                if(ImGui::Checkbox("alpha from brightness", &a)) paper.alphaFromBrightness[(size_t)l] = a;
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("As v1 stores its images: alpha = brightness, colour at full strength.");
                ImGui::PopID();
            }
            ImGui::Checkbox("dark-ink palette", &paper.darkInk);
            ImGui::SameLine();
            if(ImGui::SmallButton("Ink on paper")){
                paper.darkInk = true; bgColour = ofFloatColor(1, 1, 1, 1);
                for(int l = 0; l < 4; l++){ paper.reading[(size_t)l] = forms2::draw::ReadDarkInk; paper.blend[(size_t)l] = "multiply"; }
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Reset##paper")) paper = forms2::draw::Paper{};
            ImGui::TextDisabled("Saved with the patch; a scene's \"paper\" object sets them on load.");
        }
        if(ImGui::CollapsingHeader("Post (output)")){
            auto f = [&](const char* n, float& v, float mn, float mx){ ImGui::SetNextItemWidth(160 * z); ImGui::SliderFloat(n, &v, mn, mx, "%.3f"); };
            ImGui::Checkbox("post", &post.enabled); ImGui::SameLine(); ImGui::Checkbox("stretch", &post.stretch); ImGui::SameLine(); ImGui::Checkbox("line", &post.line);
            f("hue", post.hue, 0, 1); f("saturation", post.sat, 0, 2); f("value", post.val, 0, 2);
            f("levels min", post.levelMin, 0, 1); f("levels", post.levelMax, 0.001f, 1); f("gamma", post.gamma, 0.1f, 5);
            f("levels low", post.low, 0, 1); f("levels high", post.high, 0, 1);
            f("line width", post.lineWidth, 0, 64); f("line soft", post.lineSoft, 0, 64); f("line soft v", post.lineSoftV, 0, 540);
            f("line alpha", post.lineAlpha, 0, 1); f("line ease", post.lineEase, 0, 2); f("line offset", post.lineOffset, -256, 256);
            ImGui::SetNextItemWidth(160 * z); ImGui::SliderInt("line curve", &post.lineEaseCurve, 0, 2);
            float lc[3] = {post.lineColor.r, post.lineColor.g, post.lineColor.b};
            if(ImGui::ColorEdit3("line colour", lc, ImGuiColorEditFlags_NoInputs)) post.lineColor = ofFloatColor(lc[0], lc[1], lc[2]);
            static const char* en[4] = {"left", "right", "top", "bottom"};
            for(int e = 0; e < 4; e++){
                ImGui::PushID(e + 950);
                ImGui::SetNextItemWidth(120 * z); ImGui::SliderFloat((std::string("feather ") + en[e]).c_str(), &post.edges.width[e], 0, 0.5f, "%.3f");
                ImGui::SameLine(); ImGui::SetNextItemWidth(90 * z); ImGui::SliderFloat("curve", &post.edges.curve[e], 0.1f, 5, "%.2f");
                ImGui::PopID();
            }
            if(ImGui::SmallButton("Defaults (patch 8)")){ forms::Post d; copyPostSettings(d, post); }
            ImGui::TextDisabled("Display only: the sound reads the flat score before all of this.");
        }
    }
    static void copyPostSettings(const forms::Post& a, forms::Post& b){
        b.enabled = a.enabled; b.stretch = a.stretch; b.hue = a.hue; b.sat = a.sat; b.val = a.val; b.useOpacity = a.useOpacity;
        b.levelMin = a.levelMin; b.levelMax = a.levelMax; b.gamma = a.gamma; b.low = a.low; b.high = a.high;
        b.line = a.line; b.lineWidth = a.lineWidth; b.lineSoft = a.lineSoft; b.lineSoftV = a.lineSoftV; b.lineAlpha = a.lineAlpha;
        b.lineColor = a.lineColor; b.lineEase = a.lineEase; b.lineEaseCurve = a.lineEaseCurve; b.lineOffset = a.lineOffset; b.edges = a.edges;
    }
    forms2::json postToJson() const {
        const forms::Post& p = post;
        return {{"enabled", p.enabled}, {"stretch", p.stretch}, {"hue", p.hue}, {"sat", p.sat}, {"val", p.val}, {"levelMin", p.levelMin}, {"levelMax", p.levelMax},
                {"gamma", p.gamma}, {"low", p.low}, {"high", p.high}, {"line", p.line}, {"lineWidth", p.lineWidth}, {"lineSoft", p.lineSoft}, {"lineSoftV", p.lineSoftV},
                {"lineAlpha", p.lineAlpha}, {"lineColor", {p.lineColor.r, p.lineColor.g, p.lineColor.b}}, {"lineEase", p.lineEase}, {"lineCurve", p.lineEaseCurve},
                {"lineOffset", p.lineOffset}, {"featherW", {p.edges.width[0], p.edges.width[1], p.edges.width[2], p.edges.width[3]}},
                {"featherK", {p.edges.curve[0], p.edges.curve[1], p.edges.curve[2], p.edges.curve[3]}}};
    }
    void postFromJson(const forms2::json& j){
        if(!j.is_object()) return;
        forms::Post& p = post;
        auto b = [&](const char* k, bool& v){ if(j.contains(k) && j[k].is_boolean()) v = j[k].get<bool>(); };
        auto f = [&](const char* k, float& v){ if(j.contains(k) && j[k].is_number()) v = j[k].get<float>(); };
        b("enabled", p.enabled); b("stretch", p.stretch); b("line", p.line);
        f("hue", p.hue); f("sat", p.sat); f("val", p.val); f("levelMin", p.levelMin); f("levelMax", p.levelMax); f("gamma", p.gamma); f("low", p.low); f("high", p.high);
        f("lineWidth", p.lineWidth); f("lineSoft", p.lineSoft); f("lineSoftV", p.lineSoftV); f("lineAlpha", p.lineAlpha); f("lineEase", p.lineEase); f("lineOffset", p.lineOffset);
        if(j.contains("lineCurve") && j["lineCurve"].is_number()) p.lineEaseCurve = j["lineCurve"].get<int>();
        if(j.contains("lineColor") && j["lineColor"].is_array() && j["lineColor"].size() == 3) p.lineColor = ofFloatColor(j["lineColor"][0].get<float>(), j["lineColor"][1].get<float>(), j["lineColor"][2].get<float>());
        for(int e = 0; e < 4; e++){
            if(j.contains("featherW") && j["featherW"].is_array() && j["featherW"].size() == 4) p.edges.width[e] = j["featherW"][(size_t)e].get<float>();
            if(j.contains("featherK") && j["featherK"].is_array() && j["featherK"].size() == 4) p.edges.curve[e] = j["featherK"][(size_t)e].get<float>();
        }
    }
    // ---- stats, renderer, Compare GPU/CPU
    void drawStats(float z){
        // ---- stats
        ImGui::Spacing();
        statsOpen = ImGui::CollapsingHeader("Stats");
        if(statsOpen && E().ready()){
            auto& st = E().stats();
            ImGui::Text("planned phrases %ld (re-planned %ld)", st.plannedPhrases, st.replannedPhrases);
            ImGui::Text("events stored per layer %d / %d / %d / %d", st.liveEvents[0], st.liveEvents[1], st.liveEvents[2], st.liveEvents[3]);
            ImGui::Text("events visible per layer %d / %d / %d / %d   drawn %d", visPerLayer[0], visPerLayer[1], visPerLayer[2], visPerLayer[3], drawnEvents);
            if(!cpuRenderer.get()) ImGui::Text("effect passes this frame %ld (layer-bus and earlier effects batched)", gl.effectPasses());
            if(ImGui::BeginTable("##timing", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)){
                ImGui::TableSetupColumn("2 s window", ImGuiTableColumnFlags_WidthStretch); ImGui::TableSetupColumn("avg"); ImGui::TableSetupColumn("max");
                ImGui::TableHeadersRow();
                auto row = [&](const char* n, int k, const char* unit = "ms"){
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::TextUnformatted(n);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f %s", timing[k].avg(), unit);
                    ImGui::TableNextColumn(); ImGui::Text("%.2f %s", timing[k].max(), unit);
                };
                row("performer (queue, timeline, planning)", T_PERF);
                row("engine update (views)", T_ENGINE);
                row("collect visible events", T_VISIBLE);
                row("prepare events (copy, modulation fns)", T_PREP);
                if(!cpuRenderer.get()){
                    row("GPU draw, CPU side (submit)", T_GPU_CPU);
                    if(glQueryOk){
                        row("GPU time, all passes (timer queries)", T_GPU);
                        row("  GPU layers (events, effects, groups)", T_G_LAYERS);
                        row("  GPU covers (masks)", T_G_COVERS);
                        row("  GPU composite", T_G_COMP);
                        if(td3.any() || scene3D) row("  GPU flat score pass (3D)", T_G_FLAT);
                    }
                    row("column readback", T_COLUMN);
                } else row("CPU renderer", T_CPUR);
                row("sound (update side)", T_SOUND);
                row("audio callback load", T_AUDIO, "%");
                row("total frame", T_TOTAL);
                ImGui::EndTable();
            }
            double avg = timing[T_TOTAL].avg();
            ImGui::Text("%.1f fps   budget 16.7 ms: %s", avg > 0 ? 1000.0 / avg : 0.0, avg <= 16.7 ? "ok" : "over");
            ImGui::PlotLines("##fh", frameHist.data(), (int)frameHist.size(), histPos, "frame ms (0-33)", 0, 33.4f, ImVec2(-1, 50 * z));
            if(!glQueryOk) ImGui::TextDisabled("GL timer queries unavailable: CPU-side timing only.");
            ImGui::Text("planning %.2f ms (max %.2f)   render %.2f ms   node %.2f ms", st.lastPlanMs, st.maxPlanMs, renderMs, frameMs);
        }
        {   // background colour of the shown output (never sounds)
            ofFloatColor bc = bgColour.get(); float c3[3] = {bc.r, bc.g, bc.b};
            if(ImGui::ColorEdit3("Background", c3, ImGuiColorEditFlags_NoInputs)) bgColour = ofFloatColor(c3[0], c3[1], c3[2], 1);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Shown output only: the sonification reads the score before the background.");
        }
        if(statsOpen){
        ImGui::Text("renderer: %s", cpuRenderer.get() ? "CPU reference (1/4 size)" : "GPU");
        ImGui::SameLine();
        bool c = cpuRenderer.get();
        if(ImGui::Checkbox("CPU", &c)) cpuRenderer = c;
        ImGui::SameLine();
        if(ImGui::SmallButton("Compare GPU/CPU")) pendingCompare = true;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Renders this frame with both renderers at %d x %d and shows the difference (debug).", kCmpW, kCmpH);
        }
        if(!cmpResult.empty()){
            ImGui::TextWrapped("%s", cmpResult.c_str());
            if(cmpTexCpu.isAllocated() && cmpTexGpu.isAllocated()){
                ImVec2 sz(kCmpW * 0.85f * z, kCmpH * 0.85f * z);
                ImGui::BeginGroup(); ImGui::TextDisabled("CPU"); ImGui::Image((ImTextureID)(uintptr_t)cmpTexCpu.getTextureData().textureID, sz); ImGui::EndGroup();
                ImGui::SameLine();
                ImGui::BeginGroup(); ImGui::TextDisabled("GPU"); ImGui::Image((ImTextureID)(uintptr_t)cmpTexGpu.getTextureData().textureID, sz); ImGui::EndGroup();
            }
        }

    }

    // ---------------------------------------------------------------- v2 GUI host (v2Gui.h)
    void fillHost(){
        forms2::gui::GuiHost& h = v2host;
        v2gui.cx.host = &h;
        h.z = ofxOceanodeShared::getZoomLevel();
        h.engine = [this]() -> forms2::Engine* { return E().ready() ? &E() : nullptr; };
        h.perf = &perf;
        h.sceneText = [this](){ return currentText; };
        h.applyInstance = [this](const forms2::Instance& in, const std::string& text) -> std::vector<std::string> {
            if(!E().ready()) return {"no scene"};
            if(!E().setInstance(in)) return E().problems().empty() ? std::vector<std::string>{"could not apply the instance"} : E().problems();
            currentText = text; sceneDirty = true; mods.clear();
            return {};
        };
        h.applyScene = [this](const std::string& text) -> std::vector<std::string> {
            macrosFromScene = false;
            if(!loadScene(text, false)) return loadErrors.empty() ? std::vector<std::string>{"the scene did not load"} : loadErrors;
            sceneDirty = true;
            return {};
        };
        h.nowBeat = nowBeat - curOrigin;
        h.playing = play.get(); h.usingTransport = usingTransport;
        h.bpm = bpm.get(); h.fps = ofGetFrameRate();
        h.togglePlay = [this](){ play = !play.get(); };
        h.restart = [this](){ pendingReload = pendingRestart = true; };
        h.setBpm = [this](float v){ bpm = v; };
        h.sceneName = loadedName; h.sceneFile = sceneFile.get(); h.fileStatus = fileStatus; h.dirty = sceneDirty;
        h.problems = loadErrors; h.warnings = E().warnings();
        h.reload = [this](){ pendingReload = true; pendingRestart = true; };
        h.saveAs = [this](const std::string& n){ std::snprintf(saveName, sizeof saveName, "%s", n.c_str()); saveSceneAs(); };
        h.sceneFolders = [this](){ return sceneFolders(); };
        h.openScene = [this](const std::string& p){
            sceneFile = p;              // its listener reloads (and restarts) with this file
            padsScanned = false;        // Perform's pads: this file's folder
            fileStatus = "opened " + std::filesystem::path(p).filename().string();
        };
        h.macro = [this](int i){ return macros[std::clamp(i, 0, 7)].get(); };
        h.setMacro = [this](int i, float v){ macros[std::clamp(i, 0, 7)] = v; };
        h.isMuted = [this](const std::string& id){ return muteMap[id]; };
        h.isSoloed = [this](const std::string& id){ return soloMap[id]; };
        h.mute = [this](const std::string& id, bool on){ muteMap[id] = on; perf.mute(id, on); mods.clear(); };
        h.solo = [this](const std::string& id, bool on){ soloMap[id] = on; perf.solo(id, on); mods.clear(); };
        h.lock = [this](const std::string& id, bool on){ perf.lock(id, on); mods.clear(); };
        h.reroll = [this](const std::string& id){ perf.reroll(id); mods.clear(); };
        h.held = holdOn; h.jumpDegree = jumpDeg;
        h.hold = [this](bool on){ holdOn = on; perf.hold(on); mods.clear(); };
        h.jump = [this](int d){ jumpDeg = d; perf.jump(d); mods.clear(); };
        h.clearHarmony = [this](){ holdOn = false; jumpDeg = -1000; perf.clearHarmony(); mods.clear(); };
        // scene pads: the scene folder
        if(!padsScanned) scanPads();
        h.padNames.clear(); h.currentPad = h.queuedPad = -1;
        for(size_t i = 0; i < padFiles.size(); i++){
            h.padNames.push_back(ofFilePath::getBaseName(padFiles[i]));
            if(padFiles[i] == sceneFile.get()) h.currentPad = (int)i;
            if(perf.pending().active && padFiles[i] == queuedFile) h.queuedPad = (int)i;
        }
        h.queuePad = [this](int i){
            if(i < 0 || i >= (int)padFiles.size()) return;
            std::string text;
            if(readFile(padFiles[(size_t)i], text)) queueText(text, padFiles[(size_t)i], false);
        };
        h.hasSnapshot = [this](int i){ return i >= 0 && i < (int)snaps.size() && snaps[(size_t)i].is_object(); };
        h.storeSnapshot = [this](int i){ storeSnapshot(i); };
        h.recallSnapshot = [this](int i){ recallSnapshot(i); };
        // assistant
        auto& a = h.ai;
        a.mode = aiModeIdx;
        a.hasKey = aiKey; a.busy = aiBusy || ai.isPending(); a.auditioning = auditioning; a.abShowingOld = abOld;
        a.status = aiStatus; a.explanation = aiExplanation; a.kind = auditionKind;
        a.problems = auditioning ? std::vector<std::string>{} : aiProblems;
        a.rejected = aiRejected;
        a.auditionBefore = auditionPrev;
        a.send = [this](int mode, const std::string& text){
            if(aiBusy || ai.isPending() || !aiKey) return;
            if(auditioning) discardAudition();
            aiModeIdx = std::clamp(mode, 0, 5);
            std::snprintf(aiPrompt, sizeof aiPrompt, "%s", text.c_str());
            sendRequest(makeRequest((forms2::AssistMode)aiModeIdx, text), false);
        };
        a.keep = [this](){ keepAudition(); };
        a.discard = [this](){ discardAudition(); };
        a.ab = [this](){ auditionAB(); };
        h.classicSections = {"Perform", "Sound", "3D", "Stats", "Assistant (classic)"};
        h.drawClassic = [this](const std::string& s){
            float zz = ofxOceanodeShared::getZoomLevel();
            if(s == "Perform") drawPerform(zz);
            else if(s == "Sound") drawSound(zz);
            else if(s == "3D") draw3D(zz);
            else if(s == "Stats") drawStats(zz);
            else if(s == "Assistant (classic)") drawAssistant(zz);
        };
        if(h.seedScenes.empty()){
            h.seedScenes.push_back(exampleScenePath());
            for(auto& f : jsonFiles(templatesDir())) h.seedScenes.push_back(f);
        }
    }

    // ---------------------------------------------------------------- perform (pads, setlist, snapshots, timeline)
    static std::string templatesDir(){
        namespace fs = std::filesystem;
        return (fs::path(exampleScenePath()).parent_path().parent_path() / "templates").string();
    }
    static std::vector<std::string> jsonFiles(const std::string& dir){
        namespace fs = std::filesystem;
        std::vector<std::string> v; std::error_code ec;
        if(!dir.empty() && fs::is_directory(dir, ec))
            for(auto& e : fs::directory_iterator(dir, ec))
                if(e.is_regular_file() && ofToLower(e.path().extension().string()) == ".json") v.push_back(e.path().string());
        std::sort(v.begin(), v.end());
        return v;
    }
    // the scene folders for Setup → Open scene: the playing file's folder, then the addon's scenes
    // (legacy = the v1 scenes converted by v2Legacy.h, examples, templates); each folder once
    std::vector<std::pair<std::string, std::vector<std::string>>> sceneFolders(){
        namespace fs = std::filesystem;
        fs::path scenes = fs::path(exampleScenePath()).parent_path();
        std::vector<std::pair<std::string, std::string>> dirs = {
            {"This scene's folder", fs::path(sceneFile.get()).parent_path().string()},
            {"Legacy (v1 scenes, converted)", (scenes / "legacy").string()},
            {"Examples", (scenes / "examples").string()},
            {"Scenes", scenes.string()},
            {"Templates", templatesDir()}};
        std::vector<std::pair<std::string, std::vector<std::string>>> out;
        std::set<std::string> seen;
        for(auto& [label, d] : dirs){
            std::error_code ec;
            std::string key = fs::weakly_canonical(d, ec).string();
            if(d.empty() || !seen.insert(ec ? d : key).second) continue;
            auto files = jsonFiles(d);
            if(!files.empty()) out.push_back({label, files});
        }
        return out;
    }
    void scanPads(){
        namespace fs = std::filesystem;
        padFiles = jsonFiles(fs::path(sceneFile.get()).parent_path().string());
        templateFiles = jsonFiles(templatesDir());
        padsScanned = true;
    }
    forms2::Boundary boundary() const { return (forms2::Boundary)std::clamp(queueBoundary, 0, 3); }
    // queues a scene text with the window's boundary / crossfade
    void queueText(const std::string& text, const std::string& file, bool dirty){
        forms2::json j;
        try { j = forms2::json::parse(text); } catch(const std::exception& e){ performStatus = std::string("not queued: ") + e.what(); return; }
        // checked first: a scene that does not load is not queued
        forms2::Engine probe;
        if(!probe.loadJson(j)){ performStatus = "not queued: " + (probe.problems().empty() ? std::string("scene problems") : probe.problems().front()); return; }
        queuedText = text; queuedFile = file; queuedDirty = dirty;
        double at = perf.queueScene(j, boundary(), queueCrossfade);
        performStatus = "queued " + probe.scene().name + (at > nowBeat + 1e-6 ? " at beat " + ofToString(at, 2) : " now");
    }
    void storeSnapshot(int i){ snaps[(size_t)i] = perf.snapshot(); performStatus = "snapshot " + ofToString(i + 1) + " stored"; }
    // recall: the snapshot's musical state continues from the current clock (beats shifted)
    void recallSnapshot(int i){
        forms2::json sn = snaps[(size_t)i];
        if(!sn.is_object()) return;
        double d = nowBeat - sn.value("now", 0.0);
        sn["now"] = nowBeat;
        for(auto& sl : sn["slots"]){
            sl["origin"] = sl.value("origin", 0.0) + d;
            sl["fadeStart"] = sl.value("fadeStart", 0.0) + d;
            if(sl.value("endBeat", -1.0) >= 0) sl["endBeat"] = sl.value("endBeat", 0.0) + d;
        }
        if(sn.contains("pending") && sn["pending"].value("active", false)) sn["pending"]["at"] = sn["pending"].value("at", 0.0) + d;
        if(perf.restore(sn)){ lastCur = nullptr; queuedText.clear(); performStatus = "snapshot " + ofToString(i + 1) + " recalled"; audioSeek = true; }
        else performStatus = perf.problems.empty() ? "snapshot not recalled" : perf.problems.front();
    }
    void startSetlist(){
        std::vector<forms2::SetEntry> v;
        for(auto& u : setUi){ forms2::SetEntry e; e.scene = u.scene; e.bars = u.bars; e.minutes = u.minutes; e.crossfadeBeats = u.crossfade; v.push_back(e); }
        if(v.empty()) return;
        if(perf.setSetlist(v, nowBeat)){ lastCur = nullptr; queuedText.clear(); performStatus = "setlist started"; }
        else performStatus = perf.problems.empty() ? "setlist not started" : perf.problems.front();
    }

    void drawPerform(float z){
        sectionTitle("Perform");
        if(!padsScanned) scanPads();
        // queue options
        static const char* bnames[] = {"now", "next bar", "next phrase", "next 4 bars"};
        ImGui::SetNextItemWidth(120 * z);
        ImGui::Combo("switch at", &queueBoundary, bnames, 4);
        ImGui::SameLine();
        ImGui::SetNextItemWidth(90 * z);
        ImGui::DragFloat("crossfade beats", &queueCrossfade, 0.25f, 0, 64, queueCrossfade <= 0 ? "hard cut" : "%.1f");
        auto& pd = perf.pending();
        if(pd.active) ImGui::TextColored(ImVec4(1, 0.8f, 0.3f, 1), "queued change at beat %.2f (in %.2f beats)%s", pd.at, pd.at - nowBeat, pd.crossfade > 0 ? ", crossfade" : "");
        if(perf.slotCount() > 1) ImGui::Text("crossfading: out %.0f%%  in %.0f%%", perf.slotGain(0) * 100, perf.slotGain(perf.slotCount() - 1) * 100);
        if(!performStatus.empty()) ImGui::TextDisabled("%s", performStatus.c_str());

        // scene pads
        ImGui::TextDisabled("Scenes (scene folder)");
        ImGui::SameLine();
        if(ImGui::SmallButton("Rescan")) scanPads();
        float avail = ImGui::GetContentRegionAvail().x, padW = 130 * z;
        int perRow = std::max(1, (int)(avail / (padW + 6 * z)));
        for(int i = 0; i < (int)padFiles.size(); i++){
            ImGui::PushID(300 + i);
            std::string nm = ofFilePath::getBaseName(padFiles[(size_t)i]);
            bool cur = padFiles[(size_t)i] == sceneFile.get();
            if(cur){ ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.45f, 0.7f, 1)); }
            if(ImGui::Button(nm.c_str(), ImVec2(padW, 34 * z))){
                std::string t;
                if(readFile(padFiles[(size_t)i], t)) queueText(t, padFiles[(size_t)i], false);
            }
            if(cur) ImGui::PopStyleColor();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Queue %s (%s)", nm.c_str(), bnames[std::clamp(queueBoundary, 0, 3)]);
            if((i + 1) % perRow != 0 && i + 1 < (int)padFiles.size()) ImGui::SameLine();
            ImGui::PopID();
        }
        // new scene from a template
        if(!templateFiles.empty()){
            std::vector<std::string> names; for(auto& f : templateFiles) names.push_back(ofFilePath::getBaseName(f));
            templateIdx = std::clamp(templateIdx, 0, (int)names.size() - 1);
            ImGui::SetNextItemWidth(130 * z);
            if(ImGui::BeginCombo("##tmpl", names[(size_t)templateIdx].c_str())){
                for(int i = 0; i < (int)names.size(); i++) if(ImGui::Selectable(names[(size_t)i].c_str(), i == templateIdx)) templateIdx = i;
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            ImGui::SetNextItemWidth(120 * z);
            ImGui::InputText("##newname", newName, sizeof newName);
            ImGui::SameLine();
            if(ImGui::Button("New scene from template")){
                forms2::json t;
                if(forms2::loadTemplateFile(templateFiles[(size_t)templateIdx], t)){
                    uint64_t seed = ((uint64_t)ofRandom(1, 1 << 30) << 16) ^ (uint64_t)ofGetElapsedTimeMillis();
                    queueText(forms2::sceneFromTemplate(t, newName, seed).dump(2), "", true);
                } else performStatus = "cannot read the template";
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("A copy of the template with a new name and seed, queued; Save scene as... keeps it.");
        }

        // setlist
        if(ImGui::TreeNode("Setlist")){
            bool running = !perf.setlist().empty();
            int cur = perf.currentSetIndex();
            if(ImGui::SmallButton("+ current scene")){
                try { SetUi u; u.scene = forms2::json::parse(currentText); u.name = loadedName.empty() ? "scene" : loadedName; setUi.push_back(u); } catch(...){}
            }
            ImGui::SameLine();
            if(ImGui::SmallButton("Start")) startSetlist();
            ImGui::SameLine();
            if(!running) ImGui::BeginDisabled();
            if(ImGui::SmallButton("Next")){ if(cur + 1 < (int)perf.setlist().size()) perf.queueSetEntry(cur + 1, boundary(), queueCrossfade); }
            ImGui::SameLine();
            if(ImGui::SmallButton("Stop setlist")){ macrosFromScene = false; balanceFromScene = false; loadScene(currentText, false); }
            if(!running) ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::TextDisabled(running ? "running: entry %d of %d" : "stopped", cur + 1, (int)perf.setlist().size());
            int del = -1, up = -1;
            if(!setUi.empty() && ImGui::BeginTable("##set", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit)){
                ImGui::TableSetupColumn("scene", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableSetupColumn("bars"); ImGui::TableSetupColumn("minutes"); ImGui::TableSetupColumn("fade"); ImGui::TableSetupColumn(""); ImGui::TableSetupColumn("");
                ImGui::TableHeadersRow();
                for(int i = 0; i < (int)setUi.size(); i++){
                    auto& u = setUi[(size_t)i];
                    ImGui::PushID(400 + i);
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    if(running && i == cur) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.5f, 1), "> %s", u.name.c_str());
                    else ImGui::Text("  %s", u.name.c_str());
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(50 * z); ImGui::DragFloat("##b", &u.bars, 1, 0, 999, "%.0f");
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Length in bars (0 = until Next)");
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(50 * z); ImGui::DragFloat("##m", &u.minutes, 0.05f, 0, 120, "%.2f");
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Length in minutes (wins over bars)");
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(45 * z); ImGui::DragFloat("##f", &u.crossfade, 0.25f, 0, 64, "%.1f");
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Crossfade beats into the next entry");
                    ImGui::TableNextColumn(); if(i > 0 && ImGui::SmallButton("^")) up = i;
                    ImGui::TableNextColumn(); if(ImGui::SmallButton("x")) del = i;
                    ImGui::PopID();
                }
                ImGui::EndTable();
            }
            if(up > 0) std::swap(setUi[(size_t)up], setUi[(size_t)up - 1]);
            if(del >= 0) setUi.erase(setUi.begin() + del);
            ImGui::TextDisabled("Edits apply on Start.");
            ImGui::TreePop();
        }

        // snapshots
        ImGui::TextDisabled("Snapshots (click: recall, shift-click or right-click: store)");
        for(int i = 0; i < 8; i++){
            ImGui::PushID(500 + i);
            bool has = snaps[(size_t)i].is_object();
            if(has){ ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.25f, 0.5f, 0.3f, 1)); }
            if(ImGui::Button(ofToString(i + 1).c_str(), ImVec2(32 * z, 0))){
                if(ImGui::GetIO().KeyShift || !has) storeSnapshot(i); else recallSnapshot(i);
            }
            if(ImGui::IsItemClicked(ImGuiMouseButton_Right)) storeSnapshot(i);
            if(has) ImGui::PopStyleColor();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip(has ? "Recall (shift / right-click: store again)" : "Store the current state");
            ImGui::SameLine();
            ImGui::PopID();
        }
        if(ImGui::SmallButton("Clear snapshots")) for(auto& x : snaps) x = forms2::json();

        // timeline
        if(ImGui::TreeNode("Timeline")){
            bool rec = perf.recording();
            if(ImGui::Checkbox("Record performed actions", &rec)) perf.setRecording(rec);
            ImGui::SameLine();
            ImGui::TextDisabled("%d recorded, %d in the timeline", (int)perf.recorded().size(), (int)perf.timeline().size());
            if(ImGui::SmallButton("Use recording as timeline")){ perf.setTimeline(perf.recordedTimeline()); performStatus = "timeline set (plays when the clock crosses it)"; }
            ImGui::SameLine();
            if(ImGui::SmallButton("Clear recording")) perf.clearRecorded();
            ImGui::SameLine();
            if(ImGui::SmallButton("Clear timeline")) perf.setTimeline({});
            auto list = [&](const char* id, const std::vector<forms2::TimelineAction>& t){
                ImGui::BeginChild(id, ImVec2(-1, 90 * z), true);
                for(auto& a : t){
                    std::string what = a.action == "scene" ? (a.data.contains("scene") ? "scene " + a.data["scene"].value("name", std::string("?")) : "setlist entry " + ofToString((int)a.value + 1))
                                                           : a.action + (a.target.empty() ? "" : " " + a.target);
                    if(a.action == "macro" || a.action == "jump") what += " " + ofToString(a.value, 2);
                    bool past = a.beat <= nowBeat;
                    ImGui::TextColored(past ? ImVec4(0.6f, 0.6f, 0.6f, 1) : ImVec4(1, 1, 1, 1), "%8.2f  %s", a.beat, what.c_str());
                }
                ImGui::EndChild();
            };
            ImGui::TextDisabled("recorded"); list("##rec", perf.recorded());
            ImGui::TextDisabled("timeline"); list("##tl", perf.timeline());
            ImGui::TreePop();
        }
    }

    // ---------------------------------------------------------------- sound section
    void drawSound(float z){
        sectionTitle("Sound");
        static const char* modes[] = {"v1 SuperCollider bank", "v2 engine", "off"};
        int m = std::clamp(soundMode.get(), 0, 2);
        ImGui::SetNextItemWidth(180 * z);
        if(ImGui::Combo("output", &m, modes, 3)){ soundMode = m; audioFailed = false; }
        if(m == 0){
#ifdef FORMS_HAS_SC
            ImGui::Text("%s", sound && sound->running() ? "running (connect Audio to SC Output)" : "not running (connect Audio to SC Output)");
#else
            ImGui::TextDisabled("built without SuperCollider: no v1 sound");
#endif
        }
        float v = volume.get();
        ImGui::SetNextItemWidth(160 * z);
        if(ImGui::SliderFloat("volume", &v, 0, 4)) volume = v;
        if(m != 1) return;
        int k = sndIfft ? 1 : 0;
        static const char* kinds[] = {"oscillator bank", "IFFT"};
        ImGui::SetNextItemWidth(140 * z);
        if(ImGui::Combo("engine", &k, kinds, 2)){ sndIfft = k == 1; audioSeek = true; }
        ImGui::SameLine();
        ImGui::Checkbox("drum voices", &sndDrums);
        ImGui::SetNextItemWidth(160 * z);
        ImGui::SliderFloat("pitch balance", &sndBalance, 0, 1, "%.2f");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("0 flat, 1 full equal-loudness curve (the scene's value on load)");
        // output device
        if(deviceNames.empty()) for(auto& d : forms2::audio::AudioOut::outputDevices()) deviceNames.push_back(d.name);
        ImGui::SetNextItemWidth(220 * z);
        if(ImGui::BeginCombo("device", sndDevice.empty() ? "system default" : sndDevice.c_str())){
            if(ImGui::Selectable("system default", sndDevice.empty())){ sndDevice.clear(); audioReopen = true; }
            for(auto& n : deviceNames) if(ImGui::Selectable(n.c_str(), n == sndDevice)){ sndDevice = n; audioReopen = true; }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if(ImGui::SmallButton("Refresh")) deviceNames.clear();
        if(audioFailed && ImGui::SmallButton("Retry")) audioFailed = false;
        // level meter (dBFS, -60..0)
        auto bar = [&](const char* lbl, float pk){
            float db = pk > 1e-6f ? 20 * std::log10(pk) : -120;
            float u = std::clamp((db + 60) / 60, 0.f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, db > -1 ? ImVec4(0.9f, 0.3f, 0.2f, 1) : ImVec4(0.3f, 0.75f, 0.4f, 1));
            char t[32]; std::snprintf(t, sizeof t, "%s %.1f dB", lbl, db < -100 ? -INFINITY : db);
            ImGui::ProgressBar(u, ImVec2(220 * z, 0), t);
            ImGui::PopStyleColor();
        };
        bar("L", meterL); bar("R", meterR);
        ImGui::TextDisabled("%s", audio ? audio->status.c_str() : (audioFailed ? "could not open the output device" : "stopped"));
    }

    void drawAuditionBanner(float z){
        if(!auditioning) return;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.35f, 0.25f, 0.05f, 1));
        ImGui::BeginChild("##audition", ImVec2(-1, 34 * z), true);
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1, 0.85f, 0.4f, 1), "Auditioning assistant answer (%s):", auditionKind.c_str());
        ImGui::SameLine();
        if(ImGui::Button("Keep")) keepAudition();
        ImGui::SameLine();
        if(ImGui::Button("Discard")) discardAudition();
        ImGui::EndChild();
        ImGui::PopStyleColor();
    }

    void drawAssistant(float z){
        sectionTitle("Assistant");
        if(!aiKey){
            ImGui::BeginDisabled();
        }
        static const char* modes[] = {"create instrument", "create kit", "create scene", "modify", "fix", "explain"};
        ImGui::SetNextItemWidth(170 * z);
        ImGui::Combo("mode", &aiModeIdx, modes, 6);
        ImGui::SameLine();
        ai.drawButton(ImVec2(38 * z, 0));
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Model picker (and the same request as a popup)");
        ImGui::InputTextMultiline("##aiprompt", aiPrompt, sizeof aiPrompt, ImVec2(-1, 60 * z));
        drawAuditionBanner(z);
        bool canSend = aiKey && !aiBusy && !ai.isPending() && !auditioning;
        if(!canSend && aiKey) ImGui::BeginDisabled();
        if(ImGui::Button("Send", ImVec2(80 * z, 0))) sendRequest(makeRequest((forms2::AssistMode)aiModeIdx, aiPrompt), false);
        if(!canSend && aiKey) ImGui::EndDisabled();
        ImGui::SameLine();
        bool noUndo = !v2gui.cx.history.canUndo() || auditioning, noRedo = !v2gui.cx.history.canRedo() || auditioning;
        if(noUndo) ImGui::BeginDisabled();
        if(ImGui::Button("Undo")) undo();
        if(noUndo) ImGui::EndDisabled();
        ImGui::SameLine();
        if(noRedo) ImGui::BeginDisabled();
        if(ImGui::Button("Redo")) redo();
        if(noRedo) ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", aiTargetId.empty() ? "" : ("on " + aiTargetId).c_str());
        ai.drawPopup();
        if(!aiKey){
            ImGui::EndDisabled();
            ImGui::TextColored(ImVec4(1, 0.65f, 0.3f, 1), "Inactive: no Requesty API key (data/config/requesty.md or REQUESTY_API_KEY).");
            if(ImGui::SmallButton("Check again")) aiKey = aiKeyPresent();
        }
        if(!aiStatus.empty()) ImGui::TextWrapped("%s", aiStatus.c_str());
        else if(!ai.getStatus().empty()) ImGui::TextDisabled("%s", ai.getStatus().c_str());
        for(auto& p : aiProblems) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "- %s", p.c_str());
        if(!aiExplanation.empty()){
            ImGui::BeginChild("##explain", ImVec2(-1, 140 * z), true);
            ImGui::TextWrapped("%s", aiExplanation.c_str());
            ImGui::EndChild();
        }
    }
};
