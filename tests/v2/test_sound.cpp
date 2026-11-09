// Tests for the v2 sound engine core (v2Sound.h). Renders WAVs to the scratchpad.
#include "v2Sound.h"
#include <cstdio>
#include <fstream>
#include <sys/stat.h>

using namespace forms2;
using namespace forms2::snd;

static const std::string OUT = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2/sound/";
static const double SR = 48000, BPM = 120, BPS = BPM / 60.0 / SR, FPS = 60;
static int fails = 0, passes = 0;
static void check(bool ok, const std::string& name, const std::string& info) {
    (ok ? passes : fails)++;
    printf("[%s] %s  %s\n", ok ? "PASS" : "FAIL", name.c_str(), info.c_str());
}
static std::string fmt(const char* f, double a, double b = 0, double c = 0, double d = 0) { char buf[256]; snprintf(buf, sizeof buf, f, a, b, c, d); return buf; }

// ---------------------------------------------------------------- wav writer (16-bit stereo)
static void writeWav(const std::string& name, const std::vector<float>& L, const std::vector<float>& R) {
    std::ofstream f(OUT + name, std::ios::binary);
    auto u32 = [&](uint32_t v) { f.write((const char*)&v, 4); };
    auto u16 = [&](uint16_t v) { f.write((const char*)&v, 2); };
    uint32_t n = (uint32_t)L.size(), bytes = n * 4;
    f.write("RIFF", 4); u32(36 + bytes); f.write("WAVEfmt ", 8); u32(16); u16(1); u16(2); u32((uint32_t)SR); u32((uint32_t)SR * 4); u16(4); u16(16);
    f.write("data", 4); u32(bytes);
    for (uint32_t i = 0; i < n; i++) for (float x : {L[i], R[i]}) { int16_t v = (int16_t)std::lround(std::clamp(x, -1.f, 1.f) * 32767); f.write((const char*)&v, 2); }
}

// ---------------------------------------------------------------- render harness
struct Render { std::vector<float> L, R, add; };
using ColFn = std::function<std::vector<float>(double beat)>;
static Render render(SoundSettings st, double seconds, ColFn col, std::vector<Event> evs = {}, bool stepped = false) {
    SoundEngine e; e.s = st; e.s.interpolate = !stepped; e.prepare(SR, 256);
    e.pushEvents(evs);
    Render r; size_t total = (size_t)(seconds * SR);
    r.L.resize(total); r.R.resize(total); r.add.resize(total);
    double frameBeat = BPM / 60.0 / FPS; double nextCol = 0;
    for (size_t i = 0; i < total; i += 256) {
        int n = (int)std::min<size_t>(256, total - i);
        double b = i * BPS;
        while (nextCol <= b + n * BPS + frameBeat) { e.pushColumn(nextCol, col(nextCol)); nextCol += frameBeat; }  // columns one frame ahead
        e.process(&r.L[i], &r.R[i], n, b, BPS);
        std::copy(e.additiveStem().begin(), e.additiveStem().begin() + n, r.add.begin() + (long)i);
    }
    return r;
}
static SoundSettings base(EngineKind k) {
    SoundSettings s = SoundSettings::v2Modern(); s.engine = k; s.pitchBalance = 0; s.normalise = false; s.limit = false; s.additiveGain = 1; return s;
}
static std::vector<float> oneRow(int r, float a, int rows = 85) { std::vector<float> v((size_t)rows, 0.f); v[(size_t)r] = a; return v; }

