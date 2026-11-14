//
//  formsBox.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The ESPECTRES 2027 player, without its renderer: scenes and setlist (JSON files in the data
//  folder, bin/data/Forms by default), the requests for each image, and the musical values of the
//  image under the header. Its built-in player is the clock (formsClock.h Transport), the play-head
//  (Header) and the scroller (scrollerCore.h), so one node drives the piece; also the layers, the
//  post-processing, the sonification and the window. Logic in formsEngine.h; this is parameters
//  and UI. Abstract: the node is Forms GL Box (formsGLBox.h), which draws the requests.
//

#pragma once

#include "ofxOceanodeNodeModel.h"
#include "ofxOceanodeShared.h"
#include "ofxOceanodeGlobalPitch.h"
#include "imgui.h"
#include "formsEngine.h"
#include "formsCoverGL.h"
#include "formsEditor.h"
#include "formsClock.h"
#include "formsPanels.h"
#include "scrollerCore.h"
#include "layerRing.h"
#include "formsPost.h"
#include "formsBackground.h"
#include "formsSound.h"
#include "formsControls.h"
#include "formsEditorPost.h"
#include "formsEditorSound.h"
#include "formsLane.h"
#include "formsTransportFeed.h"
#include "formsLayerFx.h"
#include "formsLayerGain.h"
#if __has_include("ofxOceanodeTransport.h") && __has_include("ofxOceanodeContainer.h")
#include "ofxOceanodeTransport.h"
#include "ofxOceanodeContainer.h"
#define FORMS_HAS_TRANSPORT 1
#endif
#include <numeric>
#include <array>
#include <cstdio>

class formsBox : public ofxOceanodeNodeModel {
public:
    explicit formsBox(const std::string& nodeName) : ofxOceanodeNodeModel(nodeName) {}

#ifdef FORMS_HAS_SC
    // with the SuperCollider servers: the Audio output plays the sonification
    formsBox(const std::string& nodeName, std::vector<serverManager*> servers) : formsBox(nodeName) { scServers = std::move(servers); }
#endif

    virtual ~formsBox(){
        ofxOceanodeGlobalPitch::unfollow(this);
        releaseTransport();
        scheduleOut = nullptr;
    }

    std::string getWindowTitle() override { return "Forms GL Box " + ofToString(getNumIdentifier()); }
    ofParameter<bool>* getWindowToggle() override { return &showWindow; }

    void setup() override {
        ctl.node = this;
        ctl.setupTimeline();
        // a destination of the patch's Global Pitch node (Scale: the drawn scale, its root and shift;
        // Transpose: a bend of the sound). Without the node its state is neutral: nothing changes.
        ofxOceanodeGlobalPitch::follow(this, true, {}, {});
        // ---- always on the node
        addParameter(showWindow.set("Show", false));
        addCustomRegion(faceRegion.set("Preview", [this](){ editor.drawNodeFace(); }), [this](){ editor.drawNodeFace(); });
        addParameter(go.set("GO"));
        addParameter(autoGo.set("Auto GO", true));
        addParameter(useSetlist.set("Setlist", true));
        addParameter(sceneIndex.set("Scene", 0, 0, 255));
        addParameter(speed.set("Speed", 1, -24, 24));
        // int 0/1: the custom GUI's MultiToggle drives int or float, not bool
        addParameter(pause.set("Pause", 0, 0, 1));
        addParameter(sequence.set("Sequence", true));
        addParameter(seqFade.set("Seq Fade", 0, 0, 1));
        addParameter(newPng.set("New PNG"));
        addParameter(header.set("Header", 0.218182, 0, 1));
        addParameter(headerPlus.set("Header+", 0, 0, 1));
        addParameter(smooth.set("Smooth", 0.0001, 0, 0.999));
        addOutputParameter(output.set("Output", nullptr));   // the final image (post-processed)
#ifdef FORMS_HAS_SC
        sound = std::make_shared<forms::SoundEngine>(scServers);
        addOutputParameter(audioOut.set("Audio", nodePort(0, sound.get())));
#endif
        addOutputParameter(headerPosOut.set("Header Pos", 0, 0, 16384));
        addOutputParameter(headerNorm.set("Header Norm", 0, 0, 1));
        addOutputParameter(phaseOut.set("Phase", 0, 0, 1));
        addOutputParameter(bpmOut.set("BPM", 120, 1, 2000));
        addOutputParameter(kickOut.set("Kick", 0, 0, 1));   // the kick layer's bottom strip at the header
        // the scene lane's gates (written by a timeline Chords lane; no widget, its pin shows when connected)
        lane.handle = addParameter(lane.gates.set("Scene Lane", {0.0f}, {0.0f}, {1.0f}),
                                   ofxOceanodeParameterFlags_NoGuiWidget | ofxOceanodeParameterFlags_DisableSavePreset);
        listeners.push(lane.gates.newListener([this](std::vector<float>& v){ lane.gatesChanged(v); }));
        // the harmony under the playhead, as notes (for other synths): the bar's chord root (MIDI)
        // and the scale's pitch classes on the real root + transpose (sorted, 0..11; what the
        // partials snap to)
        addOutputParameter(rootOut.set("Root Out", 24, 0, 127));
        addOutputParameter(scaleOut.set("Scale Out", {0}, {0}, {11}));

        // ---- the window's settings (publishable with a right-click; the legacy ones were on the node)
        resetSetlist.set("Reset Setlist");
        actualBarIn.set("actualBarIn", 0, 0, 64);
        analogMode.set("Analog Mode", false);
        bpmAdd.set("BPM Add", 0, -1, 1);
        maxMinutes.set("Max Minutes", 11, 0.1, 600);
        barMult.set("Bar Multiplier", 1, 0.25, 16);   // every layer's bars times this (wide projections keep their proportions)
        barAdd.set("Bar Add", 0, -16, 64);   // whole bars added to every layer after the multiplier
        sessionLimit.set("Session Limit", false);   // off: images never stop by themselves (a show turns it on)
        autoHeader.set("Auto Header", false);
        tremoloOn.set("Tremolo", false);
        hardCut.set("Hard Cut", false);   // manual scene changes: cut every layer at the change
        headerMin.set("Header Min", 3.0f / 7, 0, 1);
        headerMax.set("Header Max", 4.0f / 7, 0, 1);
        viewW.set("Width", 1920, 1, 16384);
        viewH.set("Height", 1080, 1, 16384);
        ctl.reg(resetSetlist); ctl.reg(actualBarIn); ctl.reg(analogMode); ctl.reg(bpmAdd); ctl.reg(maxMinutes); ctl.reg(sessionLimit); ctl.reg(barMult); ctl.reg(barAdd);
        ctl.reg(autoHeader); ctl.reg(tremoloOn); ctl.reg(hardCut); ctl.reg(headerMin); ctl.reg(headerMax);
        ctl.reg(viewW); ctl.reg(viewH);
        // the transport follows Forms Box's beat (off: the transport runs on its own)
        driveTransport.set("Drive Transport", false);
        ctl.reg(driveTransport);
        postCtl.setup(ctl);
        soundCtl.setup(ctl, ofToDataPath("Forms/samples/KICKS", true));

        // ---- legacy outputs (what the 2027_4 patch read: published only in old patches, or by hand)
        pitchBend.set("Pitch Bend", 0, -48, 48);
        fade.set("Fade", 1, 0, 1);
        seqMinutes.set("Seq Minutes", 0, 0, 600);
        speedOut.set("Speed Out", 1, -24, 24);
        autoPng.set("Auto", true);
        sequenceOut.set("Sequence Out", true);
        restartSetlist.set("Restart Setlist");
        phLock.set("phLock", 0, 0, 1);
        phOff.set("phOff", 0, 0, 1);
        actualBarOut.set("actualBar", 0, 0, 64);
        opacity.set("Opacity", 1, 0, 1);
        inHeader.set("In Header", "");
        scheduleOut.set("Schedule", nullptr);
        actualBPM.set("actualBPM", 120, 1, 999);
        actualBars.set("actualBars", 8, 0, 256);
        actualNoteGrid.set("actualNoteGrid", 100, 0, 999);
        actualRoot.set("actualRoot", 0, 0, 64);
        actualScale.set("actualScale", {0}, {0}, {48});
        actualSnapshot.set("actualSnapshot", -1, -1, 255);
        actualTrans.set("actualTrans", 0, -48, 48);
        sceneName.set("Scene Name", "");
        // What the 1920 Director output directly: the scene root and the Director slot of the scene the setlist is on
        sceneRoot.set("Root", 24, 0, 127);
        setlistSnapshot.set("Setlist Snapshot", -1, -1, 255);
        ctl.regOutput(pitchBend); ctl.regOutput(fade); ctl.regOutput(seqMinutes); ctl.regOutput(speedOut);
        ctl.regOutput(autoPng); ctl.regOutput(sequenceOut); ctl.regOutput(restartSetlist); ctl.regOutput(phLock);
        ctl.regOutput(phOff); ctl.regOutput(actualBarOut); ctl.regOutput(opacity); ctl.regOutput(inHeader);
        ctl.regOutput(scheduleOut); ctl.regOutput(actualBPM); ctl.regOutput(actualBars); ctl.regOutput(actualNoteGrid);
        ctl.regOutput(actualRoot); ctl.regOutput(actualScale); ctl.regOutput(actualSnapshot); ctl.regOutput(actualTrans);
        ctl.regOutput(sceneName); ctl.regOutput(sceneRoot); ctl.regOutput(setlistSnapshot);
        // The scene's performance settings (what Macro_8 output), smoothed like the patch's Smoothers
        for(auto& p : forms::perfParams()){
            if(!p.output) continue;
            auto o = std::make_unique<PerfOut>();
            o->p = &p;
            if(p.type == 'f'){ o->f.set(p.key, p.def.get<float>(), p.min, p.max); ctl.regOutput(o->f); }
            else if(p.type == 'i'){ o->i.set(p.key, p.def.get<int>(), (int)p.min, (int)p.max); ctl.regOutput(o->i); }
            else { o->v.set(p.key, p.def.get<std::vector<float>>(), {p.min}, {p.max}); ctl.regOutput(o->v); }
            perfOuts.push_back(std::move(o));
        }
        // the old node's parameters, in its order (a patch without "formsPublished" gets them back)
        legacyKeys.clear();
        for(auto* p : std::vector<ofAbstractParameter*>{&resetSetlist, &actualBarIn, &analogMode, &bpmAdd, &maxMinutes,
                &autoHeader, &tremoloOn, &hardCut, &headerMin, &headerMax, &viewW, &viewH,
                &pitchBend, &fade, &seqMinutes, &speedOut, &autoPng, &sequenceOut, &restartSetlist, &phLock, &phOff,
                &actualBarOut, &opacity, &inHeader, &scheduleOut, &actualBPM, &actualBars, &actualNoteGrid,
                &actualRoot, &actualScale, &actualSnapshot, &actualTrans, &sceneName, &sceneRoot, &setlistSnapshot})
            legacyKeys.push_back(p->getEscapedName());
        for(auto& o : perfOuts) legacyKeys.push_back(o->p->type == 'f' ? o->f.getEscapedName() : o->p->type == 'i' ? o->i.getEscapedName() : o->v.getEscapedName());
        // a new node: the few that are worth modulating
        for(auto* p : std::vector<ofAbstractParameter*>{&soundCtl.volume, &postCtl.hue, &postCtl.levelMax, &soundCtl.kickOn})
            ctl.publish(p->getEscapedName());

        // the data folder (scenes, presets, library, setlists, thumbnails, sessions): relative to the
        // app's data folder unless absolute
        addInspectorParameter(rendererFolder.set("Renderer", "Forms"));
        addInspectorParameter(maxPending.set("Max Pending", 2, 1, 16));
        // Size of the rendered images (any size; also in the System tab)
        addInspectorParameter(imageWidth.set("Image Width", 1920, 16, 16384));
        addInspectorParameter(imageHeight.set("Image Height", 1080, 16, 16384));
        addInspectorParameter(prime.set("Prime", 2, 0, 8));
        // a new image replaces the one under the header as soon as it is ready (instead of
        // scrolling in from the right edge); off = as before
        addInspectorParameter(immediateReplace.set("Replace at Playhead", false));
        addInspectorParameter(pitchclassFolder.set("Pitchclass", "Supercollider/Pitchclass"));
        listeners.push(pitchclassFolder.newListener([this](string&){ reload(); }));
        addInspectorParameter(builtInPlayer.set("Built-in Player", true));
        addInspectorParameter(saveFrame.set("Save Frame"));
        listeners.push(saveFrame.newListener([this](){ saveFramePending = true; }));
        clock.noise2 = [](float x, float seed){ return ofNoise(x, seed); };
        head.noise2 = [](float x, float seed){ return ofNoise(x, seed); };
        listeners.push(newPng.newListener([this](){ clock.newPng(ofGetElapsedTimef()); }));

        listeners.push(go.newListener([this](){ request(false); }));
        listeners.push(maxPending.newListener([this](int& n){ engine.maxPending = n; }));
        listeners.push(imageWidth.newListener([this](int& n){ engine.width = n; }));
        listeners.push(imageHeight.newListener([this](int& n){ engine.height = n; }));
        listeners.push(resetSetlist.newListener([this](){ engine.resetSetlist(); }));
        listeners.push(useSetlist.newListener([this](bool& b){ engine.useSetlist = b; }));
        listeners.push(sceneIndex.newListener([this](int& i){ pickManualScene(i); }));
        listeners.push(rendererFolder.newListener([this](string&){ reload(); }));

        editor.engine = &engine;
        editor.autoGo = &autoGo;
        // Stage > Layers: the bar multiplier / add for every layer (node settings)
        editor.barMult = &barMult; editor.barAdd = &barAdd;
        editor.nodeParamMenu = [this](const std::string& key){ ctl.menu(key); };
        editor.noNewImagesWhy = [this]() -> std::string {
            if(engine.replaying) return "";
            if(!autoGo) return "Auto GO is off";
            if(!autoPng){
                if(sessionLimit && seqMinutes.get() >= maxMinutes.get()) return "the session ended (Session time limit: Max Minutes " + ofToString(maxMinutes.get(), 1) + " reached)";
                if(!sequence) return "Sequence is switched off";
                return "automatic images are off";
            }
            return "";
        };
        editor.resumeImages = [this](){
            if(!autoGo) autoGo = true;
            // the clock restarts its session timer on Sequence's rising edge
            if(sequence){ sequence = false; resumeImagesIn = 2; } else sequence = true;
        };
        editor.importPath = ofToDataPath("Presets/ESPECTRES/2--ESPECTRES_1920", true);
        editor.exportPanorama = [this](){ return exportPanorama(); };
        editor.lookPng = [this](const std::string& png){ return lookPng(png); };
        editor.sendRequest = [this](const std::string& p){ send(p); };
        editor.kickLevel = &kickLevel;
        editor.playScene = [this](const std::string& name){
            auto it = std::find(sceneNames.begin(), sceneNames.end(), name);
            if(it == sceneNames.end() && engine.scenes.count(name)){
                // a scene made since the last reload (assistant, Perform): the list catches up
                sceneNames.clear();
                for(auto& [n, sc] : engine.scenes) sceneNames.push_back(n);
                it = std::find(sceneNames.begin(), sceneNames.end(), name);
            }
            if(it == sceneNames.end()) return;
            useSetlist = false;
            useSetlistLocal = false;
            sceneIndex = (int)(it - sceneNames.begin());
            if(editor.perform.switchNow) switchPending = true;   // Switch now (Perform)
        };
        // Render next now: a next scene other than the one playing switches now (as Switch now);
        // the same scene: just the next image, as before
        editor.requestNext = [this](bool force){
            const forms::Scene* n = engine.currentScene();
            auto& h = engine.schedule->layers[0].header;
            const bool otherScene = n && h && h->scene != n->name;
            // the same scene (Switch now ticked): the new image replaces the one playing and then
            // plays once in full, instead of being gone when the page ends
            if(force && (editor.perform.switchNow || otherScene)){ switchKeepImage = !otherScene; switchPending = true; }
            else request(force);
        };
        editor.previousImage = [this](){ engine.previousImage(); };
        editor.canPreviousImage = [this](){ return !gateBypass && engine.canPrevious(); };
        editor.switchScene = [this](){ switchPending = true; };
        editor.renderScene = [this](const std::string& name){ send(engine.makeRequest(true, name)); };
        editor.sendPreviews = [this](const std::string& scene, const std::vector<std::pair<std::string, forms::json>>& variants){
            for(auto& path : engine.makePreviews(scene, variants)) send(path);
        };
        editor.reload = [this](){ reload(); };
        editor.browseKickSample=[this](forms::Scene& scene){
            editor.kickSampleError.clear();
            auto choice=ofSystemLoadDialog("Choose a kick sample (WAV or AIFF)");
            if(!choice.bSuccess) return false;
            try{engine.importKick(scene,choice.getPath());return true;}
            catch(const std::exception& error){editor.kickSampleError=error.what();return false;}
        };
        editor.selectKickSample=[this](forms::Scene& scene,const std::string& name){
            editor.kickSampleError.clear();
            try{
                auto path=forms::fs::path(soundCtl.kickFolder.get())/name;
#ifdef FORMS_HAS_SC
                if(sound){
                    auto names=kickBaseNames();auto it=std::find(names.begin(),names.end(),name);
                    if(it!=names.end())path=sound->kicks()[it-names.begin()];
                }
#endif
                engine.importKick(scene,path);return true;
            }catch(const std::exception& error){editor.kickSampleError=error.what();return false;}
        };
        // a folder kick (by number or by name) is embedded when its scene is saved
        engine.embedFolderKick = [this](forms::Scene& scene){
            if(scene.kickSample) return;
            const forms::json perf = forms::completePerformance(scene.performance);
            if(forms::perfF(perf, "kDetectOn", 1) <= 0.5f || forms::perfF(perf, "kickSynth", 0) > 0.5f) return;
#ifdef FORMS_HAS_SC
            if(!sound) return;
            const auto& files = sound->kicks();
            if(files.empty()) return;
            int idx = -1;
            std::string want = perf.contains("kickSample") && perf["kickSample"].is_string() ? perf["kickSample"].get<std::string>() : std::string();
            if(!want.empty()) for(int i = 0; i < (int)files.size(); i++) if(ofFilePath::getFileName(files[i]) == want){ idx = i; break; }
            if(idx < 0) idx = ofClamp((int)std::lround(forms::perfF(perf, "kickSelect", 11)), 0, (int)files.size() - 1);
            try{
                int number = (int)std::lround(forms::perfF(perf, "kickSelect", 11));
                engine.importKick(scene, files[idx]);
                scene.performance["kickSelect"] = number;   // its number stays, should the embedded sample be removed
                engine.note("embedded kick " + ofFilePath::getFileName(files[idx]) + " in " + scene.name);
            }catch(const std::exception& error){ engine.note(std::string("kick not embedded: ") + error.what()); }
#endif
        };
        engine.sceneRenamed = [this](const std::string& from, const std::string& to){
            for(auto& im : scroller.heldImages()) if(im->scene == from) im->scene = to;
            for(auto& row : lane.rows) if(row == from) row = to;
            if(lane.hardCut.erase(from)) lane.hardCut.insert(to);
            laneMemo.clear();
            // Changing the alphabetical list must not select a different manual scene.
            sceneNames.clear();
            for(auto& [name, sc] : engine.scenes) sceneNames.push_back(name);
            auto it = std::find(sceneNames.begin(), sceneNames.end(), engine.manualScene);
            if(it != sceneNames.end()) sceneIndex = (int)(it - sceneNames.begin());
            sceneWasRenamed(from, to);
        };
        editor.newSession = [this](){ engine.newSession(); primePending = true; };
        // the size chosen in Stage > Look & harmony: the images are drawn at it AND the output
        // texture takes it (the node's Width / Height can still be set apart afterwards, e.g. to
        // show large images on a smaller output)
        editor.setImageSize = [this](int w, int h){ imageWidth = w; imageHeight = h; viewW = w; viewH = h; };
        editor.useSetlist = &useSetlistLocal;
        editor.playerOn = [this](){ return builtInPlayer.get(); };
        // the view: the final image (every layer, post-processed), else layer 1
        editor.playTexture = [this]() -> const ofTexture* { return output.get() ? output.get() : scroller.fbo.isAllocated() ? &scroller.fbo.getTexture() : nullptr; };
        editor.gridInfo = [this](){
            forms::Editor::GridInfo g;
            auto& h = engine.schedule->layers[0].header;
            if(!h) return g;
            g.valid = true;
            auto si = engine.scenes.find(h->scene);
            g.root = si != engine.scenes.end() ? si->second.root : 24;
            g.notegrid = h->notegrid;
            g.imageH = imageHeight;
            auto hv = engine.headerValues(head.actualBar);
            g.trans = hv.valid ? hv.trans : 0;
            g.scale = hv.valid ? hv.scale : h->scaleNotes;
            g.chordRoot = (int)rootOut.get();
            g.width = viewW;
            for(auto& sp : scroller.lastSpans) g.spans.push_back({0, sp.x, (float)viewW, sp.bars});
            for(int i = 0; i < (int)rings.size(); i++) for(auto& d : rings[i].lastDrawn) g.spans.push_back({i + 1, d.x, (float)viewW, d.len / 4});
            return g;
        };
        editor.headerNormalized = [this](){ return viewW > 0 ? head.headerPos / (float)viewW : 0.0f; };
        editor.setHeaderNormalized = [this](float x){ header = ofClamp(x, 0, 1); };
        editor.drawPlayControls = [this](){ drawPlayControls(); };
        editor.setHardCut = [this](bool b){ hardCut = b; };
        editor.getHardCut = [this](){ return hardCut.get(); };
        editor.drawPostTab = [this](){ postCtl.draw(ctl, post); };
        editor.drawSoundTab = [this](){ drawSoundTab(); };
        editor.drawExportTab = [this](){ drawExportTab(); };
        editor.drawNodeTab = [this](){ ctl.drawList(); };
        postCtl.drawEffectPreset=[this](int l){
            if(auto current=engine.sceneAtHeader()){
                auto found=engine.scenes.find(current->name);
                if(found!=engine.scenes.end())editor.drawLayerEffectPreset(found->second,l);
            }
        };
        editor.drawLayerLook = [this](int l){ ensureLayerCount(l+1); postCtl.drawLayerFx(ctl, l, true, false); };
        editor.layerFxOn = &postCtl.fxOn;   // "Layer effects on" (the patch's master switch)
        editor.drawKickMeter = [this](float threshold){ soundCtl.drawKickMeter(soundView, threshold); };
        editor.drawLayerSound = [this](int l){ ensureLayerCount(l+1); soundCtl.drawLayer(ctl, soundView, l); };
        editor.layerMS = [this](int l){ return l < 0 || l >= (int)layerMute.size() ? 0 : (layerMute[l] ? 1 : 0) | (layerSolo[l] ? 2 : 0); };
        editor.toggleLayerMS = [this](int l, int which){ if(l < 0) return; ensureLayerCount(l+1); bool& b = which ? layerSolo[l] : layerMute[l]; b = !b; };
        editor.instMS = [this](const std::string& r){ return (instMute.count(r) ? 1 : 0) | (instSolo.count(r) ? 2 : 0); };
        editor.toggleInstMS = [this](const std::string& r, int which){
            auto& set = which ? instSolo : instMute;
            if(!set.erase(r)) set.insert(r);
            instrumentFilterChanged();
        };
        editor.muteSummary = [this](){
            int lm = 0, ls = 0;
            for(int i = 0; i < (int)layerMute.size(); i++){ lm += layerMute[i]; ls += layerSolo[i]; }
            std::string s;
            auto add = [&](int n, const char* what){ if(n) s += (s.empty() ? "" : ", ") + std::to_string(n) + " " + what; };
            add(lm, lm == 1 ? "layer muted" : "layers muted"); add(ls, ls == 1 ? "layer soloed" : "layers soloed");
            add((int)instMute.size(), instMute.size() == 1 ? "instrument muted" : "instruments muted");
            add((int)instSolo.size(), instSolo.size() == 1 ? "instrument soloed" : "instruments soloed");
            return s;
        };
        editor.clearMuteSolo = [this](){
            for(int i = 0; i < (int)layerMute.size(); i++){ layerMute[i] = layerSolo[i] = false; }
            bool inst = !instMute.empty() || !instSolo.empty();
            instMute.clear(); instSolo.clear();
            if(inst) instrumentFilterChanged();
        };
        engine.laneSceneAt = [this](int bar, bool& cut){ return laneScene(bar, cut); };
        lane.onRowPlay = [this](const std::string& name){ if(editor.playScene) editor.playScene(name); };
        lane.drawPanel = [this](){ drawLanePanel(); };

        reload();
        engine.newSession();
        primePending = true;
    }

