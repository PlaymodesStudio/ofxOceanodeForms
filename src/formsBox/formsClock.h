//
//  formsClock.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Transport (clock, BPM easing, speed, image requests, session timer) and Header (play-head
//  position, auto sweep, beat repeat, header jumps, strobe gate, tremolo, opacity) of the
//  ESPECTRES patch, as plain C++ stepped once per frame. They replace Phasor_1, Macro_5..10 and
//  the top-level wiring around them; formulas follow the spec of the 1920/2027 patch. Per-frame
//  smoothers keep their coefficients (frame-rate dependent, as in the patch). Timers that ran on
//  phasors fixed at 120 BPM use seconds.
//

#pragma once

#include <cmath>
#include <functional>
#include <random>
#include <vector>
#include <nlohmann/json.hpp>

namespace forms {

using json = nlohmann::json;

inline float fracf(float x){ return x - std::floor(x); }
inline float clampf(float x, float a, float b){ return x < a ? a : (x > b ? b : x); }
inline float mapClamp(float x, float i0, float i1, float o0, float o1){
    if(i1 == i0) return x < i0 ? o0 : o1;
    return clampf(o0 + (x - i0) / (i1 - i0) * (o1 - o0), std::min(o0, o1), std::max(o0, o1));
}
inline float smoothInterp(float a, float b, float p){ return a + (b - a) * (0.5f * p + 1.5f * p * p - p * p * p); }
inline float customPow(float v, float k){
    float k1 = 2 * k * 0.99999f, k2 = k1 / (1 - 0.999999f * k);
    return v * (k2 + 1) / (k2 * std::fabs(v) + 1);
}
inline float biPow(float v, float k){ float u = customPow(2 * v - 1, k); return (u + 1) / 2; }

// Performance value of the image under the header (from its scene), with a default
inline float perfF(const json& p, const char* k, float d){
    if(!p.contains(k)) return d;
    const json& v = p[k];
    if(v.is_number()) return v.get<float>();
    if(v.is_array() && !v.empty() && v[0].is_number()) return v[0].get<float>();
    return d;
}
inline std::vector<float> perfV(const json& p, const char* k, std::vector<float> d){
    if(!p.contains(k)) return d;
    const json& v = p[k];
    if(v.is_array()){ std::vector<float> o; for(auto& x : v) if(x.is_number()) o.push_back(x.get<float>()); return o.empty() ? d : o; }
    if(v.is_number()) return {v.get<float>()};
    return d;
}

// ================================================================ Transport

class Transport {
public:
    // inputs
    float speed = 1;            // "Speed" (custom GUI ±24)
    bool pause = false;
    bool autoGo = true;         // user switch; session automation cannot override it
    bool analogMode = false;    // pitch follows speed (tape)
    float bpmAdd = 0;           // -1..1 → ±999
    bool sequence = true;       // SEQUENCE toggle
    float maxMinutes = 11;
    bool limitSession = true;          // off: the session never ends by itself (Max Minutes is ignored, no fade)
    float seqFade = 0;          // 0: master gain 1, 1: follow the session fade curve
    // from the image under the header (may be a fraction of a bar)
    double bars = 8;
    float targetBpm = 120;
    json perf = json::object();

    // outputs
    float phase = 0, bpm = 120, speedOut = 0, pitchBend = 0, fade = 1, seqMinutes = 0;
    bool autoPng = true;
    int goCount = 0, restartCount = 0;  // incremented on each GO / setlist restart (the node fires voids)

    std::function<float(float, float)> noise2 = [](float x, float){ return 0.5f + 0.5f * std::sin(x * 6.2831853f); };
    std::mt19937 rng {std::random_device{}()};

    void newPng(double t){ newPngAt = t; ramping = true; rampAutomatic = false; }   // manual GO 200 ms later

