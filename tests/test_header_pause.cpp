// Timed header effects go on while the image stands still (Header::still / runWhenStill / freeBeats).
// clang++ -std=c++17 -O1 -I src/formsBox -I ../../libs/json/include tests/test_header_pause.cpp -o /tmp/hp && /tmp/hp
#include "formsClock.h"
#include <cassert>
#include <cstdio>
#include <set>
using namespace forms;
int main(){
    for(bool run : {true, false}){
        Header head; head.width = 1920; head.bars = 8; head.header = 0.5f; head.smooth = 0; head.runWhenStill = run;
        double beat = 0;
        head.beatAt = [&](float, float x){ return beat + (x / head.width) * 32; };
        head.perf = {{"repeatChance", 1}, {"repeatBarStart", 0.0}, {"repeatMults", {2}}, {"repeatNum", {4}},
                     {"gateChance", 1}, {"gateMults", {1}}, {"headerLfoDepth", 0.2}, {"headerLfoBeats", 4}};
        int f = 0;
        auto frame = [&](bool still){ if(!still) beat += 2 / 60.0; head.still = still; head.phase = (float)std::fmod(beat / 32, 1.0); head.step(f / 60.0, 1.f / 60); f++; };
        for(int i = 0; i < 1500; i++) frame(false);
        // 20 s standing still at 120 BPM = 40 beats
        std::set<int> repeats, gates; float lfoLo = 1e9, lfoHi = -1e9;
        for(int i = 0; i < 1200; i++){ frame(true); repeats.insert((int)std::lround(head.repeatPx)); gates.insert((int)head.gate); lfoLo = std::min(lfoLo, head.lfoPx); lfoHi = std::max(lfoHi, head.lfoPx); }
        if(run){
            assert(std::fabs(head.freeBeats - 40) < 0.01);
            assert(repeats.size() == 4);          // the repeat steps through its 4 places over the still image
            assert(gates.size() == 2);            // the gate opens and closes
            assert(lfoHi - lfoLo > 300);          // the LFO swings (0.2 x 1920 = 384 px)
        }else{
            assert(head.freeBeats == 0 && repeats.size() == 1 && gates.size() == 1 && lfoHi - lfoLo < 0.01f);
        }
        frame(false);
        assert(std::fmod(head.freeBeats, 8.0) == 0);   // moving again: back on the image's own beat (whole 2-bar spans)
        printf("runWhenStill %d: ok (freeBeats after resume %.0f)\n", (int)run, head.freeBeats);
    }
    return 0;
}
