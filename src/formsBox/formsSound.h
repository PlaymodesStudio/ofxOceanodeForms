//
//  formsSound.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Forms Box's sound: a SuperCollider graph owned by the node. Forms Box is not an SC node itself
//  (that would rename it "SC Forms Box" and break patches); it owns this engine, an scNode that is
//  never in the patch, and its "Audio" output (ScBus) points at it. serverManager finds the engine
//  through that port when SC Output (or anything else) is wired to Audio, and builds it like any
//  other SC node.
//
//  Per server, one group at the head of the graph:
//      FormsKick  → kick bus (audio, stereo) + duck bus (control: the ducking envelope)
//      FormsBank512 per layer (ampbus: 512 control buses each, written with /c_setn every frame)
//                 → mix bus (dry) + wet bus (each layer's reverb send, equal power), audio stereo
//      FormsFX    mix bus + reverb(wet bus) → fx bus (LeakDC, long reverb, EQ, compressor, high shelf, RLPF, gain)
//      FormsMaster fx bus + kick bus → the node's output bus (bus compressor, fade, L/R, limiter, volume)
//  SynthDefs: ofxOceanodeForms/synthdefs/formsBox.scd (compiled into CompiledSynthdefs/Forms).
//
//  The node fills `in` (what to play) every frame and calls send(); the engine sends only what
//  changed (arrays when the scene / harmony changes, amplitudes every frame).
//

#pragma once

#include "formsSoundModel.h"

#if __has_include("scNode.h") && __has_include("serverManager.h") && __has_include("ofxSCSynth.h")
#define FORMS_HAS_SC 1
#include "scNode.h"
#include "serverManager.h"
#include "ofxSCServer.h"
#include "ofxSCSynth.h"
#include "ofxSCBus.h"
#include "ofxSCGroup.h"
#include "ofxSCBuffer.h"
#include "ofMain.h"
#include <filesystem>
#include <map>
#include <memory>

namespace forms {

// What the node wants to hear (filled every frame)
struct SoundFrame {
    struct Layer {
        bool active = false;                 // has an image under the header
        std::vector<float> amps;             // SOUND_PARTIALS, 0..1
        std::vector<std::vector<float>> sub; // sharp attacks: the amplitudes of the frame's later slots (amps is slot 0)
        std::vector<float> pitch, gl, gr, duckw;   // SOUND_PARTIALS each
        int arraysVersion = 0;               // bumped when pitch / gains / duck weights change
        float level = 1;
        float send = 0;                      // reverb send 0..1 (equal power: 1 = all to the reverb)
    };
    std::vector<Layer> layers = std::vector<Layer>(SOUND_LAYERS);
    float bend = 0, detune = 0.0746f, bankGain = 0.0216f, lag = 1.0f / 30;
    // sharp attacks (off: amplitudes once per frame, untimed, as always): slot k of a layer is sent for
    // the instant now + lead + k x slotDt; attack: a partial's rise out of silence (s)
    bool sharp = false;
    float attack = 0.001f, slotDt = 1.0f / 240;
    double lead = 0.02;
    // kick
    int kickCount = 0;                       // bumped for every kick to play
    int kickSample = 0;                      // index in the kick list
    float kickRate = 1, kickAmp = 0.15f, kickGain = 1.5f, duckDecay = 1, duckCurve = -4;
    float kickGate=1;
    bool kickSynth=false;
    float kickStart=160,kickEnd=48,kickDecay=.45f,kickClick=.3f,kickNoise=0,kickDrive=.1f;
    float imageDuckDepth=0;
    // fx / master
    float comp = 0, fxGain = 1.5f;
    float fade = 1, lgain = 1, rgain = 1, volume = 1;
    bool limit = true;
};

class SoundEngine : public scNode {
public:
    explicit SoundEngine(std::vector<serverManager*> s) : scNode("Forms Sound"), servers(std::move(s)) {
        addOutput("Out");
        // loaded at boot, or right away (with a /sync) if the server is running: before any graph build
        requireSynthdefs();
    }
    ~SoundEngine(){
        for(auto& [server, st] : states){
            release(server, st, true);
            freeBuffers(server, st);
            dropBus(server, st.mixBus, true); dropBus(server, st.fxBus, true); dropBus(server, st.kickBus, true); dropBus(server, st.wetBus, true);
            dropBus(server, st.duckBus, false); dropBus(server, st.grBus, false);
            for(auto*& b : st.ampBus) dropBus(server, b, false);
        }
        states.clear();
    }

