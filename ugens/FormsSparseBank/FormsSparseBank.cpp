//
//  FormsSparseBank.cpp — Forms Box's oscillator bank as one UGen (ESPECTRES 2027).
//
//  FormsSparseBank.ar(amps, pitch, gl, gr, duckw, lag, bend, detune, duck)  ->  [left, right]
//
//  What FormsBank512 did with 1024 SinOsc and a few thousand control-rate units, in one unit:
//      a_i = Lag(clip(sanitize(amps_i), 0, 1), lag) * (1 - duckw_i * clip(sanitize(duck), 0, 1))
//      L   = sum_i sin(2 pi f(pitch_i + bend) t + phase_i)           * a_i * gl_i
//      R   = sum_i sin(2 pi f(pitch_i + bend + detune) t + phase'_i) * a_i * gr_i
//  f = midicps, sanitized and clipped to 0..20000 Hz; every oscillator starts at a random phase;
//  the per-partial gain is ramped across each block (as an audio * control multiply is); the Lag
//  is SuperCollider's Lag.kr (b1 = exp(log(0.001) / (lag * control rate))).
//
//  Sparse: an oscillator whose gain is zero at both ends of the block is not run. An image column
//  is mostly black, so most of the bank is skipped most of the time; a skipped oscillator keeps
//  its phase where it was (nobody hears where it would have been). The ones that run are packed
//  into contiguous arrays and advanced together as rotating unit vectors (the compiler turns the
//  loop over oscillators into SIMD), so a fully lit bank is also several times cheaper than the
//  SinOsc graph.
//
//  Inputs: 0 n (scalar, the number of partials), 1 lag, 2 bend, 3 detune, 4 duck, then n amps,
//  n pitches, n left gains, n right gains, n duck weights (control rate).
//

#include "SC_PlugIn.h"
#include <algorithm>
#include <cmath>
#include <cstring>

static InterfaceTable* ft;

namespace {

inline float cleanf(float x){ return std::isfinite(x) ? x : 0.f; }
inline float clip01(float x){ return x < 0.f ? 0.f : (x > 1.f ? 1.f : x); }
const float kSilent = 1e-6f;   // a lagged amplitude under this, with a silent target, is zero (-120 dB)

}

struct FormsSparseBank : public Unit {
    int n;                 // partials
    float* mem;            // everything below, one allocation
    float *y, *lastPitch;                  // n: the lagged amplitude, the pitch the rotation was made for
    float *c, *s, *cw, *sw, *gPrev;        // 2n: oscillator state (left 0..n-1, right n..2n-1)
    float *C, *S, *CW, *SW, *A, *DA;       // 2n: the running oscillators, packed
    int* idx;                              // 2n: which oscillator each packed slot is
    unsigned char* fresh;                  // 2n: its rotation matches the current frequency
    float lastBend, lastDetune, lastLag, b1;
    bool first;
};

static void FormsSparseBank_next(FormsSparseBank* unit, int inNumSamples);

static void FormsSparseBank_Ctor(FormsSparseBank* unit){
    unit->mem = nullptr;
    int n = (int)IN0(0);
    if(n < 1 || (int)unit->mNumInputs < 5 + 5 * n){ n = 0; }
    unit->n = n;
    SETCALC(FormsSparseBank_next);
    if(n > 0){
        const size_t floats = (size_t)n * 2 + (size_t)n * 2 * 5 + (size_t)n * 2 * 6;
        const size_t bytes = floats * sizeof(float) + (size_t)n * 2 * sizeof(int) + (size_t)n * 2;
        unit->mem = (float*)RTAlloc(unit->mWorld, bytes);
        if(!unit->mem){ unit->n = 0; }
        else{
            memset(unit->mem, 0, bytes);
            float* p = unit->mem;
            unit->y = p; p += n; unit->lastPitch = p; p += n;
            unit->c = p; p += 2 * n; unit->s = p; p += 2 * n; unit->cw = p; p += 2 * n; unit->sw = p; p += 2 * n; unit->gPrev = p; p += 2 * n;
            unit->C = p; p += 2 * n; unit->S = p; p += 2 * n; unit->CW = p; p += 2 * n; unit->SW = p; p += 2 * n; unit->A = p; p += 2 * n; unit->DA = p; p += 2 * n;
            unit->idx = (int*)p; unit->fresh = (unsigned char*)(unit->idx + 2 * n);
            RGen& rgen = *unit->mParent->mRGen;
            for(int o = 0; o < 2 * n; o++){
                float ph = rgen.frand() * 6.2831853f;
                unit->c[o] = std::cos(ph); unit->s[o] = std::sin(ph);
            }
            for(int i = 0; i < n; i++) unit->y[i] = clip01(cleanf(IN0(5 + i)));   // Lag starts at its input
        }
    }
    unit->lastBend = unit->lastDetune = 0; unit->lastLag = -1; unit->b1 = 0; unit->first = true;
    ClearUnitOutputs(unit, 1);
}

static void FormsSparseBank_Dtor(FormsSparseBank* unit){
    if(unit->mem) RTFree(unit->mWorld, unit->mem);
}

