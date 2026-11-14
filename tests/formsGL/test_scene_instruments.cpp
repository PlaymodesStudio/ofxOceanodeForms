// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_scene_instruments.cpp -o /tmp/test_scene_instruments
// Run with a writable copy of the Forms data folder (it saves a scene there). No live application data is changed.
// A scene's own version of an instrument (Scene::instruments): kept as the difference from the shared
// instrument, played only by that scene, saved with the scene, never written to the instrument.
#include "formsLiveEdit.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
using namespace forms;
static json itemsOf(const json& req, const std::string& ref){ json out=json::array(); for(auto& it:req.value("items",json::array())) if(it.value("preset",std::string())==ref) out.push_back(it); return out; }
int main(int argc,char**argv){
    assert(argc==2); Engine e; e.rendererFolder=argv[1]; e.loadAll();
    const std::string R="hits/blocking snares", ST="stack/techno blocks";
    assert(e.presets.count(R) && e.presets.count(ST));
    const Preset libR=e.presets.at(R), libST=e.presets.at(ST);

    // ---- 1. the difference and back
    {
        Preset own=libR; own.params["bandLow"]=0.11; own.params["clearChance"]=0; own.params.erase("repChance"); own.params["blocking"]=false;
        json d=presetOwnDiff(libR,own);
        assert(d["params"].size()==3 && d["unset"]==json::array({"repChance"}) && presetOwnCount(d)==4);
        Preset back=presetWithOwn(libR,d);
        assert(back.params==own.params && back.layers==own.layers);
        assert(presetOwnDiff(libR,libR).empty() && presetWithOwn(libR,json::object()).params==libR.params);
        // a later change of the shared instrument reaches the scene where the scene did not change it
        Preset lib2=libR; lib2.params["level"]=0.5; lib2.params["bandLow"]=0.9;
        Preset seen=presetWithOwn(lib2,d);
        assert(seen.params["level"]==0.5 && seen.params["bandLow"]==0.11);
        // layered: a layer's settings as a per-layer difference; a changed layer list whole
        size_t li=0; for(; li<libST.layers.size(); li++) if(libST.layers[li].contains("params")) break;
        assert(li<libST.layers.size());
        Preset s2=libST; s2.layers[li]["params"]["level"]=0.25;
        json ds=presetOwnDiff(libST,s2);
        assert(ds.contains("layerParams") && !ds.contains("layers") && ds["layerParams"].size()==1);
        assert(presetWithOwn(libST,ds).layers==s2.layers);
        // the shared instrument's layers reordered later: the scene's layer changes are not laid on another kind of layer
        { Preset moved=libST; json other=json{{"gen","sweep"},{"params",json::object()}}; moved.layers[li]=other; assert(presetWithOwn(moved,ds).layers==moved.layers); }
        Preset s3=libST; s3.layers.erase(s3.layers.begin());
        json d3=presetOwnDiff(libST,s3); assert(d3.contains("layers") && presetWithOwn(libST,d3).layers==s3.layers);
        std::puts("ok 1: difference / apply, removed keys, layered instruments, shared changes still reach the scene");
    }

    // ---- 2. two scenes with the same instrument: only the one with its own version changes
    Scene base=e.scenes.at("DUBTECH");
    base.layers.resize(1); base.globalEffects.clear(); base.layers[0].effectPreset.clear(); base.layers[0].fixed=json();
    base.layers[0].cells={Cell{R,100,0},Cell{ST,100,0}}; base.layers[0].groupProb={1}; base.layers[0].globalProb=100; base.layers[0].rows=96;
    Scene A=base; A.name="T_OWN_A"; Scene B=base; B.name="T_OWN_B";
    e.scenes[A.name]=A; e.scenes[B.name]=B;
    json staged; e.requestSink=[&](const fs::path&,json j){ staged=std::move(j); };
    auto request=[&](const std::string& scene){ e.useSetlist=false; e.manualScene=scene; e.newSession(321); assert(!e.makeRequest(true).empty()); json r=staged; r.erase("id"); r.erase("scene"); r.erase("session"); return r; };
    json a0=request(A.name), b0=request(B.name);
    assert(a0==b0 && !itemsOf(a0,R).empty());
    {
        Preset own=libR; own.params["bandLow"]=0.11; own.params["bandHigh"]=0.2; own.params["blocking"]=false;
        e.scenes[A.name].instruments[R]=presetOwnDiff(libR,own);
    }
    json a1=request(A.name), b1=request(B.name);
    assert(b1==b0);                                         // the other scene: identical
    assert(e.presets.at(R).params==libR.params);            // the shared instrument: untouched
    for(auto& it:itemsOf(a1,R)){ assert(std::abs(it["params"]["bandLow"].get<double>()-0.11)<1e-6 && std::abs(it["params"]["bandHigh"].get<double>()-0.2)<1e-6 && it["params"]["blocking"]==false); }
    for(auto& it:itemsOf(b1,R)) assert(std::abs(it["params"]["bandLow"].get<double>()-libR.params["bandLow"].get<double>())<1e-6);
    assert(itemsOf(a1,ST)==itemsOf(a0,ST));                 // its other instruments: identical
    assert(e.ownScene==nullptr && e.ownCache.empty());      // the context never leaks out of a request
    std::puts("ok 2: the scene's version plays only in that scene; other scene, other instruments and the shared instrument unchanged");

    // ---- 3. an instrument nested in a layered one, the layer effect and a scene-wide effect follow too
    {
        Preset nest; nest.gen="stack"; nest.name="t own nest"; nest.layers=json::array({json{{"preset",R}}}); e.presets[nest.ref()]=nest;
        Scene& a=e.scenes[A.name]; a.layers[0].cells={Cell{nest.ref(),100,0}};
        json n=request(A.name); bool any=false;
        for(auto& it:n["items"]) if(it.value("gen",std::string())=="hits" && it["params"].contains("bandLow") && std::abs(it["params"]["bandLow"].get<double>()-0.11)<1e-6) any=true;
        assert(any);
        std::string fx; for(auto& [r,p]:e.presets) if(p.gen=="effects" && e.onlyEffects(p) && p.params.is_object() && !p.params.empty()){ fx=r; break; }
        assert(!fx.empty());
        a.layers[0].cells={Cell{R,100,0}}; a.layers[0].effectPreset=fx; a.globalEffects={fx};
        json f0=request(A.name);
        Preset fxOwn=e.presets.at(fx); std::string key=fxOwn.params.begin().key(); fxOwn.params.erase(key);
        a.instruments[fx]=presetOwnDiff(e.presets.at(fx),fxOwn);
        assert(a.instruments[fx]["unset"]==json::array({key}));
        json f1=request(A.name);
        int le=0, ge=0; for(auto& it:f1["items"]){ if(it.value("layerEffect",false)) le++; if(it.value("globalEffect",false)) ge++; }
        assert(le>0 && ge>0 && f1["items"]!=f0["items"]);
        a.instruments.erase(fx); a.layers[0].effectPreset.clear(); a.globalEffects.clear(); e.presets.erase(nest.ref());
        std::printf("ok 3: nested instrument, layer effect (%d items) and scene-wide effect (%d items) use the scene's version\n", le, ge);
    }

    // ---- 4. Immediate mode pieces: an edit of the scene's version patches only what changed
    {
        Scene before=e.scenes[A.name], after=before;
        Preset own=e.effectivePreset(after,R); own.params["level"]=0.33;
        after.instruments[R]=presetOwnDiff(libR,own);
        Preset b=e.effectivePreset(before,R), a=e.effectivePreset(after,R);
        assert(changedKeys(b,a)==std::set<std::string>{"level"});
        e.scenes[A.name]=after;
        Engine::OwnScope scope(e,&e.scenes[A.name]);
        json patched=patchForPreset(e,a1,b,a,77);
        json pi=itemsOf(patched,R), oi=itemsOf(a1,R); assert(pi.size()==oi.size() && !pi.empty());
        for(size_t i=0;i<pi.size();i++){ json x=pi[i], y=oi[i]; assert(std::abs(x["params"]["level"].get<double>()-0.33)<1e-6); x["params"].erase("level"); y["params"].erase("level"); assert(x==y); }
        assert(itemsOf(patched,ST)==itemsOf(a1,ST));
        // first edit of an instrument: "before" is the shared one
        assert(e.effectivePreset(B,R).params==libR.params);
        // an edit of the shared instrument, seen through the scene's version
        const json& d=e.scenes[A.name].instruments[R];
        Preset lib2=libR; lib2.params["bandLow"]=0.5;                    // a setting the scene changed: hidden
        assert(changedKeys(presetWithOwn(libR,d),presetWithOwn(lib2,d)).empty());
        lib2=libR; lib2.params["noiseScale"]=0.2;                        // one it left alone: comes through
        assert(changedKeys(presetWithOwn(libR,d),presetWithOwn(lib2,d))==std::set<std::string>{"noiseScale"});
        std::puts("ok 4: a scene-version edit patches only the changed setting; shared edits pass where the scene left the setting alone");
    }
    assert(e.ownScene==nullptr);

    // ---- 5. saved with the scene; the instrument file is not written; reload brings it back
    {
        fs::path presetFile=e.presetPath(libR); std::string beforeBytes; { std::ifstream in(presetFile,std::ios::binary); beforeBytes.assign(std::istreambuf_iterator<char>(in),{}); }
        json wantA=e.scenes[A.name].instruments;
        e.saveScene(e.scenes[A.name]); e.saveScene(e.scenes[B.name]);
        Engine e2; e2.rendererFolder=argv[1]; e2.loadAll();
        assert(e2.scenes.at(A.name).instruments==wantA && e2.scenes.at(B.name).instruments.empty());
        json fileB=readJsonFile(e2.scenePath(e2.scenes.at(B.name))); assert(!fileB.contains("instruments"));
        std::string afterBytes; { std::ifstream in(presetFile,std::ios::binary); afterBytes.assign(std::istreambuf_iterator<char>(in),{}); }
        assert(beforeBytes==afterBytes && e2.presets.at(R).params==libR.params);
        // renaming the instrument: the scene's version follows
        assert(e2.renamePreset(R,"blocking snares renamed"));
        assert(e2.scenes.at(A.name).instruments.contains("hits/blocking snares renamed") && !e2.scenes.at(A.name).instruments.contains(R));
        assert(e2.renamePreset("hits/blocking snares renamed","blocking snares"));
        e2.deleteScene(A.name); e2.deleteScene(B.name);
        std::puts("ok 5: saved with the scene, instrument file byte-identical, reload restores it, rename follows");
    }
    std::puts("PASS: scene-owned instrument versions");
}
