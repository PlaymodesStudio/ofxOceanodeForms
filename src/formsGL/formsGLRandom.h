//
//  formsGLRandom.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  The ESPECTRES_2027 sketch's random streams, bit for bit, so that a request draws the same
//  onsets and pitches here as in Processing (the sketch stays as it is):
//    JavaRandom   java.util.Random (48-bit LCG, nextFloat / nextInt / nextGaussian ...)
//    PRandom      Processing's PApplet: random(), random(lo, hi), randomGaussian(), noise(),
//                 randomSeed(), noiseSeed(), noiseDetail()
//    SketchPRNG   the sketch's own PRNG class (PRNG.pde: an LCG and its Perlin noise)
//  and the Processing math the port needs with Java's float semantics (pjRound = Math.round).
//
//  Exactness needs the compiler not to fuse multiply-adds (Java never does): compile with
//  -ffp-contract=off, or keep the pragma below.
//

#pragma once

#include <cmath>
#include <cstdint>
#include <vector>

#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace formsgl {

constexpr float P_PI = 3.14159265358979323846f;   // (float) Math.PI
constexpr float P_TWO_PI = P_PI * 2.0f;
constexpr float P_HALF_PI = P_PI / 2.0f;

// ---- java.util.Random
class JavaRandom {
public:
    explicit JavaRandom(int64_t seed = 0){ setSeed(seed); }
    void setSeed(int64_t seed){
        s = (uint64_t)(seed ^ 0x5DEECE66DLL) & ((1ULL << 48) - 1);
        haveNextNextGaussian = false;
    }
    int32_t next(int bits){
        s = (s * 0x5DEECE66DULL + 0xBULL) & ((1ULL << 48) - 1);
        return (int32_t)(int64_t)(s >> (48 - bits));
    }
    int32_t nextInt(){ return next(32); }
    int32_t nextInt(int32_t bound){
        if(bound <= 0) return 0;
        if((bound & -bound) == bound) return (int32_t)(((int64_t)bound * (int64_t)next(31)) >> 31);
        int32_t bits, val;
        do {
            bits = next(31);
            val = bits % bound;
        } while(bits - val + (bound - 1) < 0);
        return val;
    }
    int64_t nextLong(){ return ((int64_t)next(32) << 32) + next(32); }
    bool nextBoolean(){ return next(1) != 0; }
    float nextFloat(){ return next(24) / (float)(1 << 24); }
    double nextDouble(){ return (double)(((int64_t)next(26) << 27) + next(27)) * 0x1.0p-53; }
    double nextGaussian(){
        if(haveNextNextGaussian){ haveNextNextGaussian = false; return nextNextGaussian; }
        double v1, v2, ss;
        do {
            v1 = 2 * nextDouble() - 1;
            v2 = 2 * nextDouble() - 1;
            ss = v1 * v1 + v2 * v2;
        } while(ss >= 1 || ss == 0);
        double multiplier = std::sqrt(-2 * std::log(ss) / ss);   // StrictMath in Java: may differ in the last bit
        nextNextGaussian = v2 * multiplier;
        haveNextNextGaussian = true;
        return v1 * multiplier;
    }
private:
    uint64_t s = 0;
    double nextNextGaussian = 0;
    bool haveNextNextGaussian = false;
};

// ---- Processing math with Java semantics
inline int pjRound(float x){ return (int)std::floor(x + 0.5f); }            // Math.round(float)
inline float pjPow(float a, float b){ return (float)std::pow((double)a, (double)b); }
inline float pjCos(float a){ return (float)std::cos((double)a); }
inline float pjSin(float a){ return (float)std::sin((double)a); }
inline float pjSqrt(float a){ return (float)std::sqrt((double)a); }
inline float pjMap(float v, float is, float ie, float os, float oe){ return os + (oe - os) * ((v - is) / (ie - is)); }
inline float pjLerp(float a, float b, float t){ return a + (b - a) * t; }
inline float pjConstrain(float v, float lo, float hi){ return v < lo ? lo : v > hi ? hi : v; }
inline int pjConstrain(int v, int lo, int hi){ return v < lo ? lo : v > hi ? hi : v; }

