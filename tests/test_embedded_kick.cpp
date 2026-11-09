// clang++ -std=c++17 -O1 -I src/formsBox -I ../../libs/json/include tests/test_embedded_kick.cpp -o /tmp/test_embedded_kick
// Run with a fresh scratch directory.
#include "formsUndo.h"
#include "formsSoundModel.h"
#include <cassert>
#include <cstdio>
using namespace forms;
static std::vector<uint8_t> load(const fs::path& path){
    std::ifstream f(path,std::ios::binary);return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)),{});
}
static void save(const fs::path& path,const std::vector<uint8_t>& bytes){
    fs::create_directories(path.parent_path());std::ofstream f(path,std::ios::binary);f.write((const char*)bytes.data(),bytes.size());
}
static std::vector<uint8_t> wave(){
    std::vector<uint8_t> b;
    auto tag=[&](const char* s){b.insert(b.end(),s,s+4);};
    auto u16=[&](int n){b.push_back(n&255);b.push_back(n>>8);};
    auto u32=[&](int n){for(int k=0;k<4;++k)b.push_back((n>>(k*8))&255);};
    tag("RIFF");u32(44);tag("WAVE");tag("fmt ");u32(16);u16(1);u16(2);
    u32(44100);u32(176400);u16(4);u16(16);tag("data");u32(8);
    for(int i=0;i<8;++i)b.push_back(i*31);return b;
}
int main(int argc,char** argv){
    assert(argc==2);fs::path root=argv[1];
    for(int n=1;n<260;++n){
        std::vector<uint8_t> bytes(n);for(int i=0;i<n;++i)bytes[i]=i;
        assert(formskick::decode(formskick::encode(bytes))==bytes);
    }
    auto source=root/"any folder"/"subfolder"/"kick.wav";auto bytes=wave();save(source,bytes);
    Engine engine;engine.rendererFolder=root/"original";
    Scene scene;scene.name="EMBEDDED KICK";scene.layers.resize(1);
    engine.importKick(scene,source);
    assert(scene.kickSample && scene.performance["kickSample"]=="kick.wav");
    assert(audioChannels(engine.kickFile(scene).string())==2);
    assert(load(engine.kickFile(scene))==bytes);
    Scene twin=scene;auto otherBytes=bytes;otherBytes.back()^=127;
    auto otherSource=root/"another folder"/"kick.wav";save(otherSource,otherBytes);engine.importKick(twin,otherSource);
    assert(twin.kickSample->id!=scene.kickSample->id && engine.kickFile(twin)!=engine.kickFile(scene));
    engine.scenes[scene.name]=scene;engine.saveScene(scene);
    auto file=engine.scenePath(scene);auto saved=readJsonFile(file);
    assert(saved["kickSampleAsset"]["encoding"]=="base64");
    assert(saved.dump().find(source.string())==std::string::npos);
    engine.useSetlist=false;engine.manualScene=scene.name;engine.newSession(42);
    auto request=readJsonFile(engine.makeRequest(true));
    assert(!request.contains("kickSampleAsset") && request["performance"]["kickSample"]=="kick.wav");
    // A scene file alone must survive relocation with the source and cache gone.
    Engine moved;moved.rendererFolder=root/"relocated";
    fs::create_directories(moved.scenesDir());fs::copy_file(file,moved.scenesDir()/file.filename());
    writeJsonAtomic(moved.rendererFolder/"generators.json",json::object());
    fs::remove(source);fs::remove_all(engine.rendererFolder/"samples");
    moved.loadAll();auto& restored=moved.scenes.at(scene.name);
    assert(restored.kickSample && load(moved.kickFile(restored))==bytes);
    moved.currentBank="copies";auto duplicate=moved.duplicateScene(scene.name);
    assert(!duplicate.empty() && readJsonFile(moved.scenePath(moved.scenes.at(duplicate)))["kickSampleAsset"]==saved["kickSampleAsset"]);
    assert(moved.renameScene(duplicate,"Renamed kick scene"));
    assert(moved.scenes.at("Renamed kick scene").kickSample->data==scene.kickSample->data);
    // Undo snapshots share the immutable sample instead of repeatedly dumping its bytes.
    auto snapshot=captureState(moved);
    assert(snapshot.kickSamples.at(scene.name)==restored.kickSample);
    assert(snapshot.scenes.at(scene.name).second.find("\"data\"")==std::string::npos);
    UndoStack undo;undo.engine=&moved;undo.reset();
    moved.scenes.at(scene.name).kickSample.reset();undo.commit("clear sample");
    assert(undo.undo() && moved.scenes.at(scene.name).kickSample);
    assert(undo.redo() && !moved.scenes.at(scene.name).kickSample);
    // Unsupported files and corrupt embedded payloads cannot replace the working selection.
    auto bad=root/"bad.txt";save(bad,{1,2,3});
    auto original=scene.kickSample;bool rejected=false;
    try{engine.importKick(scene,bad);}catch(...){rejected=true;}
    assert(rejected && scene.kickSample==original);
    auto corrupt=*original;corrupt.id="../../escape";rejected=false;
    try{formskick::materialize(corrupt,root/"cache");}catch(...){rejected=true;}
    assert(rejected);
    // AIFF channel metadata must retain stereo as well as the exact source bytes.
    std::vector<uint8_t> aiff={'F','O','R','M',0,0,0,20,'A','I','F','F','C','O','M','M',0,0,0,2,0,2};
    auto aiffFile=root/"stereo.aiff";save(aiffFile,aiff);
    engine.importKick(scene,aiffFile);assert(audioChannels(engine.kickFile(scene).string())==2);
    assert(load(engine.kickFile(scene))==aiff);
    std::puts("PASS: portable embedded kick, exact WAV/AIFF bytes, stereo, relocation, rename, duplicate, undo and invalid-file rejection");
}
