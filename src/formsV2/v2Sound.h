// ESPECTRES v2 sound engine core (ROADMAP 5.7). Headless DSP, plain C++17.
// Columns of the image (brightness per row, row 0 = bottom) arrive at video rate with their
// playhead beat; they are interpolated per sample, turned into partials by an oscillator bank or
// an inverse-FFT additive engine (low register on oscillators), mixed with event-driven drum and
// note voices (transient path, never through the IFFT), weighted by pitch balance, loudness
// normalised and limited. Default: the v1 match (v1::, SoundSettings), v1 itself untouched.
#pragma once
#include "v2Core.h"
#include <complex>
#include <cstdio>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <set>

namespace forms2 {
namespace snd {

constexpr double kTwoPi = 6.283185307179586;
using cplx = std::complex<double>;

// ---------------------------------------------------------------- small helpers
struct Rng {  // xorshift, deterministic noise
    uint64_t s;
    explicit Rng(uint64_t seed = 1) : s(seed ? seed : 0x9E3779B97F4A7C15ull) {}
    double next() { s ^= s << 13; s ^= s >> 7; s ^= s << 17; return (double)(s >> 11) / 4503599627370496.0 * 2.0 - 1.0; }
};

struct Biquad {  // RBJ biquad
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum Type { LP, HP, BP };
    void set(Type t, double f, double q, double sr) {
        f = std::clamp(f, 10.0, sr * 0.45);
        double w = kTwoPi * f / sr, c = std::cos(w), al = std::sin(w) / (2 * q), a0 = 1 + al;
        if (t == LP) { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }
        else if (t == HP) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; }
        else { b0 = al; b1 = 0; b2 = -al; }
        a1 = -2 * c; a2 = 1 - al;
        b0 /= a0; b1 /= a0; b2 /= a0; a1 /= a0; a2 /= a0;
    }
    double tick(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// iterative radix-2 FFT (inverse includes 1/N)
inline void fft(std::vector<cplx>& a, bool inverse) {
    size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; i++) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        double ang = kTwoPi / (double)len * (inverse ? 1 : -1);
        cplx wl(std::cos(ang), std::sin(ang));
        for (size_t i = 0; i < n; i += len) {
            cplx w(1);
            for (size_t j = 0; j < len / 2; j++) {
                cplx u = a[i + j], v = a[i + j + len / 2] * w;
                a[i + j] = u + v; a[i + j + len / 2] = u - v; w *= wl;
            }
        }
    }
    if (inverse) for (auto& x : a) x /= (double)n;
}

// A-weighting in dB (inverse of an equal-loudness curve, close to 40 phon)
inline double aWeightDb(double f) {
    double f2 = f * f;
    double ra = (12194.0 * 12194.0 * f2 * f2) /
                ((f2 + 20.6 * 20.6) * std::sqrt((f2 + 107.7 * 107.7) * (f2 + 737.9 * 737.9)) * (f2 + 12194.0 * 12194.0));
    return 20 * std::log10(std::max(ra, 1e-12)) + 2.0;
}
// pitch balance gain: 0 = flat, 1 = full compensation (boost lows, slight cut of the 2-5 kHz bump)
inline double pitchBalanceGain(double f, double balance) {
    double a = std::clamp(aWeightDb(f), -30.0, 3.0);
    return std::pow(10.0, -balance * a / 20.0);
}

// ---------------------------------------------------------------- additive engines
// Both get one amplitude per partial per sub-block and ramp to it; same interface.
struct Additive {
    virtual ~Additive() = default;
    virtual void prepare(double sr, const std::vector<double>& freqs) = 0;
    virtual void render(const std::vector<float>& amps, float* out, int n) = 0;  // adds into out
    virtual int latency() const = 0;  // samples by which the amplitudes are delayed
    virtual void reset() = 0;
};

// (a) phase-continuous sine bank. mask selects the partials it owns (used by the hybrid).
struct OscBank : Additive {
    double sr = 48000, smoothMs = 0;
    std::vector<double> w, ph, cur;
    std::vector<char> mask;
    void prepare(double s, const std::vector<double>& freqs) override {
        sr = s; size_t n = freqs.size();
        w.resize(n); ph.assign(n, 0); cur.assign(n, 0);
        if (mask.size() != n) mask.assign(n, 1);
        for (size_t i = 0; i < n; i++) { w[i] = kTwoPi * freqs[i] / sr; if (freqs[i] <= 0 || freqs[i] > sr * 0.47) mask[i] = 0; }
    }
    void render(const std::vector<float>& amps, float* out, int n) override {
        double k = smoothMs > 0 ? 1 - std::exp(-n / (smoothMs * 0.001 * sr)) : 1;
        for (size_t r = 0; r < w.size(); r++) {
            if (!mask[r]) continue;
            double a0 = cur[r], a1 = a0 + (amps[r] - a0) * k;
            cur[r] = a1;
            double p = ph[r], wr = w[r];
            if (a0 < 1e-7 && a1 < 1e-7) { p += wr * n; }
            else {
                double da = (a1 - a0) / n;
                for (int i = 0; i < n; i++) { a0 += da; out[i] += (float)(a0 * std::sin(p)); p += wr; }
            }
            ph[r] = std::fmod(p, kTwoPi);
        }
    }
    int latency() const override { return 0; }
    void reset() override { std::fill(ph.begin(), ph.end(), 0); std::fill(cur.begin(), cur.end(), 0); }
};

// (b) inverse-FFT additive: each partial is a Hann-windowed sinusoid at its exact (fractional)
// frequency, written as a precomputed spectral kernel (2K+1 bins of the window's exact DTFT),
// phase tracked across frames, overlap-added (frames crossfade). Partials below splitHz stay on
// an internal oscillator bank (the low-register hybrid).
struct IfftBank : Additive {
    int N = 512, hop = 128, K = 8;
    double splitHz = 300, sr = 48000;
    std::vector<double> fbin, w, ph;
    std::vector<int> kc;
    std::vector<std::vector<cplx>> kern;
    std::vector<char> high;
    std::vector<cplx> X;
    std::vector<double> ola;
    size_t rp = 0; long counter = 0;
    OscBank low;
    std::vector<float> lowAmps;

    static cplx dirichlet(double d, int N) {
        double s = std::sin(M_PI * d / N);
        double mag = std::abs(s) < 1e-12 ? (double)N * std::cos(M_PI * d) / std::cos(M_PI * d / N) : std::sin(M_PI * d) / s;
        double a = -M_PI * d * (N - 1) / N;
        return cplx(mag * std::cos(a), mag * std::sin(a));  // mag may be negative
    }
    cplx hannW(double d) const { return 0.5 * dirichlet(d, N) - 0.25 * dirichlet(d - 1, N) - 0.25 * dirichlet(d + 1, N); }

