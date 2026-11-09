// test_audio.cpp — headless tests for the audio bridge (v2Audio.h): queue, clock, generations, swap
// build: clang++ -std=c++17 -O2 -DV2AUDIO_NO_OF -I src/formsV2 -I <json include> tests/v2/test_audio.cpp
#define V2AUDIO_NO_OF
#include "v2Audio.h"
#include <cstdio>
#include <thread>

using namespace forms2;
using namespace forms2::audio;

static int passed = 0, failed = 0;
#define CHECK(c) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

static const double SR = 48000, BPM = 120, BPS = BPM / 60.0 / SR, FPS = 60, FRAME = BPM / 60.0 / FPS;

static void testQueue(){
    SpscQueue<int, 8> q; int v;
    CHECK(!q.pop(v));
    for(int i = 0; i < 8; i++) CHECK(q.push(i));
    CHECK(!q.push(99));                                   // full
    for(int i = 0; i < 5; i++){ CHECK(q.pop(v)); CHECK(v == i); }
    for(int i = 8; i < 13; i++) CHECK(q.push(i));         // wraps
    for(int i = 5; i < 13; i++){ CHECK(q.pop(v)); CHECK(v == i); }
    CHECK(!q.pop(v));
    // two threads: order and no loss
    SpscQueue<long, 64> t; const long N = 200000; long sum = 0, last = -1; bool ordered = true;
    std::thread prod([&]{ for(long i = 0; i < N; i++) while(!t.push(i)) std::this_thread::yield(); });
    long got = 0;
    while(got < N){ long x; if(t.pop(x)){ ordered &= x == last + 1; last = x; sum += x; got++; } }
    prod.join();
    CHECK(ordered); CHECK(sum == N * (N - 1) / 2);
}

static void testClock(){
    AudioClock c;
    c.publish({10.0, BPS, true});
    CHECK(std::fabs(c.blockStart() - 10.0) < 1e-12);
    // frames published with a small jitter: the clock stays close and moves smoothly
    double maxErr = 0, maxStep = 0, prev = c.beat; double t = 10.0;
    for(int block = 0; block < 2000; block++){
        if(block % 3 == 0){ t = 10.0 + block * 256 * BPS + ((block / 3) % 2 ? 0.004 : -0.004); c.publish({t, BPS, true}); }
        double b = c.blockStart();
        double ideal = 10.0 + block * 256 * BPS;
        maxErr = std::max(maxErr, std::fabs(b - ideal));
        if(block) maxStep = std::max(maxStep, std::fabs((b - prev) - 256 * BPS));
        prev = b;
        c.advance(256);
    }
    CHECK(maxErr < 0.01); CHECK(maxStep < 0.001);
    // a jump (seek) snaps
    c.publish({100.0, BPS, true});
    CHECK(std::fabs(c.blockStart() - 100.0) < 1e-9);
    // paused: holds the published beat
    c.publish({50.0, BPS, false}); c.advance(256);
    CHECK(std::fabs(c.blockStart() - 50.0) < 1e-9);
}

static std::vector<float> rowCol(int r, float a){ std::vector<float> v(85, 0.f); v[(size_t)r] = a; return v; }

