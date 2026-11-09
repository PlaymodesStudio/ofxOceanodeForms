//
//  v2Audio.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The bridge from the update (draw) thread to the audio callback for the v2 SoundEngine (v2Sound.h).
//  - SpscQueue: lock-free single-producer / single-consumer ring (fixed capacity, no allocation).
//  - Bridge (plain C++): columns (with their beat), events near the playhead and clock messages
//    go to the audio thread through queues; the engine is built / prepared on the update thread
//    and handed over with an atomic pointer (the old one comes back through a retire queue and is
//    deleted on the update thread). The audio side never locks and never frees.
//  - Audio clock: follows the beat published every frame (playhead beat minus one frame),
//    advancing per sample, slewing small errors and snapping on big ones (seek).
//  - AudioOut (openFrameworks, unless V2AUDIO_NO_OF): an ofSoundStream (48 kHz, stereo, 256 frames)
//    calling Bridge::process from its callback.
//
//  Note: SoundEngine::pushColumn / pushEvents copy into its own containers (may allocate);
//  the bridge itself does not.
//

#pragma once

#include "v2Sound.h"
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>

namespace forms2 {
namespace audio {

// ---------------------------------------------------------------- lock-free SPSC ring
template <class T, size_t N>
class SpscQueue {
public:
    static_assert((N & (N - 1)) == 0, "N must be a power of two");
    bool push(const T& v){
        size_t h = head.load(std::memory_order_relaxed);
        if(h - tail.load(std::memory_order_acquire) >= N) return false;   // full
        buf[h & (N - 1)] = v;
        head.store(h + 1, std::memory_order_release);
        return true;
    }
    bool pop(T& out){
        size_t t = tail.load(std::memory_order_relaxed);
        if(t == head.load(std::memory_order_acquire)) return false;       // empty
        out = buf[t & (N - 1)];
        tail.store(t + 1, std::memory_order_release);
        return true;
    }
    size_t size() const { return head.load(std::memory_order_acquire) - tail.load(std::memory_order_acquire); }
private:
    std::array<T, N> buf{};
    std::atomic<size_t> head{0}, tail{0};
};

constexpr int kMaxRows = 2160;            // a full image column (v1 match reads every row)

struct ColumnMsg { int gen = 0; double beat = 0; int layer = 0; int n = 0; float rows[kMaxRows]; };
struct LayerMsg { int gen = 0; int layer = 0; snd::V1Layer cfg; };
struct KickBuf { std::vector<float> L, R; double sr = 48000; };
struct EventMsg { int gen = 0; Event* e = nullptr; };
struct ClockMsg { double beat = 0; double beatsPerSample = 0; bool playing = false; };

// ---------------------------------------------------------------- audio clock
// beat of the next block: follows the published beat (extrapolated by the samples since)
struct AudioClock {
    double beat = 0, target = 0, bps = 0, lastBps = 2.0 / 48000; long since = 0; bool playing = false, valid = false;
    double snapBeats = 0.25, slew = 0.05;
    void publish(const ClockMsg& m){
        target = m.beat; bps = m.beatsPerSample; playing = m.playing; since = 0;
        if(bps > 0) lastBps = bps;
        if(!valid){ beat = target; valid = true; }
    }
    // the beat at the start of this block (and the step per sample)
    double blockStart(){
        if(!playing) { beat = target; return beat; }
        double t = target + since * bps, err = t - beat;
        if(std::fabs(err) > snapBeats) beat = t; else beat += err * slew;
        return beat;
    }
    void advance(int n){ if(playing) beat += n * bps; since += n; }
};

// ---------------------------------------------------------------- bridge
class Bridge {
public:
    using Engine = snd::SoundEngine;
    double sampleRate = 48000; int blockSize = 256;

    ~Bridge(){ delete pending.exchange(nullptr); delete current; collect(); }