// ---------------------------------------------------------------- measurement
static double toneAmp(const std::vector<float>& x, size_t a, size_t n, double f) {  // Hann Goertzel
    cplx acc = 0; double ws = 0;
    for (size_t i = 0; i < n; i++) { double w = 0.5 - 0.5 * std::cos(kTwoPi * i / n); ws += w; acc += w * (double)x[a + i] * std::polar(1.0, -kTwoPi * f * i / SR); }
    return 2 * std::abs(acc) / ws;
}
static double peakFreq(const std::vector<float>& x, size_t a, double fmin = 20) {
    size_t N = 1 << 15; std::vector<cplx> X(N);
    for (size_t i = 0; i < N; i++) X[i] = (0.5 - 0.5 * std::cos(kTwoPi * i / N)) * x[a + i];
    fft(X, false);
    size_t best = (size_t)(fmin * N / SR); double bm = 0;
    for (size_t k = best; k < N / 2 - 1; k++) if (std::abs(X[k]) > bm) { bm = std::abs(X[k]); best = k; }
    double l = std::log(std::abs(X[best - 1]) + 1e-20), c = std::log(std::abs(X[best]) + 1e-20), r = std::log(std::abs(X[best + 1]) + 1e-20);
    return (best + 0.5 * (l - r) / (l - 2 * c + r)) * SR / N;
}
static double rms(const std::vector<float>& x, size_t a, size_t n) { double s = 0; for (size_t i = 0; i < n; i++) s += (double)x[a + i] * x[a + i]; return std::sqrt(s / n); }
static double db(double x) { return 20 * std::log10(std::max(x, 1e-12)); }
static double bandEnergy(const std::vector<float>& x, size_t a, size_t n, double f0, double f1) {  // sum of |X|^2 in band, Hann
    size_t N = 1; while (N < n) N <<= 1;
    std::vector<cplx> X(N, 0);
    for (size_t i = 0; i < n; i++) X[i] = (0.5 - 0.5 * std::cos(kTwoPi * i / n)) * x[a + i];
    fft(X, false); double s = 0;
    for (size_t k = (size_t)(f0 * N / SR); k <= (size_t)(f1 * N / SR) && k < N / 2; k++) s += std::norm(X[k]);
    return s;
}