    // ---- presets: the window's settings and what is published
    void presetSave(ofJson& j) override {
        // a queued hard cut (Perform) sets Hard Cut only until the change: the patch keeps the setting
        bool tempCut = !editor.perform.cutFor.empty(), cutNow = hardCut.get();
        if(tempCut) hardCut.setWithoutEventNotifications(editor.perform.cutWas);
        ctl.save(j);
        if(tempCut) hardCut.setWithoutEventNotifications(cutNow);
        lane.save(j);
        j["formsSceneBank"] = engine.currentBank;
        // v1 backports: the Perform tab (faders, snapshots), the look, the assistant's model
        j["formsPerform"] = editor.perform.save();
        j["formsTheme"] = forms::theme::enabled();
        ofJson a = editor.aiSaved;
        if(a.is_null()) a = ofJson::object();
        editor.ai.save(a);
        j["formsAssistant"] = a;
    }
    void loadBeforeConnections(ofJson& j) override {
        ensurePatchLayers(j);
        bool legacy = !j.contains("formsPublished");
        ctl.loadPublished(j, legacyKeys);
        lane.load(j);   // rows before the timeline binds the lane
        if(j.contains("formsSceneBank") && j["formsSceneBank"].is_string()) engine.currentBank = j["formsSceneBank"].get<std::string>();
        // an old patch post-processes and sonifies with its own nodes: Output stays the plain composite
        if(legacy) postCtl.enabled = false;
    }
    void presetRecallAfterSettingParameters(ofJson& j) override {
        ensurePatchLayers(j);
        ctl.loadValues(j);
        if(j.contains("formsPerform")) editor.perform.load(j["formsPerform"]);
        if(j.contains("formsTheme") && j["formsTheme"].is_boolean()) forms::theme::enabled() = j["formsTheme"].get<bool>();
        if(j.contains("formsAssistant") && j["formsAssistant"].is_object()){ editor.aiSaved = j["formsAssistant"]; editor.ai.load(editor.aiSaved); }
        postCtl.migrate(j);   // one blur / trail setting for all layers (before 2026-11-06 evening)
    }

    void presetHasLoaded() override {
        reload();
        engine.useSetlist = useSetlist;
        pickManualScene(sceneIndex);
    }