    // ---- update thread
    // a new engine (prepared here) replaces the running one; its messages use a new generation
    void installEngine(Engine* e){
        e->prepare(sampleRate, blockSize);
        int g = ++gen_;
        e->setPitchBalance(pitchBalance.load());
        pendingGen.store(g, std::memory_order_relaxed);
        Engine* old = pending.exchange(e, std::memory_order_acq_rel);
        delete old;                                   // never reached the audio thread
        appliedBalanceOnUpdate = pitchBalance.load();
    }
    // rows: row 0 = bottom; layer 0..3 (v1 match: one bank per layer)
    bool pushColumn(double beat, const float* rows, int n, int layer = 0){
        colMsg.gen = gen_; colMsg.beat = beat; colMsg.layer = layer; colMsg.n = std::min(n, kMaxRows);
        std::memcpy(colMsg.rows, rows, sizeof(float) * (size_t)colMsg.n);
        return columns.push(colMsg);
    }
    // v1 match: a layer's mapping (root, note grid, harmony, level, reverb send) changed
    bool setLayer(int layer, const snd::V1Layer& cfg){ return layers.push({gen_, layer, cfg}); }
    // kick sample for the running engine (deleted later on this thread)
    bool setKickSample(std::vector<float> L, std::vector<float> R, double sr){
        auto* k = new KickBuf{std::move(L), std::move(R), sr};
        if(!kicksIn.push(k)){ delete k; return false; }
        return true;
    }
    bool pushEvent(const Event& e){
        Event* p = new Event(e);
        if(!events.push({gen_, p})){ delete p; return false; }
        return true;
    }
    void publishClock(double beat, double beatsPerSample, bool playing){ clocks.push({beat, beatsPerSample, playing}); }
    void resetLoudness(){ loudReset.store(true); }
    // deletes what the audio thread is done with (call every frame)
    void collect(){
        Event* e; while(doneEvents.pop(e)) delete e;
        Engine* x; while(retired.pop(x)) delete x;
        KickBuf* k; while(kicksDone.pop(k)) delete k;
    }
    int generation() const { return gen_; }

    // controls (any thread)
    std::atomic<float> volume{1.f}, pitchBalance{0.f};     // pitch balance 0 = the v1 sound
    std::atomic<float> bend{0.f}, ampMul{1.f};            // v1 match: clock pitch bend, image level
    std::atomic<float> peakL{0}, peakR{0};             // last block's peaks (meter)
    std::atomic<long> blocks{0}, dropped{0};
    std::atomic<float> load{0};                       // last callback's time / its budget (0..1+)