    void prepare(double s, const std::vector<double>& freqs) override {
        sr = s; size_t n = freqs.size();
        fbin.resize(n); w.resize(n); ph.assign(n, 0); kc.resize(n); kern.assign(n, {}); high.assign(n, 0);
        low.mask.assign(n, 0);
        for (size_t r = 0; r < n; r++) {
            double f = freqs[r];
            fbin[r] = f * N / sr; w[r] = kTwoPi * f / sr; kc[r] = (int)std::lround(fbin[r]);
            bool ok = f > 0 && fbin[r] < N / 2 - K - 1;
            if (!ok) continue;
            if (f < splitHz) { low.mask[r] = 1; continue; }
            high[r] = 1;
            for (int j = -K; j <= K; j++) kern[r].push_back(hannW((kc[r] + j) - fbin[r]));
        }
        low.prepare(sr, freqs);
        lowAmps.assign(n, 0);
        X.assign(N, 0); ola.assign(2 * N, 0); rp = 0; counter = 0;
    }
    void frame(const std::vector<float>& amps) {
        std::fill(X.begin(), X.end(), cplx(0));
        for (size_t r = 0; r < fbin.size(); r++) {
            if (!high[r]) continue;
            if (amps[r] > 1e-7) {
                cplx c = std::polar(0.5 * amps[r], ph[r]);
                for (int j = -K; j <= K; j++) {
                    int k = kc[r] + j;
                    cplx v = c * kern[r][j + K];
                    X[(size_t)((k % N + N) % N)] += v;
                    X[(size_t)(((-k) % N + N) % N)] += std::conj(v);
                }
            }
            ph[r] = std::fmod(ph[r] + w[r] * hop, kTwoPi);
        }
        fft(X, true);
        double scale = 2.0 * hop / N;  // Hann OLA sum = 0.5 N/hop
        for (int i = 0; i < N; i++) ola[(rp + i) % ola.size()] += X[i].real() * scale;
    }
    void render(const std::vector<float>& amps, float* out, int n) override {
        if (counter % hop == 0) frame(amps);  // sub-blocks divide the hop
        for (int i = 0; i < n; i++) { out[i] += (float)ola[rp]; ola[rp] = 0; rp = (rp + 1) % ola.size(); }
        counter += n;
        low.render(amps, out, n);
    }
    int latency() const override { return N / 2; }
    void reset() override { std::fill(ph.begin(), ph.end(), 0); std::fill(ola.begin(), ola.end(), 0); rp = 0; counter = 0; low.reset(); }
};

// ---------------------------------------------------------------- drum voices (transient path)
enum class DrumType { Kick, Snare, Hat, Tick, Tom, Taiko, Zap };
enum class DrumMode { Off = 0, Mix = 1, Sidechain = 2 };

inline DrumType drumTypeOf(const std::string& look) {
    auto has = [&](const char* s) { return look.find(s) != std::string::npos; };
    if (has("snare")) return DrumType::Snare;
    if (has("hat")) return DrumType::Hat;
    if (has("tick") || has("speckle") || has("dot")) return DrumType::Tick;
    if (has("tom")) return DrumType::Tom;
    if (has("taiko")) return DrumType::Taiko;
    if (has("zap")) return DrumType::Zap;
    return DrumType::Kick;
}

struct DrumParams {
    DrumType type = DrumType::Kick;
    double level = 0.8, fStart = 160, fEnd = 48, decay = 0.45, click = 0.3, noise = 0, drive = 0.1;
    double scAmount = 0.6, scAttackMs = 2, scReleaseMs = 120, pan = 0;
    DrumMode mode = DrumMode::Off;
};

// defaults per type, then the event's drum* keys (v2Schema drumVoice)
inline DrumParams drumParamsOf(const Event& e) {
    DrumParams d; d.type = drumTypeOf(e.look);
    switch (d.type) {
        case DrumType::Kick: break;
        case DrumType::Snare: d.fStart = 220; d.fEnd = 180; d.decay = 0.18; d.noise = 0.7; break;
        case DrumType::Hat: d.fStart = 6000; d.fEnd = 6000; d.decay = 0.06; d.noise = 0.7; d.click = 0.1; break;
        case DrumType::Tick: d.fStart = 3000; d.fEnd = 3000; d.decay = 0.015; d.noise = 0.3; break;
        case DrumType::Tom: d.fStart = 180; d.fEnd = 110; d.decay = 0.35; break;
        case DrumType::Taiko: d.fStart = 120; d.fEnd = 70; d.decay = 0.8; d.noise = 0.25; d.click = 0.5; break;
        case DrumType::Zap: d.fStart = 3000; d.fEnd = 80; d.decay = 0.2; d.click = 0; break;
    }
    d.level = e.param("drumLevel", d.level) * e.level;
    d.fStart = e.param("drumPitchStart", d.fStart); d.fEnd = e.param("drumPitchEnd", d.fEnd);
    d.decay = std::max(0.005, e.param("drumDecay", d.decay));
    d.click = e.param("drumClick", d.click); d.noise = e.param("drumNoise", d.noise); d.drive = e.param("drumDrive", d.drive);
    d.scAmount = e.param("scAmount", d.scAmount); d.scAttackMs = e.param("scAttack", d.scAttackMs); d.scReleaseMs = e.param("scRelease", d.scReleaseMs);
    d.pan = e.param("pan", 0);
    d.mode = (DrumMode)std::clamp((int)std::lround(e.param("drumVoice", 0)), 0, 2);
    return d;
}

struct DrumVoice {
    DrumParams p; double sr = 48000, t = 0, ph = 0, phm = 0, tauP = 0.035; Rng rng; Biquad nf; bool alive = true;
    DrumVoice(const DrumParams& d, double s, uint64_t seed) : p(d), sr(s), rng(seed) {
        switch (p.type) {
            case DrumType::Snare: tauP = 0.02; nf.set(Biquad::BP, 3000, 0.7, sr); break;
            case DrumType::Hat: tauP = 1; nf.set(Biquad::HP, 7000, 0.7, sr); break;
            case DrumType::Tick: tauP = 1; nf.set(Biquad::HP, 4000, 0.7, sr); break;
            case DrumType::Tom: tauP = 0.06; nf.set(Biquad::BP, 600, 0.8, sr); break;
            case DrumType::Taiko: tauP = 0.09; nf.set(Biquad::LP, 800, 0.7, sr); break;
            case DrumType::Zap: tauP = p.decay * 0.25; nf.set(Biquad::BP, 2000, 1, sr); break;
            default: tauP = 0.035; nf.set(Biquad::BP, 1500, 0.7, sr); break;
        }
    }
    double tick() {
        double att = std::min(1.0, t / 0.00025);                    // 0.25 ms linear attack
        double env = att * std::exp(-6.9 * t / p.decay);             // -60 dB at decay
        double f = p.fEnd + (p.fStart - p.fEnd) * std::exp(-t / tauP);
        double tone;
        if (p.type == DrumType::Hat || p.type == DrumType::Tick) {  // metallic FM
            phm += kTwoPi * f * 1.48 / sr;
            tone = std::sin(ph + 3.0 * std::sin(phm));
        } else tone = std::sin(ph);
        ph += kTwoPi * f / sr;
        double toneEnv = p.type == DrumType::Snare ? att * std::exp(-6.9 * t / (p.decay * 0.6)) : env;
        double n = rng.next();
        double y = (1 - p.noise) * tone * toneEnv + p.noise * nf.tick(n) * 2.0 * env
                 + p.click * att * std::exp(-t / 0.001) * (0.5 + 0.5 * n);
        if (p.drive > 0) { double g = 1 + p.drive * 8; y = std::tanh(y * g) / std::tanh(g); }
        t += 1 / sr;
        if (t > p.decay * 1.5 + 0.01) alive = false;
        return y * p.level;
    }
};

// simple per-event note voice (hybrid note events: exact pitch, sharp attack, own envelope)
struct NoteVoice {
    double sr, f, ph = 0, t = 0, dur, att, rel, level, pan; bool alive = true;
    double tick() {
        double e = std::min(1.0, t / att);
        if (t > dur) e *= std::exp(-6.9 * (t - dur) / rel);
        if (t > dur + rel) alive = false;
        double y = level * e * std::sin(ph);
        ph += kTwoPi * f / sr; t += 1 / sr;
        return y;
    }
};

// ---------------------------------------------------------------- v1 match (ROADMAP 5.15)
// v1's sonification (formsSoundModel.h + FormsBank512 / FormsKick / FormsFX / FormsMaster in
// synthdefs/formsBox.scd), ported so v2 sounds like v1 by default. The maths below is a copy of
// formsSoundModel.h (same float / double types: tests/v2/test_sound_vs_v1.cpp checks they agree).
namespace v1 {
constexpr int kPartials = 512;
constexpr int kLayers = 4;

inline float equalLoudness(float midi, float blend) {
    double f = 440.0 * std::pow(2.0, (midi - 69.0) / 12.0);
    f = std::max(1.0, f);
    double lf = std::log(f);
    double el = 1.95136520 - 0.376892168 * lf + 0.0200933686 * lf * lf + 3.85649121e-6 * f - 3.19833134e-10 * f * f;
    return (float)((1 - blend) + blend * el);
}
inline float valueNoise(float x, uint32_t seed) {
    auto h = [seed](int32_t i) {
        uint32_t v = (uint32_t)i * 374761393u + seed * 668265263u;
        v = (v ^ (v >> 13)) * 1274126177u;
        return ((v ^ (v >> 16)) & 0xffffff) / float(0xffffff);
    };
    int32_t i = (int32_t)std::floor(x);
    float t = x - i;
    t = t * t * (3 - 2 * t);
    return h(i) + (h(i + 1) - h(i)) * t;
}
inline std::vector<int> scaleGrid(int root, int trans, const std::vector<float>& scale) {
    std::vector<int> pcs;
    for (float s : scale) { int pc = ((int)std::lround(s) + root + trans) % 12; if (pc < 0) pc += 12; pcs.push_back(pc); }
    if (pcs.empty()) for (int i = 0; i < 12; i++) pcs.push_back(i);
    std::sort(pcs.begin(), pcs.end());
    pcs.erase(std::unique(pcs.begin(), pcs.end()), pcs.end());
    return pcs;
}
inline float nearestInGrid(float p, const std::vector<int>& pcs) {
    float best = p, bd = 1e9f;
    int oct = (int)std::floor(p / 12.0f);
    for (int o = oct - 1; o <= oct + 1; o++)
        for (int pc : pcs) { float n = o * 12.0f + pc, d = std::abs(n - p); if (d < bd) { bd = d; best = n; } }
    return best;
}

// partials laid out in pitch (a whole number per semitone, partial 0 the highest), each reading
// the rows [rowA, rowB) of the column (row 0 = top), as forms::partialLayout
struct Layout { int n = 0, perSemitone = 1; std::vector<float> rel; std::vector<int> rowA, rowB; };
inline Layout layout(int h, int notegrid, int imageH = 0, int maxPartials = kPartials) {
    Layout L;
    L.rel.assign(maxPartials, 0); L.rowA.assign(maxPartials, 0); L.rowB.assign(maxPartials, 0);
    if (h < 1) return L;
    int ng = std::max(1, notegrid);
    if (imageH <= 0) imageH = h;
    double noteh = (double)h / ng;
    double safe = (double)(imageH / ng) * h / (double)imageH;
    double top = (h - safe) / noteh;
    int q = std::max(1, (int)std::floor(maxPartials / (top + 2.0)));
    int n = std::min(maxPartials, (int)std::floor((top + 1.0) * q) + 1);
    L.n = n; L.perSemitone = q;
    for (int i = 0; i < n; i++) L.rel[i] = (float)(-1.0 + (double)(n - 1 - i) / q);
    for (int i = 0; i < n; i++) { L.rowA[i] = h; L.rowB[i] = 0; }
    for (int r = 0; r < h; r++) {
        double p = (h - safe - (r + 0.5)) / noteh;
        int k = std::max(0, std::min(n - 1, (int)std::lround((p + 1.0) * q)));
        int i = n - 1 - k;
        L.rowA[i] = std::min(L.rowA[i], r);
        L.rowB[i] = std::max(L.rowB[i], r + 1);
    }
    for (int i = 0; i < n; i++) if (L.rowB[i] <= L.rowA[i]) {
        int r = std::max(0, std::min(h - 1, (int)std::floor(h - safe - L.rel[i] * noteh)));
        L.rowA[i] = r; L.rowB[i] = r + 1;
    }
    return L;
}
inline std::vector<float> pitches(int root, const Layout& L, int trans, const std::vector<float>& scale, float scaleQuant) {
    std::vector<float> p(L.rel.size(), (float)root);
    auto grid = scaleGrid(root, trans, scale);
    float q = std::max(0.0f, std::min(1.0f, scaleQuant));
    for (int i = 0; i < L.n; i++) {
        float raw = root + L.rel[i];
        p[i] = q > 0 ? raw + (nearestInGrid(raw, grid) - raw) * q : raw;
    }
    return p;
}
// stereo field x equal loudness x balance (forms::partialGains)
inline void gains(const std::vector<float>& pitch, int layer, float pan, float spread, float loudnessBlend,
                  float spreadLow, float spreadHigh, std::vector<float>& gl, std::vector<float>& gr) {
    int n = (int)pitch.size();
    gl.assign(n, 0); gr.assign(n, 0);
    float bl = std::min(1.0f, 1 - pan), br = std::min(1.0f, 1 + pan);
    for (int i = 0; i < n; i++) {
        float field = valueNoise(i * 8.0f / n, 0x5eed + std::max(0, std::min(kLayers - 1, layer)) * 7919) * 2 - 1;
        float t = (pitch[i] - spreadLow) / std::max(1.0f, spreadHigh - spreadLow);
        t = std::max(0.0f, std::min(1.0f, t));
        float p = field * t * t * (3 - 2 * t) * spread;
        float el = equalLoudness(pitch[i], loudnessBlend);
        gl[i] = el * (1 + p) * 0.5f * bl;
        gr[i] = el * (1 - p) * 0.5f * br;
    }
}
inline std::vector<float> duckWeights(const std::vector<float>& pitch, float duckBelow, float duckDepth, float duckShape) {
    std::vector<float> w(pitch.size());
    float span = std::max(1.0f, duckBelow - 27);
    for (size_t i = 0; i < pitch.size(); i++) {
        float t = (duckBelow - pitch[i]) / span;
        t = std::max(0.0f, std::min(1.0f, t));
        w[i] = duckDepth * std::pow(t, std::max(0.05f, duckShape));
    }
    return w;
}
// SuperCollider Env segment 1 -> 0 with curvature c, x in 0..1
inline double envDown(double x, double c) {
    if (x >= 1) return 0;
    if (x <= 0) return 1;
    if (std::abs(c) < 1e-3) return 1 - x;
    return 1 - (1 - std::exp(c * x)) / (1 - std::exp(c));
}

// SC-style allpass (AllpassL: fixed fractional delay, feedback from the 60 dB decay time)
struct Allpass {
    std::vector<double> buf; size_t w = 0; double d = 1, fb = 0;
    void init(double delayS, double decayS, double sr) {
        d = delayS * sr; buf.assign((size_t)d + 3, 0); w = 0;
        fb = std::exp(std::log(0.001) * delayS / decayS);
    }
    double tick(double x) {
        size_t n = buf.size();
        double rp = (double)w + n - d; size_t i0 = (size_t)rp % n, i1 = (i0 + 1) % n; double f = rp - std::floor(rp);
        double v = buf[i0] + (buf[i1] - buf[i0]) * f;
        double dwr = x + fb * v;
        buf[w] = dwr; w = (w + 1) % n;
        return v - fb * dwr;
    }
};
// RBJ shelves / peak as SC's BLowShelf / BHiShelf (rs 1) and BPeakEQ (rq)
struct Eq {
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    enum T { Low, High, Peak, LP, HP };
    void set(T t, double f, double q, double dB, double sr) {
        f = std::clamp(f, 10.0, sr * 0.45);
        double A = std::pow(10.0, dB / 40), w = kTwoPi * f / sr, c = std::cos(w), s = std::sin(w);
        double cb0, cb1, cb2, ca0, ca1, ca2;
        if (t == Low || t == High) {
            double al = s / 2 * std::sqrt((A + 1 / A) * (1.0 / q - 1) + 2), sq = 2 * std::sqrt(A) * al;
            if (t == Low) { cb0 = A * ((A + 1) - (A - 1) * c + sq); cb1 = 2 * A * ((A - 1) - (A + 1) * c); cb2 = A * ((A + 1) - (A - 1) * c - sq);
                            ca0 = (A + 1) + (A - 1) * c + sq; ca1 = -2 * ((A - 1) + (A + 1) * c); ca2 = (A + 1) + (A - 1) * c - sq; }
            else { cb0 = A * ((A + 1) + (A - 1) * c + sq); cb1 = -2 * A * ((A - 1) + (A + 1) * c); cb2 = A * ((A + 1) + (A - 1) * c - sq);
                   ca0 = (A + 1) - (A - 1) * c + sq; ca1 = 2 * ((A - 1) - (A + 1) * c); ca2 = (A + 1) - (A - 1) * c - sq; }
        } else if (t == Peak) {
            double al = s * q / 2;   // q = rq here
            cb0 = 1 + al * A; cb1 = -2 * c; cb2 = 1 - al * A; ca0 = 1 + al / A; ca1 = -2 * c; ca2 = 1 - al / A;
        } else {
            double al = s / (2 * q);
            if (t == LP) { cb0 = (1 - c) / 2; cb1 = 1 - c; cb2 = (1 - c) / 2; } else { cb0 = (1 + c) / 2; cb1 = -(1 + c); cb2 = (1 + c) / 2; }
            ca0 = 1 + al; ca1 = -2 * c; ca2 = 1 - al;
        }
        b0 = cb0 / ca0; b1 = cb1 / ca0; b2 = cb2 / ca0; a1 = ca1 / ca0; a2 = ca2 / ca0;
    }
    double tick(double x) { double y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

// FormsFX: LeakDC(dry) + LongReverb(wet) -> eq13 -> (compressor: off, comp 0) -> high shelf -> RLPF -> gain
struct Fx {
    double sr = 48000, gain = 2;
    double leakX[2] = {0, 0}, leakY[2] = {0, 0}, leakWX[2] = {0, 0}, leakWY[2] = {0, 0};
    std::vector<Allpass> ap[2];
    Eq revLp[2], revHp[2], eq[2][5], shelf[2], lpf[2];
    void prepare(double s, double revTime, uint64_t seed = 0xF0F0) {
        sr = s; Rng rng(seed);
        for (int c = 0; c < 2; c++) {
            ap[c].assign(32, {});
            leakX[c] = leakY[c] = leakWX[c] = leakWY[c] = 0;
        }
        for (int k = 0; k < 32; k++) for (int c = 0; c < 2; c++) ap[c][k].init(0.001 + 0.049 * (0.5 + 0.5 * rng.next()), revTime, sr);
        for (int c = 0; c < 2; c++) {
            revLp[c].set(Eq::LP, 440 * std::pow(2.0, (100 - 69) / 12.0), 0.70710678, 0, sr);
            revHp[c].set(Eq::HP, 440 * std::pow(2.0, (17 - 69) / 12.0), 0.70710678, 0, sr);
            eq[c][0].set(Eq::Low, 80, 1, 10, sr);
            eq[c][1].set(Eq::Peak, 270, 1, 3.69, sr);
            eq[c][2].set(Eq::Peak, 800, 1, 2.69, sr);
            eq[c][3].set(Eq::Peak, 2000, 1, 1.34, sr);
            eq[c][4].set(Eq::High, 7623, 1, 2.69, sr);
            shelf[c].set(Eq::High, 3328, 1, -6.72, sr);
            lpf[c].set(Eq::LP, std::min(20000.0, 440 * std::pow(2.0, (120.36 - 69) / 12.0)), 1.0, 0, sr);
        }
    }
    void process(double dry[2], double wet[2], double out[2]) {
        const double coef = 0.995;   // LeakDC default
        for (int c = 0; c < 2; c++) {
            double d = dry[c] - leakX[c] + coef * leakY[c]; leakX[c] = dry[c]; leakY[c] = d;
            double w = wet[c] - leakWX[c] + coef * leakWY[c]; leakWX[c] = wet[c]; leakWY[c] = w;
            for (auto& a : ap[c]) w = a.tick(w);
            w = revHp[c].tick(revLp[c].tick(w));
            double x = d + w;
            for (auto& q : eq[c]) x = q.tick(x);
            x = lpf[c].tick(shelf[c].tick(x));
            out[c] = x * gain;
        }
    }
};

// WAV reader (PCM 16 / 24 / 32, float 32), for the kick samples; false if unreadable
inline bool loadWav(const std::string& path, std::vector<float>& L, std::vector<float>& R, double& sr) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    unsigned char hdr[12]; bool ok = false; int ch = 0, bits = 0, fmt = 0;
    L.clear(); R.clear();
    if (std::fread(hdr, 1, 12, f) == 12 && !std::memcmp(hdr, "RIFF", 4) && !std::memcmp(hdr + 8, "WAVE", 4)) {
        unsigned char c[8];
        while (std::fread(c, 1, 8, f) == 8) {
            uint32_t len = c[4] | (c[5] << 8) | (c[6] << 16) | ((uint32_t)c[7] << 24);
            if (!std::memcmp(c, "fmt ", 4)) {
                std::vector<unsigned char> b(len); if (std::fread(b.data(), 1, len, f) != len) break;
                fmt = b[0] | (b[1] << 8); ch = b[2] | (b[3] << 8);
                sr = (double)(b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24)); bits = b[14] | (b[15] << 8);
                if (fmt == 0xFFFE && len >= 26) fmt = b[24] | (b[25] << 8);
                if (len & 1) std::fseek(f, 1, SEEK_CUR);
            } else if (!std::memcmp(c, "data", 4) && ch > 0 && bits > 0) {
                int bps = bits / 8; size_t frames = len / (size_t)(bps * ch);
                std::vector<unsigned char> b(len); size_t got = std::fread(b.data(), 1, len, f); frames = std::min(frames, got / (size_t)(bps * ch));
                L.resize(frames); R.resize(frames);
                for (size_t i = 0; i < frames; i++) for (int k = 0; k < std::min(ch, 2); k++) {
                    const unsigned char* p = b.data() + (i * ch + k) * bps; float v = 0;
                    if (fmt == 3 && bits == 32) std::memcpy(&v, p, 4);
                    else if (bits == 16) v = (int16_t)(p[0] | (p[1] << 8)) / 32768.f;
                    else if (bits == 24) v = (int32_t)(((uint32_t)p[0] << 8) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 24)) / 2147483648.f;
                    else if (bits == 32) v = (int32_t)(p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24)) / 2147483648.f;
                    (k == 0 ? L : R)[i] = v;
                }
                if (ch == 1) R = L;
                ok = frames > 0; break;
            } else std::fseek(f, len + (len & 1), SEEK_CUR);
        }
    }
    std::fclose(f);
    return ok;
}
}  // namespace v1

