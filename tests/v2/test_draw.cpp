//
//  test_draw.cpp — headless tests of the v2 CPU reference renderer (v2Draw.h)
//  Synthetic events only (no sequencer). Writes demo PPM frames for inspection.
//

#include "v2Draw.h"
#include <cstdio>
#include <string>
#include <sys/stat.h>

using namespace forms2;
using namespace forms2::draw;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static const char* kFrames = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2/frames/";

static Renderer makeR(int w = 400, int h = 300, double zoom = 16){
    Renderer r; r.frame.width = w; r.frame.height = h; r.frame.headerX = w / 2.0;
    r.layers = {LayerDef{zoom, "lighten", 1}};
    return r;
}
static Event block(double start, double len, double lo, double hi, int layer = 0){
    Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = start; e.length = len; e.layer = layer;
    e.pitched = false; e.p = {{"heightLow", lo}, {"heightHigh", hi}, {"width", len}, {"decay", 1000}, {"softness", 0}};
    return e;
}
static float colMax(const Renderer& r, int x){ auto c = r.column(x); float m = 0; for(float v : c) m = std::max(m, v); return m; }
// first lit column from the left
static int leadingEdge(const Renderer& r){ for(int x = 0; x < r.frame.width; x++) if(colMax(r, x) > 0.5f) return x; return -1; }
static int peakRow(const std::vector<float>& c){ int b = 0; for(size_t i = 0; i < c.size(); i++) if(c[i] > c[(size_t)b]) b = (int)i; return b; }
static int countPeaks(const std::vector<float>& c, float thr){
    int n = 0; for(size_t i = 1; i + 1 < c.size(); i++) if(c[i] > thr && c[i] >= c[i - 1] && c[i] > c[i + 1]) n++; return n;
}

