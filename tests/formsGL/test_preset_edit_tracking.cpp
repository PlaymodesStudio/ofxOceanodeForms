// Scene-tab and nested instrument edits must reach immediate mode independently
// of the Instruments tab's selection, including edits made during a redraw.
// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_preset_edit_tracking.cpp -o /tmp/test_preset_edit_tracking
#include "formsLiveEdit.h"
#include "formsGLSchemas.h"
#include <cassert>
#include <cstdio>
using namespace forms;

int main(){
    Engine engine;
    auto schemas=json::parse(formsgl::generatorSchemasJson());
    for(auto& g:schemas["generators"]) engine.gens.push_back(genDefFromJson(g));
    Preset bass; bass.gen="pulse"; bass.name="bass"; bass.params={{"volume",2},{"thickness",3}};
    Preset other=bass; other.name="other";
    Preset stack; stack.gen="stack"; stack.name="stack";
    stack.layers=json::array({{{"preset",bass.ref()}},{{"gen","pulse"},{"params",{{"volume",1}}}}});
    engine.presets={{bass.ref(),bass},{other.ref(),other},{stack.ref(),stack}};
    json recipe={{"seed",123},{"items",json::array()}};
    for(auto& item:engine.presetItemsSeeded(bass,123)) recipe["items"].push_back(item);
    for(auto& item:engine.presetItemsSeeded(other,123)) recipe["items"].push_back(item);
    PresetEditTracker tracker;
    Preset before,after;
    assert(!tracker.next(engine.presets,true,false,before,after));
    // No GUI selection is involved: edit the bass through the shared preset map.
    engine.presets.at(bass.ref()).params["volume"]=1;
    assert(tracker.next(engine.presets,true,false,before,after));
    assert(before.ref()==bass.ref() && before.params["volume"]==2 && after.params["volume"]==1);
    auto patched=patchForPreset(engine,recipe,before,after,123);
    assert(patched["items"][0]["params"]["volume"]==1);
    assert(patched["items"][1]==recipe["items"][1]);
    // Drag again, edit another instrument, then an inline stack child while busy.
    engine.presets.at(bass.ref()).params["volume"]=0.75;
    assert(!tracker.next(engine.presets,true,true,before,after));
    engine.presets.at(bass.ref()).params["volume"]=0.5;
    engine.presets.at(other.ref()).params["thickness"]=1;
    engine.presets.at(stack.ref()).layers[1]["params"]["volume"]=0.25;
    assert(!tracker.next(engine.presets,true,true,before,after));
    std::set<std::string> delivered;
    while(tracker.next(engine.presets,true,false,before,after)){
        assert(delivered.insert(after.ref()).second);
        if(after.ref()==bass.ref()){
            assert(before.params["volume"]==1 && after.params["volume"]==0.5);
            patched=patchForPreset(engine,patched,before,after,123);
        }else if(after.ref()==other.ref()){
            patched=patchForPreset(engine,patched,before,after,123);
        }else{
            assert(after.ref()==stack.ref());
            assert(before.layers[1]["params"]["volume"]==1 && after.layers[1]["params"]["volume"]==0.25);
        }
    }
    assert(delivered.size()==3);
    assert(patched["items"][0]["params"]["volume"]==0.5);
    assert(patched["items"][1]["params"]["thickness"]==1);
    // Notes don't redraw; Immediate off keeps its existing next-page behavior.
    engine.presets.at(bass.ref()).notes="editor notes";
    assert(!tracker.next(engine.presets,true,false,before,after));
    engine.presets.at(bass.ref()).params["volume"]=0.2;
    assert(!tracker.next(engine.presets,false,false,before,after));
    assert(!tracker.next(engine.presets,true,false,before,after));
    // Revert is an edit too, and requires no Save to reach the current recipe.
    engine.presets.at(bass.ref())=bass;
    assert(tracker.next(engine.presets,true,false,before,after));
    assert(before.params["volume"]==0.2 && after.params["volume"]==2);
    std::puts("PASS: scene edits, queued drags, multiple presets, nested stacks, revert and Immediate off");
}
