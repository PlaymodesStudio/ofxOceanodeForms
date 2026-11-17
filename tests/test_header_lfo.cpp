// The scene's header LFO (formsClock.h Header::step, "headerLfo*" performance settings).
// clang++ -std=c++17 -O1 -DOLD -I tools/pre_header_tab_2026-11-17 -I ../../libs/json/include tests/test_header_lfo.cpp -o /tmp/lfo_old && /tmp/lfo_old > /tmp/lfo_old.txt
// clang++ -std=c++17 -O1 -I src/formsBox -I ../../libs/json/include tests/test_header_lfo.cpp -o /tmp/lfo_new && /tmp/lfo_new /tmp/lfo_old.txt
#include "formsClock.h"
#include <cassert>
#include <cstdio>
#include <cstring>
using namespace forms;

// a busy scene (jumps, repeats, gate, tremolo) with the LFO at depth 0: every frame's outputs
static void trace(FILE* out){
    Header head; head.rng.seed(1234); head.width = 1920; head.bars = 8; head.tremoloOn = true;
    head.perf = {{"HeaderModChance", 0.5}, {"repeatChance", 0.7}, {"gateChance", 0.5}, {"tremoloChance", 0.8},
                 {"headerLfoDepth", 0}, {"headerLfoShape", 4}, {"headerLfoBeats", 1}};
    double beat = 0;
    head.beatAt = [&](float, float x){ return beat + (x / head.width) * 32; };
    for(int f = 0; f < 6000; f++){
        beat = f / 60.0 * 2;   // 120 BPM
        head.phase = (float)std::fmod(beat / 32, 1.0);
        head.step(f / 60.0, 1.f / 60);
        fprintf(out, "%a %a %a %a %d\n", head.headerPos, head.repeatPx, head.opacity, head.phLock, head.actualBar);
    }
}

int main(int argc, char** argv){
#ifdef OLD
    trace(stdout);
    return 0;
#else
    // 1. depth 0: bit for bit what the header did before the LFO existed
    if(argc > 1){
        FILE* tmp = tmpfile(); trace(tmp); rewind(tmp);
        FILE* old = fopen(argv[1], "r"); assert(old);
        char a[256], b[256]; int lines = 0;
        while(fgets(a, sizeof a, tmp)){ assert(fgets(b, sizeof b, old)); assert(!strcmp(a, b)); lines++; }
        assert(!fgets(b, sizeof b, old) && lines == 6000);
        printf("depth 0: %d frames identical to the pre-LFO header\n", lines);
    }
    // 2. shapes: range, and the musical place decides (not the frame rate)
    for(int shape = 0; shape < kHeaderLfoShapes; shape++){
        std::function<float(float, float)> noise = [](float x, float){ return 0.5f + 0.5f * std::sin(x * 6.2831853f); };
        float lo = 2, hi = -2;
        for(int i = 0; i < 4000; i++){ float v = headerLfoValue(shape, i / 100.0, noise); lo = std::min(lo, v); hi = std::max(hi, v); assert(v >= -1.0001f && v <= 1.0001f); }
        assert(hi > 0.6f && lo < -0.6f);
        assert(headerLfoValue(shape, 3.37, noise) == headerLfoValue(shape, 3.37, noise));
    }
    // 3. sine, depth 0.5, 4 beats, header at the middle: headerPos = 960 + 480 sin(2 pi beat / 4); the
    //    transport's place (headerPos + repeatPx - lfoPx) does not move
    for(float dt : {1.f / 30, 1.f / 60, 1.f / 144}){
        Header head; head.width = 1920; head.bars = 8; head.header = 0.5f; head.smooth = 0;
        double beat = 0;
        head.beatAt = [&](float, float x){ return beat + (x / head.width) * 32; };
        head.perf = {{"headerLfoDepth", 0.5}, {"headerLfoShape", 0}, {"headerLfoBeats", 4}};
        int n = (int)(40 / dt);
        for(int f = 0; f < n; f++){
            beat = f * dt * 2; head.phase = (float)std::fmod(beat / 32, 1.0);
            head.step(f * dt, dt);
            if(f * dt < 30) continue;   // the header's own glide has settled
            float base = head.headerPos + head.repeatPx - head.lfoPx;
            assert(std::fabs(base - 960) < 0.5f);
            assert(std::fabs(head.lfoPx - 480 * std::sin(6.2831853 * beat / 4)) < 0.05f);
        }
    }
    // 4. square, 1 beat, quarter-cycle phase offset; clipped at the view's edges with the header near the left
    {
        Header head; head.width = 1920; head.bars = 8; head.header = 0.1f; head.smooth = 0;
        double beat = 0;
        head.beatAt = [&](float, float x){ return beat + (x / head.width) * 32; };
        head.perf = {{"headerLfoDepth", 0.5}, {"headerLfoShape", 4}, {"headerLfoBeats", 1}, {"headerLfoPhase", 0.25}};
        for(int f = 0; f < 3000; f++){
            beat = f / 60.0 * 2 + 1e-4; head.phase = (float)std::fmod(beat / 32, 1.0);
            head.step(f / 60.0, 1.f / 60);
            if(f < 1800) continue;
            bool up = std::fmod(beat + 0.25, 1.0) < 0.5;
            assert(std::fabs(head.headerPos - (up ? 192 + 480 : 0)) < 0.5f);   // 192 - 480 < 0: held at the left edge
            assert(head.headerPos >= 0 && head.headerPos <= 1919);
        }
    }
    // 5. the gate keeps its musical timing under an LFO
    {
        Header head; head.width = 1920; double beat = 4;
        head.beatAt = [&](float, float x){ return beat + (x / head.width - 0.5) * 32; };
        head.header = 0.5f; head.smooth = 0;
        head.perf = {{"gateChance", 1}, {"gateMults", {1}}, {"headerLfoDepth", 0.8}, {"headerLfoShape", 1}, {"headerLfoBeats", 0.5}};
        for(int f = 0; f < 2400; f++){
            beat = 4 + f / 120.0; head.phase = (float)(beat / 32); head.step(f / 60.0, 1.f / 60);
            if(f < 1200) continue;
            double hb = head.beatAt(0, head.headerPos - head.lfoPx);
            double g = hb / 8 - std::floor(hb / 8);
            if(g > 0.51 && g < 0.99){ double ph = std::fmod(hb - (std::floor(hb / 8) * 8 + 4), 1.0); if(std::fabs(ph - 0.25) > 0.02 && ph > 0.02 && ph < 0.98) assert(head.gate == (ph < 0.25 ? 1.f : 0.f)); }
        }
    }
    printf("header LFO: all good\n");
    return 0;
#endif
}
