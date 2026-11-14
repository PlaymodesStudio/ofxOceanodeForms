//
//  formsSoundModel.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The maths of Forms Box's sonification, without SuperCollider or openFrameworks (so it can be
//  tested headless). formsSound.h sends what this computes to the FormsBank512 / FormsKick /
//  FormsFX / FormsMaster SynthDefs (ofxOceanodeForms/synthdefs/formsBox.scd).
//
//  Each layer's header column (1 x H pixels) drives a bank of up to 512 sine partials, left and
//  right (the right one slightly detuned). The partials are laid out in pitch the way the renderer
//  draws it (see PartialLayout): a whole number per semitone, one exactly on each semitone above
//  the scene root, each the brightest max(R,G,B) of its rows (+ the clock's pitch bend),
//  optionally pulled to the nearest note of the scale (scale quantize). The 1920 patch mapped rows
//  linearly to root + (notegrid + 1) and patched the error with -1.33 st (still up to 0.6 st off);
//  its scale grid had the root's pitch class 3 hard-coded; its kick ducking ran at 120 BPM.
//

#pragma once

#include <vector>
#include <string>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <cstdio>
#include <cstring>
#include <nlohmann/json.hpp>

namespace forms {

constexpr int SOUND_PARTIALS = 512;
constexpr int SOUND_LAYERS = 4; // initial allocation for legacy patches; storage grows dynamically

// Per-layer settings (Sonification tab)
struct SoundLayer {
    bool on = true;
    float level = 1;          // linear gain of the layer's bank
    float pan = 0;            // -1 left .. 1 right (balance on top of the spread)
    float spread = 1;         // 0 mono .. 1 the full per-partial stereo field
    float reverb = 1;         // x the scene's reverb send (0 dry .. 2 double)
};

// Global settings (Sonification tab; the ones marked "scene" can also come from the scene)
struct SoundSettings {
    bool on = true;
    std::vector<SoundLayer> layers = std::vector<SoundLayer>(SOUND_LAYERS);
    float bankGain = 0.0216f;         // the bank sum's gain (16/1080 rescaled to 512 partials)
    float detune = 0.0746f;           // semitones, right bank
    float loudnessBlend = 0.2f;       // 0 flat .. 1 the full equal-loudness curve
    float lag = 1.0f / 30;            // amplitude smoothing in the synth (s)
    // Sharp attacks (2026-11-14, optional; off = the sonification as it was): the columns the header
    // crosses during a frame are read in 4 groups and sent each for its own instant (timestamped), the
    // bank draws a line between them, a partial out of silence rises in `attack` s from phase 0, and
    // `lag` is the release only. See ugens/FormsSparseBank (mode 1).
    bool sharp = false;
    float attack = 0.001f;            // s
    float spreadLow = 40, spreadHigh = 100;   // MIDI: below spreadLow the partials stay centred
    // kick ducking of the partials
    float duckBelow = 64;             // MIDI: partials below this are ducked
    float duckDepth = 0.89f;          // at the lowest partials
    float duckShape = 0.6f;           // exponent of the depth curve
    float duckBeats = 2;              // the envelope's length in beats (1 s at 120 BPM, as before)
    float duckCurve = -4;             // envelope curvature
    bool duckFullBand = true;         // duck the full image mix, including its reverb
    // kick
    bool kickOn = true;
    float kickThreshold = 0.5f;       // mean brightness of the strip that fires a kick
    float kickStrip = 100.0f / 3840;  // the strip: this fraction of the image height, at the bottom
    float kickMinGap = 0.08f;         // s between kicks
    float kickTune = -2;              // semitones on top of the scene's transpose (the 1920 rate)
    float kickGain = 1.5f;            // the PolyMixer level the kick had
    std::string kickFolder;           // empty: <Pitchclass folder>/../../Presets/... (set by the node)
    // FX and master
    float volume = 1;                 // master (the patch's Volume, 0..8)
    float limit = 0.98f;
    bool lrBalance = false;           // the scenes' L/R auto-balance (Macro_11)
};

// Equal-loudness gain of a partial (the patch's Equal_Loudness, blended)
inline float equalLoudness(float midi, float blend){
    double f = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    f = std::max(1.0, f);
    double lf = std::log(f);
    double el = 1.95136520 - 0.376892168 * lf + 0.0200933686 * lf * lf + 3.85649121e-6 * f - 3.19833134e-10 * f * f;
    return (float)((1 - blend) + blend * el);
}

// The pitch classes of the scale on the root (+ transpose), sorted
inline std::vector<int> scaleGrid(int root, int trans, const std::vector<float>& scale){
    std::vector<int> pcs;
    for(float s : scale){ int pc = ((int)std::lround(s) + root + trans) % 12; if(pc < 0) pc += 12; pcs.push_back(pc); }
    if(pcs.empty()) for(int i = 0; i < 12; i++) pcs.push_back(i);
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}

// The bar's chord root as a MIDI note: scene root + transpose + the scale step at the bar's degree
// (a degree past the scale's end goes up an octave; no scale: the root itself)
inline int chordRootMidi(int sceneRoot, int degree, int trans, const std::vector<float>& scale){
    int n = (int)scale.size();
    if(n == 0) return sceneRoot + trans;
    int oct = (int)std::floor(degree / (double)n);
    int idx = degree - oct * n;
    return sceneRoot + trans + (int)std::lround(scale[idx]) + 12 * oct;
}

inline float nearestInGrid(float p, const std::vector<int>& pcs){
    float best = p, bd = 1e9f;
    int oct = (int)std::floor(p / 12.0f);
    for(int o = oct - 1; o <= oct + 1; o++)
        for(int pc : pcs){ float n = o * 12.0f + pc, d = std::abs(n - p); if(d < bd){ bd = d; best = n; } }
    return best;
}

// Where the partials sit. The renderer draws pitch p (semitones above the scene root) at
//     y = H - p * H / notegrid - safey,   safey = H / notegrid in integer pixels (Render.pde)
// so the partials are laid out in pitch, not in rows: a whole number of partials per semitone,
// one exactly on every semitone the renderer can draw, each reading the rows of its own pitch
// band. A note drawn on the scale sounds at exactly its pitch (what Root Out / Scale Out say).
// Partial 0 is the highest; partials past `n` are unused (silent).
struct PartialLayout {
    int n = 0;                  // partials in use
    int perSemitone = 1;
    std::vector<float> rel;     // pitch above the root (semitones), SOUND_PARTIALS
    std::vector<int> rowA, rowB;   // the rows each partial reads: [rowA, rowB)
};
// h: the column's height (the layer buffer); imageH: the rendered image's height (safey is
// computed at that size, then scaled)
inline PartialLayout partialLayout(int h, int notegrid, int imageH = 0, int maxPartials = SOUND_PARTIALS){
    PartialLayout L;
    L.rel.assign(maxPartials, 0); L.rowA.assign(maxPartials, 0); L.rowB.assign(maxPartials, 0);
    if(h < 1) return L;
    int ng = std::max(1, notegrid);
    if(imageH <= 0) imageH = h;
    double noteh = (double)h / ng;                                    // rows per semitone
    double safe = (double)(imageH / ng) * h / (double)imageH;         // the renderer's integer safey, scaled
    double top = (h - safe) / noteh;                                   // the pitch at y = 0
    int q = std::max(1, (int)std::floor(maxPartials / (top + 2.0)));   // partials per semitone
    int n = std::min(maxPartials, (int)std::floor((top + 1.0) * q) + 1);   // from p = -1 up to the top
    L.n = n; L.perSemitone = q;
    for(int i = 0; i < n; i++) L.rel[i] = (float)(-1.0 + (double)(n - 1 - i) / q);   // k = n-1-i: 0 is the lowest
    // every row goes to the partial nearest its pitch (rows are ordered, so each partial gets a run)
    for(int i = 0; i < n; i++){ L.rowA[i] = h; L.rowB[i] = 0; }
    for(int r = 0; r < h; r++){
        double p = (h - safe - (r + 0.5)) / noteh;
        int k = std::max(0, std::min(n - 1, (int)std::lround((p + 1.0) * q)));
        int i = n - 1 - k;
        L.rowA[i] = std::min(L.rowA[i], r);
        L.rowB[i] = std::max(L.rowB[i], r + 1);
    }
    // a partial narrower than a row: the row at its pitch
    for(int i = 0; i < n; i++) if(L.rowB[i] <= L.rowA[i]){
        int r = std::max(0, std::min(h - 1, (int)std::floor(h - safe - L.rel[i] * noteh)));
        L.rowA[i] = r; L.rowB[i] = r + 1;
    }
    return L;
}

// MIDI pitch of each partial, without the pitch bend: root + its pitch above the root, pulled to
// the nearest scale note by scaleQuant (0..1). Unused partials: the root (they are silent).
inline std::vector<float> partialPitches(int root, const PartialLayout& L, int trans, const std::vector<float>& scale, float scaleQuant){
    std::vector<float> p(L.rel.size(), (float)root);
    auto grid = scaleGrid(root, trans, scale);
    float q = std::max(0.0f, std::min(1.0f, scaleQuant));
    for(int i = 0; i < L.n; i++){
        float raw = root + L.rel[i];
        p[i] = q > 0 ? raw + (nearestInGrid(raw, grid) - raw) * q : raw;
    }
    return p;
}

// Smooth 1-D value noise in [0, 1] (the patch's Noise node: a frozen field across the partials)
inline float valueNoise(float x, uint32_t seed){
    auto h = [seed](int32_t i){
        uint32_t v = (uint32_t)i * 374761393u + seed * 668265263u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return ((v ^ (v >> 16)) & 0xffffff) / float(0xffffff);
    };
    int32_t i = (int32_t)std::floor(x);
    float t = x - i;
    t = t * t * (3 - 2 * t);
    return h(i) + (h(i + 1) - h(i)) * t;
}

// Left / right gains of each partial: stereo field (highs spread, lows centred) x equal loudness
// x the layer's balance. pitch: the partials' MIDI pitches.
inline void partialGains(const std::vector<float>& pitch, int layer, const SoundSettings& s, std::vector<float>& gl, std::vector<float>& gr){
    int n = (int)pitch.size();
    gl.assign(n, 0); gr.assign(n, 0);
    const SoundLayer& L = s.layers[std::max(0, std::min((int)s.layers.size() - 1, layer))];
    float bl = std::min(1.0f, 1 - L.pan), br = std::min(1.0f, 1 + L.pan);
    for(int i = 0; i < n; i++){
        float field = valueNoise(i * 8.0f / n, 0x5eed + layer * 7919) * 2 - 1;          // -1..1
        float t = (pitch[i] - s.spreadLow) / std::max(1.0f, s.spreadHigh - s.spreadLow);
        t = std::max(0.0f, std::min(1.0f, t));
        float p = field * t * t * (3 - 2 * t) * L.spread;
        float el = equalLoudness(pitch[i], s.loudnessBlend);
        gl[i] = el * (1 + p) * 0.5f * bl;
        gr[i] = el * (1 - p) * 0.5f * br;
    }
}

// How much each partial is ducked by the kick (0 none .. duckDepth)
inline std::vector<float> duckWeights(const std::vector<float>& pitch, const SoundSettings& s){
    std::vector<float> w(pitch.size());
    float span = std::max(1.0f, s.duckBelow - 27);   // reaches full depth around MIDI 27 (the 1920 bottom)
    for(size_t i = 0; i < pitch.size(); i++){
        float t = (s.duckBelow - pitch[i]) / span;
        t = std::max(0.0f, std::min(1.0f, t));
        w[i] = s.duckDepth * std::pow(t, std::max(0.05f, s.duckShape));
    }
    return w;
}
inline std::vector<float> imageDuckWeights(const std::vector<float>& pitch,const SoundSettings& s){
    return s.duckFullBand?std::vector<float>(pitch.size(),0):duckWeights(pitch,s);
}
inline float duckEnvelope(double elapsed,double duration,float curve){
    if(elapsed<0 || duration<=0 || elapsed>=duration)return 0;
    double x=elapsed/duration;
    return (float)(std::fabs(curve)<1e-5?1-x:1-std::expm1(curve*x)/std::expm1(curve));
}

// Amplitudes from a column of RGBA pixels (row 0 = top), times `mul`: each partial the brightest
// max(R,G,B) of its rows. T: the pixels' channel type (16 bits live; 8 bits where a file gave them).
// Silence floor: a 16-bit level under half an 8-bit step (-54 dB) is 0, as it was when the buffers
// were 8 bits and rounded it away. Without it the faint ends of fades and trails (down to -96 dB,
// inaudible) keep partials running: the sparse bank only skips a partial whose target is exactly 0.
template<class T> inline void columnAmps(const T* rgba, int h, float mul, const PartialLayout& L, std::vector<float>& amps){
    const int silent = (int)std::numeric_limits<T>::max() / 510;   // 16 bits: 128; 8 bits: 0 (no floor)
    amps.assign(L.rel.size(), 0);
    if(!rgba || h < 1) return;
    for(int i = 0; i < L.n; i++){
        int m = 0;
        for(int r = L.rowA[i]; r < L.rowB[i] && r < h; r++){
            const T* px = rgba + r * 4;
            m = std::max(m, (int)std::max(px[0], std::max(px[1], px[2])));
        }
        amps[i] = m < silent ? 0.0f : m / (float)std::numeric_limits<T>::max() * mul;
    }
}

// The scenes' L/R auto-balance (Macro_11): a per-bar LFO (sine to square by hardness, or noise),
// switched on step by step with the step chances, eased in and out. Off: both gains 1.
struct LRBalance {
    float go = 0, eased = 0, noise = 0;
    int lastStep = -1;
    uint32_t rng = 0x1234567;
    float l = 1, r = 1;
    float rand01(){ rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xffffff) / float(0xffffff); }
    // phLock: the header's position in its image (0..1); bars: the image's bars; bpm; dt (s)
    void step(float phLock, double bars, float bpm, float dt, const nlohmann::json& perf, bool enabled){
        auto f = [&](const char* k, float d){ return perf.contains(k) && perf[k].is_number() ? perf[k].get<float>() : d; };
        float amount = enabled ? f("LRDryWet", 0) : 0;
        if(amount <= 0){ l = r = 1; eased = go = 0; return; }
        float th = f("LRTh", 0.25f), hard = f("LRHardness", 1), noiseAmt = f("LRNoiseAmt", 0);
        float stepBars = std::max(1.0f, f("LRStepBars", 8)), smoothMs = f("LRSmotthMs", 4000);
        std::vector<float> chances(8, 1);
        if(perf.contains("LRprobSteps[]") && perf["LRprobSteps[]"].is_array() && !perf["LRprobSteps[]"].empty()){
            chances.clear();
            for(auto& v : perf["LRprobSteps[]"]) chances.push_back(v.is_number() ? v.get<float>() : 1);
        }
        float pos = phLock * (float)std::max(1.0 / 16, bars);               // bars into the image
        float ph = pos * th; ph -= std::floor(ph);
        float s = std::sin(6.2831853f * (ph + 0.25f));
        float k = 1 - 0.95f * std::max(0.0f, std::min(1.0f, hard));   // 1 sine .. 0.05 almost square
        float osc = 0.5f + 0.5f * (s < 0 ? -1 : 1) * std::pow(std::abs(s), k);
        noise += dt * bpm / 60.0f / 4.0f * th / 4;             // Speed Th/4 per bar
        float nz = valueNoise(noise, 77);
        float p = osc + (nz - osc) * noiseAmt;
        float sp = pos / stepBars; sp -= std::floor(sp);
        int stepIdx = std::min(7, (int)(sp * 8));
        if(stepIdx != lastStep){ lastStep = stepIdx; go = rand01() < chances[stepIdx % chances.size()] ? 1.0f : 0.0f; }
        float tau = smoothMs / 1000.0f;
        eased = tau <= 0 ? go : eased + (go - eased) * (1 - std::exp(-dt / std::max(0.001f, tau / 3)));
        float wet = amount * eased;
        l = std::sqrt(std::max(0.0f, (1 - wet) + wet * p));
        r = std::sqrt(std::max(0.0f, (1 - wet) + wet * (1 - p)));
    }
};