int main() {
    mkdir((OUT.substr(0, OUT.size() - 7)).c_str(), 0755); mkdir(OUT.c_str(), 0755);
    const int ROW880 = 45;  // -24 + 45 = 21 semitones over C4 = 880 Hz
    double f880 = 261.6256 * std::pow(2.0, 21 / 12.0);

    // 1. single row -> sine at the right frequency, both engines
    for (auto k : {EngineKind::Bank, EngineKind::Ifft}) {
        auto r = render(base(k), 1.2, [&](double) { return oneRow(ROW880, 0.5f); });
        double f = peakFreq(r.L, 8000), a = toneAmp(r.add, 8000, 32768, f880);
        std::string nm = k == EngineKind::Bank ? "bank" : "ifft";
        check(std::abs(f - f880) < 0.3 && std::abs(a - 0.5) < 0.01, "single row sine (" + nm + ")", fmt("f=%.3f Hz (want %.3f) amp=%.4f (want 0.5)", f, f880, a));
        writeWav("single_row_" + nm + ".wav", r.L, r.R);
    }

    // 2. interpolation removes 60 Hz frame stepping
    {
        auto col = [&](double b) { double t = b / (BPM / 60.0); return oneRow(ROW880, (float)(0.3 + 0.25 * std::sin(kTwoPi * 2.3 * t))); };
        auto si = render(base(EngineKind::Bank), 2.0, col), ss = render(base(EngineKind::Bank), 2.0, col, {}, true);
        auto side = [&](const Render& r) { double e = 0; for (int h = 1; h <= 3; h++) e += std::pow(toneAmp(r.add, 16000, 65536, f880 + 60 * h), 2) + std::pow(toneAmp(r.add, 16000, 65536, f880 - 60 * h), 2); return std::sqrt(e); };
        double car = toneAmp(si.add, 16000, 65536, f880);
        double di = db(side(si) / car), ds = db(side(ss) / car);
        check(di < ds - 20 && di < -60, "interpolation removes 60 Hz stepping", fmt("sidebands at f0+-60k: interpolated %.1f dBc, stepped %.1f dBc", di, ds));
        writeWav("stepping_interpolated.wav", si.L, si.R); writeWav("stepping_stepped.wav", ss.L, ss.R);
    }

    // 3. IFFT vs bank A/B: chord in mid/high register
    {
        std::vector<int> rows = {40, 44, 47, 52, 59, 64, 71, 76, 83};
        auto col = [&](double) { std::vector<float> v(85, 0.f); for (size_t i = 0; i < rows.size(); i++) v[(size_t)rows[i]] = 0.1f + 0.02f * i; return v; };
        auto rb = render(base(EngineKind::Bank), 1.5, col), ri = render(base(EngineKind::Ifft), 1.5, col);
        SoundEngine tmp; tmp.s = base(EngineKind::Bank); tmp.prepare(SR, 256);
        double maxDb = 0, maxHz = 0;
        for (int r : rows) {
            double f = tmp.rowFreq(r);
            double ab = toneAmp(rb.add, 16000, 32768, f), ai = toneAmp(ri.add, 16000, 32768, f);
            maxDb = std::max(maxDb, std::abs(db(ai / ab)));
            std::vector<float> one = ri.add;  // frequency check on the top partial region
            (void)one;
        }
        // residual: what IFFT adds outside the partials (truncated kernel leakage)
        double tot = bandEnergy(ri.add, 16000, 32768, 20, 20000), part = 0;
        for (int r : rows) { double f = tmp.rowFreq(r); part += bandEnergy(ri.add, 16000, 32768, f - 15, f + 15); }
        double resid = db(std::sqrt(std::max(tot - part, 1e-30) / tot));
        double fTop = peakFreq(ri.add, 16000, 3000), fWant = tmp.rowFreq(83); maxHz = std::abs(fTop - fWant);
        double rmsd = db(rms(ri.add, 16000, 32768) / rms(rb.add, 16000, 32768));
        check(maxDb < 0.1 && maxHz < 0.5 && resid < -50, "A/B ifft vs bank (mid/high)", fmt("max partial amp diff %.4f dB, top partial freq err %.3f Hz, off-partial residual %.1f dB, rms diff %.4f dB", maxDb, maxHz, resid, rmsd));
        writeWav("ab_chord_bank.wav", rb.L, rb.R); writeWav("ab_chord_ifft.wav", ri.L, ri.R);
    }

    // 4. low hybrid keeps bass clean (two adjacent semitones near 55 Hz)
    {
        auto col = [&](double) { std::vector<float> v(85, 0.f); v[0] = 0.3f; v[1] = 0.3f; return v; };  // C2-ish 65.4, 69.3 Hz
        SoundSettings hy = base(EngineKind::Ifft), no = hy; no.splitHz = 0;
        auto rb = render(base(EngineKind::Bank), 1.5, col), rh = render(hy, 1.5, col), rn = render(no, 1.5, col);
        auto err = [&](const Render& r) { double tot = 0; for (size_t i = 16000; i < 16000 + 32768; i++) { double d = r.add[i] - rb.add[i]; tot += d * d; } return db(std::sqrt(tot / 32768) / rms(rb.add, 16000, 32768)); };
        double eh = err(rh), en = err(rn);
        check(eh < -80, "low hybrid keeps bass clean", fmt("error vs bank: hybrid %.1f dB, all-IFFT %.1f dB (bass partials %.1f / %.1f Hz)", eh, en, 261.6256 * std::pow(2, -2.0), 261.6256 * std::pow(2, -23 / 12.0)));
    }

    // 5. kick: attack < 1 ms, pitch drop
    {
        Event k; k.id = 1; k.kind = EventKind::Hit; k.look = "kick"; k.start = 0.5; k.transient = true;
        k.p = {{"drumVoice", 1}, {"drumClick", 0}, {"drumDrive", 0}, {"drumPitchStart", 160}, {"drumPitchEnd", 48}, {"drumDecay", 0.45}};
        auto r = render(base(EngineKind::Bank), 1.0, [](double) { return std::vector<float>(85, 0.f); }, {k});
        size_t s0 = (size_t)(0.5 / BPS); size_t on = s0; while (on < r.L.size() && std::abs(r.L[on]) < 1e-6) on++;
        double mx = 0; for (size_t i = on; i < on + 2400; i++) mx = std::max(mx, (double)std::abs(r.L[i]));
        // envelope rise: first sample of |x| reaching 90% of the peak of the first cycle's envelope (sine starts at 0, so use a
        // quadrature-free bound: max over the first 1 ms vs the expected envelope there)
        double m1 = 0; for (size_t i = on; i < on + 48; i++) m1 = std::max(m1, (double)std::abs(r.L[i]));
        double sinBound = std::sin(kTwoPi * 160 * 0.001);  // phase reached after 1 ms at 160 Hz
        double envAt1ms = m1 / std::max(1e-9, sinBound) / (mx);
        auto zcFreq = [&](size_t a, size_t n) { int z = 0; size_t first = 0, last = 0; for (size_t i = a + 1; i < a + n; i++) if (r.L[i - 1] < 0 && r.L[i] >= 0) { if (!z) first = i; last = i; z++; } return z > 1 ? (z - 1) * SR / (double)(last - first) : 0.0; };
        double fe = zcFreq(on, 960), fl = zcFreq(on + 9600, 9600);
        double onsetErrMs = ((double)on - s0) / SR * 1000;
        check(envAt1ms > 0.9 && fe > fl * 1.6 && fl < 60, "kick attack < 1 ms and pitch drop", fmt("envelope at 1 ms = %.2f of peak, onset offset %.3f ms, freq 0-20 ms %.1f Hz -> 200-400 ms %.1f Hz", envAt1ms, onsetErrMs, fe, fl));
        writeWav("kick_single.wav", r.L, r.R);
    }

    // 6. spectral sidechain ducks the drum's band and recovers
    {
        auto pad = [&](double) { std::vector<float> v(85, 0.f); v[9] = 0.1f; v[69] = 0.1f; return v; };  // 110 Hz and 3.5 kHz partials
        auto padDemo = [&](double) { std::vector<float> v(85, 0.f); for (int i = 0; i < 85; i += 3) v[(size_t)i] = 0.05f; return v; };
        std::vector<Event> evs;
        for (int i = 0; i < 4; i++) {
            Event k; k.id = 10 + i; k.kind = EventKind::Hit; k.look = "kick"; k.start = 1.0 + i * 2; k.transient = true;
            k.p = {{"drumVoice", 2}, {"scAmount", 0.8}, {"scAttack", 2}, {"scRelease", 120}};
            evs.push_back(k);
        }
        auto r = render(base(EngineKind::Bank), 5.0, pad, evs);
        size_t hit = (size_t)(1.0 / BPS);
        double before = bandEnergy(r.add, hit - 4800, 2400, 40, 200), during = bandEnergy(r.add, hit + 240, 2400, 40, 200);
        double after = bandEnergy(r.add, hit + 28800, 2400, 40, 200);
        double hiB = bandEnergy(r.add, hit - 4800, 2400, 2000, 6000), hiD = bandEnergy(r.add, hit + 240, 2400, 2000, 6000);
        double dDuck = 10 * std::log10(during / before), dRec = 10 * std::log10(after / before), dHi = 10 * std::log10(hiD / hiB);
        check(dDuck < -6 && std::abs(dRec) < 1 && std::abs(dHi) < 1.5, "spectral sidechain duck + release", fmt("40-200 Hz additive: during %.1f dB, +0.6 s %.2f dB; 2-6 kHz during %.2f dB", dDuck, dRec, dHi));
        auto rd = render(base(EngineKind::Bank), 5.0, padDemo, evs);
        writeWav("demo_kicks_sidechain.wav", rd.L, rd.R);
        for (auto& e : evs) e.p["drumVoice"] = 1;
        auto rm = render(base(EngineKind::Bank), 5.0, padDemo, evs);
        writeWav("demo_kicks_mix.wav", rm.L, rm.R);
    }

    // 7. pitch balance: 0 flat, 1 weighted
    {
        SoundEngine t; t.s = base(EngineKind::Bank); t.prepare(SR, 256);
        int lo = 9, hi = 45;  // ~110 Hz, 880 Hz
        auto col = [&](double) { std::vector<float> v(85, 0.f); v[(size_t)lo] = 0.2f; v[(size_t)hi] = 0.2f; return v; };
        SoundSettings s0 = base(EngineKind::Bank), s1 = s0; s1.pitchBalance = 1;
        auto r0 = render(s0, 1.0, col), r1 = render(s1, 1.0, col);
        double flo = t.rowFreq(lo), fhi = t.rowFreq(hi);
        double d0 = db(toneAmp(r0.add, 8000, 32768, flo) / toneAmp(r0.add, 8000, 32768, fhi));
        double d1 = db(toneAmp(r1.add, 8000, 32768, flo) / toneAmp(r1.add, 8000, 32768, fhi));
        double want = aWeightDb(fhi) - aWeightDb(flo);
        check(std::abs(d0) < 0.05 && std::abs(d1 - want) < 0.2, "pitch balance 0 flat / 1 weighted", fmt("low/high level: balance 0 %.2f dB, balance 1 %.2f dB (curve %.2f dB) at %.1f/%.1f Hz", d0, d1, want, flo) + fmt(" (%.1f Hz)", fhi));
    }

    // 8. loudness normalisation per scene
    {
        auto mk = [&](float a) { return [a](double) { std::vector<float> v(85, 0.f); for (int i = 30; i < 70; i += 5) v[(size_t)i] = a; return v; }; };
        SoundSettings s = base(EngineKind::Bank); s.normalise = true; s.normTimeS = 1.0;
        auto a = render(s, 10, mk(0.05f)), b = render(s, 10, mk(0.4f));
        double ra = rms(a.L, (size_t)(8 * SR), (size_t)SR), rb = rms(b.L, (size_t)(8 * SR), (size_t)SR);
        check(std::abs(db(ra / rb)) < 0.5, "loudness normalisation", fmt("inputs 18 dB apart -> outputs %.2f dB apart (rms %.4f / %.4f)", db(ra / rb), ra, rb));
    }

    // 9. limiter keeps peaks <= ceiling
    {
        SoundSettings s = base(EngineKind::Bank); s.limit = true; s.ceiling = 0.891;
        std::vector<Event> evs;
        for (int i = 0; i < 6; i++) { Event k; k.id = 100 + i; k.kind = EventKind::Hit; k.look = i % 2 ? "snare" : "kick"; k.start = 0.25 + i * 0.5; k.p = {{"drumVoice", 1}, {"drumLevel", 1}, {"drumDrive", 0.8}}; evs.push_back(k); }
        auto r = render(s, 2.0, [](double) { std::vector<float> v(85, 0.f); for (int i = 0; i < 85; i += 2) v[(size_t)i] = 0.2f; return v; }, evs);
        double pk = 0; for (size_t i = 0; i < r.L.size(); i++) pk = std::max(pk, (double)std::max(std::abs(r.L[i]), std::abs(r.R[i])));
        double pre = 0; for (float x : r.add) pre = std::max(pre, (double)std::abs(x));
        check(pk <= 0.891 + 1e-6, "limiter ceiling", fmt("peak out %.4f (ceiling 0.891), additive peak before limiter %.2f", pk, pre));
        writeWav("limiter_loud.wav", r.L, r.R);
    }

    // 10. determinism
    {
        std::vector<Event> evs; Event k; k.id = 7; k.kind = EventKind::Hit; k.look = "snare"; k.start = 0.3; k.seed = 99; k.p = {{"drumVoice", 2}}; evs.push_back(k);
        Event h = k; h.id = 8; h.look = "hat"; h.start = 0.6; evs.push_back(h);
        SoundSettings s = base(EngineKind::Ifft); s.normalise = true; s.limit = true; s.pitchBalance = 0.5;
        auto col = [](double b) { std::vector<float> v(85, 0.f); v[(size_t)(int)(b * 10) % 85] = 0.3f; return v; };
        auto a = render(s, 1.0, col, evs), b = render(s, 1.0, col, evs);
        check(a.L == b.L && a.R == b.R, "determinism", fmt("%.0f samples identical", (double)a.L.size()));
    }

    // 11. note voice (hybrid note events): exact pitch
    {
        Event n; n.id = 50; n.kind = EventKind::Note; n.degree = 9; n.start = 0.1; n.length = 2; n.p = {{"noteVoice", 1}};
        SoundSettings s = base(EngineKind::Bank);
        auto r = render(s, 1.5, [](double) { return std::vector<float>(85, 0.f); }, {n});
        Scale sc; double want = 261.6256 * std::pow(2.0, sc.semitonesOf(9) / 12.0), f = peakFreq(r.L, 9600);
        check(std::abs(f - want) < 0.3, "note event voice exact pitch", fmt("f=%.3f Hz (want %.3f)", f, want));
    }

    // demos: chord column and a sweep, both engines; all drum types
    {
        SoundSettings s = SoundSettings::v2Modern(); s.engine = EngineKind::Ifft;
        auto chord = [](double) { std::vector<float> v(85, 0.f); for (int r : {12, 24, 28, 31, 36, 40, 43, 48}) v[(size_t)r] = 0.5f; return v; };
        auto c = render(s, 4.0, chord); writeWav("demo_chord_ifft.wav", c.L, c.R);
        s.engine = EngineKind::Bank; c = render(s, 4.0, chord); writeWav("demo_chord_bank.wav", c.L, c.R);
        auto sweep = [](double b) { std::vector<float> v(85, 0.f); double pos = std::fmod(b * 5, 84.0); int r = (int)pos; double t = pos - r; v[(size_t)r] = (float)(1 - t); v[(size_t)r + 1] = (float)t; return v; };
        c = render(s, 8.0, sweep); writeWav("demo_sweep_bank.wav", c.L, c.R);
        s.engine = EngineKind::Ifft; c = render(s, 8.0, sweep); writeWav("demo_sweep_ifft.wav", c.L, c.R);
        std::vector<Event> evs; const char* looks[] = {"kick", "snare", "hat", "tick", "tom", "taiko", "zap"};
        for (int i = 0; i < 7; i++) { Event k; k.id = 200 + i; k.kind = EventKind::Hit; k.look = looks[i]; k.start = 0.5 + i; k.p = {{"drumVoice", 1}}; evs.push_back(k); }
        c = render(s, 4.5, [](double) { return std::vector<float>(85, 0.f); }, evs); writeWav("demo_drum_types.wav", c.L, c.R);
    }

    printf("\nSUMMARY: %d passed, %d failed\n", passes, fails);
    return fails ? 1 : 0;
}