    void step(double t, float dt){
        if(dt <= 0) return;
        // --- BPM: ease to the header image's BPM over 4 s (Value_Easer_1 on a 4 s phasor)
        if(targetBpm != easeTarget){ easeStart = easedBpm; easeTarget = targetBpm; easeT0 = t; }
        float p = (float)((t - easeT0) / 4.0);
        easedBpm = p >= 1 ? easeTarget : smoothInterp(easeStart, easeTarget, clampf(p, 0, 1));
        bpm = easedBpm + mapClamp(bpmAdd, -1, 1, -999, 999);

        // --- speed modulation from the scene (Macro_8 phasorSpeedOut): noise × mod × eased chance
        float n = std::pow(clampf(noise2((float)t * 0.2f, 0), 0, 1), 2.4f);
        float sp = mapClamp(n, 0.1f, 0.9f, perfF(perf, "PhasorMinSpeed", -0.1135f), perfF(perf, "PhasorMaxSpeed", 0.5075f));
        sp *= perfF(perf, "phasorSpeedMod", 0);
        // Chance_3 every 6 s, eased over 3 s (BiPow 0.597)
        double c6 = std::floor(t / 6.0);
        if(c6 != chanceCycle){
            chanceCycle = c6;
            float u = std::uniform_real_distribution<float>(0, 1)(rng);
            float v = u < perfF(perf, "phasorModChance", 0) ? 1.0f : 0.0f;
            if(v != chanceTarget){ chanceStart = chanceEased; chanceTarget = v; chanceT0 = t; }
        }
        float pe = (float)((t - chanceT0) / 3.0);
        chanceEased = pe >= 1 ? chanceTarget : smoothInterp(chanceStart, chanceTarget, biPow(clampf(pe, 0, 1), 0.597f));
        float phasorSpeed = sp * chanceEased;

        // --- Macro_7: smooth 0.9, pause, tape pitch
        float in = phasorSpeed + speed;
        smoothedSpeed = first ? in : 0.9f * smoothedSpeed + 0.1f * in;
        first = false;
        speedOut = smoothedSpeed * (pause ? 0.0f : 1.0f);
        float a = std::fabs(smoothedSpeed);
        pitchBend = (analogMode && a > 1e-6f) ? 12.0f * std::log2(a) : 0.0f;

        // --- the main phasor: one cycle per image (bars × 4 beats at speed 1)
        float rate = bpm / 60.0f * speedOut * 0.25f / (float)std::max(1.0 / 16, bars);
        phase += rate * dt;
        if(phase >= 1 || phase < 0){
            phase = fracf(phase);
            if(autoPng) automaticPng(t);
        }
        if(!autoGo && rampAutomatic) ramping = false;
        if(!autoGo) delayedNewPng = false;
        if(ramping && t - newPngAt >= 0.2){ ramping = false; goCount++; }
        if(delayedNewPng && t >= delayedNewPngAt){ delayedNewPng = false; automaticPng(t); }

        // --- session sequence (spec §2.5). AUTOPNG has two writers: the SEQUENCE toggle (every
        // change of it) and "minutes < max" (every change of that).
        if(!started){ started = true; counterT0 = t; lastSequence = sequence; }
        if(sequence != lastSequence){ lastSequence = sequence; setAuto(sequence, t); }
        seqMinutes = (float)((t - counterT0) / 60.0);
        bool shouldRun = !limitSession || seqMinutes < maxMinutes;
        if(shouldRun != lastShouldRun){ lastShouldRun = shouldRun; setAuto(shouldRun, t); }
        float x = (limitSession && maxMinutes > 0) ? seqMinutes / maxMinutes : 0;
        bool over = x > 1;
        if(over != restartSnap){
            // reStartSnap: past the end → one more image and SEQUENCE off; back below → SEQUENCE on
            restartSnap = over;
            if(over) automaticPng(t);
            sequenceRequest = over ? 0 : 1;
        }
        float curve = limitSession ? fadeCurve(x) : 1.0f;   // no session: no fade in or out
        fade = 1.0f * (1 - clampf(seqFade, 0, 1)) + curve * clampf(seqFade, 0, 1);
    }