    void update(ofEventArgs&) override {
        ensureSceneLayers();
        if(primePending){
            primePending = false;
            for(int i = 0; i < prime; i++) request(true);
        }

        engine.now = ofGetElapsedTimef();
        readGlobalPitch();
        laneMemo.clear();                 // the lane's answers, once per frame
        lane.syncRows(sceneNames);
        ctl.update();
        if(builtInPlayer) runPlayer();
        driveTransportStep();

        auto v = engine.headerValues(builtInPlayer ? head.actualBar : actualBarIn.get());
        if(v.valid){
            if(actualBPM != v.bpm) actualBPM = v.bpm;
            if(actualBars != forms::barCount(v.bars)) actualBars = forms::barCount(v.bars);   // an int output: bars it touches (0.25 -> 1)
            if(actualNoteGrid != v.notegrid) actualNoteGrid = v.notegrid;
            if(actualRoot != v.root) actualRoot = v.root;
            if(actualTrans != v.trans) actualTrans = v.trans;
            if(actualSnapshot != v.snapshot) actualSnapshot = v.snapshot;
            if(actualScale.get() != v.scale) actualScale = v.scale;
            auto& h = engine.schedule->layers[0].header;
            if(h && sceneName.get() != h->scene) sceneName = h->scene;
            if(h){
                auto it = engine.scenes.find(h->scene);
                int r0 = it != engine.scenes.end() ? engine.effectiveRoot(it->second) : (h->root >= 0 ? h->root : sceneRoot.get());   // live, as the partials
                if(sceneRoot != r0) sceneRoot = r0;
            }
            int root = sceneRoot.get();
            float r = (float)forms::chordRootMidi(root, v.root, v.trans, v.scale);
            if(rootOut.get() != r) rootOut = r;
            std::vector<float> pcs;
            for(int pc : forms::scaleGrid(root, v.trans, v.scale)) pcs.push_back((float)pc);
            if(scaleOut.get() != pcs) scaleOut = pcs;
        }
        if(const forms::Scene* next = engine.currentScene()){
            if(setlistSnapshot != next->snapshot) setlistSnapshot = next->snapshot;
        }
        scheduleOut = engine.schedule.get(); // re-set every frame, so a late connection still gets it
        scroller.immediate = immediateReplace;
        for(auto& r : rings) r.immediate = immediateReplace;

        auto& h = engine.schedule->layers[0].header;
        if(h) editor.setHeaderImage(*h);
        if(h) updatePerformanceOutputs(h->perf);
        // the window's "Use setlist" checkbox and the Setlist parameter are the same setting
        if(useSetlistLocal != useSetlist.get()){
            if(useSetlistLocal != lastUseSetlist) useSetlist = useSetlistLocal;
            else useSetlistLocal = useSetlist;
        }
        lastUseSetlist = useSetlistLocal;
        // Switch now: after the setlist setting caught up (a pad sets it this frame)
        if(switchPending){ switchPending = false; switchNowRun(switchKeepImage); switchKeepImage = false; }
        editor.imageW = imageWidth;
        editor.imageH = imageHeight;
        editor.tick();
    }

    void draw(ofEventArgs&) override {
        if(showWindow.get()) drawWindow();
    }

protected:   // (Forms GL Box derives from Forms Box)
    struct PerfOut {
        const forms::PerfParam* p = nullptr;
        ofParameter<float> f;
        ofParameter<int> i;
        ofParameter<vector<float>> v;
        float smoothed = 0;
        bool started = false;
    };
    std::vector<std::unique_ptr<PerfOut>> perfOuts;

    void updatePerformanceOutputs(const forms::json& perf){
        for(auto& o : perfOuts){
            const auto& p = *o->p;
            if(p.type == 'v'){
                auto v = forms::perfV(perf, p.key, p.def.get<std::vector<float>>());
                if(o->v.get() != v) o->v = v;
                continue;
            }
            float target = forms::perfF(perf, p.key, p.def.get<float>());
            if(p.type == 'f') target = editor.perform.apply(p, target);   // Perform macros (1: unchanged)
            o->smoothed = o->started ? p.smooth * o->smoothed + (1 - p.smooth) * target : target;
            o->started = true;
            if(p.type == 'f'){ if(o->f.get() != o->smoothed) o->f = o->smoothed; }
            else { int iv = (int)std::lround(target); if(o->i.get() != iv) o->i = iv; }
        }
    }

    // ---------------------------------------------------------------- built-in player

    // The clock, the header and the scroller, in that order, like the Forms Transport → Forms
    // Header → Layer Scroller chain of the older patches
    void runPlayer(){
        auto* L = &engine.schedule->layers[0];
        float now = ofGetElapsedTimef(), dt = ofGetLastFrameTime();
        // transport: values of the image under the header
        if(L->header){
            clock.bars = L->header->bars;
            {   // the scene's BPM as it is now (the Live tab edits it), else the image's
                auto si = engine.scenes.find(L->header->scene);
                clock.targetBpm = si != engine.scenes.end() ? si->second.bpm : L->header->bpm;
            }
            // the scene's performance settings as they are now (the Live tab edits them), else the image's
            const forms::json livePerfNow = livePerf(L->header);
            clock.perf = livePerfNow;
            head.bars = L->header->bars;
            head.perf = livePerfNow;
        }
        clock.speed = speed;
        clock.autoGo = autoGo;
        clock.pause = pause != 0;
        clock.analogMode = analogMode;
        clock.bpmAdd = bpmAdd;
        if(resumeImagesIn > 0 && --resumeImagesIn == 0) sequence = true;
        clock.sequence = sequence;
        clock.maxMinutes = maxMinutes;
        clock.limitSession = sessionLimit;
        engine.barMult = forms::quantBars(barMult);   // on the 1/16 grid, like the lengths
        engine.barAdd = barAdd;
        clock.seqFade = seqFade;
        int g = clock.goCount, r = clock.restartCount;
        clock.step(now, dt);
        int req = clock.takeSequenceRequest();
        if(req >= 0 && sequence != (req == 1)) sequence = req == 1;
        phaseOut = clock.phase;   // every frame, like a Phasor
        bpmOut = clock.bpm;       // every frame: BPM Control re-sends it
        pitchBend = clock.pitchBend;
        fade = clock.fade;
        seqMinutes = clock.seqMinutes;
        speedOut = clock.speedOut;
        bool generating = autoGo && clock.autoPng;
        if(autoPng != generating) autoPng = generating;
        if(sequenceOut != sequence.get()) sequenceOut = sequence.get();
        for(int i = r; i < clock.restartCount; i++) restartSetlist.trigger();
        for(int i = g; i < clock.goCount; i++) goTick();

        // header
        head.width = viewW;
        head.beatAt=[this](float phase,float x){return scroller.beatAt(phase,x,viewW);};
        head.phase = clock.phase;
        head.bpm = clock.bpm;
        head.header = header;
        head.headerPlus = headerPlus;
        head.smooth = smooth;
        head.autoHeader = autoHeader;
        head.tremoloOn = tremoloOn;
        head.headerMin = headerMin;
        head.headerMax = headerMax;
        head.step(now, dt);
        headerPosOut = head.headerPos;
        headerNorm = head.headerNorm;
        phLock = head.phLock;
        phOff = head.phOff;
        if(actualBarOut != head.actualBar) actualBarOut = head.actualBar;
        opacity = head.opacity;

        // scroller (layer 1, the clock's timing). It also counts the beat under the header, which
        // the other layers follow.
        scroller.keepAlpha = true;
        // each image's opacity: its scene's layer setting now (edits show at once), else its own
        auto opacity = [this](const forms::Image& im) -> float {
            return engine.imageOpacity(im);
        };
        auto drawGain=[this](const ofTexture& t,float x,float w,float h,float g,float fraction,float u0,float uw){layerGain.draw(t,x,w,h,g,fraction,u0,uw);};
        scroller.drawWithGain=drawGain;
        scroller.opacityOf = opacity;
        for(auto& r : rings){
            r.drawWithGain=drawGain;
            r.opacityOf = opacity;
            r.hardCutIn = [this](double a, double b){ return hardCutIn(a, b); };
            r.sceneAt = [this](double beat){ return engine.sceneAtBeat(beat); };
        }
        scroller.defaultBars = forms::barCount(clock.bars);
        // the live buffers' depth (Forms GL Box's option; 16 bits unless switched off)
        { const int fmt = liveFormat(); scroller.bufferFormat = fmt; for(auto& r : rings) r.bufferFormat = fmt; layerFx.internalFormat = fmt; post.internalFormat = fmt; }
        scroller.update(L, clock.phase, head.headerPos, viewW, viewH);
        if(scroller.beatRebased){ feed.restart(); laneMemo.clear(); }
        engine.schedule->headerBeat = scroller.headerBeat;
        engine.hardCutManual = hardCut;
        // layer 1's image reached the header: line the plan up with where it really started
        if(L->header && L->header != lastSyncedHeader){
            lastSyncedHeader = L->header;
            engine.syncLayer1(L->header, scroller.headerStart);
        }
        if(inHeader.get() != scroller.inHeader) inHeader = scroller.inHeader;

        // Phase 3: the extra layers (their scenes follow layer 1's plan, bar by bar)
        if(autoGo) send(engine.requestLayers());
        const forms::Scene* cs = engine.sceneAtHeader();
        int nLayers = 1;
        for(int i = 1; i < (int)engine.schedule->layers.size(); i++){
            auto& Li = engine.schedule->layers[i];
            if((cs && i < (int)cs->layers.size()) || !Li.slots.empty() || !Li.ready.empty()) nLayers = i + 1;
        }
        for(int i = 1; i < nLayers; i++){
            auto& ring = rings[i - 1];
            ring.active = cs && i < (int)cs->layers.size();
            // the layer's beat offset (its scene's, live): it shows what the others showed that many beats ago
            double offsetBeats = (cs && i < (int)cs->layers.size()) ? cs->layers[i].beatOffset : 0.0;
            ring.update(engine.schedule->layer(i), scroller.headerBeat - offsetBeats, head.headerPos, viewW, viewH);
        }
        logBarLine(nLayers);
        logTimeline(nLayers);
        // Forms GL Box's audition: its page alone on the stage (drawn into layer 1's buffer), and the
        // image the sound reads, for this frame; the playback underneath goes on
        int stageLayers = nLayers;
        exportLayerCount = stageLayers;
        std::shared_ptr<forms::Image> keepHeader;
        engine.trackPlayed();        // Previous image: what layer 1 showed
        bool audition = auditionStage();
        gateBypass = audition;       // audition is heard alone, whatever is muted
        if(audition){ stageLayers = 1; keepHeader = engine.schedule->layers[0].header; engine.schedule->layers[0].header = auditionImage(); }
        layerEffects(stageLayers);   // before the composite and the sound: both use the processed layers
        applyCovers(stageLayers);
        composite(stageLayers);
        runSound(stageLayers);
        if(audition) engine.schedule->layers[0].header = keepHeader;

        if(saveFramePending){
            saveFramePending = false;
            saveComposite(ofToDataPath("FormsBox_frame", true));
            scroller.saveFrame(ofToDataPath("FormsBox_frame_layer1", true), clock.phase);
            saveOutputPending = true;
        }
        // the final image: post-processing (formsPost.h)
        postCtl.apply(post);
        output = compositeFbo.isAllocated() ? const_cast<ofTexture*>(post.process(compositeFbo.getTexture(), head.headerPos, head.opacity, ofGetLastFrameTime())) : nullptr;
        // the background colour, last (black: skipped, Output exactly as before)
        if(output && (postCtl.paper || !forms::backgroundIsBlack(postCtl.bgR, postCtl.bgG, postCtl.bgB)))
            output = const_cast<ofTexture*>(applyBackground(*output, stageLayers));
        // edge feathers, the very last (all 0: skipped), toward the background colour
        if(output && post.edges.active()){
            bool black = forms::backgroundIsBlack(postCtl.bgR, postCtl.bgG, postCtl.bgB);
            ofFloatColor target = postCtl.paper && black ? ofFloatColor(1, 1, 1) : ofFloatColor(postCtl.bgR, postCtl.bgG, postCtl.bgB);
            output = const_cast<ofTexture*>(post.feather(*output, target));
        }
        if(exportPending) exportCurrentFrame();
        if(saveOutputPending && output){   // and the final image as it is seen (Output)
            saveOutputPending = false;
            ofShortPixels px;
            if(output == &compositeFbo.getTexture()) compositeFbo.readToPixels(px);
            else if(output == &bgFbo.getTexture()) bgFbo.readToPixels(px);
            else if(output == &post.featherFbo.getTexture()) post.featherFbo.readToPixels(px);
            else post.fbo.readToPixels(px);
            px.setNumChannels(3);
            ofSaveImage(px, ofToDataPath("FormsBox_frame_output.png", true));
        }
    }

    // The first hard cut (layer 1's plan, or predicted from the setlist) at a beat in (a, b], or -1
    // (the layer rings never repeat an image across one)
    double hardCutIn(double a, double b) const { return engine.hardCutIn(a, b); }

    // ---------------------------------------------------------------- effects before the sound

