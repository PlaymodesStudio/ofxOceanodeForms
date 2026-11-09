// clang++ -std=c++17 -O1 -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/formsGL/test_solo_blocks.cpp -o /tmp/test_solo_blocks
#include "formsEngine.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
using forms::json;
int main(){
    formsgl::Schemas schemas;assert(schemas.loadEmbedded());
    json params={{"steps",16},{"patternSteps",8},{"fills",{8}},{"playOn","odd"},{"repChance",100},
        {"clearChance",100},{"clearScope","the repetition"},{"arp","up"},{"arpSteps",4},
        {"length",{2,2}},{"partials",{3,3}},{"octave",2},{"lfoPower",0},{"envPower",1}};
    json item={{"gen","pulse"},{"preset","pulse/arp loop block"},{"params",params}};
    json req={{"size",{320,180}},{"bars",4},{"seed",123},{"layer",1},{"items",json::array({item})}};
    auto lower=formsgl::Sketch(schemas).render(req);
    req["layer"]=3;auto upper=formsgl::Sketch(schemas).render(req);
    assert(lower.error.empty() && upper.error.empty() && lower.rgba==upper.rgba);
    assert(upper.covers.size()==1 && *lower.covers[0].alpha==*upper.covers[0].alpha);
    const auto& mask=upper.covers[0];
    assert(mask.settings.mode=="eraser" && mask.settings.target=="allBelow");
    assert(mask.settings.growRight==0 && mask.settings.growHeight==0 && mask.settings.feather==0 && mask.settings.release==0);
    for(int y=0;y<mask.h;++y)for(int x=0;x<mask.w;++x){
        bool block=(x/40)%2==1;assert((*mask.alpha)[y*mask.w+x]==(block?255:0));
        // Apply the production eraser blend to a lower layer's alpha: no opaque black.
        float belowAlpha=1-(*mask.alpha)[y*mask.w+x]/255.f;
        assert(belowAlpha==(block?0.f:1.f));
    }
    // Per-bar clipping and volume apply to the eraser as well as to the notes.
    req["items"][0]["clipBars"]={1,2};req["items"][0]["gain"]=0.5;
    auto clipped=formsgl::Sketch(schemas).render(req);assert(clipped.covers.size()==1);
    assert(clipped.covers[0].settings.amount==.5f);
    for(int y=0;y<180;++y)for(int x=0;x<320;++x){
        auto a=(*clipped.covers[0].alpha)[y*320+x];
        assert(a==((x>=120&&x<160)?255:0));
    }
    req["items"][0]["gain"]=0;
    assert(formsgl::Sketch(schemas).render(req).covers.empty());
    req["items"][0].erase("gain");req["items"][0].erase("clipBars");req["items"][0]["params"]["clearChance"]=0;
    assert(formsgl::Sketch(schemas).render(req).covers.empty());
    std::puts("PASS: legacy solo blocks erase lower alpha on any layer; exact windows, clipping, gain and clearChance");
}