// ---------------------------------------------------------------- limiter (look-ahead, hard ceiling)
struct Limiter {
    double ceiling = 0.891, releaseMs = 80, lookMs = 1, sr = 48000, g = 1;
    int L = 48;
    std::deque<double> dl, dr, hold;
    std::deque<double> avg; double avgSum = 0;
    void prepare(double s) { sr = s; L = std::max(1, (int)(lookMs * 0.001 * sr)); dl.assign(L, 0); dr.assign(L, 0); hold.assign(L, 1); avg.assign(L, 1); avgSum = L; g = 1; }
    void process(float& l, float& r) {
        double pk = std::max(std::abs((double)l), std::abs((double)r));
        double need = pk > ceiling ? ceiling / pk : 1.0;
        hold.push_back(need); hold.pop_front();
        double m = *std::min_element(hold.begin(), hold.end());
        double rel = std::exp(-1.0 / (releaseMs * 0.001 * sr));
        g = std::min(m, rel * g + (1 - rel) * m);
        avg.push_back(g); avgSum += g - avg.front(); avg.pop_front();
        double ga = std::min(avgSum / L, g + 1.0);
        dl.push_back(l); dr.push_back(r);
        double ol = dl.front() * ga, orr = dr.front() * ga;
        dl.pop_front(); dr.pop_front();
        l = (float)std::clamp(ol, -ceiling, ceiling); r = (float)std::clamp(orr, -ceiling, ceiling);  // safety
    }
};