    // Live GPU buffers: 16 bits per channel (GL_RGBA16), or 8 (GL_RGBA) as a fallback if 16 costs
    // frame rate. The scrollers, the layer rings, the layer effects, the composite, the background
    // and the post-processing follow it. The images are drawn in 16 bits either way.
    int liveBits = 16;
    int liveFormat() const { return liveBits == 8 ? GL_RGBA : GL_RGBA16; }
    int compositeFormat = 0, bgFormat = 0;
    forms::LayerGain layerGain;
    forms::LayerFx layerFx;
    // H blur, V blur and the trail on every layer's buffer; the trail's length is in beats, so
    // each layer gets its own pixels (its images scroll at their own speed)
    // each layer its own settings (Post-processing tab, L1..L4)
    forms::LayerFxSettings layerFxSettings(int l, double lenBeats) const {
        forms::LayerFxSettings fx;
        if(!postCtl.anyFx(l)) return fx;
        fx.hBlur = postCtl.hBlur[l]; fx.vBlur = postCtl.vBlur[l]; fx.trailLevel = postCtl.trailLevel[l];
        fx.trailPx = postCtl.trailBeats[l] * viewW / std::max(1.0, lenBeats);
        fx.maskAmount=postCtl.maskAmount[l];fx.maskWidth=postCtl.maskWidth[l];fx.maskHeight=postCtl.maskHeight[l];
        fx.maskBlur=postCtl.maskBlur[l];fx.maskContrast=postCtl.maskContrast[l];fx.maskThreshold=postCtl.maskThreshold[l];fx.maskInvert=postCtl.maskInvert[l];fx.maskSeed=postCtl.maskSeed[l];
        fx.maskSpeed=postCtl.maskAnimate[l]?postCtl.maskSpeed[l].get():0;
        fx.maskBeat=scroller.headerBeat;fx.maskHeaderX=head.headerPos;fx.maskPixelsPerBeat=viewW/std::max(1.0,lenBeats);
        fx.maskPhase=(scroller.headerBeat+head.repeatPx/std::max(1.f,(float)viewW)*scroller.headerLen)*fx.maskSpeed;

        return fx;
    }
    // The layer effects belong to the scene: the controls (Post-processing tab, the node, a timeline)
    // show and drive the ones of the scene under the header. Scene -> controls when another scene
    // comes on stage or its stored effects were edited (the layer cards in Stage); controls -> scene
    // when a control moved by itself (then the scene has unsaved edits).
    std::string fxSyncScene;
    std::vector<forms::LayerFxValues> fxSynced;
    void syncSceneLayerFx(){
        const forms::Scene* hs = engine.sceneAtHeader();
        if(!hs) return;
        auto it = engine.scenes.find(hs->name);
        if(it == engine.scenes.end()) return;
        forms::Scene& sc = it->second;
        const bool arrived = sc.name != fxSyncScene;
        ensureLayerCount(sc.layers.size());
        const int n = postCtl.FX_LAYERS;
        if((int)fxSynced.size() < n) fxSynced.resize(n);
        for(int l = 0; l < n; l++){
            const bool has = l < (int)sc.layers.size();
            const forms::LayerFxValues stored = forms::LayerFxValues::from(has ? sc.layers[l].fx : forms::json(), l);
            const forms::LayerFxValues live = postCtl.fxValues(l);
            if(arrived || !(stored == fxSynced[l])){
                if(!(live == stored)) postCtl.setFxValues(l, stored);
                fxSynced[l] = stored;
            }else if(has && !(live == fxSynced[l])){
                sc.layers[l].fx = live.toJson(l);
                if(sc.layers[l].fx.empty()) sc.layers[l].fx = forms::json();
                fxSynced[l] = live;
                editor.dirty.insert(sc.name);
            }
        }
        fxSyncScene = sc.name;
    }
    void layerEffects(int nLayers){
        syncSceneLayerFx();
        layerFx.process(scroller.fbo, layerFxSettings(0, scroller.headerLen));
        for(int i = 1; i < nLayers; i++){
            auto& r = rings[i - 1];
            double len = r.current ? 4.0 * std::max(1.0 / forms::kBarGrid, r.current->bars) : scroller.headerLen;
            layerFx.process(r.fbo, layerFxSettings(i, len));
        }
    }

    formscover::Compositor coverCompositor;
    void applyCovers(int nLayers){
        std::vector<ofFbo*> buffers{&scroller.fbo};
        for(int i=1;i<nLayers;++i) buffers.push_back(&rings[i-1].fbo);
        std::vector<int> order;
        const forms::Scene* scene=engine.sceneAtHeader();
        if(scene) for(int i:forms::stackOrder(scene->layers)) if(i<nLayers) order.push_back(i);
        for(int i=0;i<nLayers;++i) if(std::find(order.begin(),order.end(),i)==order.end()) order.push_back(i);
        std::vector<formscover::Compositor::Source> sources;
        auto page=[&](int layer,std::shared_ptr<forms::Image> im,float x,float fraction){
            if(!im) return;
            float opacity=layer==0 ? (scroller.opacityOf ? scroller.opacityOf(*im) : 1.f) :
                (rings[layer-1].opacityOf ? rings[layer-1].opacityOf(*im) : 1.f);
            if(opacity<=0) return;
            for(size_t k=0;k<im->covers.size();++k){
                auto settings=im->covers[k].settings;
                sources.push_back({settings,layer,(float)viewW/(4.f*(float)std::max(1.0/forms::kBarGrid,im->bars)),false,
                    [im,k,x,fraction,opacity,this](int w,int h){
                        formscover::Compositor::drawMask(im->covers[k],x*w/(float)viewW,w,fraction*w,h,opacity,(float)im->sliceU0(),(float)im->sliceUW());
                    }});
            }
        };
        for(int i:order){
            if(!layerAudible(i)) continue;
            auto im=i==0 ? engine.schedule->layers[0].header : rings[i-1].current;
            if(im){
                auto sit=engine.scenes.find(im->scene);
                {
                    auto settings=formscover::read(sit!=engine.scenes.end() && i<(int)sit->second.layers.size() ? sit->second.layers[i].cover : im->layerCover);
                    if(settings.enabled) sources.push_back({settings,i,(float)viewW/(4.f*(float)std::max(1.0/forms::kBarGrid,im->bars)),true,
                        [&,i](int w,int h){ofSetColor(255);buffers[i]->draw(0,0,w,h);}});
                }
            }
            if(gateBypass) continue; // the audition is alone; no other layer to cover
            if(i==0){ for(auto& v:scroller.visiblePages()) page(0,v.image,v.x,1); }
            else for(auto& d:rings[i-1].lastDrawn){
                for(auto& slot:engine.schedule->layers[i].slots) if(slot.img && slot.img->png==d.png && slot.start==d.start){
                    if(rings[i-1].drawable(slot.img)) page(i,slot.img,d.x,(float)((slot.end()-slot.start)/slot.len));
                    break;
                }
            }
        }
        coverCompositor.apply(buffers,order,sources,viewW,viewH);
    }

    // ---------------------------------------------------------------- transport and timeline

    forms::SceneLane lane;
    std::map<int, std::pair<std::string, bool>> laneMemo;
    ofParameter<bool> driveTransport;
#ifdef FORMS_HAS_TRANSPORT
    std::weak_ptr<ofxOceanodeTransport> claimed;
#endif
    forms::TransportFeed feed;
    std::string transportStatus;

#ifdef OFXOCEANODE_HAS_TIMELINE_INSTRUMENT
public:
    // The scene lane: a chord bank for the timeline's Chords lane, a row per scene
    bool getTimelineInstrument(ofxOceanodeTimelineInstrument& out) override {
        if(!lane.handle) return false;
        lane.syncRows(sceneNames);
        lane.instrument(out, std::set<std::string>(sceneNames.begin(), sceneNames.end()));
        return true;
    }
protected:   // (Forms GL Box derives from Forms Box)
#endif

    bool drivingTransport() const {
#ifdef FORMS_HAS_TRANSPORT
        auto r = claimed.lock();
        return r && r->getExternalClockOwner() == this;
#else
        return false;
#endif
    }

    // The lane's scene for layer 1's image starting at `bar`: read ahead from the timeline while
    // Forms Box drives the transport (the timeline's beats are then Forms Box's), else the scene
    // the lane plays now
    std::string laneScene(int bar, bool& cut){
        auto it = laneMemo.find(bar);
        if(it == laneMemo.end()){
            std::string n = drivingTransport() ? lane.at(this, bar * 4.0 + 1e-3) : lane.live();
            it = laneMemo.emplace(bar, std::make_pair(n, !n.empty() && lane.hardCut.count(n) > 0)).first;
        }
        cut = it->second.second;
        return it->second.first;
    }

    // Drive Transport: Forms Box is the transport's external clock (the hook MIDI clock uses): every
    // frame its beat under the header (beat repeats left out: the transport never runs back), its
    // tempo as it really moves, and whether it plays. A header jump is a seek.
    void driveTransportStep(){
#ifdef FORMS_HAS_TRANSPORT
        auto* c = getHostContainer();
        auto root = c ? c->getRootTransport() : nullptr;
        if(!(driveTransport && builtInPlayer && root)){ releaseTransport(); transportStatus = driveTransport ? "no transport" : ""; return; }
        if(claimed.lock() != root || root->getExternalClockOwner() != this){
            if(root->hasExternalClock() && root->getExternalClockOwner() != this && !claimed.lock()){
                transportStatus = "another clock drives the transport (MIDI clock?)";
            }
            root->claimExternalClock(this);
            claimed = root;
            feed.restart();
        }
        // the beat under the header without the beat repeat, read across page edges (a repeat can
        // reach back into the previous page when pages are short)
        feed.step(scroller.beatAt(clock.phase, head.headerPos + head.repeatPx, viewW), scroller.headerLen, 0, viewW, clock.bpm, pause != 0, ofGetLastFrameTime());
        ofxOceanodeTransport::ExternalClockUpdate u;
        u.beat = feed.beat;
        u.steadyTimeUs = ofxOceanodeTransport::steadyNowUs();
        u.bpm = (float)feed.bpm;
        u.playing = feed.playing;
        u.jump = feed.jump;
        u.maxLeadBeats = 0.25;
        root->updateExternalClock(this, u);
        transportStatus = "driving: beat " + ofToString(feed.beat, 1) + ", " + ofToString(feed.bpm, 1) + " BPM";
#endif
    }
    void releaseTransport(){
#ifdef FORMS_HAS_TRANSPORT
        if(auto r = claimed.lock()) r->releaseExternalClock(this);
        claimed.reset();
#endif
    }

    // The lane's panel in the timeline (its "Scenes" tab)
    void drawLanePanel(){
        ImGui::TextWrapped("A row per Forms Box scene: a block plays that scene. Where the lane has no block, the setlist (or the manual scene) plays.");
        ImGui::TextWrapped(drivingTransport() ? "Forms Box drives the transport: each image gets the scene of the block at the bar it starts (read ahead)."
                                              : "Forms Box does not drive the transport: the scene the lane plays now is used for the next images. Turn on Drive Transport (Play tab) to place scenes exactly.");
        if(!lane.hardCut.empty()){
            std::string cuts; for(auto& s : lane.hardCut) cuts += (cuts.empty() ? "" : ", ") + s;
            ImGui::TextDisabled("Hard cut into: %s", cuts.c_str());
        }
        ImGui::TextDisabled("Right-click a row: hard cut into that scene, or play it now.");
    }