    // -1 nothing, 0/1: the node sets its SEQUENCE parameter (the patch wrote SEQUENCE_ON)
    int takeSequenceRequest(){ int r = sequenceRequest; sequenceRequest = -1; return r; }

private:
    float easedBpm = 120, easeStart = 120, easeTarget = -1;
    double easeT0 = 0;
    double chanceCycle = -1, chanceT0 = 0;
    float chanceTarget = 0, chanceStart = 0, chanceEased = 0;
    float smoothedSpeed = 0;
    bool first = true;
    double newPngAt = 0, delayedNewPngAt = 0, counterT0 = 0;
    bool ramping = false, delayedNewPng = false, rampAutomatic = false;
    bool started = false, lastSequence = true, lastShouldRun = true, restartSnap = false;
    int sequenceRequest = -1;

    void automaticPng(double t){
        if(!autoGo) return;
        // An explicit New PNG request keeps priority over automatic wraps.
        if(ramping && !rampAutomatic) return;
        newPng(t);
        rampAutomatic = true;
    }

    // AUTOPNG edges: rising → timer reset + setlist restart; falling → setlist restart and one
    // more image 1 s later
    void setAuto(bool on, double t){
        if(on == autoPng) return;
        autoPng = on;
        restartCount++;
        if(on){ counterT0 = t; lastShouldRun = true; }
        else { delayedNewPng = true; delayedNewPngAt = t + 1.0; }
    }

    // Curve_1: (0,0) (0.021108,0) (0.051011,1) (0.976253,1) (1,0), linear; beyond 1 → 0
    static float fadeCurve(float x){
        if(x <= 0.021108f) return 0;
        if(x < 0.051011f) return (x - 0.021108f) / (0.051011f - 0.021108f);
        if(x <= 0.976253f) return 1;
        if(x < 1) return 1 - (x - 0.976253f) / (1 - 0.976253f);
        return 0;
    }
};

// ================================================================ Header

class Header {
public:
    std::function<double(float phase,float headerPos)> beatAt;
    // inputs
    float width = 1920;
    float phase = 0;            // Transport phase
    float bpm = 120;            // Transport BPM (eased scene BPM)
    double bars = 8;
    float header = 0.218182f;   // "Header" slider 0..1
    float headerPlus = 0;       // "Header+" slider 0..1
    float smooth = 0.0001f;     // "Smooth" knob (Smoother_1)
    bool autoHeader = false;
    bool tremoloOn = false;     // the scenes' tremolo on the opacity (off: Opacity = beat-repeat fade x strobe gate)
    float headerMin = 3.0f / 7, headerMax = 4.0f / 7;   // fractions of the width (MultiSlider grid)
    json perf = json::object();

    // outputs
    float headerPos = 0;        // px (float)
    int headerPx = 0;           // trunc(headerPos)
    float headerNorm = 0, phLock = 0, phOff = 0, opacity = 1;
    int actualBar = 0;
    float repeatPx = 0;
    float repeatOpacity = 1, gate = 1, tremolo = 1;

    std::function<float(float, float)> noise2 = [](float x, float){ return 0.5f + 0.5f * std::sin(x * 6.2831853f); };
    std::mt19937 rng {std::random_device{}()};