// ---------------------------------------------------------------- the engine
// rows: brightness 0..1, row 0 = bottom; layer 0..3 (v1 match: one bank per layer)
struct Column { double beat = 0; std::vector<float> rows; int layer = 0; };
enum class EngineKind { Bank, Ifft };

// one v1 layer: how its column maps to pitch and how loud it is (v1: scene root, image note grid,
// harmony transpose / scale / scaleQuant, the Sonification tab's L1..L4 settings)
struct V1Layer {
    bool on = true;
    int notegrid = 100, root = 27, imageH = 0;   // imageH 0: the column's height
    int trans = 0; float scaleQuant = 0;
    float scale[12] = {}; int scaleN = 0;        // scale degrees (semitones); 0 = chromatic
    float level = 1, send = 0.1119f, pan = 0, spread = 1;   // send: reverb, equal power (perf reverbAmt)
};

struct SoundSettings {
    // v1 match (default): v1's partial layout, gains, lag, detuned stereo pair, kick duck, FX, limiter
    bool v1Match = true;
    V1Layer layers[v1::kLayers];
    float bankGain = 0.0216f, detune = 0.0746f, loudnessBlend = 0.2f, lagS = 1.0f / 30;
    float spreadLow = 40, spreadHigh = 100;
    float duckBelow = 64, duckDepth = 0.89f, duckShape = 0.6f, duckBeats = 2, duckCurve = -4;
    float bend = 0;                     // the clock's pitch bend (semitones)
    float ampMul = 1;                   // v1 imageMul (head opacity / post Levels)
    bool kickOn = true; int kickLayer = 0;
    float kickThreshold = 0.5f, kickStrip = 100.0f / 3840, kickMinGap = 0.08f, kickTune = -2;
    float kickAmp = 0.16f, kickGain = 1.5f;   // perf kickVol x the PolyMixer level
    bool kickSynth = true;              // no kick sample loaded: v2's synthetic kick instead
    bool fx = true; float fxGain = 2, revTime = 4;   // FormsFX (SpectrumGain 2, LongReverb 4 s)
    // v2 options (defaults keep the v1 sound)
    EngineKind engine = EngineKind::Bank;   // Ifft: v2 path only (v1Match false)
    int rows = 1080;                    // rows per column (v1 match: the image column, e.g. 1080)
    PitchSpace pitch;                   // v2 path: row 0 = pitch.lowSemi
    double refHz = 261.6256;            // v2 path: semitone 0 (C4)
    std::function<double(double)> freqOfSemis;
    bool interpolate = false;           // true: sub-pixel playhead (v2); false: frame-stepped (v1)
    double additiveGain = 0.2, smoothMs = 0;
    int fftSize = 512, fftHop = 128, fftKernel = 8; double splitHz = 300;
    double pitchBalance = 0;            // 0 = v1 (only its equal-loudness blend)
    bool normalise = false; double targetRms = 0.1, normTimeS = 2.0;
    bool limit = true; double ceiling = 0.99, limitLookMs = 10;   // SC Limiter.ar(sig, 0.99, 0.01)
    int bands = 24;
    double scRef = 0.1;
    double noteAttackMs = 3, noteReleaseMs = 200, noteLevel = 0.3;