    // The Play tab's controls (the window's widgets write these parameters)
    void drawPlayControls(){
        float z = ofxOceanodeShared::getZoomLevel();
        if(!builtInPlayer){
            ImGui::TextDisabled("Built-in Player is off (inspector): nothing plays here; the Schedule output carries the images.");
            return;
        }
        forms::savedWithPatchNote();
        // two columns: the transport | the header and, under it, the scroller
        if(!ImGui::BeginTable("##play", 2, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) return;
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Transport");
        ImGui::PushItemWidth(-90 * z);
        forms::paramCheck("Pause", pause); ctl.nodeMenu(pause.getEscapedName());
        ImGui::SameLine();
        forms::paramCheck("Sequence", sequence); ctl.nodeMenu(sequence.getEscapedName());
        ImGui::SameLine();
        ctl.check(analogMode, "Analog");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Analog Mode");
        ImGui::SameLine();
        if(ImGui::Button("New PNG")) newPng.trigger();
        ctl.check(driveTransport, "Drive transport");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The transport (timelines, synced Phasors, box synths) follows Forms Box's beat under the header,\nits tempo and Pause. Off: the transport runs on its own and Forms Box ignores it.");
        if(!transportStatus.empty()){ ImGui::SameLine(); ImGui::TextDisabled("%s", transportStatus.c_str()); }
#ifdef FORMS_HAS_TIMELINE_MENU
        if(ImGui::Button("Scene lane to timeline")){
            lane.syncRows(sceneNames);
            if(!ctl.timeline.addChordTrack(*lane.handle, "Scenes")) engine.note("no timeline to put the scene lane on");
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("A timeline track with a Scenes lane: a row per scene, draw blocks to choose what plays when.\nRight-click a row for its hard cut. With Drive transport on, scenes start exactly at their blocks.");
        ImGui::SameLine();
        ImGui::TextDisabled(lane.onTimeline(this) ? (drivingTransport() ? "lane: read ahead" : "lane: live (drive the transport to place it exactly)") : "no scene lane");
#endif
        forms::paramSlider("Speed", speed, "%.2f"); ctl.nodeMenu(speed.getEscapedName());
        ctl.slider(bpmAdd, "%.2f", "BPM Add");
        ctl.check(sessionLimit, "Session time limit");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("On: the session ends after Max Minutes (the sequence goes off, no new images, Seq Fade fades the output), as in the installation.\nOff (default): images never stop by themselves.");
        ImGui::BeginDisabled(!sessionLimit);
        ctl.slider(maxMinutes, "%.1f", "Max Minutes");
        ImGui::EndDisabled();
        forms::paramSlider("Seq Fade", seqFade, "%.2f"); ctl.nodeMenu(seqFade.getEscapedName());
        forms::meter("Phase", phaseOut, 0, 1, "%.3f");
        forms::meter("Session", seqMinutes, 0, std::max(0.1f, maxMinutes.get()), "%.1f min");
        forms::meter("Fade", fade, 0, 1, "%.2f");
        ImGui::Text("BPM %.1f   pitch bend %+.2f   %s", bpmOut.get(), pitchBend.get(), autoPng.get() ? "auto images on" : "auto images off");
        ImGui::PopItemWidth();

        ImGui::TableNextColumn();
        ImGui::SeparatorText("Header");
        ImGui::PushItemWidth(-90 * z);
        forms::paramSlider("Header", header, "%.3f"); ctl.nodeMenu(header.getEscapedName());
        forms::paramSlider("Header+", headerPlus, "%.3f"); ctl.nodeMenu(headerPlus.getEscapedName());
        forms::paramSlider("Smooth", smooth, "%.4f"); ctl.nodeMenu(smooth.getEscapedName());
        ctl.check(tremoloOn, "Tremolo");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The scenes' opacity tremolo (a wobble of the image's opacity). Off: the opacity only follows\nthe beat-repeat fade and the strobe gate.");
        ctl.check(autoHeader, "Auto Header");
        if(autoHeader){
            ctl.slider(headerMin, "%.3f", "Header Min");
            ctl.slider(headerMax, "%.3f", "Header Max");
        }
        forms::meter("phLock", phLock, 0, 1, "%.3f");
        forms::meter("Opacity", opacity, 0, 1, "%.2f");
        ImGui::Text("bar %d   header at %.0f px", actualBarOut.get(), headerPosOut.get());
        ImGui::PopItemWidth();

        ImGui::SeparatorText("Scroller");
        ImGui::Text("%d x %d", viewW.get(), viewH.get());
        std::string f = inHeader.get();
        auto slash = f.find_last_of('/');
        ImGui::TextDisabled("in header: %s", slash == std::string::npos ? f.c_str() : f.substr(slash + 1).c_str());
        if(ImGui::Button("Save Frame")) saveFramePending = true;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Saves the output as data/FormsBox_frame.png (and which images were showing)");
        ImGui::EndTable();
    }

    forms::Transport clock;
    forms::Header head;
    forms::ScrollerCore scroller;
    std::deque<forms::LayerRing> rings = std::deque<forms::LayerRing>(3); // one per extra layer
    ofFbo compositeFbo;
    int lastLoggedBar = -1;
    std::shared_ptr<forms::Image> lastSyncedHeader;

    // Reads `rows` pixels of column x of an FBO, from row y0 (row 0 = top), as RGBA bytes. The
    // pixel-pack state is set and restored around it: other nodes may leave a PBO bound (which
    // turns our pointer into an offset in GPU memory) or other row settings.
    static bool readColumn(ofFbo& f, int x, int y0, int rows, std::vector<unsigned short>& px){   // 16 bits per channel
        if(!f.isAllocated() || rows < 1) return false;
        int w = (int)f.getWidth(), h = (int)f.getHeight();
        x = ofClamp(x, 0, w - 1);
        y0 = ofClamp(y0, 0, h - 1);
        rows = std::min(rows, h - y0);
        px.assign(rows * 4 + 64, 0);
        GLint prevFb = 0, prevPbo = 0, pAlign = 4, pRow = 0, pSkipR = 0, pSkipP = 0;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevFb);
        glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &prevPbo);
        glGetIntegerv(GL_PACK_ALIGNMENT, &pAlign);
        glGetIntegerv(GL_PACK_ROW_LENGTH, &pRow);
        glGetIntegerv(GL_PACK_SKIP_ROWS, &pSkipR);
        glGetIntegerv(GL_PACK_SKIP_PIXELS, &pSkipP);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glPixelStorei(GL_PACK_SKIP_ROWS, 0);
        glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, f.getId());
        glReadPixels(x, y0, 1, rows, GL_RGBA, GL_UNSIGNED_SHORT, px.data());   // FBO row 0 = the image's top
        glBindFramebuffer(GL_READ_FRAMEBUFFER, prevFb);
        glPixelStorei(GL_PACK_ALIGNMENT, pAlign);
        glPixelStorei(GL_PACK_ROW_LENGTH, pRow);
        glPixelStorei(GL_PACK_SKIP_ROWS, pSkipR);
        glPixelStorei(GL_PACK_SKIP_PIXELS, pSkipP);
        glBindBuffer(GL_PIXEL_PACK_BUFFER, prevPbo);
        return true;
    }

    // ---------------------------------------------------------------- sonification
    //
    // Kick detection (what Macro_4 did: the bottom strip of the header column, mean brightness over
    // a threshold, times the scene's Kick detect), read from the kick layer (a scene layer marked
    // "kick"; none: layer 1). Then, while the Audio output is wired to an SC Output, every layer's
    // header column feeds its bank (formsSound.h). Columns are read only when they are needed.
    float kickLevel = 0;
    double lastKickAt = -1e9;
    int kicksFired = 0;
    forms::SoundSettings snd;
    forms::LRBalance lrBal;
    std::vector<std::vector<unsigned short>> cols = std::vector<std::vector<unsigned short>>(4);
    struct LayerKey { int root = -1000, notegrid = -1, trans = 0; float q = -1; std::vector<float> scale; std::vector<float> settings; };
    std::vector<LayerKey> layerKeys = std::vector<LayerKey>(4);
    std::vector<forms::PartialLayout> layouts = std::vector<forms::PartialLayout>(4);
    std::vector<int> layoutH = std::vector<int>(4), layoutGrid = std::vector<int>(4), layoutImageH = std::vector<int>(4);
    float duckView = 0;
    std::map<std::string, float> fxSent;
#ifdef FORMS_HAS_SC
    forms::SoundFrame frame;