    void step(double t, float dt){
        if(dt <= 0) return;
        float W = width;
        if(W <= 0) return;
        // The smoothing state is in pixels, but a resize must keep its musical
        // position. Otherwise 1080p -> 4K initially halves the normalized header.
        if(!firstFrame && lastWidth > 0 && W != lastWidth) hs *= W / lastWidth;
        lastWidth = W;
        // --- manual header + random jumps (Macro_8 HeaderPosOut)
        float hdrIn = clampf(header + headerPlus * (1 - header), 0, 1);
        // Phasor_3: global BPM, period Div beats; Div random in [1, headerMaxDiv] per cycle
        jumpPhase += dt * bpm / 60.0f / std::max(1.0f, jumpDiv);
        if(jumpPhase >= 1){
            jumpPhase = fracf(jumpPhase);
            jumpDiv = 1 + uni() * (std::max(1.0f, perfF(perf, "headerMaxDiv", 8)) - 1);
            jumpTarget = uni();
            jumpChance = uni() < perfF(perf, "HeaderModChance", 0) ? 1.0f : 0.0f;
        }
        jumpSw = 0.9113f * jumpSw + (1 - 0.9113f) * jumpChance;
        float sw3 = hdrIn * (1 - jumpSw) + jumpTarget * jumpSw;
        float s2 = mapClamp(clampf(noise2((float)t * 0.1f, 3), 0, 1), 0, 1, 0.93f, 0.99f);
        headerPosOut = firstFrame ? sw3 : s2 * headerPosOut + (1 - s2) * sw3;
        float target = mapClamp(headerPosOut, 0, 1, 0, W);
        hs = firstFrame ? target : smooth * hs + (1 - smooth) * target;

        float minPx=headerMin*W,maxPx=headerMax*W;
        float stretch=(maxPx-minPx)/W;
        float ph10=stretch>1e-6f?fracf(phase/stretch):0;
        float sweep=minPx+(maxPx-minPx)*ph10;
        float baseHeader=std::fmod(hs+(autoHeader?sweep:0.f),W);
        float beats=(float)std::max(1.0/16,bars)*4.f;
        auto beatOf=[&](float x){return beatAt?beatAt(phase,x):(double)(phase+x/W)*beats;};
        double baseBeat=beatOf(baseHeader);
        // --- beat repeat: advance from the musical beat, never an independent BPM timer
        float ph9 = fracf(2 * fracf(phase + hs / W));
        if(firstFrame || ph9<lastPh9){
            auto mults=perfV(perf,"repeatMults",{0.5f,1,0.66f,1.5f,2,0.25f});
            beatLen=std::max(.0001f,mults[std::min((size_t)(uni()*mults.size()),mults.size()-1)]*.5f);
        }
        bool afterStart = ph9 >= perfF(perf, "repeatBarStart", 0.75f);
        if(afterStart != lastAfterStart){
            lastAfterStart = afterStart;
            bool on = afterStart && uni() < perfF(perf, "repeatChance", 0);
            if(on && !repeatOn)repeatStartBeat=std::floor((baseBeat+1e-7)/beatLen)*beatLen;
            repeatOn = on;
        }
        lastPh9 = ph9;
        float stepPx=W/beats*beatLen;
        if(repeatOn && beatLen > 0){
            double cycles=std::max(0.,(baseBeat-repeatStartBeat)/beatLen);
            double whole=std::floor(cycles+1e-7);
            repeatCount=(int)whole;repeatPhase=(float)std::max(0.,cycles-whole);
        }
        int numReps = std::max(1, (int)perfF(perf, "repeatNum", 4));
        int k = repeatCount % numReps;
        repeatPx = repeatOn ? k * stepPx : 0;
        repeatOpacity = repeatOn ? (repeatPhase < perfF(perf, "repeatPW", 0.75f) ? 1.0f : 0.0f) : 1.0f;

        // --- auto sweep (Macro_10): follows a column from headerMin to headerMax, then jumps back
        // Don't reflect a backwards repeat into the future when it crosses the left edge.
        int available=stepPx>0?(int)std::floor(baseHeader/stepPx):0;
        if(repeatOn && k>available)repeatPx=(repeatCount%std::max(1,std::min(numReps,available+1)))*stepPx;
        headerPos = baseHeader-repeatPx;
        headerPx = (int)headerPos;
        headerNorm = clampf(headerPos / W, 0, 1);
        phOff = headerPos / W;
        phLock = std::fmod(phase + phOff, 1.0f);
        actualBar = std::min(std::max(1, (int)std::ceil(bars - 1e-9)) - 1, (int)(phLock * std::max(1.0 / 16, bars)));

        // --- strobe gate: in the second half of every 2-bar span, with gateChance
        double heardBeat=beatOf(headerPos);
        double window=std::floor(heardBeat/8.0);
        float g=(float)(heardBeat/8.0-window);
        bool win = g >= 0.5f;
        if(win != lastWin || (win && window!=lastGateWindow)){
            lastWin = win;
            lastGateWindow=window;
            gateOn = win && uni() < perfF(perf, "gateChance", 0);
            if(win){
                auto gm = perfV(perf, "gateMults", {4});
                gateMult = gm[std::min((size_t)(uni() * gm.size()), gm.size() - 1)];
            }
        }
        double cycles=(heardBeat-(window*8+4))*gateMult;
        gatePhase=(float)(cycles-std::floor(cycles));
        gate = gateOn ? (gatePhase < 0.25f ? 1.0f : 0.0f) : 1.0f;

        // --- tremolo (Macro_8 opacityOut)
        float n2 = clampf(noise2((float)t * 0.1f, 3), 0, 1);
        float tremSpeed = mapClamp(n2 * n2, 0, 1, 1, perfF(perf, "tremoloMaxSpeed", 16));
        tremPhase = fracf(tremPhase + dt * bpm / 60.0f * tremSpeed / 4.0f);
        float envPh = envPhase + dt * bpm / 60.0f / 16.0f;
        if(envPh >= 1){ envPh = fracf(envPh); envGate = uni() < perfF(perf, "tremoloChance", 0); envStageT = 0; }
        envPhase = envPh;
        envStageT += dt * bpm / 60.0f / 16.0f;   // in Phasor_2 cycles
        float env;
        if(envGate){
            if(lastEnvGate == false){ envStageT = 0; releaseFrom = envValue; }
            float pa = envStageT / 0.3333f;
            env = pa < 1 ? smoothInterp(0, 1, customPow(pa, -0.2537f)) : 1;
        }else{
            if(lastEnvGate) { envStageT = 0; releaseFrom = envValue; }
            float pr = envStageT / 0.3333f;
            env = pr < 1 ? smoothInterp(releaseFrom, 0, customPow(pr, 0.3134f)) : 0;
        }
        lastEnvGate = envGate;
        envValue = env;
        envSmoothed = 0.9403f * envSmoothed + (1 - 0.9403f) * env;
        float F = envSmoothed * (1 - perfF(perf, "tremoloFloor", 0.3134f));
        tremolo = 1 - F * (1 - std::cos(6.2831853f * tremPhase)) / 2;
        float globalOp = tremoloOn ? mapClamp(tremolo, 0, 1, 0.0001f, 1) : 1.0f;
        globalSmoothed = firstFrame ? globalOp : 0.7388f * globalSmoothed + (1 - 0.7388f) * globalOp;

        opacity = repeatOpacity * globalSmoothed * gate;
        firstFrame = false;
    }

private:
    float headerPosOut = 0, hs = 0, lastWidth = 0;
    float jumpPhase = 0, jumpDiv = 1, jumpTarget = 0, jumpChance = 0, jumpSw = 0;
    bool lastAfterStart = false, repeatOn = false;
    float lastPh9 = 0, beatLen = 0.5f, repeatPhase = 0;
    int repeatCount = 0;
    double repeatStartBeat=0,lastGateWindow=0;
    bool lastWin = false, gateOn = false;
    float gateMult = 4, gatePhase = 0;
    float tremPhase = 0, envPhase = 0, envStageT = 0, envValue = 0, releaseFrom = 0, envSmoothed = 0, globalSmoothed = 1;
    bool envGate = false, lastEnvGate = false;
    bool firstFrame = true;

    float uni(){ return std::uniform_real_distribution<float>(0, 1)(rng); }
};

} // namespace forms