// ---------------------------------------------------------------- tests
static void testHeaderCrossing(){
    for(double zoom : {8.0, 16.0, 40.0}) for(double depth : {0.2, 1.0, 3.0}){
        Renderer r = makeR(400, 100, zoom);
        Event e = block(10, 50, 0, 1); e.depth = depth;
        r.frame.nowBeat = 10; r.render({e});
        CHECK(leadingEdge(r) == 200, "zoom %g depth %g: at its beat leading edge %d != header 200", zoom, depth, leadingEdge(r));
        // two beats before its beat it is 2 * ppb * depth px right of the header
        r.frame.nowBeat = 9.5; r.render({e});
        double expect = 200 + 0.5 * (400 / zoom) * depth;
        int le = leadingEdge(r);
        CHECK(std::fabs(le - std::floor(expect)) <= 1, "zoom %g depth %g: leading edge %d expected %.1f", zoom, depth, le, expect);
    }
}
static void testCulling(){
    Renderer r = makeR();
    std::vector<Event> ev{block(0, 1, 0, 1), block(-100, 1, 0, 1), block(500, 1, 0, 1)};
    ev[2].depth = 0.5;
    r.frame.nowBeat = 0; r.render(ev);
    CHECK(r.stats.drawn == 1 && r.stats.culled == 2, "culling drawn %d culled %d", r.stats.drawn, r.stats.culled);
    // a far-future event with deep depth (large) is still off screen; small depth brings it in
    Event far = block(30, 1, 0, 1); far.depth = 0.2;   // 30 beats * 25 px * 0.2 = 150 px right
    r.render({far});
    CHECK(r.stats.drawn == 1, "low depth event 30 beats ahead should be visible");
}
static Event note(int degree, double start = 0, double len = 4){
    Event e; e.kind = EventKind::Note; e.look = "note"; e.degree = degree; e.start = start; e.length = len;
    e.p = {{"thickness", 2}, {"partials", 0}, {"softness", 0}, {"attack", 0}, {"release", 0}};
    return e;
}
static void testPitch(){
    std::vector<Scale> scales(3);
    scales[1].semis = {0, 3, 5, 7, 10}; scales[1].tonic = 2;                     // minor pentatonic in D
    scales[2].tuning.cents = {0, 240, 480, 720, 960};                            // 5-EDO (microtonal)
    for(size_t si = 0; si < scales.size(); si++) for(int deg : {-3, 0, 4, 9}){
        Renderer r = makeR(400, 840); r.frame.scale = scales[si];
        r.frame.nowBeat = 1; r.render({note(deg)});
        double h = r.frame.space.heightOf(scales[si].semitonesOf(deg));
        double y = (1 - h) * 840;
        int pr = peakRow(r.column(200));
        CHECK(std::fabs(pr + 0.5 - y) <= 1.0, "scale %zu degree %d: row %d expected %.1f", si, deg, pr, y);
    }
    // pitchFine +100 cents moves one semitone up (8.4 px at 840 px / 100 semis: v1's grid, the default since 5.15)
    Renderer r = makeR(400, 840);
    Event a = note(0), b = note(0); b.p["pitchFine"] = 100;
    r.frame.nowBeat = 1; r.render({a}); int ra = peakRow(r.column(200));
    r.render({b}); int rb = peakRow(r.column(200));
    CHECK(std::abs((ra - rb) - 8) <= 1, "pitchFine 100 cents moved %d rows (expected 8)", ra - rb);
}
static void testDetuneVoices(){
    for(int v : {1, 2, 3}){
        Renderer r = makeR(400, 840);
        Event e = note(7); e.p["detuneVoices"] = v; e.p["detune"] = 100;
        r.frame.nowBeat = 1; r.render({e});
        int n = countPeaks(r.column(200), 0.3f);
        CHECK(n == v, "detuneVoices %d drew %d lines", v, n);
    }
}
static void testDrawnAnimated(){
    // drawn vibrato: pitchFine varies along the note -> the line's row changes across columns
    Renderer r = makeR(400, 840);
    r.frame.mod.drawn = [](const Event&, const std::string& d, double u, double base){ return d == "pitchFine" ? base + 150 * std::sin(2 * M_PI * u * 4) : base; };
    Event e = note(7, 0, 8);
    r.frame.nowBeat = 0; r.render({e});
    int mn = 10000, mx = -1;
    for(int x = 205; x < 390; x++){ int pr = peakRow(r.column(x)); mn = std::min(mn, pr); mx = std::max(mx, pr); }
    CHECK(mx - mn >= 20, "drawn vibrato row range %d (expected a wavy line)", mx - mn);
    // animated tremolo: level depends on the frame's beat, the same over the whole event
    Renderer q = makeR(400, 840);
    q.frame.mod.animated = [](const Event&, const std::string& d, Beat now, double base){ return d == "level" ? base * (0.55 + 0.45 * std::sin(2 * M_PI * now)) : base; };
    Event f = note(7, -4, 16);
    double lv[2];
    for(int i = 0; i < 2; i++){
        q.frame.nowBeat = i == 0 ? 0.25 : 0.75; q.render({f});
        float lo = 1, hi = 0;
        for(int x = 150; x < 390; x += 10){ float m = colMax(q, x); lo = std::min(lo, m); hi = std::max(hi, m); }
        CHECK(hi - lo < 0.02f, "animated tremolo should be uniform along the event (%.3f..%.3f)", lo, hi);
        lv[i] = hi;
    }
    CHECK(std::fabs(lv[0] - 1.0) < 0.02 && std::fabs(lv[1] - 0.1) < 0.02, "animated tremolo levels %.3f %.3f", lv[0], lv[1]);
}
static void testCover(){
    Renderer r = makeR(400, 200, 16);   // 25 px per beat
    r.layers = {LayerDef{16, "lighten", 1}, LayerDef{16, "lighten", 1}};
    Event bg; bg.kind = EventKind::Sweep; bg.start = -20; bg.length = 40; bg.pitched = false; bg.height = 0; bg.height2 = 1; bg.layer = 0;
    bg.p = {{"softness", 0}};
    Event sh; sh.kind = EventKind::Shape; sh.look = "rect"; sh.layer = 1; sh.start = 0; sh.length = 2; sh.pitched = false;
    sh.p = {{"heightCenter", 0.5}, {"heightSize", 0.2}, {"softness", 0}, {"coverEnable", 1}, {"coverDilate", 4},
            {"coverFeather", 0}, {"coverRelease", 2}, {"coverAmount", 1}};
    r.frame.nowBeat = 0; r.render({bg, sh});
    const Canvas& l0 = r.layer(0);
    // rect: x 200..250, y 80..120; dilated by 4 -> 76..124 and x 196..254
    auto A = [&](int x, int y){ return l0.at(x, y)[3]; };
    CHECK(A(225, 100) < 0.01f, "inside the shape layer 0 not erased (%.3f)", A(225, 100));
    CHECK(A(225, 78) < 0.01f && A(197, 100) < 0.01f, "inside the dilated contour not erased (%.3f %.3f)", A(225, 78), A(197, 100));
    CHECK(A(225, 70) > 0.99f && A(190, 100) > 0.99f, "outside the contour should stay (%.3f %.3f)", A(225, 70), A(190, 100));
    // trail: 2 beats = 50 px to the later side, fading linearly
    float t1 = A(266, 100), t2 = A(290, 100), t3 = A(320, 100);
    CHECK(t1 < t2 && t2 < 0.99f && t1 > 0.05f, "trail should fade in over release (%.3f %.3f)", t1, t2);
    CHECK(t3 > 0.99f, "beyond the release the layer is back (%.3f)", t3);
    // black mode: layer 0 painted opaque black
    sh.p["coverMode"] = 1; r.render({bg, sh});
    const float* p = r.layer(0).at(225, 100);
    CHECK(p[0] < 0.01f && p[3] > 0.99f, "black mode should be opaque black");
}
static void testPremultiplied(){
    for(std::string blend : {"lighten", "add", "normal"}){
        Renderer r = makeR(100, 100);
        r.layers = {LayerDef{16, "normal", 1}, LayerDef{16, blend, 1}};
        Event e; e.kind = EventKind::Shape; e.look = "rect"; e.layer = 1; e.start = -1; e.length = 2; e.pitched = false; e.level = 0.2;
        e.p = {{"heightCenter", 0.5}, {"heightSize", 0.5}, {"softness", 0}};
        r.render({e});
        float v = r.column(50)[50];
        CHECK(std::fabs(v - 0.2f) < 0.01f, "%s: dim pixel over black became %.3f (expected 0.2)", blend.c_str(), v);
        // over an opaque black layer too
        Event k = e; k.layer = 0; k.level = 1; k.colourRole = "black"; k.p["heightSize"] = 1;
        r.render({k, e});
        CHECK(std::fabs(r.column(50)[50] - 0.2f) < 0.01f, "%s: dim pixel level changed", blend.c_str());
    }
    // gain scales the premultiplied colour
    Renderer r = makeR(100, 100); r.layers = {LayerDef{16, "lighten", 0.5}};
    Event e = block(-1, 2, 0, 1); r.render({e});
    CHECK(std::fabs(r.column(50)[50] - 0.5f) < 0.01f, "gain 0.5 gave %.3f", r.column(50)[50]);
}
static void testReverse(){
    Renderer r = makeR(400, 100, 16);
    // a ramp: gradient hit decaying across the slot (asymmetric in time)
    Event g = block(0, 4, 0, 1); g.p["decay"] = 1.5;
    r.frame.nowBeat = 0; r.render({g});
    std::vector<float> before(400); for(int x = 0; x < 400; x++) before[(size_t)x] = r.column(x)[50];
    Event fx; fx.kind = EventKind::Effect; fx.look = "reverse"; fx.start = 0; fx.length = 4;
    r.render({g, fx});
    int bad = 0;
    for(int x = 200; x < 300; x++){ float a = r.column(x)[50], b = before[(size_t)(499 - x)]; if(std::fabs(a - b) > 0.02f) bad++; }
    CHECK(bad == 0, "reverse: %d columns not mirrored in the slot", bad);
    CHECK(std::fabs(r.column(150)[50] - before[150]) < 1e-6f, "reverse changed columns outside the slot");
    CHECK(r.column(299)[50] > r.column(200)[50], "reverse: the slot should now brighten towards its end");
}
static void testColumn(){
    Renderer r = makeR(400, 200);
    Event e = block(0, 1, 0.25, 0.5); r.frame.nowBeat = 0.5; r.render({e});
    auto c = r.column(200);
    CHECK(c[125] > 0.99f && c[105] > 0.99f, "header column should be lit in the band (%.2f %.2f)", c[125], c[105]);
    CHECK(c[50] < 0.01f && c[180] < 0.01f, "header column dark outside the band");
    CHECK(colMax(r, 150) < 0.01f, "past columns empty");
}

