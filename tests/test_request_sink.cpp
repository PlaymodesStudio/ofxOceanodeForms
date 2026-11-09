// clang++ -std=c++17 -O1 -I src/formsBox -I ../../libs/json/include tests/test_request_sink.cpp -o /tmp/test_request_sink
// Run with a disposable scratch directory.
#include "formsEngine.h"
#include <cassert>
#include <cstdio>
#include <thread>
using namespace forms;

int main(int argc,char** argv){
    assert(argc==2);
    Scene scene; scene.name="request_test"; scene.layers.resize(2);
    Engine files,memory;
    files.rendererFolder=fs::path(argv[1])/"files";
    memory.rendererFolder=fs::path(argv[1])/"memory";
    for(auto* engine:{&files,&memory}){
        engine->scenes[scene.name]=scene;
        engine->manualScene=scene.name; engine->useSetlist=false; engine->newSession(42);
    }
    memory.session=files.session;
    std::map<fs::path,json> staged;
    memory.requestSink=[&](const fs::path& path,json request){staged[path]=std::move(request);};
    auto filePath=files.makeRequest(true),memoryPath=memory.makeRequest(true);
    assert(!filePath.empty() && !memoryPath.empty());
    assert(fs::exists(filePath) && !fs::exists(memoryPath));
    assert(staged.at(memoryPath)==readJsonFile(filePath));
    assert(memory.pending.size()==1 && memory.lastImage->id==staged.at(memoryPath)["id"]);
    auto extraFile=files.makeRequest(true,"",nullptr,1,8,&scene);
    auto extraMemory=memory.makeRequest(true,"",nullptr,1,8,&scene);
    assert(!extraFile.empty() && !extraMemory.empty() && !fs::exists(extraMemory));
    assert(staged.at(extraMemory)==readJsonFile(extraFile));
    assert(staged.at(extraMemory)["layer"]==2);
    // Persist the original requests on the worker, retaining the archive protocol.
    std::thread writer([&]{for(auto& [path,request]:staged) writeJsonAtomic(path,request);});
    writer.join();
    for(auto& [path,request]:staged) assert(readJsonFile(path)==request);
    auto image=memory.lastImage;
    memory.onReady(image->id,"extra.png","extra.json");
    assert(memory.schedule->layer(1).ready.front()==image);
    std::puts("PASS: requests bypass caller-thread disk I/O; worker persistence, seeds, layers and delivery preserved");
}