    // the kick samples (sorted file names); set before the graph is built, or call reloadKicks()
    void setKickFolder(const std::string& folder){
        if(folder == kickFolder) return;
        kickFolder = folder;
        kickFiles.clear();
        namespace fs = std::filesystem;
        std::error_code ec;
        if(fs::is_directory(folder, ec))
            for(auto& e : fs::directory_iterator(folder, ec)){
                auto ext = ofToLower(e.path().extension().string());
                if(ext == ".wav" || ext == ".aif" || ext == ".aiff") kickFiles.push_back(e.path().string());
            }
        std::sort(kickFiles.begin(), kickFiles.end());
        for(auto& [server, st] : states) freeBuffers(server, st);   // reloaded on the next send
    }
    const std::vector<std::string>& kicks() const { return kickFiles; }
    int addKickFile(const std::string& path){
        auto it=std::find(kickFiles.begin(),kickFiles.end(),path);
        if(it!=kickFiles.end()) return (int)(it-kickFiles.begin());
        kickFiles.push_back(path);return (int)kickFiles.size()-1;
    }

    // the SynthDef folder: the addon's synthdefs/CompiledSynthdefs/Forms
    std::string requiredSynthdefFolder() const override { return synthdefFolder(); }
    static std::string synthdefFolder(){
        namespace fs = std::filesystem;
        fs::path p = fs::path(__FILE__).parent_path().parent_path().parent_path() / "synthdefs" / "CompiledSynthdefs" / "Forms";
        return p.string();
    }
    void requireSynthdefs(){
        if(required) return;
        required = true;
        for(auto* sm : servers) if(sm) sm->requireSynthdefFolder(synthdefFolder());
    }
    bool running() const { for(auto& [s, st] : states) if(st.group) return true; return false; }