    // the v2 engine as it was before the v1 match (85 rows, pitch balance, loudness normaliser)
    static SoundSettings v2Modern() {
        SoundSettings s; s.v1Match = false; s.rows = 85; s.interpolate = true; s.pitchBalance = 0.5;
        s.normalise = true; s.ceiling = 0.891; s.limitLookMs = 1; return s;
    }
};

class SoundEngine {
public:
    SoundSettings s;
    static constexpr int kSub = 16;     // v2 control sub-block
    static constexpr int kCtl = 64;     // v1: SuperCollider's control block

    void prepare(double sampleRate, int blockSize) {
        sr = sampleRate; block = blockSize;
        lim.ceiling = s.ceiling; lim.lookMs = s.limitLookMs; lim.prepare(sr);
        normGain = 1; normMs = s.targetRms * s.targetRms;
        stem.assign((size_t)std::max(blockSize, kCtl), 0);
        columns.clear(); drums.clear(); notes.clear(); events.clear(); fired.clear();
        bandF.clear(); bandBp.clear(); bandEnv.assign((size_t)s.bands, 0);
        for (int b = 0; b < s.bands; b++) {
            double f = 40 * std::pow(16000.0 / 40.0, (double)b / (s.bands - 1));
            bandF.push_back(f); Biquad q; q.set(Biquad::BP, f, 2.0, sr); bandBp.push_back(q);
        }
        if (s.v1Match) {
            for (int l = 0; l < v1::kLayers; l++) { auto& V = vl[l]; V.cols.clear(); V.h = -1; buildLayer(l, std::max(1, s.rows)); }
            fxc.gain = s.fxGain; fxc.prepare(sr, s.revTime);
            duckT = -1; kickPrev = false; lastKick = -1e9; samples = 0; kickPos = -1;
            return;
        }
        freqs.resize((size_t)s.rows);
        for (int r = 0; r < s.rows; r++) freqs[(size_t)r] = rowFreq(r);
        if (s.engine == EngineKind::Bank) { auto b = std::make_unique<OscBank>(); b->smoothMs = s.smoothMs; add = std::move(b); }
        else {
            auto b = std::make_unique<IfftBank>();
            b->N = s.fftSize; b->hop = std::max(kSub, s.fftHop / kSub * kSub); b->K = s.fftKernel; b->splitHz = s.splitHz;
            b->low.smoothMs = s.smoothMs; add = std::move(b);
        }
        add->prepare(sr, freqs);
        weight.resize(freqs.size());
        for (size_t r = 0; r < freqs.size(); r++) weight[r] = (float)pitchBalanceGain(freqs[r], s.pitchBalance);
        amps.assign(freqs.size(), 0); scGain.assign(freqs.size(), 1);
    }
    double rowFreq(int r) const {
        double h = s.rows > 1 ? (double)r / (s.rows - 1) : 0;
        double semis = s.pitch.semisOf(h);
        return s.freqOfSemis ? s.freqOfSemis(semis) : s.refHz * std::pow(2.0, semis / 12.0);
    }
    int latencySamples() const { return !s.v1Match && add ? add->latency() : 0; }
    void setPitchBalance(double b) {
        s.pitchBalance = b;
        for (size_t r = 0; r < freqs.size() && r < weight.size(); r++) weight[r] = (float)pitchBalanceGain(freqs[r], b);
    }
    void setScale(const Scale& sc) { scale = sc; }
    void resetLoudness() { normGain = 1; normMs = s.targetRms * s.targetRms; }
    // v1 match: a layer's mapping changed (harmony / settings); rebuilt at its column height
    void setLayer(int l, const V1Layer& L) {
        if (l < 0 || l >= v1::kLayers) return;
        s.layers[l] = L;
        if (s.v1Match) buildLayer(l, vl[l].h > 0 ? vl[l].h : std::max(1, s.rows));
    }
    void setBend(float b) { if (b != s.bend) { s.bend = b; for (int l = 0; l < v1::kLayers; l++) rotors(l); } }
    // kick sample (stereo, its own rate); empty: the synthetic kick (if kickSynth)
    void setKickSample(std::vector<float> L, std::vector<float> R, double rate) { kickL = std::move(L); kickR = std::move(R); kickSr = rate; if (kickR.size() != kickL.size()) kickR = kickL; }
    // the same without allocating (audio thread): swaps buffers with the caller
    void swapKickSample(std::vector<float>& L, std::vector<float>& R, double rate) { kickPos = -1; std::swap(kickL, L); std::swap(kickR, R); kickSr = rate; if (kickR.size() != kickL.size()) kickR = kickL; }
    void triggerKick() { kickNow = true; }
    // v1 match: arrays of a layer (MIDI pitch, gains, duck weights) for tests / meters
    const std::vector<float>& layerPitch(int l) const { return vl[l].pitch; }
    const std::vector<float>& layerGl(int l) const { return vl[l].gl; }
    const std::vector<float>& layerGr(int l) const { return vl[l].gr; }
    const std::vector<float>& layerDuck(int l) const { return vl[l].duckw; }
    const std::vector<float>& layerTarget(int l) const { return vl[l].target; }
    int layerPartials(int l) const { return vl[l].lay.n; }
    int kicksFired() const { return kicks; }

