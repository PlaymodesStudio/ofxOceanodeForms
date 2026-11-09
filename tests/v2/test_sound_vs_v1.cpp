// A/B of the v2 sound engine (v2Sound.h, v1 match) against v1's sonification on identical columns.
// v1 side: formsSoundModel.h (the exact arrays and amplitudes v1 sends to SuperCollider) and a
// reference renderer of FormsBank512 written here (sin per sample, SC control-rate Lag, ramped mul,
// random start phases). The FX (FormsFX port v1::Fx) and limiter are shared by both sides, so the
// spectral A/B checks the bank; the FX are a port of the SynthDef, not a capture of scsynth.
// Also renders paired demo WAVs (v1 model / v2 now / v2 before) for listening.
// Build (addon folder): clang++ -std=c++17 -O2 -DV2AUDIO_NO_OF -I src/formsV2 -I src/formsGL -I src/formsBox
//        -I <OF>/libs/json/include tests/v2/test_sound_vs_v1.cpp
#include "v2Sound.h"
#include "formsSoundModel.h"
#include "formsGLSketch.h"
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

using namespace forms2;
using namespace forms2::snd;

static const std::string WAVS = "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027/notes/v2_sound/";
static const std::string RF = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/rf/before4/";
static const std::string KICKS = "/Users/santiagovilanova/Documents/OF/of_playmodes/openFrameworks/apps/Playmodes/OceanodeScroller/bin/data/Forms/samples/KICKS/";
static const double SR = 48000, FPS = 60;
static const int H = 1080, NG = 100, ROOT = 27;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& name, const std::string& info) {
    (ok ? passes : fails)++;
    printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name.c_str(), info.c_str());
}
static std::string fmt(const char* f, double a = 0, double b = 0, double c = 0, double d = 0) { char buf[256]; snprintf(buf, sizeof buf, f, a, b, c, d); return buf; }