    // ---- audio thread: n stereo frames into separate buffers
    void process(float* outL, float* outR, int n){
        if(Engine* p = pending.exchange(nullptr, std::memory_order_acq_rel)){
            if(current) retired.push(current);        // retire queue is large; full = leak, never block
            current = p; curGen = pendingGen.load(std::memory_order_relaxed);
            clock.valid = false;
        }
        ClockMsg cm; bool gotClock = false;
        while(clocks.pop(cm)) gotClock = true;
        if(gotClock) clock.publish(cm);
        // columns / events of the current generation; others are stale (before a swap)
        ColumnMsg* cmsg = &colScratch;
        while(columns.pop(*cmsg)){
            if(cmsg->gen != curGen || !current) { dropped++; continue; }
            if((int)colVec.size() != cmsg->n) colVec.resize((size_t)cmsg->n);   // sized once per engine
            std::copy(cmsg->rows, cmsg->rows + cmsg->n, colVec.begin());
            current->pushColumn(cmsg->beat, colVec, cmsg->layer);
        }
        LayerMsg lm;
        while(layers.pop(lm)) if(lm.gen == curGen && current) current->setLayer(lm.layer, lm.cfg);
        KickBuf* kb;
        while(kicksIn.pop(kb)){
            if(current) current->swapKickSample(kb->L, kb->R, kb->sr);   // the old buffers go back in kb (freed on the update thread)
            if(!kicksDone.push(kb)) {}
        }
        EventMsg em;
        while(events.pop(em)){
            if(em.gen == curGen && current){ evVec.resize(1); evVec[0] = *em.e; current->pushEvents(evVec); }
            if(!doneEvents.push(em.e)) {}             // full: leak rather than free here
        }
        if(!current || !clock.valid){ silence(outL, outR, n); return; }
        if(loudReset.exchange(false)) current->resetLoudness();
        float pb = pitchBalance.load(std::memory_order_relaxed);
        if(pb != appliedBalance){ current->setPitchBalance(pb); appliedBalance = pb; }
        current->setBend(bend.load(std::memory_order_relaxed));
        current->s.ampMul = ampMul.load(std::memory_order_relaxed);

        double b0 = clock.blockStart();
        float target = clock.playing ? volume.load(std::memory_order_relaxed) : 0.f;
        if(clock.playing || gain > 1e-4f){
            // always a positive step (the engine divides by it); paused, the beat does not move
            current->process(outL, outR, n, b0, clock.bps > 0 ? clock.bps : clock.lastBps);
            float pl = 0, pr = 0, step = (target - gain) / (float)n;
            for(int i = 0; i < n; i++){
                gain += step;
                outL[i] *= gain; outR[i] *= gain;
                pl = std::max(pl, std::fabs(outL[i])); pr = std::max(pr, std::fabs(outR[i]));
            }
            gain = target;
            peakL.store(pl); peakR.store(pr);
        } else { silence(outL, outR, n); }
        clock.advance(n);
        blocks++;
    }
    // interleaved stereo (ofSoundBuffer): uses fixed scratch buffers
    void processInterleaved(float* io, int n, int channels){
        auto t0 = std::chrono::steady_clock::now();
        int done = 0;
        while(done < n){
            int m = std::min(n - done, (int)scratchL.size());
            process(scratchL.data(), scratchR.data(), m);
            for(int i = 0; i < m; i++){
                float* f = io + (size_t)(done + i) * channels;
                f[0] = scratchL[(size_t)i];
                if(channels > 1) f[1] = scratchR[(size_t)i];
                for(int c = 2; c < channels; c++) f[c] = 0;
            }
            done += m;
        }
        double sec = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        load.store((float)(sec * sampleRate / std::max(1, n)));
    }
    double audioBeat() const { return clock.beat; }   // audio thread / tests
    Engine* running() const { return current; }       // audio thread / tests

private:
    SpscQueue<ColumnMsg, 64> columns;
    SpscQueue<LayerMsg, 16> layers;
    SpscQueue<KickBuf*, 16> kicksIn, kicksDone;
    ColumnMsg colMsg;                                 // update-thread scratch (large)
    SpscQueue<EventMsg, 1024> events;
    SpscQueue<Event*, 2048> doneEvents;
    SpscQueue<ClockMsg, 64> clocks;
    SpscQueue<Engine*, 64> retired;
    std::atomic<Engine*> pending{nullptr};
    std::atomic<int> pendingGen{0};
    std::atomic<bool> loudReset{false};
    int gen_ = 0;                                     // update thread
    float appliedBalanceOnUpdate = 0.f;
    // audio thread
    Engine* current = nullptr; int curGen = -1;
    AudioClock clock;
    float gain = 0, appliedBalance = -1;
    ColumnMsg colScratch;
    std::vector<float> colVec; std::vector<Event> evVec;
    std::array<float, 1024> scratchL{}, scratchR{};
    static void silence(float* l, float* r, int n){ std::fill(l, l + n, 0.f); std::fill(r, r + n, 0.f); }
};

} // namespace audio
} // namespace forms2

#ifndef V2AUDIO_NO_OF
#include "ofMain.h"
namespace forms2 {
namespace audio {

// ofSoundStream owned by the node; the callback runs the bridge
class AudioOut : public ofBaseSoundOutput {
public:
    Bridge bridge;
    std::string status;
    ~AudioOut(){ close(); }

    static std::vector<ofSoundDevice> outputDevices(){
        std::vector<ofSoundDevice> v;
        ofSoundStream s;
        for(auto& d : s.getDeviceList()) if(d.outputChannels >= 2) v.push_back(d);
        return v;
    }
    // deviceName empty = system default
    bool open(const std::string& deviceName){
        close();
        ofSoundStreamSettings st;
        st.sampleRate = 48000; st.bufferSize = 256; st.numOutputChannels = 2; st.numInputChannels = 0; st.numBuffers = 2;
        if(!deviceName.empty()){
            for(auto& d : outputDevices()) if(d.name == deviceName){ st.setOutDevice(d); break; }
        }
        st.setOutListener(this);
        bridge.sampleRate = 48000; bridge.blockSize = 256;
        bool ok = stream.setup(st);
        isOpen = ok;
        status = ok ? "running (48 kHz, 256)" + (deviceName.empty() ? std::string(" on the default device") : " on " + deviceName) : "could not open the output device";
        return ok;
    }
    void close(){ if(isOpen){ stream.stop(); stream.close(); isOpen = false; } }
    bool running() const { return isOpen; }

    void audioOut(ofSoundBuffer& buf) override {
        bridge.processInterleaved(buf.getBuffer().data(), (int)buf.getNumFrames(), (int)buf.getNumChannels());
    }
private:
    ofSoundStream stream;
    bool isOpen = false;
};

} // namespace audio
} // namespace forms2
#endif