    void pushColumn(double beat, const std::vector<float>& rows, int layer = 0) {
        if (s.v1Match) {
            if (layer < 0 || layer >= v1::kLayers) return;
            auto& q = vl[layer].cols;
            if (!q.empty() && beat <= q.back().beat) q.pop_back();
            q.push_back({beat, rows, layer});
            while (q.size() > 256) q.pop_front();
            return;
        }
        if (layer != 0) return;
        if (!columns.empty() && beat <= columns.back().beat) columns.pop_back();
        columns.push_back({beat, rows, 0});
        columns.back().rows.resize((size_t)s.rows, 0.f);
        while (columns.size() > 512) columns.pop_front();
    }
    void pushEvents(const std::vector<Event>& evs) {
        for (auto& e : evs) if (!fired.count(e.id)) {
            bool dup = false;
            for (auto& x : events) if (x.id == e.id) { x = e; dup = true; break; }
            if (!dup) events.push_back(e);
        }
    }

    // stereo output for n samples; beat of the first sample and beats per sample
    void process(float* outL, float* outR, int n, double beatAtBlockStart, double beatsPerSample) {
        if ((int)stem.size() < n) stem.resize((size_t)n);
        if (s.v1Match) { processV1(outL, outR, n, beatAtBlockStart, beatsPerSample); return; }
        std::fill(outL, outL + n, 0.f); std::fill(outR, outR + n, 0.f); std::fill(stem.begin(), stem.begin() + n, 0.f);
        double lat = latencySamples() * beatsPerSample;
        for (int i0 = 0; i0 < n; i0 += kSub) {
            int m = std::min(kSub, n - i0);
            double b = beatAtBlockStart + i0 * beatsPerSample - lat;
            triggerEvents(b, beatsPerSample, m);
            readColumn(b);
            for (size_t r = 0; r < amps.size(); r++) amps[r] *= weight[r] * scGain[r] * (float)s.additiveGain;
            if (m == kSub) add->render(amps, &stem[(size_t)i0], m);
            else { float tmp[kSub] = {0}; add->render(amps, tmp, kSub); std::copy(tmp, tmp + m, &stem[(size_t)i0]); }
            for (int i = i0; i < i0 + m; i++) {
                double L = stem[(size_t)i] * 0.7071, R = L, sc = 0;
                voices(L, R, sc);
                analyseSidechain(sc);
                outL[i] = (float)L; outR[i] = (float)R;
            }
            pruneVoices();
            updateScGains();
        }
        finish(outL, outR, n);
    }
    // mono additive bus of the last block (v1 match: (L+R)/2 of the banks, before the FX)
    const std::vector<float>& additiveStem() const { return stem; }
    double normalisationGain() const { return normGain; }

private:
    template <class V> struct Pending { V voice; int startDelay; };
    struct V1State {
        v1::Layout lay; int h = -1;
        std::vector<float> pitch, gl, gr, duckw, target, lagged, aPrev;
        std::vector<double> cL, sL, cR, sR, wLc, wLs, wRc, wRs, bLc, bLs, bRc, bRs;
        std::deque<Column> cols; double curBeat = -1e300;
    };
    double sr = 48000; int block = 256;
    std::vector<double> freqs;
    std::vector<float> weight, amps, scGain, stem;
    std::unique_ptr<Additive> add;
    std::deque<Column> columns;
    std::vector<Event> events; std::set<uint64_t> fired;
    std::vector<Pending<DrumVoice>> drums; std::vector<Pending<NoteVoice>> notes;
    std::vector<double> bandF, bandEnv; std::vector<Biquad> bandBp;
    double scAmount = 0, scAtt = 0.002, scRel = 0.12;
    Limiter lim; double normGain = 1, normMs = 0.01;
    Scale scale;
    // v1 match
    V1State vl[v1::kLayers];
    v1::Fx fxc;
    double duckT = -1, lastKick = -1e9, kickPos = -1, kickRate = 1, kickSr = 48000; long samples = 0;
    bool kickPrev = false, kickNow = false; int kicks = 0;
    std::vector<float> kickL, kickR;
    std::vector<float> busL, busR;