    // ---- scNode
    void buildSynth(ofxSCServer* server) override { auto& st = states[server]; release(server, st, true); }
    void createSynth(ofxSCServer* server) override {
        if(!server) return;
        requireSynthdefs();
        auto& st = states[server];
        release(server, st, true);
        // scsynth may still be reading the SynthDefs (/d_loadDir is asynchronous): an /s_new now
        // would fail. Wait for its /sync; poll() builds it then (at the head of the graph, where
        // serverManager would have put it).
        if(!definitionsReady(server)){ st.pendingCreate = true; return; }
        build(server);
    }
    // Grow the graph only when needed. Existing banks keep playing while new ones are inserted
    // before the shared effects bus. The server's bus allocator remains the resource limit.
    void setLayerCount(size_t count){
        if(count<=layerCount) return;
        layerCount=count;
        for(auto& [server,st]:states){
            size_t old=st.banks.size();
            st.banks.resize(count,nullptr); st.ampBus.resize(count,nullptr); st.sent.layers.resize(count);
            if(!st.group || !st.fx) continue;
            ensureBuses(server,st);
            for(size_t l=old;l<count;++l){
                if(!st.ampBus[l]){ ofLogError("Forms Box") << "Cannot allocate sound buses for layer " << l+1; continue; }
                auto* b=new ofxSCSynth("FormsBank512",server);
                b->set("out",st.mixBus->index); b->set("wetout",st.wetBus->index); b->set("send",0.0);
                b->set("ampbus",st.ampBus[l]->index); b->set("duckbus",st.duckBus->index);
                b->createAndRun(2,st.fx->nodeID,false);
                st.banks[l]=b;
            }
        }
    }
    // per frame: builds a graph that was waiting for its SynthDefs
    void poll(){
        for(auto& [server, st] : states)
            if(st.pendingCreate && definitionsReady(server)) build(server);
    }
    bool waitingForDefs() const { for(auto& [s, st] : states) if(st.pendingCreate) return true; return false; }

private:
    bool definitionsReady(ofxSCServer* server) const {
        if(server->isNRTCapturing()) return true;
        for(auto* sm : servers) if(sm && sm->getServer() == server && sm->areRequiredSynthdefsLoading()) return false;
        return true;
    }
    void build(ofxSCServer* server){
        auto& st = states[server];
        st.pendingCreate = false;
        st.banks.resize(layerCount,nullptr); st.ampBus.resize(layerCount,nullptr); st.sent.layers.resize(layerCount);
        ensureBuses(server, st);
        bool buses = st.mixBus && st.wetBus && st.fxBus && st.kickBus && st.duckBus;
        for(auto* b : st.ampBus) buses = buses && b;
        if(!buses){ ofLogError("Forms Box") << "sound: not enough SuperCollider buses, no sound"; return; }
        st.group = new ofxSCGroup(server);
        st.group->create(0, 1);                       // head of the default group
        st.kick = new ofxSCSynth("FormsKick", server);
        st.kick->set("out", st.kickBus ? st.kickBus->index : 0);
        st.kick->set("duckbus", st.duckBus ? st.duckBus->index : 0);
        st.kick->createAndRun(1, st.group->nodeID, true);
        for(int l = 0; l < (int)st.banks.size(); l++){
            auto* b = new ofxSCSynth("FormsBank512", server);
            b->set("out", st.mixBus ? st.mixBus->index : 0);
            b->set("wetout", st.wetBus ? st.wetBus->index : 0);
            b->set("send", 0.0);
            b->set("ampbus", st.ampBus[l] ? st.ampBus[l]->index : 0);
            b->set("duckbus", st.duckBus ? st.duckBus->index : 0);
            b->createAndRun(1, st.group->nodeID, false);   // runs once it has something to play
            st.banks[l] = b;
        }
        st.fx = new ofxSCSynth("FormsFX", server);
        st.fx->set("in", st.mixBus ? st.mixBus->index : 0);
        st.fx->set("wetin", st.wetBus ? st.wetBus->index : 0);
        st.fx->set("out", st.fxBus ? st.fxBus->index : 0);
        for(auto& [k, v] : fxExtra) st.fx->set(k, (double)v);
        st.fx->createAndRun(1, st.group->nodeID, true);
        st.master = new ofxSCSynth("FormsMaster", server);
        st.master->set("in", st.fxBus ? st.fxBus->index : 0);
        st.master->set("kickin", st.kickBus ? st.kickBus->index : 0);
        st.master->set("duckbus",st.duckBus?st.duckBus->index:0);
        st.master->set("out", st.outBus >= 0 ? st.outBus : 0);
        st.master->set("grbus", st.grBus ? st.grBus->index : -1);
        for(auto& [k, v] : masterExtra) st.master->set(k, (double)v);
        st.master->createAndRun(1, st.group->nodeID, true);
        st.sent = Sent(); st.sent.layers.resize(st.banks.size());                              // everything is sent again
        st.createdAt = ofGetElapsedTimef();
    }

public:
    // The node is closing: every synth off now (the app's quit can take a while after this)
    void silenceAll(){ for(auto& [server, st] : states){ release(server, st, true); st.pendingCreate = false; } }
    void free(ofxSCServer* server) override {
        auto it = states.find(server);
        if(it == states.end()) return;
        release(server, it->second, true);
        it->second.pendingCreate = false;
        it->second.outBus = -1;
    }
    void moveSynthBefore(ofxSCServer* server, int nodeID) override {
        auto it = states.find(server);
        if(it == states.end() || !it->second.group) return;
        it->second.group->moveBefore(nodeID);
        if(it->second.master) it->second.master->set("out", it->second.outBus);
    }
    int getLastSynthID(ofxSCServer* server) override {
        auto it = states.find(server);
        return it != states.end() && it->second.group ? it->second.group->nodeID : -1;
    }
    void setOutputBus(ofxSCServer* server, int, int bus) override {
        auto& st = states[server];
        st.outBus = bus;
        if(st.master) st.master->set("out", bus);
    }
    int getOutputBusIndex(ofxSCServer* server, int) override {
        auto it = states.find(server);
        return it != states.end() ? it->second.outBus : -1;
    }
    void setInputBus(ofxSCServer*, scNode*, int) override {}
    void resetInputBusses(ofxSCServer*, int) override {}
    void resendParametersForNRT() override { for(auto& [s, st] : states){ st.sent = Sent(); st.sent.layers.resize(st.banks.size()); } }