// ---------------------------------------------------------------- demo frames
static void demo(){
    mkdir("/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2", 0755);
    mkdir(kFrames, 0755);
    Renderer r; r.frame.width = 960; r.frame.height = 540; r.frame.headerX = 320; r.frame.nowBeat = 8;
    r.layers = {LayerDef{32, "lighten", 0.8}, LayerDef{16, "lighten", 1}, LayerDef{8, "add", 1}, LayerDef{12, "normal", 1}};
    std::vector<Event> ev;
    uint64_t id = 1;
    // layer 0: starfield, per-event depth (parallax), and a cloud texture
    Rng rng(42);
    for(int i = 0; i < 400; i++){
        Event s; s.id = id++; s.kind = EventKind::Hit; s.look = "dot"; s.layer = 0; s.pitched = false;
        s.start = rng.uniform(-40, 80); s.length = 0.05; s.depth = rng.uniform(0.2, 2.5);
        double h = rng.uniform(); s.height = s.height2 = h; s.level = 0.3 + 0.7 * rng.uniform();
        s.p = {{"heightLow", h}, {"heightHigh", h}, {"dotSize", 1 + 2 * s.depth / 2.5}, {"scatter", 0}, {"decay", 100}};
        s.colourRole = i % 5 ? "neutral" : "highlight"; s.seed = rng.next(); ev.push_back(s);
    }
    Event cl; cl.id = id++; cl.kind = EventKind::Sweep; cl.look = "nebula"; cl.layer = 0; cl.pitched = false; cl.start = -8; cl.length = 40;
    cl.p = {{"bandLow", 0.55}, {"bandHigh", 0.95}, {"density", 0.6}, {"grainSize", 10}, {"fade", 0.1}}; cl.level = 0.5; cl.colourRole = "shadow"; cl.seed = 7; ev.push_back(cl);
    // layer 1: melody notes with partials, a bass note, a mass glissando
    int degs[] = {7, 9, 11, 8, 7, 12, 10, 9, 14, 11};
    for(int i = 0; i < 10; i++){
        Event n; n.id = id++; n.kind = EventKind::Note; n.look = i % 3 == 2 ? "additive" : "note"; n.layer = 1; n.degree = degs[i];
        n.start = 2 + i * 1.5; n.length = 1.2; n.level = 0.9; n.colourRole = "highlight"; n.seed = id;
        n.p = {{"thickness", 4}, {"partials", 6}, {"partialDecay", 0.35}, {"release", 0.5}, {"attack", 0.05}, {"detuneVoices", i == 4 ? 3 : 1}, {"detune", 40}};
        ev.push_back(n);
    }
    Event bass = note(-5, 4, 6); bass.layer = 1; bass.colourRole = "bass"; bass.p = {{"thickness", 10}, {"partials", 3}, {"partialDecay", 0.5}, {"sub", 0.5}, {"release", 1}};
    bass.p["glideTo"] = -3; ev.push_back(bass);
    Event ms; ms.id = id++; ms.kind = EventKind::Mass; ms.layer = 1; ms.pitched = false; ms.start = 14; ms.length = 10; ms.seed = 3;
    ms.p = {{"heightFrom", 0.3}, {"heightTo", 0.75}, {"strands", 10}, {"spread", 0.08}, {"lineWidth", 1.5}, {"wobble", 0.5}}; ms.level = 0.6; ev.push_back(ms);
    // layer 2: kicks, snares (noise band), hats
    for(int b = 0; b < 24; b++){
        Event k; k.id = id++; k.kind = EventKind::Hit; k.layer = 2; k.pitched = false; k.start = b; k.length = 0.25; k.seed = (uint64_t)b * 31 + 1;
        if(b % 4 == 0){ k.look = "kick"; k.p = {{"heightLow", 0}, {"heightHigh", 0.25}, {"width", 0.5}, {"decay", 0.3}, {"sweep", 0.8}}; k.colourRole = "accent"; }
        else if(b % 4 == 2){ k.look = "noiseBand"; k.p = {{"bandLow", 0.3}, {"bandHigh", 0.7}, {"width", 0.3}, {"decay", 0.15}, {"grain", 0.7}}; }
        else { k.look = b % 8 == 1 ? "speckle" : "tick"; k.p = {{"heightLow", 0.78}, {"heightHigh", 0.95}, {"width", 0.2}, {"decay", 0.1}}; k.level = 0.7; }
        ev.push_back(k);
    }
    // layer 3: a sweep band and a shape that covers the layers below
    Event sw; sw.id = id++; sw.kind = EventKind::Sweep; sw.look = "rise"; sw.layer = 3; sw.pitched = false; sw.start = 10; sw.length = 8; sw.level = 0.5;
    sw.p = {{"bottomFrom", 0.05}, {"bottomTo", 0.4}, {"topFrom", 0.15}, {"topTo", 0.6}, {"levelFrom", 0.2}, {"levelTo", 1}}; sw.colourRole = "accent"; ev.push_back(sw);
    Event cv; cv.id = id++; cv.kind = EventKind::Shape; cv.look = "ellipse"; cv.layer = 3; cv.pitched = false; cv.start = 5; cv.length = 3; cv.level = 0.3;
    cv.p = {{"heightCenter", 0.5}, {"heightSize", 0.3}, {"coverEnable", 1}, {"coverDilate", 6}, {"coverFeather", 8}, {"coverRelease", 1.5}}; ev.push_back(cv);
    r.render(ev);
    std::string dir = kFrames;
    bool ok = r.writePPM(dir + "demo_composite.ppm");
    for(int L = 0; L < 4; L++) ok = r.writePPM(dir + "demo_layer" + std::to_string(L) + ".ppm", &r.layer(L)) && ok;
    r.frame.nowBeat = 12; r.render(ev); ok = r.writePPM(dir + "demo_composite_beat12.ppm") && ok;
    CHECK(ok, "could not write PPM frames to %s", kFrames);
    std::printf("demo: drawn %d culled %d effects %d covers %d\n", r.stats.drawn, r.stats.culled, r.stats.effects, r.stats.covers);
}

int main(){
    testHeaderCrossing();
    testCulling();
    testPitch();
    testDetuneVoices();
    testDrawnAnimated();
    testCover();
    testPremultiplied();
    testReverse();
    testColumn();
    demo();
    std::printf("test_draw: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