// ---- PApplet's random and noise
class PRandom {
public:
    // randomSeed(seed): a fresh Random's setSeed (also forgets a cached Gaussian)
    void randomSeed(int64_t seed){ r.setSeed(seed); }
    float random(float high){
        if(high == 0 || high != high) return 0;
        float v;
        do { v = r.nextFloat() * high; } while(v == high);
        return v;
    }
    float random(float low, float high){
        if(low >= high) return low;
        float diff = high - low, v;
        do { v = random(diff) + low; } while(v == high);
        return v;
    }
    float randomGaussian(){ return (float)r.nextGaussian(); }
    JavaRandom& javaRandom(){ return r; }

    void noiseSeed(int64_t seed){ perlinRandom.setSeed(seed); perlin.clear(); }
    void noiseDetail(int lod){ if(lod > 0) octaves = lod; }
    void noiseDetail(int lod, float falloff){ if(lod > 0) octaves = lod; if(falloff > 0) ampFalloff = falloff; }
    float noise(float x){ return noise(x, 0, 0); }
    float noise(float x, float y){ return noise(x, y, 0); }
    float noise(float x, float y, float z){
        if(perlin.empty()){
            perlin.resize(PERLIN_SIZE + 1);
            for(int i = 0; i < PERLIN_SIZE + 1; i++) perlin[i] = perlinRandom.nextFloat();
            if(cosTable.empty()){
                // PGraphics.cosLUT: SINCOS_PRECISION 0.5, SINCOS_LENGTH 720
                cosTable.resize(SINCOS_LENGTH);
                const float kDegToRad = P_PI / 180.0f;
                for(int i = 0; i < SINCOS_LENGTH; i++) cosTable[i] = (float)std::cos((double)(i * kDegToRad * 0.5f));
            }
        }
        if(x < 0) x = -x;
        if(y < 0) y = -y;
        if(z < 0) z = -z;
        int xi = (int)x, yi = (int)y, zi = (int)z;
        float xf = x - xi, yf = y - yi, zf = z - zi;
        float rxf, ryf, r_ = 0, ampl = 0.5f, n1, n2, n3;
        for(int i = 0; i < octaves; i++){
            int of = xi + (yi << YWRAPB) + (zi << ZWRAPB);
            rxf = fsc(xf); ryf = fsc(yf);
            n1  = perlin[of & PERLIN_SIZE];
            n1 += rxf * (perlin[(of + 1) & PERLIN_SIZE] - n1);
            n2  = perlin[(of + YWRAP) & PERLIN_SIZE];
            n2 += rxf * (perlin[(of + YWRAP + 1) & PERLIN_SIZE] - n2);
            n1 += ryf * (n2 - n1);
            of += ZWRAP;
            n2  = perlin[of & PERLIN_SIZE];
            n2 += rxf * (perlin[(of + 1) & PERLIN_SIZE] - n2);
            n3  = perlin[(of + YWRAP) & PERLIN_SIZE];
            n3 += rxf * (perlin[(of + YWRAP + 1) & PERLIN_SIZE] - n3);
            n2 += ryf * (n3 - n2);
            n1 += fsc(zf) * (n2 - n1);
            r_ += n1 * ampl;
            ampl *= ampFalloff;
            xi <<= 1; xf *= 2;
            yi <<= 1; yf *= 2;
            zi <<= 1; zf *= 2;
            if(xf >= 1.0f){ xi++; xf--; }
            if(yf >= 1.0f){ yi++; yf--; }
            if(zf >= 1.0f){ zi++; zf--; }
        }
        return r_;
    }
private:
    static constexpr int YWRAPB = 4, YWRAP = 1 << YWRAPB, ZWRAPB = 8, ZWRAP = 1 << ZWRAPB, PERLIN_SIZE = 4095;
    static constexpr int SINCOS_LENGTH = 720, PERLIN_PI = SINCOS_LENGTH / 2;
    JavaRandom r, perlinRandom;
    std::vector<float> perlin, cosTable;
    int octaves = 4;
    float ampFalloff = 0.5f;
    float fsc(float i){ return 0.5f * (1.0f - cosTable[(int)(i * PERLIN_PI) % SINCOS_LENGTH]); }
};