    // sharp attacks: how far ahead the amplitudes are stamped (the wanted lead, or what the servers need
    // for a timestamp not to be "late"); the node reads its columns that far ahead of the header
    double leadSeconds(double wanted) const {
        double lead = wanted;
        for(auto& [server, st] : states) if(server) lead = std::max(lead, server->getMinimumLeadSeconds() + 0.004);
        return std::min(lead, 0.25);
    }

    // ---- per frame
    void send(const SoundFrame& f){
        setLayerCount(f.layers.size());
        for(auto& [server, st] : states){
            if(!st.group || !st.master) continue;
            loadBuffers(server, st);
            // amplitudes: one /c_setn for all layers (split if large)
            ofxOscMessage m; m.setAddress("/c_setn");
            int args = 0;
            auto flush = [&](){ if(args){ server->sendMsg(m); m.clear(); m.setAddress("/c_setn"); args = 0; } };
            for(int l = 0; l < (int)st.banks.size(); l++){
                if(l>=(int)f.layers.size() || !st.banks[l]) continue;
                auto& L = f.layers[l];
                auto& S = st.sent.layers[l];
                bool run = L.active && L.level > 0 && !L.amps.empty();
                if(run && S.running) S.offAt = -1;   // came back before it stopped
                if(run != S.running){
                    // stop a layer once its sound has faded (the amplitudes are zeroed first)
                    if(run){ st.banks[l]->run(true); S.running = true; S.offAt = -1; }
                    else if(S.offAt < 0){ S.offAt = ofGetElapsedTimef() + 0.5f; }
                    else if(ofGetElapsedTimef() >= S.offAt){ st.banks[l]->run(false); S.running = false; S.offAt = -1; }
                }
                if(!st.ampBus[l]) continue;
                bool anyNow = false;
                for(float a : L.amps) if(a > 0){ anyNow = true; break; }
                if(f.sharp) for(auto& sv : L.sub){ if(anyNow) break; for(float a : sv) if(a > 0){ anyNow = true; break; } }
                if(!run) anyNow = false;
                if(!anyNow && !S.ampsNonZero) continue;     // silence already sent
                S.ampsNonZero = anyNow;
                if(f.sharp){
                    // sharp attacks: every slot of the frame for its own instant (scsynth applies a
                    // timestamped bundle at that instant; one too close to now goes out untimed)
                    const int slots = 1 + (int)L.sub.size();
                    for(int k = 0; k < slots; k++){
                        const std::vector<float>& a = k == 0 ? L.amps : L.sub[k - 1];
                        ofxOscMessage ms; ms.setAddress("/c_setn");
                        ms.addIntArg(st.ampBus[l]->index);
                        ms.addIntArg(SOUND_PARTIALS);
                        for(int i = 0; i < SOUND_PARTIALS; i++) ms.addFloatArg(run && i < (int)a.size() ? a[i] : 0.0f);
                        server->sendMsgAt(ms, ofxSCServer::timetagNow(f.lead + k * (double)f.slotDt));
                    }
                    continue;
                }
                if(args > 0) flush();
                m.addIntArg(st.ampBus[l]->index);
                m.addIntArg(SOUND_PARTIALS);
                for(int i = 0; i < SOUND_PARTIALS; i++) m.addFloatArg(run && i < (int)L.amps.size() ? L.amps[i] : 0.0f);
                args++;
            }
            flush();
            // arrays (pitches, gains, duck weights) when they change
            for(int l = 0; l < (int)st.banks.size(); l++){
                if(l>=(int)f.layers.size() || !st.banks[l]) continue;
                auto& L = f.layers[l];
                auto& S = st.sent.layers[l];
                if(L.arraysVersion != S.arraysVersion && (int)L.pitch.size() == SOUND_PARTIALS){
                    S.arraysVersion = L.arraysVersion;
                    st.banks[l]->set("pitch", L.pitch);
                    st.banks[l]->set("gl", L.gl);
                    st.banks[l]->set("gr", L.gr);
                    st.banks[l]->set("duckw", L.duckw);
                }
                setIf(st.banks[l], S.level, "level", L.level);
                setIf(st.banks[l], S.send, "send", L.send);
                setIf(st.banks[l], S.bend, "bend", f.bend);
                setIf(st.banks[l], S.detune, "detune", f.detune);
                setIf(st.banks[l], S.gain, "gain", f.bankGain);
                setIf(st.banks[l], S.lag, "lag", f.lag);
                // sharp attacks: the bank's mode, the time between two slots (to half a millisecond: frame times wobble) and the attack
                setIf(st.banks[l], S.sharp, "sharp", f.sharp ? 1.0f : 0.0f);
                if(f.sharp){
                    setIf(st.banks[l], S.ramp, "ramp", std::max(0.001f, std::round(f.slotDt * 2000.0f) / 2000.0f));
                    setIf(st.banks[l], S.attack, "attack", f.attack);
                }
            }
            // kick
            int sample=f.kickSample;
            bool validSample=sample>=0 && sample<(int)st.buffers.size() && st.buffers[sample].first;
            setIf(st.kick, st.sent.kickAmp, "amp",(f.kickSynth||validSample)?f.kickAmp:0);
            setIf(st.kick,st.sent.kickGate,"gate",f.kickGate);
            setIf(st.kick,st.sent.kickSource,"source",f.kickSynth?1.f:0.f);
            setIf(st.kick,st.sent.kickStart,"synthstart",f.kickStart);
            setIf(st.kick,st.sent.kickEnd,"synthend",f.kickEnd);
            setIf(st.kick,st.sent.kickDecay,"synthdecay",f.kickDecay);
            setIf(st.kick,st.sent.kickClick,"synthclick",f.kickClick);
            setIf(st.kick,st.sent.kickNoise,"synthnoise",f.kickNoise);
            setIf(st.kick,st.sent.kickDrive,"synthdrive",f.kickDrive);
            setIf(st.master,st.sent.imageDuckDepth,"duckdepth",f.imageDuckDepth);
            setIf(st.kick, st.sent.duckDecay, "duckdecay", f.duckDecay);
            setIf(st.kick, st.sent.duckCurve, "duckcurve", f.duckCurve);
            if(f.kickCount != st.sent.kickCount){
                bool first = st.sent.kickCount < 0;
                st.sent.kickCount = f.kickCount;
                if(!first){
                    auto left=validSample?st.buffers[sample].first:st.silentKick;
                    auto right=validSample&&st.buffers[sample].second?st.buffers[sample].second:left;
                    if(left){st.kick->set("bufl",left->index);st.kick->set("bufr",right->index);}
                    st.kick->set("rate", f.kickRate);
                    // Ducking is driven by the kick event, even if no sample is loaded.
                    st.kick->set("t_trig", 1.0);
                }
            }
            // fx / master
            setIf(st.fx, st.sent.comp, "comp", f.comp);
            setIf(st.fx, st.sent.fxGain, "gain", f.fxGain);
            setIf(st.master, st.sent.kickGain, "kickgain", f.kickGain);
            setIf(st.master, st.sent.fade, "fade", f.fade);
            setIf(st.master, st.sent.lgain, "lgain", f.lgain);
            setIf(st.master, st.sent.rgain, "rgain", f.rgain);
            setIf(st.master, st.sent.volume, "volume", f.volume);
            setIf(st.master, st.sent.limit, "limit", f.limit ? 1.0f : 0.0f);
        }
    }