// The packed oscillators, one block: out[i] = sum_k S[k] * A[k], then every one steps on.
// Sixteen at a time, their state in local arrays (registers) for the whole block.
static inline void runPacked(float* out, int numSamples, int from, int to,
                             float* C, float* S, const float* CW, const float* SW, float* A, const float* DA){
#pragma clang fp reassociate(on) contract(fast)
    enum { G = 16 };
    memset(out, 0, (size_t)numSamples * sizeof(float));
    for(int k0 = from; k0 < to; k0 += G){
        const int cnt = std::min((int)G, to - k0);
        float c[G], s[G], cw[G], sw[G], a[G], da[G];
        for(int j = 0; j < G; j++){
            const bool on = j < cnt;
            c[j] = on ? C[k0 + j] : 1.f; s[j] = on ? S[k0 + j] : 0.f;
            cw[j] = on ? CW[k0 + j] : 1.f; sw[j] = on ? SW[k0 + j] : 0.f;
            a[j] = on ? A[k0 + j] : 0.f; da[j] = on ? DA[k0 + j] : 0.f;
        }
        for(int i = 0; i < numSamples; i++){
            float acc = 0.f;
            for(int j = 0; j < G; j++){
                const float c0 = c[j], s0 = s[j];
                acc += s0 * a[j];
                a[j] += da[j];
                c[j] = c0 * cw[j] - s0 * sw[j];
                s[j] = s0 * cw[j] + c0 * sw[j];
            }
            out[i] += acc;
        }
        for(int j = 0; j < cnt; j++){ C[k0 + j] = c[j]; S[k0 + j] = s[j]; }
    }
}

static void FormsSparseBank_next(FormsSparseBank* unit, int inNumSamples){
    float* outL = OUT(0);
    float* outR = OUT(1);
    const int n = unit->n;
    if(n <= 0){ ClearUnitOutputs(unit, inNumSamples); return; }

    const float lag = cleanf(IN0(1));
    const float bend = cleanf(IN0(2)), detune = cleanf(IN0(3));
    const float duck = clip01(cleanf(IN0(4)));
    if(lag != unit->lastLag){
        unit->lastLag = lag;
        unit->b1 = lag <= 0.f ? 0.f : (float)std::exp(std::log(0.001) / ((double)lag * unit->mRate->mBufRate));
    }
    const float b1 = unit->b1;
    const bool allStale = unit->first || bend != unit->lastBend || detune != unit->lastDetune;
    unit->lastBend = bend; unit->lastDetune = detune; unit->first = false;
    if(allStale) memset(unit->fresh, 0, (size_t)2 * n);

    float** in = unit->mInBuf;
    float** amps = in + 5; float** pitch = amps + n; float** gl = pitch + n; float** gr = gl + n; float** dw = gr + n;
    const double radPerHz = 6.283185307179586 / SAMPLERATE;
    const float slopeK = 1.f / (float)inNumSamples;

    float *y = unit->y, *lastPitch = unit->lastPitch, *gPrev = unit->gPrev;
    float *C = unit->C, *S = unit->S, *CW = unit->CW, *SW = unit->SW, *A = unit->A, *DA = unit->DA;
    int* idx = unit->idx;
    unsigned char* fresh = unit->fresh;

    int m = 0, mLeft = 0;
    for(int side = 0; side < 2; side++){
        float** gains = side == 0 ? gl : gr;
        for(int i = 0; i < n; i++){
            float g;
            if(side == 0){
                // the lagged amplitude (once per partial), then the duck
                const float x = clip01(cleanf(*amps[i]));
                float v = x + b1 * (y[i] - x);
                if(x < kSilent && v < kSilent) v = 0.f;
                y[i] = v;
                const float pv = *pitch[i];
                if(pv != lastPitch[i]){ lastPitch[i] = pv; fresh[i] = 0; fresh[n + i] = 0; }
            }
            g = y[i] * (1.f - cleanf(*dw[i]) * duck) * cleanf(*gains[i]);
            const int o = side * n + i;
            const float g0 = gPrev[o];
            gPrev[o] = g;
            if(g0 == 0.f && g == 0.f) continue;
            if(!fresh[o]){
                double f = 440.0 * std::exp2(((double)lastPitch[i] + bend + (side ? detune : 0.f) - 69.0) / 12.0);
                if(!std::isfinite(f)) f = 0;
                f = std::min(20000.0, std::max(0.0, f));
                const double w = f * radPerHz;
                unit->cw[o] = (float)std::cos(w); unit->sw[o] = (float)std::sin(w);
                fresh[o] = 1;
            }
            idx[m] = o;
            C[m] = unit->c[o]; S[m] = unit->s[o]; CW[m] = unit->cw[o]; SW[m] = unit->sw[o];
            A[m] = g0; DA[m] = (g - g0) * slopeK;
            m++;
        }
        if(side == 0) mLeft = m;
    }

    runPacked(outL, inNumSamples, 0, mLeft, C, S, CW, SW, A, DA);
    runPacked(outR, inNumSamples, mLeft, m, C, S, CW, SW, A, DA);

    // back to their places, pulled onto the unit circle (the rotation drifts a little)
    for(int k = 0; k < m; k++){
        const float cc = C[k], ss = S[k];
        const float fix = 1.5f - 0.5f * (cc * cc + ss * ss);
        unit->c[idx[k]] = cc * fix; unit->s[idx[k]] = ss * fix;
    }
}

PluginLoad(FormsSparseBank){
    ft = inTable;
    DefineDtorUnit(FormsSparseBank);
}