    void buildLayer(int l, int h) {
        auto& V = vl[l]; const V1Layer& L = s.layers[l];
        V.h = h;
        V.lay = v1::layout(h, L.notegrid, L.imageH);
        std::vector<float> sc(L.scale, L.scale + std::clamp(L.scaleN, 0, 12));
        V.pitch = v1::pitches(L.root, V.lay, L.trans, sc, L.scaleQuant);
        v1::gains(V.pitch, l, L.pan, L.spread, s.loudnessBlend, s.spreadLow, s.spreadHigh, V.gl, V.gr);
        V.duckw = v1::duckWeights(V.pitch, s.duckBelow, s.duckDepth, s.duckShape);
        size_t n = V.pitch.size();
        if (V.target.size() != n) {
            V.target.assign(n, 0); V.lagged.assign(n, 0); V.aPrev.assign(n, 0);
            // Rand(-25, 25) start phase per oscillator (FormsBank512)
            Rng rng(0xB0A7 + 977 * (uint64_t)l);
            V.cL.resize(n); V.sL.resize(n); V.cR.resize(n); V.sR.resize(n);
            for (size_t i = 0; i < n; i++) {
                double a = 25 * rng.next(), b = 25 * rng.next();
                V.cL[i] = std::cos(a); V.sL[i] = std::sin(a); V.cR[i] = std::cos(b); V.sR[i] = std::sin(b);
            }
        }
        rotors(l);
    }
    void rotors(int l) {
        auto& V = vl[l]; size_t n = V.pitch.size();
        if (!n) return;
        V.wLc.resize(n); V.wLs.resize(n); V.wRc.resize(n); V.wRs.resize(n); V.bLc.resize(n); V.bLs.resize(n); V.bRc.resize(n); V.bRs.resize(n);
        double half = s.detune * 0.5;
        for (size_t i = 0; i < n; i++) {
            auto hz = [&](double m) { return std::clamp(440.0 * std::pow(2.0, (m - 69.0) / 12.0), 0.0, 20000.0); };
            double wl = kTwoPi * hz(V.pitch[i] + s.bend - half) / sr, wr = kTwoPi * hz(V.pitch[i] + s.bend + half) / sr;
            V.wLc[i] = std::cos(wl); V.wLs[i] = std::sin(wl); V.wRc[i] = std::cos(wr); V.wRs[i] = std::sin(wr);
            V.bLc[i] = std::cos(wl * kCtl); V.bLs[i] = std::sin(wl * kCtl); V.bRc[i] = std::cos(wr * kCtl); V.bRs[i] = std::sin(wr * kCtl);
        }
    }
    // the column under the playhead (stepped; interpolated if asked) -> partial targets; kick detection
    void readLayer(int l, double b) {
        auto& V = vl[l]; auto& q = V.cols;
        if (q.empty()) return;
        while (q.size() > 1 && q[1].beat <= b) q.pop_front();
        const Column& c0 = q[0];
        if (b < c0.beat - 1e-9 && V.curBeat == -1e300) return;   // nothing yet
        bool interp = s.interpolate && q.size() > 1 && b > c0.beat;
        if (!interp && c0.beat == V.curBeat) return;              // same frame
        V.curBeat = c0.beat;
        int h = (int)c0.rows.size();
        if (h < 1) return;
        if (h != V.h) buildLayer(l, h);
        const V1Layer& L = s.layers[l];
        float t = interp ? (float)std::clamp((b - c0.beat) / std::max(1e-9, q[1].beat - c0.beat), 0.0, 1.0) : 0.f;
        auto px = [&](int r) {   // v1 row r (0 = top)
            float v = c0.rows[(size_t)(h - 1 - r)];
            if (interp && (int)q[1].rows.size() == h) v += (q[1].rows[(size_t)(h - 1 - r)] - v) * t;
            return v;
        };
        for (int i = 0; i < (int)V.target.size(); i++) {
            float m = 0;
            if (L.on && i < V.lay.n) for (int r = V.lay.rowA[i]; r < V.lay.rowB[i] && r < h; r++) m = std::max(m, px(r));
            V.target[(size_t)i] = std::clamp(m, 0.f, 1.f) * s.ampMul;   // the synth clips to 0..1 before the lag
            if (V.target[(size_t)i] > 1) V.target[(size_t)i] = 1;
        }
        if (l == s.kickLayer && s.kickOn) {
            int strip = std::max(1, std::min(h, (int)std::lround(h * s.kickStrip)));
            double sum = 0; for (int r = 0; r < strip; r++) sum += c0.rows[(size_t)r];
            bool k = sum / strip > s.kickThreshold;
            if (k && !kickPrev && (samples - lastKick) / sr >= s.kickMinGap) kickNow = true;
            kickPrev = k;
        }
    }
    void startKick() {
        kickNow = false; kicks++; lastKick = (double)samples; duckT = 0;
        kickRate = std::pow(2.0, (s.layers[0].trans + s.kickTune) / 12.0);
        if (!kickL.empty()) kickPos = 0;
        else if (s.kickSynth) { DrumParams d; d.mode = DrumMode::Mix; d.level = s.kickAmp * s.kickGain; drums.push_back({DrumVoice(d, sr, 1234 + (uint64_t)kicks), 0}); }
    }