#endif
    forms::SoundView soundView;
    // ---- mute / solo (performance state: not in the scene, not saved with the patch)
    // Layers: a muted layer (or one not soloed while another is) is neither drawn in the composite /
    // background nor heard, nor does it fire the kick; it keeps scrolling and receiving its pages, so
    // unmuting is instant. Muted wins over soloed. Audition is heard alone regardless (gateBypass).
    std::deque<bool> layerMute = std::deque<bool>(4,false), layerSolo = std::deque<bool>(4,false);
    bool gateBypass = false;
    bool anyLayerSolo() const { for(bool b : layerSolo) if(b) return true; return false; }
    bool layerAudible(int i) const {
        if(gateBypass || i < 0 || i >= (int)layerMute.size()) return true;
        if(layerMute[i]) return false;
        return !anyLayerSolo() || layerSolo[i];
    }
    // Instruments (by preset ref): the pages are drawn again without them (Forms GL Box)
    std::set<std::string> instMute, instSolo;
    virtual void instrumentFilterChanged(){}
    virtual void sceneWasRenamed(const std::string&, const std::string&){}
    int previewKick = -1;

    float perfOutValue(const std::string& key, float def) const {
        for(auto& o : perfOuts) if(o->p->key == key) return o->p->type == 'f' ? o->f.get() : o->p->type == 'i' ? (float)o->i.get() : def;
        return def;
    }
    // the scene's live performance settings (edits apply at once), else the image's
    forms::json livePerf(const std::shared_ptr<forms::Image>& img) const {
        if(!img) return forms::json::object();
        auto it = engine.scenes.find(img->scene);
        if(it != engine.scenes.end() && it->second.performance.is_object() && !it->second.performance.empty()) return forms::completePerformance(it->second.performance);
        return img->perf;
    }
    std::vector<std::string> kickBaseNames() const {
        std::vector<std::string> n;
#ifdef FORMS_HAS_SC
        if(sound) for(auto& k : sound->kicks()) n.push_back(ofFilePath::getFileName(k));
#endif
        return n;
    }
    // which sample a scene's kick plays: its file name if set, else its number (kickSelect)
    const forms::Scene* kickScene() const {
        auto h=engine.schedule->layers[0].header;
        if(h){auto it=engine.scenes.find(h->scene);if(it!=engine.scenes.end())return &it->second;}
        return engine.currentScene();
    }
    int kickIndexFor(const forms::json& perf, const std::vector<std::string>& names,bool folderOnly=false) const {
        auto scene=kickScene();
        if(!folderOnly && scene && scene->kickSample){
#ifdef FORMS_HAS_SC
            auto path=engine.kickFile(*scene).string();
            if(sound && !path.empty()){
                auto& files=sound->kicks();auto it=std::find(files.begin(),files.end(),path);
                if(it!=files.end())return (int)(it-files.begin());
            }
#endif
            return -1;
        }
        if(names.empty()) return -1;
        if(perf.contains("kickSample") && perf["kickSample"].is_string()){
            std::string want = perf["kickSample"].get<std::string>();
            for(int i = 0; i < (int)names.size(); i++) if(names[i] == want) return i;
        }
        int k = (int)std::lround(forms::perfF(perf, "kickSelect", 11));
        return ofClamp(k, 0, (int)names.size() - 1);
    }

    void runSound(int nLayers){
        soundCtl.checkKickFolder();   // a saved folder that is gone: the app's own kicks
        soundCtl.apply(snd);
        float dt = ofGetLastFrameTime();
        double now = ofGetElapsedTimef();
        bool engineOn = false;
#ifdef FORMS_HAS_SC
        if(sound){
            sound->setKickFolder(snd.kickFolder);
            if(auto scene=kickScene()){
                auto file=engine.kickFile(*scene);
                if(!file.empty()) sound->addKickFile(file.string());
            }
            sound->poll();   // a graph waiting for its SynthDefs
            engineOn = sound->running();
            soundView.waitingForDefs = sound->waitingForDefs();
        }
#endif
        auto names = kickBaseNames();
        if(names != editor.kickNames) editor.kickNames = names;
        bool playing = engineOn && snd.on;

        // the layers' buffers and images
        int count=layerMute.size();
        std::vector<ofFbo*> fbos(count,nullptr); fbos[0]=&scroller.fbo;
        std::vector<std::shared_ptr<forms::Image>> imgs(count);
        imgs[0] = engine.schedule->layers[0].header;
        for(int i = 1; i < nLayers; i++){ fbos[i] = &rings[i - 1].fbo; imgs[i] = rings[i - 1].current; }
        int kl = 0;
        if(auto scene=kickScene()) kl=forms::kickLayer(scene->layers);
        else for(int i=1;i<nLayers;++i)if(imgs[i]&&imgs[i]->kick){kl=i;break;}
        if(kl>=count)kl=0;
        int x = (int)head.headerPos;

        // columns: every playing layer whole, else only the kick strip
        std::vector<bool> haveCol(count,false);
        if(playing){
            for(int l = 0; l < count; l++)
                if(fbos[l] && imgs[l] && snd.layers[l].on && layerAudible(l) && fbos[l]->isAllocated())
                    haveCol[l] = readColumn(*fbos[l], x, 0, (int)fbos[l]->getHeight(), cols[l]);
        }

        // ---- kick detection
        ofFbo* kf = fbos[kl];
        std::shared_ptr<forms::Image> kimg = imgs[kl];
        if(!kf || !kf->isAllocated() || !kimg || !layerAudible(kl)){ kickLevel = 0; if(kickOut.get() != 0) kickOut = 0; }
        else{
            int h = (int)kf->getHeight();
            const forms::json kperf = livePerf(kimg);   // the kick scene's own detection settings
            const float kThreshold = forms::perfF(kperf, "kickThreshold", snd.kickThreshold);
            int strip = std::max(1, std::min(h, (int)std::lround(h * forms::perfF(kperf, "kickStrip", snd.kickStrip))));
            const unsigned short* px = nullptr;
            std::vector<unsigned short> sp;
            if(haveCol[kl]) px = cols[kl].data() + (h - strip) * 4;
            else if(readColumn(*kf, x, h - strip, strip, sp)) px = sp.data();
            double sum = 0;
            if(px) for(int i = 0; i < strip; i++) sum += (px[i * 4] + px[i * 4 + 1] + px[i * 4 + 2]) / (3 * 65535.0);
            kickLevel = (float)(sum / strip);
            bool on = forms::perfF(kperf, "kDetectOn", 1) > 0.5f;
            int k = (on && kickLevel > kThreshold) ? 1 : 0;
            kickThresholdNow = kThreshold;
            if(k && !kickOut.get() && snd.kickOn && now - lastKickAt >= forms::perfF(kperf, "kickMinGap", snd.kickMinGap)){
                lastKickAt = now;
                kicksFired++;
#ifdef FORMS_HAS_SC
                frame.kickCount++;
#endif
            }
            if(kickOut.get() != k) kickOut = k;
        }

        // ---- the sound frame
        auto hv = engine.headerValues(head.actualBar);
        forms::json perf = livePerf(imgs[0]);
        float bpm = std::max(1.0f, bpmOut.get());
        float duckDecay = snd.duckBeats * forms::perfF(perf, "duckScale", 1) * 60.0f / bpm;
        duckView=forms::duckEnvelope(now-lastKickAt,duckDecay,snd.duckCurve);
        float imageMul = postCtl.enabled ? postCtl.val.get() * (postCtl.useOpacity ? std::max(0.001f, head.opacity) : 1.0f) : head.opacity;
        lrBal.step(head.phLock, imgs[0] ? imgs[0]->bars : 8, bpm, dt, perf, snd.lrBalance);
        for(int l = 0; l < count; l++){
            soundView.layerActive[l] = haveCol[l];
            soundView.layerLevel[l] = 0;
        }
#ifdef FORMS_HAS_SC
        if(playing){
            float sideChain = forms::perfF(perf, "sideChain", 1);
            for(int l = 0; l < count; l++){
                auto& L = frame.layers[l];
                L.active = haveCol[l];
                L.level = snd.layers[l].level;
                // the reverb send: the scene's reverbAmt (Macro_2 → Macro_3 passed it through) x Reverb x scene x the layer's Reverb
                L.send = ofClamp(forms::perfF(perf, "reverbAmt", 0.1119f) * soundCtl.reverbMult.get() * snd.layers[l].reverb, 0, 1);
                if(!haveCol[l]){ L.amps.clear(); continue; }
                int h = (int)fbos[l]->getHeight();
                const forms::Image& im0 = *imgs[l];
                // the partials' layout follows the renderer's rows (column height, note grid, image height)
                auto& lay = layouts[l];
                if(lay.rel.empty() || layoutH[l] != h || layoutGrid[l] != im0.notegrid || layoutImageH[l] != (int)imageHeight){
                    layoutH[l] = h; layoutGrid[l] = im0.notegrid; layoutImageH[l] = imageHeight;
                    lay = forms::partialLayout(h, im0.notegrid, imageHeight);
                    L.pitch.clear();   // pitches again
                }
                forms::columnAmps(cols[l].data(), h, imageMul, lay, L.amps);
                float sum = 0; for(float a : L.amps) sum += a;
                soundView.layerLevel[l] = sum / forms::SOUND_PARTIALS;
                // the partials' pitches, gains and duck weights: again when the harmony or the settings change
                const forms::Image& im = *imgs[l];
                auto sit = engine.scenes.find(im.scene);
                LayerKey k;
                // the scene's root now (the renderer never draws the root, only degrees: a root edit
                // acts at once on every page of the scene); the page's own root once its scene is gone
                k.root = sit != engine.scenes.end() ? engine.effectiveRoot(sit->second) : (im.root >= 0 ? im.root : 24);
                k.notegrid = im.notegrid;
                k.trans = hv.valid ? hv.trans : 0;
                // the grid is the scale this layer's page was drawn with (pages on stage can be on
                // different scales while a scale or scene change propagates layer by layer)
                k.scale = !im.scaleNotes.empty() ? im.scaleNotes : hv.scale;
                k.q = forms::perfF(livePerf(imgs[l]), "scaleQuant", 0);
                const auto& S = snd;
                k.settings = {S.layers[l].pan, S.layers[l].spread, S.loudnessBlend, S.spreadLow, S.spreadHigh,
                              S.duckBelow, S.duckDepth * sideChain, S.duckShape,S.duckFullBand?1.f:0.f};
                auto& K = layerKeys[l];
                if(L.pitch.empty() || k.root != K.root || k.notegrid != K.notegrid || k.trans != K.trans || k.q != K.q || k.scale != K.scale || k.settings != K.settings){
                    K = k;
                    L.pitch = forms::partialPitches(k.root, lay, k.trans, k.scale, k.q);
                    forms::partialGains(L.pitch, l, S, L.gl, L.gr);
                    forms::SoundSettings ds = S; ds.duckDepth = S.duckDepth * sideChain;
                    L.duckw = forms::imageDuckWeights(L.pitch, ds);
                    L.arraysVersion++;
                }
            }
            // the right bank is detuned up (FormsBank512's detune): both banks shifted down by half,
            // so the pair is centred on the pitch (the macros' notes)
            frame.bend = clock.pitchBend + gpTranspose - snd.detune * 0.5f;   // Global Pitch Transpose bends the whole spectrum (glides with it)
            frame.detune = snd.detune;
            frame.bankGain = snd.bankGain;
            frame.lag = snd.lag;
            int ki = kickIndexFor(perf, names);
            bool previewSample=previewKick>=0;
            if(previewSample){ ki = previewKick; previewKick = -1; frame.kickCount++; }
            frame.kickSample = ki;
            frame.kickSynth=!previewSample && forms::perfF(perf,"kickSynth",0)>.5f;
            frame.kickStart=forms::perfF(perf,"kickPitchStart",160);frame.kickEnd=forms::perfF(perf,"kickPitchEnd",48);
            frame.kickDecay=forms::perfF(perf,"kickDecay",.45f);frame.kickClick=forms::perfF(perf,"kickClick",.3f);
            frame.kickNoise=forms::perfF(perf,"kickNoise",0);frame.kickDrive=forms::perfF(perf,"kickDrive",.1f);
            frame.imageDuckDepth=snd.duckFullBand?ofClamp(snd.duckDepth*sideChain,0,1):0;
            frame.kickRate = std::pow(2.0f, ((hv.valid ? hv.trans : 0) + forms::perfF(perf, "kickTune", snd.kickTune) + gpTranspose) / 12.0f);
            frame.kickAmp = forms::perfF(perf, "kickVol", 0.16f);
            frame.kickGate=head.gate;
            frame.kickGain = snd.kickGain;
            frame.duckDecay = duckDecay;
            frame.duckCurve = snd.duckCurve;
            frame.comp = ofClamp(forms::perfF(perf, "Compressor", 0), 0, 1);
            frame.fxGain = perfOutValue("SpectrumGain", forms::perfF(perf, "SpectrumGain", 2));
            frame.fade = clock.fade;
            frame.lgain = lrBal.l;
            frame.rgain = lrBal.r;
            frame.volume = snd.volume;
            frame.limit = snd.limit > 0;
            auto fx = [&](const char* key, float v){
                auto it = fxSent.find(key);
                if(it != fxSent.end() && it->second == v) return;
                fxSent[key] = v;
                sound->setFx(key, v);
            };
            fx("revtime", soundCtl.revTime);
            fx("lowdb", soundCtl.lowShelf);
            fx("midlowdb", soundCtl.midLow);
            fx("middb", soundCtl.mid);
            fx("midhighdb", soundCtl.midHigh);
            fx("shelfdb", soundCtl.highShelf);
            fx("lpfpitch", soundCtl.lowpass);
            // the bus compressor (FormsMaster)
            auto mx = [&](const char* key, float v){
                std::string k = std::string("m:") + key;
                auto it = fxSent.find(k);
                if(it != fxSent.end() && it->second == v) return;
                fxSent[k] = v;
                sound->setMaster(key, v);
            };
            mx("bcon", soundCtl.compOn ? 1.0f : 0.0f);
            mx("bcthr", soundCtl.compThr);
            mx("bcratio", soundCtl.compRatio);
            mx("bcknee", soundCtl.compKnee);
            mx("bcatk", soundCtl.compAtk);
            mx("bcrel", soundCtl.compRel);
            mx("bcmakeup", soundCtl.compMakeup);
            mx("bcauto", soundCtl.compAuto ? 1.0f : 0.0f);
            mx("bcrms", soundCtl.compRms);
            mx("bchpf", soundCtl.compHpf);
            mx("bcmix", soundCtl.compMix);
            soundView.compGr = soundCtl.compOn ? sound->gainReduction() : 0.0f;
            sound->send(frame);
        }
#endif
        // the tab's view
        soundView.engineRunning = engineOn;
#ifdef FORMS_HAS_SC
        soundView.hasSC = true;
#endif
        soundView.kickLevel = kickLevel;
        soundView.kickThreshold = kickThresholdNow;
        soundView.kicksFired = kicksFired;
        soundView.duck = duckView;
        soundView.lgain = lrBal.l;
        soundView.rgain = lrBal.r;
        soundView.kickNames = names;
        auto selectedKickScene=kickScene();
        soundView.kickScene=selectedKickScene?selectedKickScene->name:std::string();
        int ki = kickIndexFor(perf, names);
        auto sampleScene=kickScene();
        soundView.kickChosen=sampleScene&&sampleScene->kickSample?sampleScene->kickSample->name+" (embedded)":ki>=0?names[ki]:std::string();
        if(forms::perfF(perf,"kickSynth",0)>.5f)soundView.kickChosen="Synthesized kick";
        soundView.kickError=editor.kickSampleError;
    }

    void drawSoundTab(){
        soundView.editKickSynth=[this](){
            if(auto scene=kickScene())editor.editScene(scene->name);
        };
        soundView.previewKick = [this](int i){ previewKick = i; };
        soundView.setSceneKick = [this](const std::string& name){
            auto scene=kickScene();if(!scene)return;
            auto it = engine.scenes.find(scene->name);
            if(it == engine.scenes.end()) return;
            it->second.performance = forms::completePerformance(it->second.performance);
            if(editor.selectKickSample && !editor.selectKickSample(it->second,name))return;
            engine.saveScene(it->second);
        };
        soundView.browseKick=[this](){
            auto scene=kickScene();if(!scene)return;
            auto it=engine.scenes.find(scene->name);
            if(it!=engine.scenes.end() && editor.browseKickSample && editor.browseKickSample(it->second)){
                engine.saveScene(it->second);editor.dirty.erase(it->first);
            }
        };
        soundCtl.draw(ctl, soundView);
    }

    void drawExportTab(){
        if(!ImGui::BeginTable("##exportColumns", 2, ImGuiTableFlags_SizingStretchProp)) return;
        ImGui::TableSetupColumn("options", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("workspace", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableNextColumn();
        ImGui::TextWrapped("Save the current frame as PNG: the composite matches Output, while separate layer files keep transparency and the layer's opacity.");
        ImGui::TextDisabled("Export dimensions resize the current live frame. For full detail, set the Play tab's Width / Height to the desired size before capturing.");
        ImGui::Spacing();
        ImGui::Checkbox("Composite as shown", &exportComposite);
        ImGui::Checkbox("Separate layers", &exportLayers);
        ImGui::Spacing();
        ImGui::TextUnformatted("Width");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##exportWidth", &exportWidth);
        exportWidth = ofClamp(exportWidth, 16, 16384);
        ImGui::TextUnformatted("Height");
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputInt("##exportHeight", &exportHeight);
        exportHeight = ofClamp(exportHeight, 16, 16384);
        ImGui::Spacing();
        ImGui::Spacing();
        bool canExport = exportComposite || exportLayers;
        ImGui::BeginDisabled(!canExport);
        if(ImGui::Button("Export PNGs", ImVec2(-FLT_MIN, 0))){
            auto pick = ofSystemSaveDialog("FormsExport.png", "Save PNG export");
            if(pick.bSuccess){
                std::filesystem::path chosen(pick.getPath());
                if(ofToLower(chosen.extension().string()) != ".png") chosen.replace_extension(".png");
                exportTargetPath = chosen.string();
                exportPending = true;
            }
        }
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Choose the destination and filename in the save dialog.");
        if(!exportStatus.empty()) ImGui::TextWrapped("%s", exportStatus.c_str());
        ImGui::TableNextColumn();
        ImGui::EndTable();
    }

    bool writeTexturePng(const ofTexture& texture, const std::filesystem::path& path, bool transparent){
        if(!texture.isAllocated() || exportWidth < 1 || exportHeight < 1) return false;
        ofFboSettings st; st.width = exportWidth; st.height = exportHeight; st.internalformat = GL_RGBA16;
        ofFbo fbo; fbo.allocate(st);
        if(!fbo.isAllocated()) return false;
        fbo.begin();
        if(transparent) ofClear(0, 0, 0, 0); else ofClear(0, 0, 0, 255);
        ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); ofSetColor(255);
        texture.draw(0, 0, exportWidth, exportHeight);
        ofPopStyle(); fbo.end();
        ofShortPixels pixels; fbo.readToPixels(pixels);   // 16 bits per channel: a 16-bit PNG
        // The live layer buffers are premultiplied; PNG layers need straight alpha for normal compositing.
        if(transparent && pixels.getNumChannels() >= 4){
            unsigned short* data = pixels.getData();
            size_t count = (size_t)exportWidth * exportHeight;
            for(size_t i = 0; i < count; i++){
                uint32_t a = data[i * 4 + 3];
                for(int c = 0; c < 3; c++) data[i * 4 + c] = a ? (unsigned short)std::min<uint32_t>(65535u, ((uint32_t)data[i * 4 + c] * 65535u + a / 2) / a) : 0;
            }
        }
        return ofSaveImage(pixels, path.string());
    }

    void exportCurrentFrame(){
        exportPending = false;
        const ofTexture* shown = output.get();
        if(!shown){ exportStatus = "No rendered frame is available yet."; return; }
        std::filesystem::path target(exportTargetPath);
        exportTargetPath.clear();
        if(target.empty()){ exportStatus = "No destination was selected."; return; }
        std::filesystem::path folder = target.parent_path();
        if(folder.empty()) folder = std::filesystem::current_path();
        std::error_code ec;
        std::filesystem::create_directories(folder, ec);
        if(ec){ exportStatus = "Cannot create output folder: " + ec.message(); return; }
        std::string stem = target.stem().string();
        if(stem.empty()) stem = "FormsExport";
        std::vector<std::string> written, failed;
        if(exportComposite){
            auto p = target;
            (writeTexturePng(*shown, p, false) ? written : failed).push_back(p.string());
        }
        if(exportLayers){
            std::vector<ofFbo*> layers{&scroller.fbo};
            for(auto& ring:rings) layers.push_back(&ring.fbo);
            for(int i = 0; i < exportLayerCount; i++){
                if(!layers[i]->isAllocated()) continue;
                auto p = folder / (stem + "_L" + ofToString(i + 1) + ".png");
                (writeTexturePng(layers[i]->getTexture(), p, true) ? written : failed).push_back(p.string());
            }
        }
        if(failed.empty()){
            exportStatus = "Saved " + std::to_string(written.size()) + " PNG(s) to " + folder.string();
        }else{
            exportStatus = "Could not save: ";
            for(size_t i = 0; i < failed.size(); i++) exportStatus += (i ? ", " : "") + failed[i];
        }
    }

    // Panorama: the last whole cycle of every layer (or the last `beats`) on one beat axis, layer 1's
    // scale (its image width per image length), composited as live. sessions/<s>/panorama_<a>-<b>.png
    std::string exportPanorama(bool withLook = true){
        std::ifstream in(engine.sessionDir() / "timeline.jsonl");
        if(!in) return "no timeline yet";
        struct E { int layer; double start, len; std::string png, blend; float gain; };
        std::vector<E> es;
        std::string line;
        while(std::getline(in, line)){
            try {
                auto j = forms::json::parse(line);
                E e{j.value("layer", 1), j.value("startBeat", 0.0), j.value("lengthBeats", 32.0), j.value("png", std::string()), "lighten", 1};
                e.blend = j.value("blend", std::string("lighten"));
                e.gain = j.value("gain", 1.0f);
                if(!e.png.empty()) es.push_back(e);
            } catch(...) {}
        }
        if(es.empty()) return "timeline is empty";
        // the cycle: least common multiple of the layers' image lengths, ending at the last fully shown image
        std::map<int, double> lens;
        double lastEnd = 0;
        for(auto& e : es){ lens[e.layer] = e.len; lastEnd = std::max(lastEnd, e.start + e.len); }
        long cycle = 1;
        for(auto& [l, len] : lens) cycle = std::lcm(cycle, (long)std::lround(len));
        double b1 = std::floor(scroller.headerBeat / 4.0) * 4.0;   // up to the bar under the header
        double b0 = std::max(0.0, b1 - cycle);
        double ppb = viewW / std::max(1.0, lens.count(1) ? lens[1] : 32.0);   // layer 1's scale
        int W = std::min(16384, (int)std::lround((b1 - b0) * ppb)), H = viewH;
        if(W < 2) return "not enough played yet";
        ofFboSettings st; st.width = W; st.height = H; st.internalformat = GL_RGBA16;
        ofFbo comp; comp.allocate(st);
        comp.begin(); ofBackground(0); comp.end();
        size_t count=engine.schedule->layers.size();
        for(auto& e:es) count=std::max(count,(size_t)e.layer);
        ensureLayerCount(count);
        std::vector<ofFbo> frames(count);
        std::vector<ofFbo*> buffers(count,nullptr);
        std::vector<std::string> blends(count,"lighten");
        std::vector<formscover::Compositor::Source> sources;
        std::vector<int> order;
        for(size_t layer=0;layer<count;++layer){
            auto& lf=frames[layer]; lf.allocate(st); buffers[layer]=&lf;
            lf.begin(); ofClear(0,0,0,0); ofPushStyle(); ofEnableAlphaBlending();
            glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            bool any=false; double layerLen=32; formscover::Settings layerCover;
            for(auto& e:es){
                if(e.layer!=(int)layer+1 || e.start+e.len<=b0 || e.start>=b1) continue;
                ofShortImage im; if(!im.load(e.png)) continue;
                float x=(e.start-b0)*ppb, width=e.len*ppb;
                layerGain.draw(im.getTexture(),x,width,H,e.gain);
                blends[layer]=e.blend; any=true; layerLen=e.len;
                try {
                    auto side=forms::readJsonFile(std::filesystem::path(e.png).replace_extension(".json"));
                    layerCover=formscover::read(side.value("layerCover",forms::json::object()));
                    auto masks=std::make_shared<std::vector<formscover::Mask>>();
                    formscover::readMasks(*masks,side,std::filesystem::path(e.png).parent_path().string());
                    for(size_t k=0;k<masks->size();++k){
                        auto settings=(*masks)[k].settings;
                        sources.push_back({settings,(int)layer,(float)ppb,false,[masks,k,x,width,W,H,gain=e.gain](int mw,int mh){
                            formscover::Compositor::drawMask((*masks)[k],x*mw/W,width*mw/W,width*mw/W,mh,gain);
                        }});
                    }
                }catch(...){}
            }
            ofPopStyle(); lf.end();
            if(!any) continue;
            if(withLook){
                auto fx=layerFxSettings(layer,layerLen);
                fx.hBlur *= (float)(layerLen*ppb/std::max(1,(int)viewW));
                fx.trailPx=postCtl.trailBeats[layer]*(float)ppb;
                fx.maskBeat=b0;fx.maskHeaderX=0;fx.maskPixelsPerBeat=ppb;
                if(fx.any()) exportFx.process(lf,fx);
            }
            if(layerCover.enabled) sources.push_back({layerCover,(int)layer,(float)ppb,true,[&,layer](int mw,int mh){ofSetColor(255);frames[layer].draw(0,0,mw,mh);}});
        }
        if(auto* scene=engine.sceneAtHeader()) for(int i:forms::stackOrder(scene->layers)) if(i<(int)count) order.push_back(i);
        for(size_t i=0;i<count;++i) if(std::find(order.begin(),order.end(),(int)i)==order.end()) order.push_back(i);
        formscover::Compositor covers; covers.apply(buffers,order,sources,W,H);
        for(int i:order){
            comp.begin(); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); glEnable(GL_BLEND);
            if(blends[i]=="add"){glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE);}
            else if(blends[i]=="normal"){glBlendEquation(GL_FUNC_ADD);glBlendFunc(GL_ONE,GL_ONE_MINUS_SRC_ALPHA);}
            else {glBlendEquation(GL_MAX);glBlendFunc(GL_ONE,GL_ONE);}
            ofSetColor(255); frames[i].draw(0,0); glBlendEquation(GL_FUNC_ADD); ofPopStyle(); comp.end();
        }
        ofShortPixels px;
        comp.readToPixels(px);
        px.setNumChannels(3);
        std::string out = (engine.sessionDir() / ("panorama_" + ofToString((int)b0) + "-" + ofToString((int)b1) + (withLook ? "" : "_raw") + ".png")).string();
        ofSaveImage(px, out);
        return out;
    }

    // ---- saved PNGs with the effects before the sound (each layer's blur and trail): what the
    // sound reads. Not the post-processing (colour, levels, stretch, header line), as decided
    // 2026-11-07.
    forms::LayerFx exportFx;
    // A rendered page (a Gallery print: k x the size) with its layer's effects, next to it as <name>_look.png.
    // Its layer and bars come from its sidecar; blur radii scale with the size (k).
    std::string lookPng(const std::string& png){
        ofShortImage im;
        if(!im.load(png)) return "";
        int W = (int)im.getWidth(), H = (int)im.getHeight();
        if(W < 1 || H < 1 || W > 16384 || H > 16384) return "";
        int layer = 0; double bars = 8;
        std::ifstream sj(ofFilePath::removeExt(png) + ".json");
        if(sj){ try { forms::json j; sj >> j; layer = std::max(0,j.value("layer", 1) - 1); ensureLayerCount(layer+1); bars = forms::barsFrom(j, "bars", 8); } catch(...) {} }
        ofFboSettings st; st.width = W; st.height = H; st.internalformat = GL_RGBA16;
        ofFbo lf; lf.allocate(st);
        lf.begin(); ofClear(0, 0, 0, 0);
        ofPushStyle(); ofEnableAlphaBlending();
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // straight -> premultiplied, as the layers
        ofSetColor(255); im.draw(0, 0, W, H);
        ofPopStyle(); lf.end();
        float k = (float)H / std::max(1, (int)viewH);   // the print's size over the live view's
        forms::LayerFxSettings fx = layerFxSettings(layer, 4.0 * bars);
        fx.hBlur *= (float)W / std::max(1, (int)viewW); fx.vBlur *= k; fx.maskBlur *= k;
        fx.trailPx = postCtl.trailBeats[layer] * W / (4.0f * (float)bars);
        fx.maskBeat=0;fx.maskHeaderX=0;fx.maskPixelsPerBeat=W/(4.f*(float)bars);
        if(fx.any()) exportFx.process(lf, fx);
        ofFbo comp; comp.allocate(st);
        comp.begin(); ofBackground(0); ofPushStyle(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); glEnable(GL_BLEND);
        glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // over black
        ofSetColor(255); lf.draw(0, 0);
        ofPopStyle(); comp.end();
        const ofTexture* exportTexture = &comp.getTexture();
        if(postCtl.featherExport){
            postCtl.apply(post);
            bool black = forms::backgroundIsBlack(postCtl.bgR, postCtl.bgG, postCtl.bgB);
            ofFloatColor target = postCtl.paper && black ? ofFloatColor(1, 1, 1) : ofFloatColor(postCtl.bgR, postCtl.bgG, postCtl.bgB);
            exportTexture = post.feather(*exportTexture, target);
        }
        ofShortPixels px;
        if(exportTexture == &post.featherFbo.getTexture()) post.featherFbo.readToPixels(px);
        else comp.readToPixels(px);
        px.setNumChannels(3);
        std::string out = ofFilePath::removeExt(png) + "_look.png";
        return ofSaveImage(px, out) ? out : "";
    }

    // The layers over black, bottom (layer 1) to top. Each layer FBO holds premultiplied colour
    // and alpha, so every blend mode sees rgb x a (lighten = max, add = sum, normal = src + dst
    // x (1 - a)); gain (0..1) scales the colour. A single layer comes out as before: rgb x a.
    void composite(int nLayers){
        int w = viewW, h = viewH;
        if(w < 1 || h < 1) return;
        if(!compositeFbo.isAllocated() || (int)compositeFbo.getWidth() != w || (int)compositeFbo.getHeight() != h || compositeFormat != liveFormat()){
            ofFboSettings st; st.width = w; st.height = h; st.internalformat = liveFormat();
            compositeFbo.allocate(st); compositeFormat = liveFormat();
        }
        compositeFbo.begin();
        ofBackground(0);
        ofPushStyle();
        // live settings: the layer's current values in its scene (edits show at once); the image's
        // own values if the scene is gone
        auto layerDef = [&](int i, const std::shared_ptr<forms::Image>& img) -> const forms::LayerDef* {
            if(!img) return nullptr;
            auto it = engine.scenes.find(img->scene);
            if(it == engine.scenes.end() || i >= (int)it->second.layers.size()) return nullptr;
            return &it->second.layers[i];
        };
        auto drawLayer = [&](ofFbo& f, const std::shared_ptr<forms::Image>& img, int i){
            if(!f.isAllocated()) return;
            const forms::LayerDef* ld = layerDef(i, img);
            std::string blend = ld ? ld->blend : img ? img->blend : std::string("lighten");
            float gain = 1;   // opacity is applied per image as the layers are drawn (scroller / rings)
            ofEnableBlendMode(OF_BLENDMODE_DISABLED);
            glEnable(GL_BLEND);
            if(blend == "add"){ glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); }
            else if(blend == "normal"){ glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); }
            else { glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE); }
            // keep the destination opaque
            ofSetColor(gain * 255, gain * 255, gain * 255, 255);
            f.draw(0, 0, w, h);
            glBlendEquation(GL_FUNC_ADD);
        };
        // stacking order from the scene at the header (layer 1 at the bottom unless moved)
        std::vector<int> order;
        const forms::Scene* hs = engine.sceneAtHeader();
        if(!hs && engine.schedule->layers[0].header){ auto it = engine.scenes.find(engine.schedule->layers[0].header->scene); if(it != engine.scenes.end()) hs = &it->second; }
        if(hs) for(int i : forms::stackOrder(hs->layers)) if(i < nLayers) order.push_back(i);
        for(int i = 0; i < nLayers; i++) if(std::find(order.begin(), order.end(), i) == order.end()) order.push_back(i);
        for(int i : order){
            if(!layerAudible(i)) continue;   // muted / not soloed: neither seen nor heard
            if(i == 0) drawLayer(scroller.fbo, engine.schedule->layers[0].header, 0);
            else drawLayer(rings[i - 1].fbo, rings[i - 1].current, i);
        }
        glBlendEquation(GL_FUNC_ADD);
        ofPopStyle();
        compositeFbo.end();
    }

    // ---------------------------------------------------------------- background (Output only)

    // The layers' coverage (alpha stacked "over", whatever their blend), then
    // out = image + background x (1 - coverage). The coverage gets the image's stretch and header
    // opacity, so the image fades into the background. Reads the layer FBOs, never writes them.
    ofFbo coverageFbo, bgFbo;
    ofShader bgShader;
    bool bgTried = false, bgOk = false;
    const ofTexture* applyBackground(const ofTexture& img, int nLayers){
        int w = (int)img.getWidth(), h = (int)img.getHeight();
        if(w < 1 || h < 1) return &img;
        if(!bgTried){
            bgTried = true;
            std::string vert = "#version 410\nuniform mat4 modelViewProjectionMatrix;\nlayout(location = 0) in vec4 position;\nvoid main(){ gl_Position = modelViewProjectionMatrix * position; }\n";
            std::string frag = forms::backgroundFragmentShader();
            bgOk = bgShader.setupShaderFromSource(GL_VERTEX_SHADER, vert) && bgShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
            if(bgOk){ bgShader.bindDefaults(); bgOk = bgShader.linkProgram(); }
            if(!bgOk) ofLogError("Forms Box") << "background shader failed: no background";
        }
        if(!bgOk) return &img;
        ofFboSettings st; st.width = w; st.height = h; st.internalformat = liveFormat();
        const bool reformat = bgFormat != liveFormat(); bgFormat = liveFormat();
        if(reformat || !coverageFbo.isAllocated() || (int)coverageFbo.getWidth() != w || (int)coverageFbo.getHeight() != h) coverageFbo.allocate(st);
        if(reformat || !bgFbo.isAllocated() || (int)bgFbo.getWidth() != w || (int)bgFbo.getHeight() != h) bgFbo.allocate(st);
        // coverage: alpha only, a + d x (1 - a), order does not matter
        coverageFbo.begin();
        ofClear(0, 0, 0, 0);
        ofPushStyle();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_TRUE);
        ofSetColor(255);
        for(int i = 0; i < nLayers; i++){
            ofFbo& f = i == 0 ? scroller.fbo : rings[i - 1].fbo;
            if(f.isAllocated() && layerAudible(i)) f.draw(0, 0, w, h);
        }
        glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
        ofPopStyle();
        coverageFbo.end();
        // what the post did to the image, done to the coverage (post off: the plain composite)
        bool postOn = post.enabled && post.ok;
        float stretchX = postOn && post.stretch ? std::max(0.0f, (float)head.headerPos) : -1.0f;
        float op = postOn && post.useOpacity ? std::max((float)head.opacity, 0.001f) : 1.0f;
        bgFbo.begin();
        ofClear(0, 0, 0, 255);
        ofPushStyle();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        bgShader.begin();
        bgShader.setUniformTexture("tImage", img, 0);
        bgShader.setUniformTexture("tCover", coverageFbo.getTexture(), 1);
        bgShader.setUniform2f("size", (float)w, (float)h);
        bgShader.setUniform1f("stretchX", stretchX);
        bgShader.setUniform1i("stretchBlend", post.stretchBlend);
        bgShader.setUniform1f("opacity", op);
        bool black = forms::backgroundIsBlack(postCtl.bgR, postCtl.bgG, postCtl.bgB);
        if(postCtl.paper && black) bgShader.setUniform3f("bg", 1, 1, 1);   // paper: white unless a colour is set
        else bgShader.setUniform3f("bg", postCtl.bgR, postCtl.bgG, postCtl.bgB);
        bgShader.setUniform1i("paper", postCtl.paper ? 1 : 0);
        ofSetColor(255);
        ofDrawRectangle(0, 0, w, h);
        bgShader.end();
        ofPopStyle();
        bgFbo.end();
        return &bgFbo.getTexture();
    }

    // The composite, plus what every layer drew and where (to check alignment and alpha offline)
    void saveComposite(const std::string& base){
        if(!compositeFbo.isAllocated()) return;
        ofShortPixels px;
        compositeFbo.readToPixels(px);
        ofSaveImage(px, base + ".png");
        ofBuffer info;
        info.append("headerBeat " + ofToString(scroller.headerBeat, 6) + "\nheaderPos " + ofToString(head.headerPos, 4) +
                    "\nview " + ofToString(viewW) + " " + ofToString(viewH) + "\nphase " + ofToString(clock.phase, 6) + "\n");
        auto& L1 = engine.schedule->layers[0];
        info.append("layer 1 header " + (L1.header ? L1.header->png + " blend " + L1.header->blend + " gain " + ofToString(L1.header->gain) : std::string("-")) + "\n");
        for(int i = 0; i < (int)rings.size(); i++){
            auto& r = rings[i];
            if(r.current) info.append("layer " + ofToString(i + 2) + " current " + r.current->png + " blend " + r.current->blend + " gain " + ofToString(r.current->gain) + "\n");
            for(auto& d : r.lastDrawn)
                info.append("layer " + ofToString(i + 2) + " drew " + d.png + " start " + ofToString(d.start, 3) + " len " + ofToString(d.len, 3) + " x " + ofToString(d.x, 3) + "\n");
        }
        ofBufferToFile(base + ".txt", info);
    }

    // sessions/<session>/timeline.jsonl: one line per image as it reaches the header (layer, id,
    // start beat, length in beats, png), for the drift check and the panorama later
    std::vector<std::shared_ptr<forms::Image>> loggedHeader = std::vector<std::shared_ptr<forms::Image>>(4);
    std::vector<double> loggedStart = std::vector<double>(4,-1e9);
    std::string loggedSession;
    virtual void recordTimeline(const forms::fs::path& path,const forms::json& record){
        std::ofstream f(path,std::ios::app);
        if(f) f<<record.dump()<<"\n";
    }
    void logTimeline(int nLayers){
        if(engine.session.empty()) return;
        if(engine.session != loggedSession){
            loggedSession = engine.session;
            for(int i = 0; i < (int)loggedHeader.size(); i++){ loggedHeader[i].reset(); loggedStart[i] = -1e9; }
            lastSyncedHeader.reset();
        }
        auto put = [&](int layer, const std::shared_ptr<forms::Image>& img, double start, double len){
            if(!img || img == loggedHeader[layer]) return;
            loggedHeader[layer] = img;
            if(start <= loggedStart[layer] + 1e-6) return;   // a beat repeat went back: already logged
            loggedStart[layer] = start;
            forms::json j{{"layer", layer + 1}, {"id", img->id}, {"startBeat", start}, {"lengthBeats", len},
                          {"headerBeat", scroller.headerBeat}, {"png", img->png}, {"scene", img->scene},
                          {"blend", img->blend}, {"gain", img->gain}};
            recordTimeline(engine.sessionDir()/"timeline.jsonl",j);
        };
        put(0, engine.schedule->layers[0].header, scroller.headerStart, scroller.headerLen);
        for(int i = 1; i < nLayers; i++){
            auto& L = engine.schedule->layers[i];
            for(auto& sl : L.slots) if(sl.img == rings[i - 1].current && sl.start <= scroller.headerBeat && scroller.headerBeat < sl.start + sl.len){
                put(i, sl.img, sl.start, sl.len); break;
            }
        }
    }

    // The sync readout: at every bar line under the header, each layer's position in its image
    void logBarLine(int nLayers){
        int bar = (int)std::floor(scroller.headerBeat / 4.0);
        if(bar == lastLoggedBar) return;
        lastLoggedBar = bar;
        if(nLayers < 2) return;
        std::string line = "bar " + std::to_string(bar) + "  beat " + ofToString(scroller.headerBeat, 2);
        for(int i = 1; i < nLayers; i++){
            auto& L = engine.schedule->layers[i];
            line += "  L" + std::to_string(i + 1) + " " + ofToString(L.phase, 3);
            if(rings[i - 1].current) line += " (" + rings[i - 1].current->id + ")";
        }
        engine.syncLog.push_back(line);
        while(engine.syncLog.size() > 64) engine.syncLog.pop_front();
    }
    bool saveFramePending = false;
    bool exportPending = false, exportComposite = true, exportLayers = false;
    int exportWidth = 1920, exportHeight = 1080, exportLayerCount = 1;
    std::string exportTargetPath;
    std::string exportStatus;

    forms::Engine engine;
    forms::Editor editor;
    customGuiRegion faceRegion;
    bool useSetlistLocal = true, lastUseSetlist = true;
    ofEventListeners listeners;
    bool primePending = false;
    std::vector<std::string> sceneNames;

    ofParameter<bool> showWindow, useSetlist;
    ofParameter<void> go, resetSetlist;
    ofParameter<int> sceneIndex, actualBarIn;
    ofParameter<forms::Schedule*> scheduleOut;
    ofParameter<int> barAdd;
    ofParameter<int> actualBPM, actualBars, actualNoteGrid, actualRoot, actualSnapshot, actualTrans;
    ofParameter<int> sceneRoot, setlistSnapshot;
    ofParameter<vector<float>> actualScale;
    ofParameter<string> sceneName, rendererFolder, pitchclassFolder;
    ofParameter<int> prime, maxPending, imageWidth, imageHeight;
    // built-in player
    ofParameter<bool> builtInPlayer, analogMode, sequence, autoPng, sequenceOut, autoHeader, autoGo, sessionLimit;
    ofParameter<bool> tremoloOn;              // the scenes' opacity tremolo (off by default)
    ofParameter<int> pause, viewW, viewH, actualBarOut;
    ofParameter<float> speed, bpmAdd, maxMinutes, barMult, seqFade, header, headerPlus, smooth, headerMin, headerMax;
    ofParameter<float> phaseOut, bpmOut, pitchBend, fade, seqMinutes, speedOut;
    ofParameter<float> headerPosOut, headerNorm, phLock, phOff, opacity;
    ofParameter<int> kickOut;
    // the Global Pitch node's values, read each frame into the engine (and the lagged transpose for the sound)
    float gpTranspose = 0;
    void readGlobalPitch(){
        using GP = ofxOceanodeGlobalPitch;
        forms::Engine::PitchFollow f;
        f.scaleOn = GP::follows(this, GP::Scale) && GP::scaleOn();
        if(f.scaleOn){
            for(float d : GP::distinctDegrees(GP::scale())){ int n = ((int)std::lround(d) % 12 + 12) % 12; if(std::find(f.notes.begin(), f.notes.end(), n) == f.notes.end()) f.notes.push_back(n); }
            std::sort(f.notes.begin(), f.notes.end());
            f.rootPc = GP::scaleRoot();
            f.shift = GP::shift();
        }
        f.transposeOn = GP::follows(this, GP::Transpose);
        f.transpose = f.transposeOn ? GP::laggedTranspose() : 0.0f;
        gpTranspose = f.transpose;
        engine.pitchFollow = f;
    }
    ofParameter<float> rootOut;                 // the bar's chord root (MIDI)
    ofParameter<vector<float>> scaleOut;        // the scale's pitch classes on root + transpose
    ofParameter<bool> hardCut;
    ofParameter<void> newPng, restartSetlist, saveFrame;
    ofParameter<bool> immediateReplace;
    ofParameter<ofTexture*> output;
    ofParameter<string> inHeader;
    // single node: the window's settings registry, post-processing and sound
    forms::ControlRegistry ctl;
    std::vector<std::string> legacyKeys;
    forms::PostControls postCtl;
    forms::Post post;
    bool saveOutputPending = false;
    forms::SoundControls soundCtl;
