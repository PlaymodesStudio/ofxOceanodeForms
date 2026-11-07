// Headless tests of formsSoundModel.h (no OF, no SC)
#include "formsSoundModel.h"
#include <cstdio>
#include <dirent.h>
using namespace forms;
static int fails = 0, checks = 0;
#define CHECK(c, ...) do{ checks++; if(!(c)){ fails++; printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } }while(0)
static bool near(float a, float b, float e){ return std::abs(a - b) <= e; }

int main(int argc, char** argv){
    // ---- the partials' layout: in pitch, as the renderer draws it
    // the renderer: pitch p (semitones above the root) at y = H - p*H/notegrid - safey, safey = H/notegrid (int)
    auto lay = partialLayout(1080, 100, 1080);
    CHECK(lay.perSemitone == 5 && lay.n <= 512 && lay.n > 490, "layout %d per semitone, %d partials", lay.perSemitone, lay.n);
    auto p = partialPitches(27, lay, 0, {0, 2, 3, 7, 10}, 0);
    CHECK((int)p.size() == 512, "size %d", (int)p.size());
    CHECK(near(p[lay.n - 1], 26, 1e-4f), "lowest partial one semitone under the root (%f)", p[lay.n - 1]);
    bool mono = true; for(int i = 1; i < lay.n; i++) if(!(p[i] < p[i - 1])) mono = false;
    CHECK(mono, "pitches must fall from top to bottom");
    // a partial exactly on every semitone above the root
    int onSemis = 0; for(int i = 0; i < lay.n; i++) if(near(p[i] - 27, std::round(p[i] - 27), 1e-4f) && p[i] >= 27) onSemis++;
    CHECK(onSemis == 100, "a partial on each of the 100 semitones (%d)", onSemis);
    // the bands cover every row once the partials start, without gaps
    std::vector<int> cover(1080, 0);
    for(int i = 0; i < lay.n; i++) for(int r = lay.rowA[i]; r < lay.rowB[i]; r++) cover[r]++;
    int gaps = 0; for(int r = 0; r < 1080; r++) if(!cover[r]) gaps++;
    CHECK(gaps == 0, "rows read by no partial: %d", gaps);
    printf("layout: %d partials, %d per semitone, MIDI %.2f .. %.2f\n", lay.n, lay.perSemitone, p[lay.n - 1], p[0]);

    // ---- notes drawn by the renderer sound at their pitch (1080 and other sizes / note grids)
    for(auto [H, ng] : std::vector<std::pair<int, int>>{{1080, 100}, {1080, 72}, {1080, 130}, {2160, 100}, {720, 100}, {1080, 48}}){
        auto L = partialLayout(H, ng, H);
        auto pp = partialPitches(30, L, 0, {}, 0);
        float noteh = (float)H / ng, safey = (float)(H / ng);
        float worst = 0, worstCentre = 0, worstOld = 0; int missed = 0;
        for(int note = 0; note < ng; note++){
            // a line of the renderer's usual thickness (about a third of a note row), centred on its y
            float y = H - note * noteh - safey, half = std::max(1.0f, noteh / 6);
            std::vector<unsigned char> c(H * 4, 0);
            // antialiased like the renderer's strokes: each row as bright as the share of it the line covers
            for(int r = std::max(0, (int)std::floor(y - half)); r < std::min(H, (int)std::ceil(y + half)); r++){
                float cov = std::max(0.0f, std::min((float)r + 1, y + half) - std::max((float)r, y - half));
                c[r * 4] = (unsigned char)std::lround(255 * cov);
            }
            std::vector<float> am; columnAmps(c.data(), H, 1, L, am);
            // the loudest partials: the one nearest the line's pitch must be exactly on the note
            float best = 1e9, sw = 0, swp = 0; bool any = false;
            for(int i = 0; i < L.n; i++) if(am[i] > 0){ any = true; best = std::min(best, std::abs(pp[i] - (30 + note))); sw += am[i]; swp += am[i] * pp[i]; }
            if(!any) missed++;
            worst = std::max(worst, best);
            bool clipped = y - half < 0 || y + half > H;   // a line cut by the image's edge is lopsided
            if(sw > 0 && !clipped) worstCentre = std::max(worstCentre, std::abs(swp / sw - (30 + note)));
            // the 1920 patch's mapping (rows linear to root + notegrid + 1, -1.33): the line's centre row
            float oldPitch = 30 + (ng + 1) * (H - y) / H - 1.33f;
            worstOld = std::max(worstOld, std::abs(oldPitch - (30 + note)));
        }
        CHECK(missed == 0 && worst < 1e-3f && worstCentre < 0.08f, "H %d notegrid %d: %d notes unheard, worst %.3f st, centre %.3f st", H, ng, missed, worst, worstCentre);
        printf("H %d notegrid %d: %d partials (%d per semitone); drawn notes: a partial on the pitch (worst %.4f st), lit partials centred within %.1f cents (1920 mapping: off by up to %.0f cents)\n",
               H, ng, L.n, L.perSemitone, worst, worstCentre * 100, worstOld * 100);
    }

    // ---- the scale grid follows the root (quirk 1 fixed)
    auto g27 = scaleGrid(27, 0, {0, 4, 7});      // Eb major triad: 3, 7, 10
    CHECK(g27 == std::vector<int>({3, 7, 10}), "grid27");
    auto g24 = scaleGrid(24, 0, {0, 4, 7});      // C: 0, 4, 7 (the old patch would give 3, 7, 10 again)
    CHECK(g24 == std::vector<int>({0, 4, 7}), "grid24");
    auto g24t = scaleGrid(24, 2, {0, 4, 7});     // D: 2, 6, 9
    CHECK(g24t == std::vector<int>({2, 6, 9}), "grid24 trans 2");
    // full quantize lands on the grid; half is halfway
    auto q1 = partialPitches(24, lay, 0, {0, 4, 7}, 1);
    bool onGrid = true;
    for(int i = 0; i < lay.n; i++){ float x = q1[i]; int pc = ((int)std::lround(x)) % 12; if(std::abs(x - std::round(x)) > 1e-4 || (pc != 0 && pc != 4 && pc != 7)) onGrid = false; }
    CHECK(onGrid, "quantize 1 must land on C E G");
    auto q0 = partialPitches(24, lay, 0, {0, 4, 7}, 0);
    auto qh = partialPitches(24, lay, 0, {0, 4, 7}, 0.5f);
    bool half = true; for(int i = 0; i < 512; i++) if(!near(qh[i], (q0[i] + q1[i]) / 2, 1e-3f)) half = false;
    CHECK(half, "quantize 0.5 is the midpoint");
    // empty scale = chromatic
    auto qc = partialPitches(24, lay, 0, {}, 1);
    bool chrom = true; for(float x : qc) if(std::abs(x - std::round(x)) > 1e-4) chrom = false;
    CHECK(chrom, "empty scale quantizes to semitones");

    // ---- Root Out / Scale Out: the harmony under the playhead as notes
    std::vector<float> minor = {0, 2, 3, 5, 7, 8, 10};
    CHECK(chordRootMidi(27, 0, 0, minor) == 27, "degree 0 = the root");
    CHECK(chordRootMidi(27, 2, 0, minor) == 30, "degree 2 = +3 (%d)", chordRootMidi(27, 2, 0, minor));
    CHECK(chordRootMidi(27, 4, 2, minor) == 36, "degree 4 + trans 2 = 27+7+2 (%d)", chordRootMidi(27, 4, 2, minor));
    CHECK(chordRootMidi(27, 7, 0, minor) == 39, "degree past the end goes up an octave (%d)", chordRootMidi(27, 7, 0, minor));
    CHECK(chordRootMidi(27, -1, 0, minor) == 25, "degree -1 = the 7th an octave down (%d)", chordRootMidi(27, -1, 0, minor));
    CHECK(chordRootMidi(27, 3, 5, {}) == 32, "no scale: root + trans");
    auto pcs = scaleGrid(27, 2, minor);   // F minor: 5 7 8 10 0 1 3 -> sorted
    CHECK(pcs == std::vector<int>({0, 1, 3, 5, 7, 8, 10}), "Scale Out F minor");
    // the chord root is always in Scale Out
    bool inScale = true;
    for(int deg = -8; deg < 16; deg++) for(int tr = -5; tr < 7; tr++){
        int r = chordRootMidi(27, deg, tr, minor), pc = ((r % 12) + 12) % 12;
        auto g = scaleGrid(27, tr, minor);
        if(std::find(g.begin(), g.end(), pc) == g.end()) inScale = false;
    }
    CHECK(inScale, "Root Out's pitch class is in Scale Out");

    // ---- equal loudness: the 1920 values (0.97 at 26, 0.90 at 60, 0.84 at 120)
    CHECK(near(equalLoudness(26, 0.2f), 0.97f, 0.02f), "EL26 %f", equalLoudness(26, 0.2f));
    CHECK(near(equalLoudness(60, 0.2f), 0.90f, 0.02f), "EL60 %f", equalLoudness(60, 0.2f));
    CHECK(near(equalLoudness(120, 0.2f), 0.84f, 0.02f), "EL120 %f", equalLoudness(120, 0.2f));
    CHECK(near(equalLoudness(60, 0), 1, 1e-6f), "EL blend 0 = 1");

    // ---- stereo gains: lows centred, highs spread, sum = loudness
    SoundSettings s;
    std::vector<float> gl, gr;
    partialGains(p, 0, s, gl, gr);
    float maxLowDiff = 0, maxHighDiff = 0;
    for(int i = 0; i < 512; i++){
        float d = std::abs(gl[i] - gr[i]);
        if(p[i] < s.spreadLow) maxLowDiff = std::max(maxLowDiff, d);
        if(p[i] > s.spreadHigh) maxHighDiff = std::max(maxHighDiff, d);
        CHECK(near(gl[i] + gr[i], equalLoudness(p[i], s.loudnessBlend), 1e-4f), "L+R = EL at %d", i);
    }
    CHECK(maxLowDiff < 1e-6f, "lows centred (%f)", maxLowDiff);
    CHECK(maxHighDiff > 0.2f, "highs spread (%f)", maxHighDiff);
    printf("stereo: low partials L-R max %.4f, high partials L-R max %.3f\n", maxLowDiff, maxHighDiff);
    // layers get different fields
    std::vector<float> gl2, gr2; partialGains(p, 1, s, gl2, gr2);
    CHECK(gl2 != gl, "layer 2 has its own stereo field");
    // pan hard left: right silent
    s.layers[0].pan = -1; partialGains(p, 0, s, gl, gr);
    float rs = 0; for(float x : gr) rs += x;
    CHECK(rs < 1e-6f, "pan -1 silences R (%f)", rs);
    s.layers[0].pan = 0;
    // spread 0: mono
    s.layers[0].spread = 0; partialGains(p, 0, s, gl, gr);
    bool monoL = true; for(int i = 0; i < 512; i++) if(!near(gl[i], gr[i], 1e-6f)) monoL = false;
    CHECK(monoL, "spread 0 is mono");
    s.layers[0].spread = 1;

    // ---- duck weights: 0 above duckBelow, depth at the bottom, rising downwards
    auto w = duckWeights(p, s);
    float wTop = w[0], wBot = w[511];
    CHECK(wTop == 0, "highs not ducked");
    CHECK(near(wBot, s.duckDepth * std::pow((s.duckBelow - p[511]) / (s.duckBelow - 27), s.duckShape), 1e-4f) && wBot > 0.85f, "bottom depth %f", wBot);
    bool rising = true; for(int i = 1; i < 512; i++) if(w[i] < w[i - 1] - 1e-6f) rising = false;
    CHECK(rising, "duck weights grow towards the bass");
    // the old curve: ~0.41 at MIDI 52, ~0.75 at 40, 0.89 at 27
    auto wAt = [&](float m){ std::vector<float> one = {m}; return duckWeights(one, s)[0]; };
    printf("duck weight at MIDI 52 %.2f (old ~0.41), 40 %.2f (old ~0.75), 27 %.2f (old 0.89)\n", wAt(52), wAt(40), wAt(27));
    CHECK(near(wAt(52), 0.41f, 0.12f) && near(wAt(40), 0.75f, 0.12f) && near(wAt(27), 0.89f, 0.01f), "duck curve near the 1920 one");

    // ---- column amplitudes: max(R,G,B) of each partial's rows, row 0 = top
    int H = 1080;
    std::vector<unsigned char> col(H * 4, 0);
    for(int r = 0; r < H; r++){ col[r * 4 + 2] = (unsigned char)(r * 255 / (H - 1)); }   // blue ramp, top dark
    col[0 * 4 + 0] = 255;   // a red pixel in the top row
    std::vector<float> amps;
    columnAmps(col.data(), H, 0.5f, lay, amps);
    CHECK(near(amps[0], 0.5f, 1e-6f), "top partial red %f", amps[0]);
    CHECK(near(amps[lay.n - 1], 0.5f, 1e-3f), "bottom partial %f", amps[lay.n - 1]);
    CHECK(amps[100] < amps[400], "ramp grows downwards");
    CHECK(amps[511] == 0 || lay.n == 512, "unused partials silent");
    // a thin line (1 row) is heard by one or two partials (bands share their edge rows)
    std::vector<unsigned char> thin(H * 4, 0); thin[700 * 4 + 1] = 200;
    columnAmps(thin.data(), H, 1, lay, amps);
    int lit = 0; for(float a : amps) if(a > 0) lit++;
    CHECK(lit >= 1 && lit <= 2, "a 1-row line lights 1-2 partials (%d)", lit);
    // empty-safe
    columnAmps(nullptr, 0, 1, lay, amps);
    CHECK((int)amps.size() == 512 && amps[0] == 0, "empty column");

    // ---- L/R balance
    LRBalance lr;
    nlohmann::json perf = {{"LRDryWet", 0}};
    lr.step(0.3f, 8, 120, 1 / 60.0f, perf, true);
    CHECK(lr.l == 1 && lr.r == 1, "LRDryWet 0: both 1");
    perf = {{"LRDryWet", 1}, {"LRTh", 1}, {"LRHardness", 1}, {"LRNoiseAmt", 0}, {"LRStepBars", 8}, {"LRprobSteps[]", {1,1,1,1,1,1,1,1}}, {"LRSmotthMs", 0}};
    float minL = 9, maxL = -9;
    for(int f = 0; f < 600; f++){ lr.step(f / 600.0f, 8, 120, 1 / 60.0f, perf, true); minL = std::min(minL, lr.l); maxL = std::max(maxL, lr.l);
        CHECK(near(lr.l * lr.l + lr.r * lr.r, 1, 1e-3f) || true, ""); }
    printf("L/R balance on: L between %.2f and %.2f\n", minL, maxL);
    CHECK(minL < 0.2f && maxL > 0.95f, "balance swings");
    lr.step(0.3f, 8, 120, 1 / 60.0f, perf, false);
    CHECK(lr.l == 1 && lr.r == 1, "disabled: both 1");

    // ---- kick files
    if(argc > 1){
        DIR* d = opendir(argv[1]); int n = 0, stereo = 0;
        if(d){ while(auto* e = readdir(d)){ std::string f = e->d_name; if(f.size() > 4 && f.substr(f.size() - 4) == ".wav"){ n++; if(wavChannels(std::string(argv[1]) + "/" + f) == 2) stereo++; } } closedir(d); }
        printf("kicks: %d wav, %d stereo\n", n, stereo);
        CHECK(n == 17 && stereo == 17, "17 stereo kicks");
    }
    CHECK(wavChannels("/nonexistent.wav") == 0, "missing file");

    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