    void processV1(float* outL, float* outR, int n, double b0, double bps) {
        if ((int)busL.size() < 4 * n) { busL.resize((size_t)n * 4); busR.resize((size_t)n * 4); }
        float* dL = busL.data(); float* dR = busR.data(); float* wL = dL + n; float* wR = dR + n;
        std::fill(dL, dL + 2 * n, 0.f); std::fill(dR, dR + 2 * n, 0.f);
        double duckDecay = s.duckBeats / std::max(1e-12, bps * sr);
        for (int i0 = 0; i0 < n; i0 += kCtl) {
            int m = std::min(kCtl, n - i0);
            double b = b0 + i0 * bps;
            triggerEvents(b, bps, m);
            for (int l = 0; l < v1::kLayers; l++) readLayer(l, b);
            if (kickNow) startKick();
            double dv = 0;
            if (duckT >= 0) { dv = std::clamp(v1::envDown(duckT / std::max(0.01, duckDecay), s.duckCurve), 0.0, 1.0); duckT += m / sr; if (duckT > duckDecay) duckT = -1; }
            double k = 1 - std::exp(std::log(0.001) * m / (std::max(1e-4f, s.lagS) * sr));
            for (int l = 0; l < v1::kLayers; l++) {
                auto& V = vl[l]; const V1Layer& L = s.layers[l];
                if (V.target.empty()) continue;
                double g = s.bankGain * L.level, sa = std::clamp((double)L.send, 0.0, 1.0) * M_PI / 2;
                double gd = g * std::cos(sa), gw = g * std::sin(sa);
                for (size_t i = 0; i < V.target.size(); i++) {
                    V.lagged[i] += (float)((V.target[i] - V.lagged[i]) * k);
                    float a1 = V.lagged[i] * (float)(1 - V.duckw[i] * dv), a0 = V.aPrev[i];
                    V.aPrev[i] = a1;
                    double cl = V.cL[i], sl = V.sL[i], cr = V.cR[i], sr2 = V.sR[i];
                    if (a0 < 1e-7f && a1 < 1e-7f) {   // silent: just advance the phases
                        if (m == kCtl) {
                            double t;
                            t = cl * V.bLc[i] - sl * V.bLs[i]; sl = sl * V.bLc[i] + cl * V.bLs[i]; cl = t;
                            t = cr * V.bRc[i] - sr2 * V.bRs[i]; sr2 = sr2 * V.bRc[i] + cr * V.bRs[i]; cr = t;
                        } else for (int j = 0; j < m; j++) {
                            double t = cl * V.wLc[i] - sl * V.wLs[i]; sl = sl * V.wLc[i] + cl * V.wLs[i]; cl = t;
                            t = cr * V.wRc[i] - sr2 * V.wRs[i]; sr2 = sr2 * V.wRc[i] + cr * V.wRs[i]; cr = t;
                        }
                    } else {
                        double gl = V.gl[i], gr = V.gr[i], wc = V.wLc[i], ws = V.wLs[i], vc = V.wRc[i], vs = V.wRs[i];
                        double a = a0, da = (a1 - a0) / m;
                        float* pdl = dL + i0; float* pdr = dR + i0; float* pwl = wL + i0; float* pwr = wR + i0;
                        for (int j = 0; j < m; j++) {
                            a += da;
                            double yl = a * gl * sl, yr = a * gr * sr2;
                            pdl[j] += (float)(yl * gd); pdr[j] += (float)(yr * gd);
                            pwl[j] += (float)(yl * gw); pwr[j] += (float)(yr * gw);
                            double t = cl * wc - sl * ws; sl = sl * wc + cl * ws; cl = t;
                            t = cr * vc - sr2 * vs; sr2 = sr2 * vc + cr * vs; cr = t;
                        }
                    }
                    double nl = 1.5 - 0.5 * (cl * cl + sl * sl), nr = 1.5 - 0.5 * (cr * cr + sr2 * sr2);   // keep on the unit circle
                    V.cL[i] = cl * nl; V.sL[i] = sl * nl; V.cR[i] = cr * nr; V.sR[i] = sr2 * nr;
                }
            }
            for (int i = i0; i < i0 + m; i++) {
                stem[(size_t)i] = 0.5f * (dL[i] + dR[i] + wL[i] + wR[i]);
                double out[2];
                if (s.fx) { double d[2] = {dL[i], dR[i]}, w[2] = {wL[i], wR[i]}; fxc.process(d, w, out); }
                else { out[0] = dL[i] + wL[i]; out[1] = dR[i] + wR[i]; }
                double L = out[0], R = out[1], sc = 0;
                if (kickPos >= 0) {   // FormsKick: PlayBuf, straight to the master (x kickVol x kickGain)
                    size_t i1 = (size_t)kickPos;
                    if (i1 + 1 >= kickL.size()) kickPos = -1;
                    else {
                        double f = kickPos - i1, kg = s.kickAmp * s.kickGain;
                        L += kg * (kickL[i1] + (kickL[i1 + 1] - kickL[i1]) * f);
                        R += kg * (kickR[i1] + (kickR[i1 + 1] - kickR[i1]) * f);
                        kickPos += kickRate * kickSr / sr;
                    }
                }
                voices(L, R, sc);
                outL[i] = (float)L; outR[i] = (float)R;
                samples++;
            }
            pruneVoices();
        }
        finish(outL, outR, n);
    }
    void voices(double& L, double& R, double& sc) {
        for (auto& v : drums) {
            if (v.startDelay > 0) { v.startDelay--; continue; }
            double y = v.voice.tick();
            double pl = std::sqrt(0.5 * (1 - v.voice.p.pan)), pr = std::sqrt(0.5 * (1 + v.voice.p.pan));
            L += y * pl; R += y * pr;
            if (v.voice.p.mode == DrumMode::Sidechain) sc += y;
        }
        for (auto& v : notes) {
            if (v.startDelay > 0) { v.startDelay--; continue; }
            double y = v.voice.tick();
            L += y * std::sqrt(0.5 * (1 - v.voice.pan)); R += y * std::sqrt(0.5 * (1 + v.voice.pan));
        }
    }
    void pruneVoices() {
        drums.erase(std::remove_if(drums.begin(), drums.end(), [](const Pending<DrumVoice>& v) { return !v.voice.alive; }), drums.end());
        notes.erase(std::remove_if(notes.begin(), notes.end(), [](const Pending<NoteVoice>& v) { return !v.voice.alive; }), notes.end());
    }
    // loudness normalisation (off in v1 match) and limiter
    void finish(float* outL, float* outR, int n) {
        double aMs = 1 - std::exp(-1.0 / (0.4 * sr)), aG = 1 - std::exp(-1.0 / (s.normTimeS * sr));
        for (int i = 0; i < n; i++) {
            if (s.normalise) {
                double x2 = 0.5 * ((double)outL[i] * outL[i] + (double)outR[i] * outR[i]);
                normMs += (x2 - normMs) * aMs;
                if (normMs > 1e-7) {
                    double target = std::clamp(s.targetRms / std::sqrt(normMs), 0.06, 4.0);
                    normGain += (target - normGain) * aG;
                }
                outL[i] *= (float)normGain; outR[i] *= (float)normGain;
            }
            if (s.limit) lim.process(outL[i], outR[i]);
        }
    }
    void readColumn(double b) {
        std::fill(amps.begin(), amps.end(), 0.f);
        if (columns.empty()) return;
        while (columns.size() > 2 && columns[1].beat <= b) columns.pop_front();
        const Column& c0 = columns[0];
        if (columns.size() == 1 || b <= c0.beat) { if (b >= c0.beat - 1e-9 || columns.size() == 1) std::copy(c0.rows.begin(), c0.rows.end(), amps.begin()); return; }
        const Column& c1 = columns[1];
        if (b >= c1.beat) { std::copy(c1.rows.begin(), c1.rows.end(), amps.begin()); return; }
        if (!s.interpolate) { std::copy(c0.rows.begin(), c0.rows.end(), amps.begin()); return; }
        float t = (float)((b - c0.beat) / (c1.beat - c0.beat));
        for (size_t r = 0; r < amps.size(); r++) amps[r] = c0.rows[r] + (c1.rows[r] - c0.rows[r]) * t;
    }
    void triggerEvents(double b, double bps, int m) {
        double end = b + m * bps;
        for (size_t k = 0; k < events.size();) {
            const Event& e = events[k];
            if (e.start < end && e.start >= b - 0.05) {
                int off = std::clamp((int)std::floor((e.start - b) / bps), 0, m - 1);
                startVoice(e, off, bps);
                fired.insert(e.id); events.erase(events.begin() + (long)k);
            } else if (e.start < b - 0.05) { fired.insert(e.id); events.erase(events.begin() + (long)k); }
            else k++;
        }
    }
    void startVoice(const Event& e, int off, double bps) {
        if (e.kind == EventKind::Hit || e.transient) {
            DrumParams d = drumParamsOf(e);
            if (d.mode == DrumMode::Off) return;
            if (d.mode == DrumMode::Sidechain) { scAmount = d.scAmount; scAtt = d.scAttackMs * 0.001; scRel = d.scReleaseMs * 0.001; }
            drums.push_back({DrumVoice(d, sr, e.seed ? e.seed : e.id + 1), off});
        } else if (e.kind == EventKind::Note && e.param("noteVoice", 0) > 0.5) {
            double semis = e.pitched ? scale.semitonesOf(e.degree) : s.pitch.semisOf(e.height);
            NoteVoice v;
            v.sr = sr; v.f = s.freqOfSemis ? s.freqOfSemis(semis) : s.refHz * std::pow(2.0, semis / 12.0);
            v.dur = std::max(0.0, e.length / bps / sr); v.att = e.param("noteAttack", s.noteAttackMs) * 0.001;
            v.rel = std::max(0.005, e.param("noteRelease", s.noteReleaseMs) * 0.001);
            v.level = s.noteLevel * e.level * pitchBalanceGain(v.f, s.pitchBalance); v.pan = e.param("pan", 0);
            notes.push_back({v, off});
        }
    }
    void analyseSidechain(double x) {
        double ka = 1 - std::exp(-1.0 / (std::max(1e-4, scAtt) * sr)), kr = 1 - std::exp(-1.0 / (std::max(1e-3, scRel) * sr));
        for (size_t b = 0; b < bandBp.size(); b++) {
            double y = std::abs(bandBp[b].tick(x));
            bandEnv[b] += (y - bandEnv[b]) * (y > bandEnv[b] ? ka : kr);
        }
    }
    void updateScGains() {
        if (bandF.empty()) return;
        double lf0 = std::log(bandF.front()), span = std::log(bandF.back()) - lf0;
        for (size_t r = 0; r < freqs.size(); r++) {
            double pos = std::clamp((std::log(std::max(freqs[r], 1.0)) - lf0) / span * (bandF.size() - 1), 0.0, (double)bandF.size() - 1);
            size_t i = (size_t)pos; double t = pos - i;
            double e = bandEnv[i] * (1 - t) + (i + 1 < bandF.size() ? bandEnv[i + 1] * t : 0);
            scGain[r] = (float)(1 - scAmount * std::min(1.0, e / s.scRef));
        }
    }
};

}  // namespace snd
}  // namespace forms2