#ifdef FORMS_HAS_SC
    std::vector<serverManager*> scServers;
    std::shared_ptr<forms::SoundEngine> sound;
    ofParameter<nodePort> audioOut;
#endif

    // a folder setting: relative to the app's data folder unless absolute
    static std::string dataPath(const std::string& p){
        if(p.empty() || std::filesystem::path(p).is_absolute()) return p;
        return ofToDataPath(p, true);
    }

    void ensureLayerCount(size_t count){
        count=std::max(count,layerMute.size());
        engine.schedule->layers.resize(std::max(count,engine.schedule->layers.size()));
        if(rings.size()+1<count) rings.resize(count-1);
        layerMute.resize(count,false); layerSolo.resize(count,false);
        cols.resize(count); layerKeys.resize(count); layouts.resize(count);
        layoutH.resize(count); layoutGrid.resize(count); layoutImageH.resize(count);
        loggedHeader.resize(count); loggedStart.resize(count,-1e9);
        soundView.layerActive.resize(count); soundView.layerLevel.resize(count);
        soundCtl.ensureLayers(ctl,count); postCtl.ensureLayers(ctl,count);
#ifdef FORMS_HAS_SC
        frame.layers.resize(count);
#endif
    }
    void ensureSceneLayers(){
        size_t count=engine.schedule->layers.size();
        for(auto& [name,scene]:engine.scenes) count=std::max(count,scene.layers.size());
        ensureLayerCount(count);
    }
    void ensurePatchLayers(const ofJson& j){
        size_t count=layerMute.size();
        if(j.contains("formsControls") && j["formsControls"].is_object())
            for(auto& [key,value]:j["formsControls"].items()){
                if(key.size()<3 || key[0]!='L' || !std::isdigit((unsigned char)key[1])) continue;
                size_t end=1; while(end<key.size() && std::isdigit((unsigned char)key[end])) ++end;
                if(end<key.size() && key[end]=='_') try{ count=std::max(count,(size_t)std::stoul(key.substr(1,end-1))); }catch(...){}
            }
        ensureLayerCount(count);
    }

    void reload(){
        engine.rendererFolder = dataPath(rendererFolder.get());
        engine.pitchclassFolder = dataPath(pitchclassFolder.get());
        engine.maxPending = maxPending;
        engine.loadAll();
        ensureSceneLayers();
        engine.width = imageWidth;
        engine.height = imageHeight;
        sceneNames.clear();
        for(auto& [name, s] : engine.scenes) sceneNames.push_back(name);
        pickManualScene(sceneIndex);
    }

    void pickManualScene(int i){
        if(sceneNames.empty()) return;
        engine.manualScene = sceneNames[ofClamp(i, 0, (int)sceneNames.size() - 1)];
    }

    // force: prime and "Render now" ignore the in-flight limit
    void request(bool force){
        std::string p = engine.makeRequest(force);
        // a page that repeats N times stands for N GOs: the next N - 1 are skipped (goTick)
        if(!p.empty()) goDebt += std::max(0, engine.lastRequestRepeat - 1);
        send(p);
    }
    // The clock's GO (one per wrap): a new page unless the last one still has repeats to play.
    // The next page is asked for when its predecessor's first copy starts, so it is rendered in time.
    int goDebt = 0;
    float kickThresholdNow = 0.5f;   // the threshold of the scene whose kick layer is read now (the meters' line)
    int resumeImagesIn = 0;   // frames until Sequence goes back on (Resume images)
    void goTick(){
        if(goDebt > 0){
            bool queued = !engine.schedule->layers[0].ready.empty();
            for(auto& [id, im] : engine.pending) if(im->layer <= 1 && !im->preview) queued = true;
            if(queued){ goDebt--; return; }
            goDebt = 0;   // nothing queued for layer 1 any more (a reset, a lost render): ask now
        }
        request(false);
    }

    // Switch now: the next scene replaces what plays, on every layer (Engine::switchNow)
    bool switchPending = false;
    bool switchKeepImage = false;    // this switch is "Next image now" on the scene that plays
    void switchNowRun(bool keepImage = false){
        auto& h = engine.schedule->layers[0].header;
        // in bars, on the 1/16-bar grid (layer 1's page may be shorter than a bar)
        double bar = builtInPlayer ? std::round(scroller.headerStart / 4.0 * forms::kBarGrid) / forms::kBarGrid
                                   : (h && h->startBar >= 0) ? h->startBar : std::floor(engine.schedule->headerBeat / 4.0);
        // the header left of the wrap: the switch image plays once, one more image is needed ahead
        float c = clock.phase + (viewW > 0 ? head.headerPos / viewW : 0);
        int debt = 0;
        auto paths = engine.switchNow(bar, debt, builtInPlayer && c < 1 ? 3 : 2, keepImage ? 1 : 0);
        if(paths.empty()) return;
        goDebt = debt;
        for(auto& p : paths) send(p);
    }

    // Forms GL Box: audition (an edited instrument's page alone on the stage, heard)
    virtual bool auditionStage(){ return false; }
    virtual std::shared_ptr<forms::Image> auditionImage(){ return nullptr; }

    // Every request goes through here (GO, prime, layers, previews, prints): Forms GL Box draws it
    virtual void send(const std::string& path) = 0;

    // ---------------------------------------------------------------- window

    void drawWindow(){
        float z = ofxOceanodeShared::getZoomLevel();
        ImGui::SetNextWindowSize(ImVec2(1180 * z, 820 * z), ImGuiCond_FirstUseEver);
        bool open = showWindow.get();
        forms::theme::Scope look;
        look.push(z);
        if(!ImGui::Begin(getWindowId().c_str(), &open)){
            ImGui::End();
            look.pop();
            if(!open) showWindow = false;
            return;
        }
        if(!open){ showWindow = false; ImGui::End(); look.pop(); return; }
        editor.drawWindowContent();
        ImGui::End();
        look.pop();
    }
};