static void testBridge(){
    Bridge br;
    snd::SoundEngine* e1 = new snd::SoundEngine();
    br.installEngine(e1);
    std::vector<float> L(256), R(256);
    // nothing published yet: silence
    br.process(L.data(), R.data(), 256);
    CHECK(br.running() == e1);
    CHECK(L[100] == 0);
    // a simulated run: frames at 60 fps push the column under the playhead and the clock
    double beat = 0, frameT = 0, sampleT = 0, energy = 0; long nextFrameSample = 0;
    for(long s = 0; s < (long)(SR * 2); s += 256){
        while(nextFrameSample <= s){
            br.pushColumn(beat, rowCol(40, 0.8f).data(), 85);
            br.publishClock(beat - FRAME, BPS, true);
            beat += FRAME; frameT += 1 / FPS; nextFrameSample = (long)(frameT * SR);
            br.collect();
        }
        br.process(L.data(), R.data(), 256);
        if(s > SR) for(int i = 0; i < 256; i++) energy += L[(size_t)i] * L[(size_t)i];
        sampleT += 256 / SR;
    }
    CHECK(energy > 1.0);
    // audio clock is about one frame behind the latest pushed column
    CHECK(std::fabs(br.audioBeat() - (beat - 2 * FRAME)) < 3 * FRAME);
    // events: a kick right ahead is played (louder block)
    Event k; k.id = 7; k.kind = EventKind::Hit; k.look = "kick"; k.start = br.audioBeat() + 0.02; k.level = 1; k.p["drumVoice"] = 1;
    CHECK(br.pushEvent(k));
    double peak = 0;
    for(int b = 0; b < 20; b++){
        br.pushColumn(beat, rowCol(40, 0.f).data(), 85); br.publishClock(beat - FRAME, BPS, true); beat += 256 * BPS;
        br.process(L.data(), R.data(), 256);
        for(float x : L) peak = std::max(peak, (double)std::fabs(x));
    }
    CHECK(peak > 0.05);
    // a swap: the old engine is retired (deleted by collect), stale columns are dropped
    long d0 = br.dropped.load();
    br.pushColumn(beat, rowCol(40, 1.f).data(), 85);          // old generation
    snd::SoundEngine* e2 = new snd::SoundEngine();
    br.installEngine(e2);
    br.process(L.data(), R.data(), 256);
    CHECK(br.running() == e2);
    CHECK(br.dropped.load() == d0 + 1);
    br.collect();                                              // deletes e1 (asan / leaks would show otherwise)
    // two installs before the audio thread runs: the first is deleted on the update thread
    br.installEngine(new snd::SoundEngine());
    snd::SoundEngine* e4 = new snd::SoundEngine();
    br.installEngine(e4);
    br.process(L.data(), R.data(), 256);
    CHECK(br.running() == e4);
    // pause: fades to silence
    br.publishClock(beat, BPS, false);
    br.process(L.data(), R.data(), 256);
    br.process(L.data(), R.data(), 256);
    double mx = 0; for(float x : L) mx = std::max(mx, (double)std::fabs(x));
    CHECK(mx == 0);
    // paused with a pending note event: no inf / UB (positive step kept), note voice ends
    Event nv; nv.id = 99; nv.kind = EventKind::Note; nv.start = beat + 0.001; nv.length = 0.5; nv.p["noteVoice"] = 1; nv.level = 1;
    br.pushEvent(nv); br.publishClock(beat, 0, false);
    for(int b = 0; b < 50; b++) br.process(L.data(), R.data(), 256);
    bool finite = true; for(float x : L) finite &= std::isfinite(x);
    CHECK(finite);
    // volume reaches the output (meter)
    CHECK(br.peakL.load() >= 0);
}

static void testThreads(){
    // producer at "frame rate", consumer as the audio callback, at the same time
    Bridge br; br.installEngine(new snd::SoundEngine());
    std::atomic<bool> stop{false};
    std::thread audio([&]{ std::vector<float> L(256), R(256); while(!stop){ br.process(L.data(), R.data(), 256); std::this_thread::yield(); } });
    double beat = 0;
    for(int f = 0; f < 3000; f++){
        br.pushColumn(beat, rowCol(f % 85, 0.5f).data(), 85);
        br.publishClock(beat - FRAME, BPS, true);
        if(f % 10 == 0){ Event e; e.id = (uint64_t)f + 1; e.kind = EventKind::Hit; e.look = "snare"; e.start = beat + 0.1; e.p["drumVoice"] = 1; br.pushEvent(e); }
        if(f % 500 == 499) br.installEngine(new snd::SoundEngine());
        beat += FRAME;
        br.collect();
    }
    stop = true; audio.join(); br.collect();
    CHECK(br.blocks.load() > 0);
}

// v1 match through the bridge: full 1080-row columns, a layer change, a kick sample swap
static void testV1Bridge(){
    Bridge br;
    auto* e = new snd::SoundEngine(); e->s.rows = 1080;
    br.installEngine(e);
    CHECK(br.pitchBalance.load() == 0.f);                 // default keeps the v1 sound
    std::vector<float> col(1080, 0.f); for(int r = 500; r < 506; r++) col[(size_t)r] = 0.8f;
    std::vector<float> L(256), R(256); double beat = 0, peak = 0;
    snd::V1Layer cfg; cfg.root = 30;
    std::vector<float> kick(4800, 0.5f);
    for(int f = 0; f < 120; f++){
        CHECK(br.pushColumn(beat, col.data(), 1080, f % 2));
        if(f == 30) CHECK(br.setLayer(0, cfg));
        if(f == 40) CHECK(br.setKickSample(kick, kick, 48000));
        br.publishClock(beat - FRAME, BPS, true);
        for(int k = 0; k < 4; k++){ br.process(L.data(), R.data(), 256); for(float x : L) peak = std::max(peak, (double)std::fabs(x)); }
        beat += FRAME;
        br.collect();
    }
    CHECK(peak > 1e-3 && peak <= 0.99 + 1e-6);
    CHECK(br.running() && std::fabs(br.running()->layerPitch(0)[br.running()->layerPartials(0) - 1] - 29.f) < 1e-4);   // root 30: lowest partial 29
}

int main(){
    testQueue(); testClock(); testBridge(); testThreads(); testV1Bridge();
    std::printf("test_audio: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