    // A raw FX control (the Sonification tab's FX section): sent now and after every rebuild
    void setFx(const std::string& key, float v){
        fxExtra[key] = v;
        for(auto& [server, st] : states) if(st.fx) st.fx->set(key, v);
    }
    // The same for FormsMaster (the bus compressor's controls)
    void setMaster(const std::string& key, float v){
        masterExtra[key] = v;
        for(auto& [server, st] : states) if(st.master) st.master->set(key, v);
    }
    // The bus compressor's gain reduction in dB (the loudest server's), read back from its control
    // bus; call once per frame (it asks for the next value)
    float gainReduction(){
        float gr = 0;
        for(auto& [server, st] : states){
            if(!st.master || !st.grBus || !liveBus(server, st.grBus, false)) continue;
            if(!st.grBus->readValues.empty() && std::isfinite(st.grBus->readValues[0])) gr = std::max(gr, st.grBus->readValues[0]);
            st.grBus->requestValues();
        }
        return gr;
    }

private:
    struct Sent {
        struct L { int arraysVersion = -1; float level = NAN, send = NAN, bend = NAN, detune = NAN, gain = NAN, lag = NAN, sharp = NAN, ramp = NAN, attack = NAN; bool running = false, ampsNonZero = true; float offAt = -1; };
        std::vector<L> layers = std::vector<L>(SOUND_LAYERS);
        int kickCount = -1;
        float kickAmp = NAN, duckDecay = NAN, duckCurve = NAN, comp = NAN, fxGain = NAN;
        float kickGain = NAN, fade = NAN, lgain = NAN, rgain = NAN, volume = NAN, limit = NAN;
        float kickGate=NAN;
        float kickSource=NAN,kickStart=NAN,kickEnd=NAN,kickDecay=NAN,kickClick=NAN,kickNoise=NAN,kickDrive=NAN,imageDuckDepth=NAN;
    };
    struct State {
        ofxSCGroup* group = nullptr;
        ofxSCSynth* kick = nullptr;
        std::vector<ofxSCSynth*> banks = std::vector<ofxSCSynth*>(SOUND_LAYERS,nullptr);
        ofxSCSynth* fx = nullptr;
        ofxSCSynth* master = nullptr;
        ofxSCBus* mixBus = nullptr;
        ofxSCBus* wetBus = nullptr;          // the layers' reverb sends
        ofxSCBus* fxBus = nullptr;
        ofxSCBus* kickBus = nullptr;
        ofxSCBus* duckBus = nullptr;
        ofxSCBus* grBus = nullptr;           // the bus compressor's gain reduction (dB)
        std::vector<ofxSCBus*> ampBus = std::vector<ofxSCBus*>(SOUND_LAYERS,nullptr);
        std::vector<std::pair<ofxSCBuffer*, ofxSCBuffer*>> buffers;   // per kick file: left, right
        ofxSCBuffer* silentKick=nullptr;
        uint32_t bufferGeneration = 0;
        int outBus = -1;
        bool pendingCreate = false;   // waiting for the SynthDefs
        float createdAt = 0;
        Sent sent;
    };
    std::vector<serverManager*> servers;
    std::map<ofxSCServer*, State> states;
    std::string kickFolder;
    std::vector<std::string> kickFiles;
    std::map<std::string, float> fxExtra, masterExtra;
    bool required = false;
    size_t layerCount = SOUND_LAYERS;

