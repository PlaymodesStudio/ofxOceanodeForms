// Fractional layer lengths (LayerDef::bars = 0.25, 1.5 ...), headless:
//   json     a 0.25-bar layer survives save / load; whole lengths are still written as integers
//   quant    lengths snap to 1/16 bar, at least 1/16
//   loop     the export loop is the common multiple of fractional lengths
//   harmony  a page shorter than a bar repeated (repeat 4) keeps one chord for all its copies; a
//            1.5-bar page reads the chords of the bars it starts in
//   phrase   pages per render (LayerDef::phrase), saved only when set; a phrase's pages in order,
//            each with its window and the phrase's chords at its bars
// The scrolling itself (contiguous pages, chords against layer 1, cuts, repeats) is checked by
// test_layers_live.cpp with FRAC=1 / FRAC=2.
//
// clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I src/formsGL -I ../../libs/json/include tests/test_fractional_bars.cpp -o /tmp/test_fractional_bars
#include "formsEngine.h"
#include "formsGLExportMedia.h"
#include <cstdio>
using namespace forms;

static int fails = 0;
#define CHECK(c) do{ if(!(c)){ fails++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

int main(){
    // json
    LayerDef l; l.bars = 0.25;
    json j = l;
    CHECK(j["bars"].is_number_float() && j["bars"].get<double>() == 0.25);
    LayerDef back = j.get<LayerDef>();
    CHECK(back.bars == 0.25);
    l.bars = 8; j = l;
    CHECK(j["bars"].is_number_integer() && j["bars"].get<int>() == 8);
    CHECK(json(LayerDef(j.get<LayerDef>()))["bars"].dump() == "8");
    CHECK(barsFrom(json{{"bars", 1.5}}, "bars") == 1.5);
    CHECK(barsFrom(json{{"bars", "x"}}, "bars", 8) == 8);
    // quant
    CHECK(quantBars(0.25) == 0.25);
    CHECK(quantBars(0.3) == 0.3125);
    CHECK(quantBars(0) == 1.0 / 16);
    CHECK(quantBars(-3) == 1.0 / 16);
    CHECK(barCount(0.25) == 1 && barCount(1.5) == 2 && barCount(8) == 8);
    CHECK(barIndex(2.999999999999) == 3 && barIndex(3.25) == 3);
    CHECK(barsText(0.25) == "0.25" && barsText(8) == "8");
    // loop
    CHECK(exportmedia::loopBars({4, 6, 8}) == 24);
    CHECK(exportmedia::loopBars({0.25, 8}) == 8);
    CHECK(exportmedia::loopBars({1.5, 8}) == 24);
    CHECK(exportmedia::loopBars({0.75, 0.5}) == 1.5);
    // harmony: layer 1's plan with a 0.25-bar page x4 from bar 3, and a 1.5-bar page from bar 4
    Engine e;
    Engine::PlanEntry a; a.startBar = 3; a.period = 0.25; a.bars = 1; a.scene = "S"; e.layer1Plan.push_back(a);
    Engine::PlanEntry b; b.startBar = 4; b.period = 1.5; b.bars = 1.5; b.scene = "S"; e.layer1Plan.push_back(b);
    CHECK(e.harmonyBar(3.0) == 3 && e.harmonyBar(3.5) == 3 && e.harmonyBar(3.75) == 3);
    CHECK(e.harmonyBar(4.0) == 4 && e.harmonyBar(5.0) == 5);
    // a page repeated across bars (0.5-bar page x4 from bar 6): every copy folds onto bar 6
    Engine::PlanEntry c; c.startBar = 6; c.period = 0.5; c.bars = 2; c.scene = "S"; e.layer1Plan.push_back(c);
    CHECK(e.harmonyBar(6.5) == 6 && e.harmonyBar(7.0) == 6 && e.harmonyBar(7.5) == 6);
    // whole-bar pages as before (8-bar page x2 from bar 8: bar 17 folds onto bar 9)
    Engine::PlanEntry d; d.startBar = 8; d.period = 8; d.bars = 16; d.scene = "S"; e.layer1Plan.push_back(d);
    CHECK(e.harmonyBar(17) == 9 && e.harmonyBar(8) == 8 && e.harmonyBar(23) == 15);
    // phrase: pages per render, rounded up to whole pages; saved only when set
    LayerDef p; p.bars = 0.5; p.phrase = 4;
    CHECK(phraseSlices(p) == 8 && phraseBars(p) == 4);
    p.phrase = 3.2; CHECK(phraseSlices(p) == 7 && phraseBars(p) == 3.5);
    p.phrase = 0.25; CHECK(phraseSlices(p) == 1);
    p.phrase = 0; CHECK(phraseSlices(p) == 1 && !json(p).contains("phrase"));
    p.phrase = 4; CHECK(json(p)["phrase"].get<double>() == 4 && json(p).get<LayerDef>().phrase == 4);
    // a phrase's pages: in order, each with its window and the phrase's chords at its bars
    auto im = std::make_shared<Image>();
    im->bars = 0.5; im->slices = 8; im->repeat = 2; im->startBar = 10; im->startBeat = 40;
    im->roots = {1, 2, 3, 4}; im->trans = {0, 0, 5, 0};
    auto pages = Engine::expandRepeats(im);
    CHECK(pages.size() == 16);
    CHECK(pages[3]->slice == 3 && pages[3]->startBar == 11.5 && pages[3]->roots == std::vector<int>{2});
    CHECK(pages[5]->roots == std::vector<int>{3} && pages[5]->trans == std::vector<int>{5});
    CHECK(pages[8]->slice == 0 && pages[8]->startBeat == 56 && pages[8]->roots == std::vector<int>{1});   // the repeat
    CHECK(pages[3]->sliceU0() == 3.0 / 8 && pages[3]->sliceUW() == 1.0 / 8);
    auto one = std::make_shared<Image>(); one->bars = 4; one->roots = {1, 2, 3, 4};
    auto ones = Engine::expandRepeats(one);
    CHECK(ones.size() == 1 && ones[0]->roots.size() == 4 && ones[0]->sliceU0() == 0 && ones[0]->sliceUW() == 1);
    std::printf("%s (%d failed)\n", fails ? "FAILED" : "all passed", fails);
    return fails ? 1 : 0;
}