// ---- the sketch's PRNG (PRNG.pde)
class SketchPRNG {
public:
    explicit SketchPRNG(int64_t seed = 0){ randomSeed(seed); initPerlin(); }
    int64_t nextLong(){ seed = (seed * a + c) & m32; return seed; }
    int32_t nextInt(){ return (int32_t)(nextLong() % INT32_MAX); }
    int32_t nextInt(int maxInt){ return nextInt() % maxInt; }
    void randomSeed(int64_t s){ seed = s % INT32_MAX; }
    float random(){ return random(0, 1); }
    float random(float max){ return random(0, max); }
    float random(float min, float max){ return pjMap((float)nextInt(), 0, (float)INT32_MAX, min, max); }
    float perlin(float x, float y){
        int i = (int)x & 255, j = (int)y & 255;
        int gTL = p[p[p[i] + j]] & 15, gTR = p[p[p[incr(i)] + j]] & 15;
        int gBL = p[p[p[i] + incr(j)]] & 15, gBR = p[p[p[incr(i)] + incr(j)]] & 15;
        float u = x - (int)x, v = y - (int)y;
        float tl = dot(gTL, u, v), tr = dot(gTR, u - 1, v), bl = dot(gBL, u, v - 1), br = dot(gBR, u - 1, v - 1);
        float top = lerpV(tl, tr, u), bottom = lerpV(bl, br, u);
        return pjMap(lerpV(top, bottom, v), -perlinBound, perlinBound, 0, 1);
    }
    float mixPerlin(float x, float y, int nbOFreq, float freqFactor, float ampFactor){
        float result = 0, freq = 1, amp = 1;
        const float maxValue = (1 - pjPow(ampFactor, (float)nbOFreq)) / (1 - ampFactor);
        for(int i = 0; i < nbOFreq; i++){ result += perlin(x * freq, y * freq) * amp; amp *= ampFactor; freq *= freqFactor; }
        return result / maxValue;
    }
    // the Perlin tables (for the GPU path, formsGLGpu.h)
    const int* perm() const { return p; }
    float gradX(int i) const { return gx[i]; }
    float gradY(int i) const { return gy[i]; }
    float bound() const { return perlinBound; }
private:
    int64_t seed = 0;
    static constexpr int64_t a = 1664525, c = 1013904223, m32 = 0xFFFFFFFFLL;
    int p[512] = {};
    float gx[16] = {}, gy[16] = {};
    // 1 / sqrt(2) in Java float: (float)(1.0 / Math.sqrt(2)) through Processing's sqrt (float)
    float perlinBound = 1.0f / pjSqrt(2.0f);
    void initPerlin(){
        float stepSize = P_TWO_PI / 16;
        for(int i = 0; i < 16; i++){
            float ang = i * stepSize;
            gx[i] = (float)std::cos((double)ang);   // PVector.fromAngle: (float) Math.cos
            gy[i] = (float)std::sin((double)ang);
        }
        int A[256], B[256];
        for(int i = 0; i < 256; i++){ A[i] = i; B[i] = i; }
        shuffle(A); shuffle(B);
        for(int i = 0; i < 256; i++){ p[i] = A[i]; p[i + 256] = B[i]; }
    }
    void shuffle(int* ar){
        for(int i = 255; i > 0; i--){ int idx = nextInt(i + 1); int t = ar[idx]; ar[idx] = ar[i]; ar[i] = t; }
    }
    static int incr(int v){ return v > 254 ? 0 : v + 1; }
    float dot(int g, float x, float y) const { return gx[g] * x + gy[g] * y; }
    static float fade(float t){ return t * t * t * (t * (t * 6 - 15) + 10); }
    static float lerpV(float a_, float b_, float t){ return a_ + fade(t) * (b_ - a_); }
};

} // namespace formsgl