    static void setIf(ofxSCSynth* s, float& last, const char* key, float v){
        if(!s || last == v) return;
        last = v;
        s->set(key, (double)v);
    }

    static bool liveBus(ofxSCServer* s, ofxSCBus* b, bool audio){
        if(!s || !b || b->index < 0) return false;
        auto& v = audio ? s->audioBusses : s->controlBusses;
        return b->index < (int)v.size() && v[b->index] == b;
    }
    static void dropBus(ofxSCServer* s, ofxSCBus*& b, bool audio){
        if(b && liveBus(s, b, audio)) b->free();
        delete b;
        b = nullptr;
    }
    void ensureBuses(ofxSCServer* server, State& st){
        auto audio = [&](ofxSCBus*& b){
            if(liveBus(server, b, true)) return;
            dropBus(server, b, true);
            b = new ofxSCBus(RATE_AUDIO, 2, server);
            if(!liveBus(server, b, true)){ delete b; b = nullptr; }
        };
        auto control = [&](ofxSCBus*& b, int n){
            if(liveBus(server, b, false)) return;
            dropBus(server, b, false);
            b = new ofxSCBus(RATE_CONTROL, n, server);
            if(!liveBus(server, b, false)){ delete b; b = nullptr; }
        };
        audio(st.mixBus); audio(st.wetBus); audio(st.fxBus); audio(st.kickBus);
        control(st.duckBus, 1); control(st.grBus, 1);
        for(auto& b : st.ampBus) control(b, SOUND_PARTIALS);
    }
    void release(ofxSCServer* server, State& st, bool sendFree){
        auto drop = [&](ofxSCSynth*& s){ delete s; s = nullptr; };
        if(st.group && sendFree) st.group->free();   // takes its synths
        delete st.group; st.group = nullptr;
        drop(st.kick); drop(st.fx); drop(st.master);
        for(auto*& b : st.banks) drop(b);
        // the buses stay (reused by the next build); freed with the engine
    }
    void freeBuffers(ofxSCServer* server, State& st){
        if(st.silentKick){
            if(server->getAllocationGeneration()==st.bufferGeneration)st.silentKick->free();
            delete st.silentKick;st.silentKick=nullptr;
        }
        if(server->getAllocationGeneration() != st.bufferGeneration){   // numbers from before a restart
            for(auto& [l, r] : st.buffers){ delete l; delete r; }
            st.buffers.clear();
            return;
        }
        for(auto& [l, r] : st.buffers){
            if(l){ l->free(); delete l; }
            if(r){ r->free(); delete r; }
        }
        st.buffers.clear();
    }
    void loadBuffers(ofxSCServer* server, State& st){
        if(server->getAllocationGeneration() != st.bufferGeneration){
            // the server restarted: the old numbers are gone
            for(auto& [l, r] : st.buffers){ delete l; delete r; }
            st.buffers.clear();
            delete st.silentKick;st.silentKick=nullptr;
            st.bufferGeneration = server->getAllocationGeneration();
        }
        if(!st.silentKick){
            st.silentKick=new ofxSCBuffer(1,1,server);
            if(st.silentKick->index>=0)st.silentKick->alloc();
            else {delete st.silentKick;st.silentKick=nullptr;}
        }
        // Imported samples append without freeing or reloading the folder's buffers.
        for(size_t i=st.buffers.size();i<kickFiles.size();++i){
            const auto& path=kickFiles[i];
            int ch = audioChannels(path);
            auto* l = new ofxSCBuffer(0, 1, server);
            if(l->index < 0){ delete l; break; }
            l->readChannel(path, {0});
            ofxSCBuffer* r = nullptr;
            if(ch >= 2){
                r = new ofxSCBuffer(0, 1, server);
                if(r->index < 0){ delete r; r = nullptr; }
                else r->readChannel(path, {1});
            }
            st.buffers.push_back({l, r});
        }
    }
};

} // namespace forms

#else
namespace forms {
struct SoundFrame {};
}
#endif