// WAV channel count (RIFF fmt chunk); 0 if unreadable
inline int wavChannels(const std::string& path){
    FILE* f = std::fopen(path.c_str(), "rb");
    if(!f) return 0;
    unsigned char hdr[12];
    int ch = 0;
    if(std::fread(hdr, 1, 12, f) == 12 && !std::memcmp(hdr, "RIFF", 4) && !std::memcmp(hdr + 8, "WAVE", 4)){
        unsigned char c[8];
        while(std::fread(c, 1, 8, f) == 8){
            uint32_t len = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
            if(!std::memcmp(c, "fmt ", 4)){
                unsigned char fmt[4];
                if(std::fread(fmt, 1, 4, f) == 4) ch = fmt[2] | (fmt[3] << 8);
                break;
            }
            std::fseek(f, len + (len & 1), SEEK_CUR);
        }
    }
    std::fclose(f);
    return ch;
}
inline int audioChannels(const std::string& path){
    if(int channels=wavChannels(path)) return channels;
    FILE* file=std::fopen(path.c_str(),"rb");if(!file)return 0;
    unsigned char header[12];int channels=0;
    if(std::fread(header,1,12,file)==12 && !std::memcmp(header,"FORM",4) &&
       (!std::memcmp(header+8,"AIFF",4)||!std::memcmp(header+8,"AIFC",4))){
        unsigned char chunk[8];
        while(std::fread(chunk,1,8,file)==8){
            uint32_t size=((uint32_t)chunk[4]<<24)|((uint32_t)chunk[5]<<16)|((uint32_t)chunk[6]<<8)|chunk[7];
            if(!std::memcmp(chunk,"COMM",4)){
                unsigned char ch[2];if(std::fread(ch,1,2,file)==2)channels=(ch[0]<<8)|ch[1];break;
            }
            if(std::fseek(file,(long)size+(size&1),SEEK_CUR))break;
        }
    }
    std::fclose(file);return channels;
}

} // namespace forms
