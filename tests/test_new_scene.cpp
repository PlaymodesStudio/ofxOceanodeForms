// Phase 5's "done" test, headless: a new scene built from scratch with Forms Box's own calls (the
// window's Instruments "+ new preset" / Save, Scenes "New", Instruments "Add to scene", Setlist
// "+ Add entry" / Save), read back from disk by a fresh engine, then asked for from the setlist.
// Writes the request paths to stdout (lines "REQUEST <path>") for the renderer step
// (test_new_scene.sh draws them with the renderer, src/formsGL, and checks the images).
// Run on a COPY of the renderer folder: it writes presets, a scene, the setlist and a session.
#include "formsEngine.h"
#include <cstdio>
#include <iostream>
using namespace forms;
static int fails = 0;
#define CHECK(c, ...) do{ if(!(c)){ fails++; printf("FAIL "); printf(__VA_ARGS__); printf("\n"); } }while(0)

int main(int argc, char** argv){
    std::string folder = argv[1];
    std::string gen1 = argc > 2 ? argv[2] : "pulse", gen2 = argc > 3 ? argv[3] : "sweep";
    std::string sceneName, ref1, ref2;
    {
        Engine e; e.rendererFolder = folder; e.loadAll();
        CHECK(e.findGen(gen1) && e.findGen(gen2), "generators %s / %s known", gen1.c_str(), gen2.c_str());
        // 1. two presets from scratch (the generators' defaults), saved
        Preset p1; p1.gen = gen1; p1.name = e.uniquePresetName(gen1, "from scratch"); e.savePreset(p1); ref1 = p1.ref();
        Preset p2; p2.gen = gen2; p2.name = e.uniquePresetName(gen2, "from scratch"); e.savePreset(p2); ref2 = p2.ref();
        // 2. a new scene with both
        sceneName = e.createScene("FROM SCRATCH");
        Scene& s = e.scenes[sceneName];
        e.addItemToScene(s, 0, ref1);
        e.addItemToScene(s, 0, ref2);
        e.saveScene(s);
        // 3. into the setlist
        e.setlist.push_back({sceneName, 2, false});
        e.saveSetlist();
        printf("made %s, %s, scene \"%s\", setlist entry %d\n", ref1.c_str(), ref2.c_str(), sceneName.c_str(), (int)e.setlist.size());
    }
    // 4. a fresh engine reads it all back
    Engine f; f.rendererFolder = folder; f.loadAll();
    CHECK(f.presets.count(ref1) && f.presets.count(ref2), "presets saved");
    CHECK(f.scenes.count(sceneName), "scene saved");
    if(!f.scenes.count(sceneName)){ printf("%d failures\n", fails); return 1; }
    const Scene& s = f.scenes.at(sceneName);
    CHECK(s.layers.size() == 1 && s.layers[0].bars == 8, "one layer of 8 bars");
    int n = 0; float p1 = -1, p2 = -1;
    for(auto& c : s.layers[0].cells){ if(c.item == ref1){ n++; p1 = c.prob; } if(c.item == ref2){ n++; p2 = c.prob; } }
    CHECK(n == 2 && p1 == 100 && p2 == 50, "both instruments in the layer (first at 100%%, second at 50%%): %d, %g, %g", n, p1, p2);
    int entry = -1;
    for(int i = 0; i < (int)f.setlist.size(); i++) if(f.setlist[i].scene == sceneName) entry = i;
    CHECK(entry >= 0, "in the setlist");
    // 5. the setlist plays it: two images
    f.width = 1920; f.height = 1080;
    f.newSession(1234);   // (a new session restarts the setlist: pick the entry after it)
    f.useSetlist = true; f.setlistEntry = std::max(0, entry); f.imagesInEntry = 0;
    int withFirst = 0;
    for(int i = 0; i < 2; i++){
        std::string path = f.makeRequest(true);
        CHECK(!path.empty(), "request %d made", i);
        if(path.empty()) continue;
        auto req = json::parse(std::ifstream(path));
        CHECK(req.value("scene", std::string()) == sceneName, "request %d is the new scene", i);
        bool g1 = false;
        for(auto& it : req["items"]) if(it.value("gen", std::string()) == gen1) g1 = true;
        if(g1) withFirst++;
        printf("REQUEST %s  (%d items:", path.c_str(), (int)req["items"].size());
        for(auto& it : req["items"]) printf(" %s", it.value("gen", it.value("method", std::string("?"))).c_str());
        printf(")\n");
    }
    CHECK(withFirst == 2, "the first instrument is in every image (%d of 2)", withFirst);

    // 6. Instruments tab previews: "Same seed" re-renders the same image, Reroll a new one
    {
        const Preset& pr = f.presets.at(ref1);
        auto previewReq = [&](bool reroll){
            if(reroll) f.previewSeedSet = false;
            json items = f.presetItemsSeeded(pr, f.nextPreviewItemsSeed());
            auto paths = f.makePreviews(sceneName, {{ref1, items}});
            json r = paths.empty() ? json() : json::parse(std::ifstream(paths[0]));
            if(!r.is_null()){ r.erase("id"); }
            return r;
        };
        f.previewLock = true;
        json a = previewReq(true), b = previewReq(false), c = previewReq(false);
        CHECK(!a.is_null() && a == b && b == c, "Same seed: three previews give the same request (seed, harmony, items)");
        json d = previewReq(true);
        CHECK(!d.is_null() && d.value("seed", 0u) != a.value("seed", 0u), "Reroll: a new seed");
        f.previewLock = false;
        json e1 = previewReq(false), e2 = previewReq(false);
        CHECK(e1.value("seed", 0u) != e2.value("seed", 0u), "unlocked: each preview a new seed");
        printf("previews: locked seeds %u %u %u, reroll %u, unlocked %u %u\n", a.value("seed", 0u), b.value("seed", 0u), c.value("seed", 0u), d.value("seed", 0u), e1.value("seed", 0u), e2.value("seed", 0u));
    }
    printf("%d failures\n", fails);
    return fails ? 1 : 0;
}