static void writeWav(const std::string& name, const std::vector<float>& L, const std::vector<float>& R) {
    std::ofstream f(WAVS + name, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    uint32_t n = (uint32_t)L.size(), bytes = n * 4;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32((uint32_t)SR); u32((uint32_t)SR * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; i++) for (float x : {L[i], R[i]}) { int16_t v = (int16_t)std::lround(std::clamp(x, -1.f, 1.f) * 32767); f.write((const char*)&v, 2); }
}

// a sequence: one image column per video frame, brightness 0..1, row 0 = TOP (as v1 reads the FBO)
using Seq = std::vector<std::vector<float>>;
static std::vector<unsigned char> toRgba(const std::vector<float>& c) {
    std::vector<unsigned char> v(c.size() * 4);
    for (size_t r = 0; r < c.size(); r++) { unsigned char b = (unsigned char)std::lround(std::clamp(c[r], 0.f, 1.f) * 255); v[r * 4] = v[r * 4 + 1] = v[r * 4 + 2] = b; v[r * 4 + 3] = 255; }
    return v;
}
static std::vector<float> quant(std::vector<float> c) { for (auto& x : c) x = std::lround(std::clamp(x, 0.f, 1.f) * 255) / 255.f; return c; }   // 8-bit, like the FBO
static float yOfPitch(double p) { return (float)(H - p * H / NG - (H / NG)); }   // the renderer (Render.pde)
static void line(std::vector<float>& c, double p, float a, double thick = 3) {
    double y = yOfPitch(p);
    for (int r = std::max(0, (int)(y - thick)); r <= std::min(H - 1, (int)(y + thick)); r++) {
        double cov = std::clamp(thick / 2 + 0.5 - std::abs(r + 0.5 - y), 0.0, 1.0);
        c[(size_t)r] = std::max(c[(size_t)r], (float)(a * cov));
    }
}

// ---------------------------------------------------------------- v1 reference renderer
struct Out { std::vector<float> L, R, bankL, bankR; };
// samePhases: the engine's start phases (else independent ones, as two SC runs would differ)
static Out renderV1(const Seq& seq, int layer = 0, bool fx = true, bool samePhases = true) {
    forms::SoundSettings S;
    auto lay = forms::partialLayout(H, NG, H);
    auto pitch = forms::partialPitches(ROOT, lay, 0, {}, 0);
    std::vector<float> gl, gr; forms::partialGains(pitch, layer, S, gl, gr);
    int n = forms::SOUND_PARTIALS;
    std::vector<double> phL(n), phR(n), wL(n), wR(n), lag(n, 0), aPrev(n, 0);
    snd::Rng rng(samePhases ? 0xB0A7 + 977 * (uint64_t)layer : 777);
    for (int i = 0; i < n; i++) {
        phL[i] = 25 * rng.next(); phR[i] = 25 * rng.next();
        double bend = -S.detune * 0.5;
        wL[i] = kTwoPi * 440 * std::pow(2.0, (pitch[i] + bend - 69) / 12.0) / SR;
        wR[i] = kTwoPi * 440 * std::pow(2.0, (pitch[i] + bend + S.detune - 69) / 12.0) / SR;
    }
    size_t total = (size_t)(seq.size() / FPS * SR);
    Out o; o.L.assign(total, 0); o.R.assign(total, 0); o.bankL.assign(total, 0); o.bankR.assign(total, 0);
    std::vector<float> amps;
    double send = 0.1119 * M_PI / 2, k = 1 - std::exp(std::log(0.001) * 64 / (S.lag * SR));
    v1::Fx f; f.gain = 2; f.prepare(SR, 4);
    Limiter lim; lim.ceiling = 0.99; lim.lookMs = 10; lim.prepare(SR);
    for (size_t b0 = 0; b0 < total; b0 += 64) {
        size_t fr = std::min(seq.size() - 1, (size_t)(b0 / SR * FPS));
        auto rgba = toRgba(seq[fr]);
        forms::columnAmps(rgba.data(), H, 1.0f, lay, amps);
        size_t m = std::min<size_t>(64, total - b0);
        for (int i = 0; i < n; i++) {
            lag[i] += (std::clamp(amps[i], 0.f, 1.f) - lag[i]) * k;
            double a0 = aPrev[i], a1 = lag[i]; aPrev[i] = a1;
            if (a0 < 1e-7 && a1 < 1e-7) { phL[i] += wL[i] * m; phR[i] += wR[i] * m; continue; }
            for (size_t j = 0; j < m; j++) {
                double a = a0 + (a1 - a0) * (j + 1) / m;
                o.bankL[b0 + j] += (float)(a * gl[i] * std::sin(phL[i]));
                o.bankR[b0 + j] += (float)(a * gr[i] * std::sin(phR[i]));
                phL[i] += wL[i]; phR[i] += wR[i];
            }
            phL[i] = std::fmod(phL[i], kTwoPi); phR[i] = std::fmod(phR[i], kTwoPi);
        }
        for (size_t j = b0; j < b0 + m; j++) {
            o.bankL[j] *= S.bankGain; o.bankR[j] *= S.bankGain;
            double d[2] = {o.bankL[j] * std::cos(send), o.bankR[j] * std::cos(send)}, w[2] = {o.bankL[j] * std::sin(send), o.bankR[j] * std::sin(send)}, y[2];
            if (fx) f.process(d, w, y); else { y[0] = o.bankL[j]; y[1] = o.bankR[j]; }
            float l = (float)y[0], r = (float)y[1];
            if (fx) lim.process(l, r);
            o.L[j] = l; o.R[j] = r;
        }
    }
    return o;
}

// ---------------------------------------------------------------- v2 engine (now / before)
static Out renderV2(const Seq& seq, SoundSettings st, bool before, int layer = 0) {
    SoundEngine e; e.s = st; e.prepare(SR, 256);
    double bpm = 120, bps = bpm / 60 / SR, frameBeat = bpm / 60 / FPS;
    size_t total = (size_t)(seq.size() / FPS * SR);
    Out o; o.L.assign(total, 0); o.R.assign(total, 0); o.bankL.assign(total, 0);
    size_t next = 0;
    for (size_t i = 0; i < total; i += 256) {
        int n = (int)std::min<size_t>(256, total - i);
        double b = i * bps;
        while (next < seq.size() && next * frameBeat <= b + n * bps) {
            const auto& c = seq[next];
            std::vector<float> rows;
            if (before) {   // the node's old resample (runSoundV2): 85 rows, max over a window
                rows.assign(85, 0);
                double half = std::max(0.5, 0.5 * H / 84);
                for (int r = 0; r < 85; r++) {
                    double y = (1.0 - r / 84.0) * (H - 1);
                    int ya = std::clamp((int)std::floor(y - half), 0, H - 1), yb = std::clamp((int)std::ceil(y + half), ya, H - 1);
                    float m = 0; for (int k = ya; k <= yb; k++) m = std::max(m, c[(size_t)k]);
                    rows[(size_t)r] = m;
                }
            } else { rows.assign(c.rbegin(), c.rend()); }   // full column, row 0 = bottom
            e.pushColumn(next * frameBeat, rows, layer);
            next++;
        }
        e.process(&o.L[i], &o.R[i], n, b, bps);
        std::copy(e.additiveStem().begin(), e.additiveStem().begin() + n, o.bankL.begin() + (long)i);
    }
    return o;
}

// ---------------------------------------------------------------- spectra
static std::vector<double> avgSpec(const std::vector<float>& L, const std::vector<float>& R) {
    const size_t N = 8192; std::vector<double> acc(N / 2, 0); int frames = 0;
    for (size_t a = 0; a + N <= L.size(); a += N / 2, frames++) {
        std::vector<cplx> X(N);
        for (size_t i = 0; i < N; i++) X[i] = (0.5 - 0.5 * std::cos(kTwoPi * i / N)) * 0.5 * (L[a + i] + R[a + i]);
        fft(X, false);
        for (size_t k = 0; k < N / 2; k++) acc[k] += std::norm(X[k]);
    }
    for (auto& x : acc) x = 10 * std::log10(x / std::max(1, frames) + 1e-20);
    return acc;
}
// mean |dB difference| over bins 20 Hz - 16 kHz within 60 dB of the louder peak
static double specDist(const std::vector<double>& a, const std::vector<double>& b) {
    double pk = -1e9; for (size_t k = 0; k < a.size(); k++) pk = std::max({pk, a[k], b[k]});
    double s = 0; int n = 0;
    for (size_t k = (size_t)(20 * 8192 / SR); k < (size_t)(16000 * 8192 / SR); k++)
        if (std::max(a[k], b[k]) > pk - 60) { s += std::abs(a[k] - b[k]); n++; }
    return n ? s / n : 0;
}
static double rmsDb(const std::vector<float>& L, const std::vector<float>& R) {
    double s = 0; for (size_t i = 0; i < L.size(); i++) s += 0.5 * ((double)L[i] * L[i] + (double)R[i] * R[i]);
    return 10 * std::log10(s / std::max<size_t>(1, L.size()) + 1e-20);
}
static double centroid(const std::vector<double>& sp) {
    double s = 0, w = 0; for (size_t k = 1; k < sp.size(); k++) { double p = std::pow(10, sp[k] / 10); s += p * k * SR / 8192; w += p; }
    return s / std::max(w, 1e-30);
}

// ---------------------------------------------------------------- sequences
static Seq sweep(double sec) {
    Seq s; int n = (int)(sec * FPS);
    for (int f = 0; f < n; f++) { std::vector<float> c(H, 0); line(c, -0.5 + 96.0 * f / n, 0.9f); s.push_back(quant(c)); }
    return s;
}
static Seq chord(double sec) {
    Seq s; int n = (int)(sec * FPS);
    for (int f = 0; f < n; f++) { std::vector<float> c(H, 0); for (int p : {12, 19, 24, 28, 31, 36}) line(c, p, f < n * 0.8 ? 0.8f : 0.f); s.push_back(quant(c)); }
    return s;
}
static Seq dense(double sec) {
    Seq s; int n = (int)(sec * FPS); snd::Rng rng(99);
    std::vector<double> ps(40), ph(40); for (int i = 0; i < 40; i++) { ps[i] = 4 + 80 * (0.5 + 0.5 * rng.next()); ph[i] = rng.next() * 6; }
    for (int f = 0; f < n; f++) {
        std::vector<float> c(H, 0);
        for (int i = 0; i < 40; i++) { double a = 0.5 + 0.5 * std::sin(ph[i] + f * 0.05 * (1 + i % 5)); if (a > 0.4) line(c, ps[i], (float)a, 2 + i % 6); }
        for (int r = H / 2; r < H; r++) c[(size_t)r] = std::max(c[(size_t)r], (float)(0.15 * (0.5 + 0.5 * rng.next())));   // noise band
        s.push_back(quant(c));
    }
    return s;
}
// a real v1 image (the C++ port of the sketch), scrolled at the scene's tempo
static bool realScene(const std::string& req, double sec, Seq& out, std::string& why) {
    formsgl::Schemas schemas;
    if (!schemas.load(RF + "generators.json")) { why = "no generators.json"; return false; }
    std::ifstream in(req); if (!in) { why = "no request"; return false; }
    formsgl::json j; in >> j;
    formsgl::Sketch sk(schemas);
    why = sk.whyNot(j); if (!why.empty()) return false;
    auto r = sk.render(j);
    if (!r.error.empty() || r.h != H) { why = r.error.empty() ? fmt("height %d", r.h) : r.error; return false; }
    double bars = j.value("bars", 8), bpm = j.value("bpm", 120), imgSec = bars * 4 * 60 / bpm;
    int n = (int)(sec * FPS);
    for (int f = 0; f < n; f++) {
        int x = std::min(r.w - 1, (int)(f / FPS / imgSec * r.w));
        std::vector<float> c(H);
        for (int y = 0; y < H; y++) {   // straight alpha over black, brightest channel
            const uint8_t* p = &r.rgba[((size_t)y * r.w + x) * 4];
            c[(size_t)y] = std::max({p[0], p[1], p[2]}) * (p[3] / 255.f) / 255.f;
        }
        out.push_back(quant(c));
    }
    return true;
}

int main() {
    mkdir(WAVS.c_str(), 0755);
    // ---- 1. arrays: v2's v1 port == formsSoundModel.h (several sizes, roots, scales, layers)
    {
        double worst = 0; int cases = 0;
        for (auto [h, ng] : std::vector<std::pair<int, int>>{{1080, 100}, {1080, 72}, {2160, 100}, {720, 48}, {1080, 130}})
            for (int layer = 0; layer < 4; layer++) {
                SoundSettings st; st.rows = h;
                V1Layer L; L.notegrid = ng; L.root = 24 + layer * 3; L.trans = layer; L.scaleQuant = layer * 0.3f;
                std::vector<float> sc = {0, 2, 3, 7, 10}; for (size_t i = 0; i < sc.size(); i++) L.scale[i] = sc[i]; L.scaleN = 5;
                L.pan = 0.3f * layer - 0.4f; L.spread = 0.8f;
                st.layers[layer] = L;
                SoundEngine e; e.s = st; e.prepare(SR, 256);
                forms::SoundSettings S; S.layers[layer].pan = L.pan; S.layers[layer].spread = L.spread;
                auto lay = forms::partialLayout(h, ng, h);
                auto p = forms::partialPitches(L.root, lay, L.trans, sc, L.scaleQuant);
                std::vector<float> gl, gr; forms::partialGains(p, layer, S, gl, gr);
                auto dw = forms::duckWeights(p, S);
                for (size_t i = 0; i < p.size(); i++)
                    worst = std::max({worst, (double)std::abs(p[i] - e.layerPitch(layer)[i]), (double)std::abs(gl[i] - e.layerGl(layer)[i]),
                                      (double)std::abs(gr[i] - e.layerGr(layer)[i]), (double)std::abs(dw[i] - e.layerDuck(layer)[i])});
                if (e.layerPartials(layer) != lay.n) worst = 1e9;
                cases++;
            }
        check(worst == 0, "arrays identical to v1 (pitch, gl, gr, duck weights)", fmt("%.0f cases, worst |diff| %g", cases, worst));
    }
    // ---- 2. per-partial amplitudes (and pitch / gain products) on real-ish columns
    Seq sw = sweep(6), ch = chord(4), dn = dense(6);
    {
        auto lay = forms::partialLayout(H, NG, H);
        auto p = forms::partialPitches(ROOT, lay, 0, {}, 0);
        forms::SoundSettings S; std::vector<float> gl, gr; forms::partialGains(p, 0, S, gl, gr);
        double worstAmp = 0, bfSemi = 0, bfSemiMax = 0, bfDb = 0, bfW = 0;
        SoundSettings st; st.layers[0].root = ROOT; st.rows = H;
        SoundEngine e; e.s = st; e.prepare(SR, 256);
        int fr = 0;
        for (const Seq* s : {&sw, &ch, &dn}) for (size_t f = 0; f < s->size(); f += 7, fr++) {
            const auto& c = (*s)[f];
            std::vector<float> amps; auto rgba = toRgba(c);
            forms::columnAmps(rgba.data(), H, 1.0f, lay, amps);
            std::vector<float> rows(c.rbegin(), c.rend());
            e.pushColumn(fr, rows);
            float L[64], R[64]; e.process(L, R, 64, fr, 1e-9);
            for (size_t i = 0; i < amps.size(); i++) {
                worstAmp = std::max(worstAmp, (double)std::abs(amps[i] - e.layerTarget(0)[i]));
                if (amps[i] > 0.05f && (int)i < lay.n) {   // before: where the old v2 put this pixel
                    double y = 0.5 * (lay.rowA[i] + lay.rowB[i]);
                    int r = (int)std::lround((1 - y / (H - 1)) * 84);
                    double oldMidi = 60 - 24 + r;   // PitchSpace -24..60 around C4
                    double d = std::abs(oldMidi - p[i]);
                    bfSemi += d * amps[i]; bfW += amps[i]; bfSemiMax = std::max(bfSemiMax, d);
                    double oldF = 440 * std::pow(2.0, (oldMidi - 69) / 12);
                    double gOld = 0.2 * pitchBalanceGain(oldF, 0.5) * 0.7071, gNew = 0.5 * (gl[i] + gr[i]) * S.bankGain;
                    bfDb += std::abs(20 * std::log10(gOld / gNew)) * amps[i];
                }
            }
        }
        check(worstAmp < 1e-6, "per-partial amplitudes identical to v1 columnAmps", fmt("worst |diff| %g (after); per-partial freq diff 0 st (after)", worstAmp));
        printf("   BEFORE (old v2 path): per-partial pitch off by %.1f st on average (max %.1f), per-partial gain off by %.1f dB (amp-weighted)\n",
               bfSemi / bfW, bfSemiMax, bfDb / bfW);
    }
    // ---- 3. duck envelope = SC Env([0,1,0],[0,d],-4)
    {
        double worst = 0;
        for (double x = 0; x <= 1; x += 0.01) { double sc = 1 - (1 - std::exp(-4 * x)) / (1 - std::exp(-4.0)); worst = std::max(worst, std::abs(sc - v1::envDown(x, -4))); }
        check(worst < 1e-12, "duck envelope curve", fmt("worst %g", worst));
    }
    // ---- 4. kick detection like formsBox (bottom strip mean > 0.5, rising edge, min gap)
    {
        SoundSettings st; st.rows = H; st.kickSynth = true;
        SoundEngine e; e.s = st; e.prepare(SR, 256);
        double bps = 2.0 / SR; int expect = 0; bool prev = false; double lastT = -1;
        for (int f = 0; f < 240; f++) {
            bool on = (f % 30) < 3 || (f == 4);   // 8 kicks + a re-trigger 1 frame after a gap
            std::vector<float> rows(H, 0.f); if (on) for (int r = 0; r < 40; r++) rows[(size_t)r] = 0.9f;
            double t = f / FPS;
            if (on && !prev && t - lastT >= 0.08) { expect++; lastT = t; }
            prev = on;
            e.pushColumn(f * 2.0 / FPS, rows);
            std::vector<float> L(800), R(800); e.process(L.data(), R.data(), 800, f * 800 * bps, bps);
        }
        check(e.kicksFired() == expect, "kick detection", fmt("%.0f kicks fired, expected %.0f", e.kicksFired(), expect));
    }
    // ---- 5. spectral A/B and demo WAVs
    struct Case { std::string name; Seq seq; };
    std::vector<Case> cases = {{"sweep", sw}, {"chord", ch}, {"dense", dn}};
    for (std::string id : {"0", "36", "63"}) {
        Seq s; std::string why;
        if (realScene(RF + "scene_reqs/" + id + ".json", 8, s, why)) cases.push_back({"scene" + id, s});
        else printf("   scene %s not rendered: %s\n", id.c_str(), why.c_str());
    }
    printf("\n%-8s | bank only: dist dB  level dB | full chain: dist dB  level dB  centroid Hz (v1 / v2)   | BEFORE: dist dB  level dB  centroid Hz\n", "case");
    for (auto& c : cases) {
        auto v1o = renderV1(c.seq, 0, true), v1b = renderV1(c.seq, 0, false), v1x = renderV1(c.seq, 0, true, false);
        SoundSettings now; now.rows = H; now.kickOn = false; now.layers[0].root = ROOT;
        SoundSettings bank = now; bank.fx = false; bank.limit = false; bank.layers[0].send = 0;
        auto v2o = renderV2(c.seq, now, false), v2b = renderV2(c.seq, bank, false);
        auto old = renderV2(c.seq, SoundSettings::v2Modern(), true);
        auto s1 = avgSpec(v1o.L, v1o.R), s2 = avgSpec(v2o.L, v2o.R), s0 = avgSpec(old.L, old.R);
        auto b1 = avgSpec(v1b.L, v1b.R), b2 = avgSpec(v2b.L, v2b.R);
        double dBank = specDist(b1, b2), dFull = specDist(s1, s2), dOld = specDist(s1, s0);
        double dPh = specDist(s1, avgSpec(v1x.L, v1x.R));   // v1 against itself with other phases (the floor)
        double lBank = rmsDb(v2b.L, v2b.R) - rmsDb(v1b.L, v1b.R), lFull = rmsDb(v2o.L, v2o.R) - rmsDb(v1o.L, v1o.R), lOld = rmsDb(old.L, old.R) - rmsDb(v1o.L, v1o.R);
        printf("%-8s |        %6.2f    %+6.2f |          %6.2f    %+6.2f   %6.0f / %6.0f            |      %6.2f   %+6.2f   %6.0f\n",
               c.name.c_str(), dBank, lBank, dFull, lFull, centroid(s1), centroid(s2), dOld, lOld, centroid(s0));
        printf("         v1 vs v1 with other start phases: %.2f dB (the phase floor)\n", dPh);
        check(dBank < 1.0 && std::abs(lBank) < 0.3 && dFull < 1.0 && std::abs(lFull) < 0.3, "A/B " + c.name, fmt("bank %.2f dB, full %.2f dB (before %.2f dB)", dBank, dFull, dOld));
        writeWav(c.name + "_A_v1model.wav", v1o.L, v1o.R);
        writeWav(c.name + "_B_v2now.wav", v2o.L, v2o.R);
        writeWav(c.name + "_C_v2before.wav", old.L, old.R);
    }
    // ---- 6. v2 with a v1 kick sample (kick detection + duck), one real scene
    {
        std::vector<float> kl, kr; double ksr = 48000;
        bool have = v1::loadWav(KICKS + "1_Mr._Bill__Kick.wav", kl, kr, ksr);
        for (size_t ci = 3; ci < cases.size(); ci++) {
            SoundSettings st; st.rows = H; st.layers[0].root = ROOT;
            SoundEngine e; e.s = st; e.prepare(SR, 256);
            if (have) e.setKickSample(kl, kr, ksr);
            const Seq& s = cases[ci].seq;
            double bps = 2.0 / SR, fb = 2.0 / FPS; size_t total = (size_t)(s.size() / FPS * SR), nx = 0;
            std::vector<float> L(total), R(total);
            for (size_t i = 0; i < total; i += 256) {
                int n = (int)std::min<size_t>(256, total - i);
                while (nx < s.size() && nx * fb <= (i + n) * bps) { e.pushColumn(nx * fb, std::vector<float>(s[nx].rbegin(), s[nx].rend())); nx++; }
                e.process(&L[i], &R[i], n, i * bps, bps);
            }
            if (!e.kicksFired()) continue;
            writeWav(cases[ci].name + "_D_v2now_with_kick.wav", L, R);
            printf("   kick demo %s: sample %s, %d kicks\n", cases[ci].name.c_str(), have ? "loaded" : "missing (synthetic)", e.kicksFired());
            break;
        }
    }
    printf("SUMMARY: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
