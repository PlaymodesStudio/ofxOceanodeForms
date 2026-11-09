// clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I ../../libs/json/include tests/test_subbeat_sync.cpp -o /tmp/test_subbeat_sync
#include "scrollerCore.h"
#include "layerRing.h"
#include <cassert>
#include <cstdio>

using namespace forms;
static std::shared_ptr<Image> page(int bars){
    auto im = std::make_shared<Image>();
    im->bars = bars; im->texture = std::make_shared<ofTexture>();
    return im;
}
static double gridError(double start){ return std::fabs(std::remainder(start, 4.0)); }

int main(){
    constexpr int W = 1920, H = 1080;
    constexpr float hp = W * 0.218182f;
    Schedule::Layer L;
    ScrollerCore main;
    main.immediate = true;
    L.ready.push_back(page(10));
    main.update(&L, 0.41694f, hp, W, H);
    std::printf("Immediate startup: page starts at %.9f beats (bar-grid error %.9f)\n", main.headerStart, gridError(main.headerStart));
    assert(gridError(main.headerStart) < 1e-6);
    // Replacing at different frame phases must not accumulate a fractional origin.
    for(int bars : {10, 8, 8, 6, 10, 8}){
        double oldStart = main.headerStart;
        double oldLen = main.headerLen;
        float phase = 0.42341f;
        double expectedBeat = oldStart + (phase + hp / W) * oldLen;
        L.ready.push_back(page(bars));
        main.update(&L, phase, hp, W, H);
        assert(gridError(main.headerStart) < 1e-6);
        if(oldLen == bars * 4.0) assert(std::fabs(main.headerBeat - expectedBeat) < 1e-4);
    }
    for(float phase : {0.6f, 0.8f, 0.99f}) main.update(&L, phase, hp, W, H);
    double beforeWrap = main.headerBeat;
    L.ready.push_back(page(8));
    main.update(&L, 0.01f, hp, W, H);
    assert(std::fabs(main.headerBeat - beforeWrap - 0.02 * 32) < 1e-4);
    assert(gridError(main.headerStart) < 1e-6);
    // Switch now shares the same replacement code even with Immediate off.
    main.immediate = false;
    L.replaceNext = page(10); L.ready.push_back(L.replaceNext);
    main.update(&L, 0.02f, hp, W, H);
    assert(!L.replaceNext && gridError(main.headerStart) < 1e-6);
    // The rendered sixteenth at the playhead must have the same beat phase on
    // layers of 8/10/6/4 bars, at fractional positions throughout a page.
    for(double beat : {42.125, 42.25, 42.5, 43.75, 44.0, 47.375}){
        for(int bars : {8, 10, 6, 4}){
            LayerRing ring; Schedule::Layer extra;
            auto im = page(bars); im->png = "grid";
            Schedule::Layer::Slot slot; slot.img = im; slot.start = 40; slot.len = bars * 4.0;
            extra.slots.push_back(slot);
            ring.update(extra, beat, hp, W, H);
            auto d = ring.lastDrawn.front();
            double sourceBeat = (hp - d.x) / W * d.len + d.start;
            assert(std::fabs(sourceBeat - beat) < 1e-5);
        }
    }
    std::puts("Immediate replacement and fractional-beat layer alignment passed");
}
