//
//  test_blob.cpp — blob masks and effect scopes (ROADMAP 5.12): v2Blob.h, the CPU drawer, the engine.
//  Writes demo frames of each preset (PPM, then PNG with sips).
//

#include "v2Engine.h"
#include "v2Draw.h"
#include "v2Schema.h"
#include <cstdio>
#include <cstdlib>
#include <string>

using namespace forms2;
using namespace forms2::draw;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static const char* kPpm = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2/blob/";
static const char* kPng = "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027/notes/v2_blob/";

static Renderer makeR(int w = 400, int h = 300, double zoom = 16){
    Renderer r; r.frame.width = w; r.frame.height = h; r.frame.headerX = w / 2.0;
    r.layers = {LayerDef{zoom, "lighten", 1}};
    return r;
}
// a full-screen block (every pixel lit), so the mask is visible as is
static Event block(double start = -20, double len = 60, const std::string& inst = "a"){
    Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = start; e.length = len; e.pitched = false;
    e.instance = inst; e.seed = 1234; e.id = 77;
    e.p = {{"heightLow", 0}, {"heightHigh", 1}, {"width", len}, {"decay", 1e6}, {"softness", 0}};
    return e;
}
static void custom(Event& e, int placement, double irr = 0.3, double soft = 0.05){
    e.p["blob"] = 6; e.p["blobPlacement"] = placement; e.p["blobScope"] = 0; e.p["blobCount"] = 4;
    e.p["blobSizeBeats"] = 3; e.p["blobSizeHeight"] = 0.3; e.p["blobIrregularity"] = irr; e.p["blobDetail"] = 2;
    e.p["blobOctaves"] = 2; e.p["blobSoftness"] = soft; e.p["blobTail"] = 0;
}
static double litFraction(const Renderer& r){
    int n = 0; for(int x = 0; x < r.frame.width; x++){ auto c = r.column(x); for(float v : c) if(v > 0.5f) n++; }
    return n / (double)(r.frame.width * r.frame.height);
}
static std::vector<float> img(const Renderer& r){ std::vector<float> v; for(int x = 0; x < r.frame.width; x++){ auto c = r.column(x); v.insert(v.end(), c.begin(), c.end()); } return v; }
static float px(const Renderer& r, int x, int y){ return r.column(x)[(size_t)y]; }

static void testPlacements(){
    // zone, mask in vs out (amount 1: in + out = the unmasked drawing)
    Renderer r = makeR();
    Event e = block(); custom(e, blob::Zone);
    e.start = -2; e.length = 8; e.p["width"] = 8;     // the element spans beats -2..6 (x 150..350)
    r.render({e}); auto in = img(r); double fin = litFraction(r);
    e.p["blobMode"] = 1; r.render({e}); auto out = img(r);
    Event plain = e; plain.p["blob"] = 0; r.render({plain}); auto base = img(r);
    double maxErr = 0; for(size_t i = 0; i < in.size(); i++) maxErr = std::max(maxErr, (double)std::fabs(in[i] + out[i] - base[i]));
    CHECK(fin > 0.005 && fin < 0.5, "zone mask in lit fraction %.3f", fin);
    CHECK(maxErr < 0.02, "mask in + mask out = unmasked (err %.3f)", maxErr);
    // blobs inside the zone heights and the element's beats
    e.p["blobMode"] = 0; e.p["blobZoneLow"] = 0.6; e.p["blobZoneHigh"] = 0.8;
    auto m = r.maskOf(e);
    bool okz = m.blobs.size() == 4;
    for(auto& b : m.blobs) okz = okz && b.ch >= 0.6 && b.ch <= 0.8 && b.cb >= -2 && b.cb <= 6;
    CHECK(okz, "zone blobs inside the zone");
    r.render({e});
    double lowLit = 0; for(int x = 0; x < 400; x++) for(int y = 240; y < 300; y++) lowLit += px(r, x, y);
    CHECK(lowLit < 1, "nothing lit far below the zone (%.2f)", lowLit);

    // full height: a blob column lit from near the top to near the bottom
    Event f = block(); custom(f, blob::FullHeight, 0, 0); f.p["blobCount"] = 1; f.p["blobSizeBeats"] = 2;
    f.start = -1; f.length = 2; f.p["width"] = 2;
    auto mf = r.maskOf(f); r.render({f});
    int cx = (int)std::floor(r.viewOf(0).xOf(mf.blobs[0].cb));
    bool full = true; for(int y = 30; y < 270; y++) full = full && px(r, cx, y) > 0.9f;
    CHECK(full, "full height blob covers the column at x %d", cx);
    CHECK(px(r, 150, 150) < 0.05f && px(r, 249, 150) < 0.05f || mf.blobs[0].rb * 25 > 24, "outside the width: dark");

    // partials: blob centres on the note's lines
    Event n; n.kind = EventKind::Note; n.look = "note"; n.degree = 2; n.start = -2; n.length = 4; n.instance = "mel"; n.seed = 9; n.id = 3;
    n.p = {{"thickness", 6}, {"partials", 3}, {"release", 0}, {"attack", 0}};
    custom(n, blob::Partials);
    auto lines = r.linesOf(n); auto mp = r.maskOf(n);
    bool onLine = true;
    for(auto& b : mp.blobs){ bool any = false; for(double l : lines) if(std::fabs(l - b.ch) < 1e-9) any = true; onLine = onLine && any; }
    CHECK(lines.size() == 4 && onLine, "partials blobs on the note's lines");

    // perlin: islands; a higher threshold shows less (mask in)
    Event p = block(); custom(p, blob::Perlin); p.p["blobSizeBeats"] = 2; p.p["blobSizeHeight"] = 0.2;
    p.p["blobThreshold"] = 0.45; r.render({p}); double f1 = litFraction(r);
    p.p["blobThreshold"] = 0.6; r.render({p}); double f2 = litFraction(r);
    CHECK(f1 > 0.1 && f1 < 0.95 && f2 < f1, "perlin lit %.3f then %.3f", f1, f2);

    // on events: one blob centred on the event
    Event h = block(); custom(h, blob::OnEvents); h.start = 0; h.length = 1; h.p["width"] = 1; h.p["heightLow"] = 0.4; h.p["heightHigh"] = 0.6;
    auto mh = r.maskOf(h);
    CHECK(mh.blobs.size() == 1 && std::fabs(mh.blobs[0].cb - 0.5) < 1e-9 && std::fabs(mh.blobs[0].ch - 0.5) < 1e-9, "on events: centred on the event");
}

static void testShape(){
    Renderer r = makeR();
    auto midCount = [&](double soft){
        Event e = block(-2, 8); e.p["width"] = 8; custom(e, blob::Zone, 0.3, soft);
        r.render({e}); int n = 0; for(float v : img(r)) if(v > 0.05f && v < 0.95f) n++; return n;
    };
    int hard = midCount(0), soft = midCount(0.8);
    CHECK(soft > hard * 3 && soft > 200, "softness gives a gradient edge (%d vs %d)", soft, hard);
    auto shot = [&](double irr, double det){
        Event e = block(-2, 8); e.p["width"] = 8; custom(e, blob::Zone, irr); e.p["blobDetail"] = det;
        r.render({e}); return img(r);
    };
    auto a = shot(0.3, 2), a2 = shot(0.3, 2), b = shot(0.9, 2), c = shot(0.3, 8);
    CHECK(a == a2, "deterministic");
    CHECK(a != b, "irregularity changes the contour");
    CHECK(a != c, "detail changes the contour");
    // tail stretches the blob on its later side
    blob::Mask m; blob::Params P; P.shape.irregularity = 0; P.shape.softness = 0; P.count = 1; P.scope = blob::Element;
    blob::Source s; s.evStart = 0; s.evLen = 0.0001; P.zoneLow = P.zoneHigh = 0.5;
    m.build(P, s, 0, 1); double cb = m.blobs[0].cb, rb = m.blobs[0].rb;
    P.shape.tail = 1; blob::Mask t; t.build(P, s, 0, 1);
    CHECK(m.value(cb + rb * 2, 0.5, 25, 300, 0) < 0.01 && t.value(cb + rb * 2, 0.5, 25, 300, 0) > 0.99 && t.value(cb - rb * 2, 0.5, 25, 300, 0) < 0.01, "tail on the later side");
}

static void testScopes(){
    Renderer r = makeR();
    // element: each event its own blobs (relative to itself)
    Event a = block(0, 4); a.id = 1; a.seed = 11; custom(a, blob::Zone);
    Event b = a; b.id = 2; b.seed = 12; b.start = 8;
    auto ma = r.maskOf(a), mb = r.maskOf(b);
    CHECK(std::fabs((ma.blobs[0].cb - a.start) - (mb.blobs[0].cb - b.start)) > 1e-6, "per element differs per event");
    // phrase: the same blobs for every event of the phrase
    for(Event* e : {&a, &b}){ e->p["blobScope"] = 1; e->p["blobSeedP"] = blob::seedToP(999); e->p["blobPhStart"] = 0; e->p["blobPhLen"] = 16; }
    ma = r.maskOf(a); mb = r.maskOf(b);
    bool same = ma.blobs.size() == mb.blobs.size();
    for(size_t i = 0; same && i < ma.blobs.size(); i++) same = ma.blobs[i].cb == mb.blobs[i].cb && ma.blobs[i].ch == mb.blobs[i].ch;
    CHECK(same, "per phrase constant within the phrase");
    Event c = a; c.p["blobSeedP"] = blob::seedToP(1000); c.p["blobPhStart"] = 16; c.start = 16;
    CHECK(r.maskOf(c).blobs[0].cb != ma.blobs[0].cb, "next phrase redraws");
    // general: one field across phrases (same value at the same beat from events of different phrases)
    Event g1 = block(10, 8); custom(g1, blob::Zone); g1.p["blobScope"] = 2; g1.p["blobSeedG"] = blob::seedToP(4242);
    g1.p["blobSeedP"] = blob::seedToP(1); g1.p["blobPhStart"] = 0; g1.p["blobPhLen"] = 16;
    Event g2 = g1; g2.start = 14; g2.p["blobSeedP"] = blob::seedToP(2); g2.p["blobPhStart"] = 16;
    auto m1 = r.maskOf(g1), m2 = r.maskOf(g2);
    double maxd = 0; int lit = 0;
    for(double bt = 14; bt < 18; bt += 0.05) for(double h = 0.02; h < 1; h += 0.05){
        double v1 = m1.value(bt, h, 25, 300, 0), v2 = m2.value(bt, h, 25, 300, 0);
        maxd = std::max(maxd, std::fabs(v1 - v2)); lit += v1 > 0.5;
    }
    CHECK(maxd < 1e-9 && lit > 0, "general continuous across phrases (diff %g, lit %d)", maxd, lit);
}

static void testMovesWithContent(){
    Renderer r = makeR();
    Event e = block(-20, 60); custom(e, blob::Zone); e.p["blobScope"] = 2; e.p["blobCount"] = 6;
    r.frame.nowBeat = 0; r.render({e}); auto a = img(r);
    r.frame.nowBeat = 2; r.render({e}); auto b = img(r);   // 2 beats * 25 px later: 50 px to the left
    double err = 0;
    for(int x = 60; x < 340; x++) for(int y = 0; y < 300; y++) err = std::max(err, (double)std::fabs(a[(size_t)x * 300 + y] - b[(size_t)(x - 50) * 300 + y]));
    CHECK(err < 0.02, "blobs move with the content (err %.3f)", err);
    // animated wobble: the contour changes with nowBeat
    e.p["blobWobble"] = 1; e.p["blobWobbleRate"] = 1;
    r.frame.nowBeat = 0; r.render({e}); a = img(r);
    r.frame.nowBeat = 0.5; r.render({e}); b = img(r);
    bool diff = false; for(int x = 60; x < 340 && !diff; x++) for(int y = 0; y < 300; y++) if(std::fabs(a[(size_t)x * 300 + y] - b[(size_t)(x - 12) * 300 + y]) > 0.3){ diff = true; break; }
    CHECK(diff, "wobble animates the contour");
}

static void testPresets(){
    const TypeDef* t = findType("melody");
    for(int i = 1; i <= 5; i++){
        blob::Shape s; CHECK(blob::presetShape(i, s), "preset %d", i);
        auto in = [&](const char* k, double v){ auto d = t->param(k); return d && v >= d->min && v <= d->max; };
        CHECK(in("blobSizeBeats", s.sizeBeats) && in("blobSizeHeight", s.sizeHeight) && in("blobAspect", s.aspect) && in("blobRotation", s.rotation)
              && in("blobIrregularity", s.irregularity) && in("blobDetail", s.detail) && in("blobOctaves", s.octaves) && in("blobTail", s.tail)
              && in("blobSoftness", s.softness), "preset %s in range", blob::presetNames()[(size_t)i].c_str());
    }
    blob::Shape s; CHECK(!blob::presetShape(0, s) && !blob::presetShape(6, s), "off / custom are not presets");
    CHECK(blob::presetNames().size() == t->param("blob")->options.size(), "preset names match the schema");
}

static void testInsertAndBus(){
    Renderer r = makeR();
    Event a = block(-4, 8, "a"); a.p["heightLow"] = 0.6; a.p["heightHigh"] = 0.9;
    Event b = block(-4, 8, "b"); b.p["heightLow"] = 0.1; b.p["heightHigh"] = 0.4; b.id = 78;
    Event fx; fx.kind = EventKind::Effect; fx.look = "blank"; fx.instance = "fx"; fx.start = -1; fx.length = 2; fx.p = {{"fxScope", 1}, {"fxTarget", blob::targetHash("a")}};
    r.render({a, b}); auto none = img(r);
    r.render({a, b, fx}); auto ins = img(r);
    bool bSame = true, aBlank = true;
    for(int x = 0; x < 400; x++) for(int y = 0; y < 300; y++){
        double h = 1 - (y + 0.5) / 300; size_t i = (size_t)x * 300 + y;
        if(h < 0.5) bSame = bSame && ins[i] == none[i];
        if(x > 180 && x < 220 && h > 0.65 && h < 0.85) aBlank = aBlank && ins[i] < 0.01;
    }
    CHECK(bSame, "per-instance insert leaves the other instance untouched");
    CHECK(aBlank, "per-instance insert acts on its target");
    // layer bus (default scope): unchanged, both blanked
    Event bus = fx; bus.p.clear();
    r.render({a, b, bus});
    CHECK(px(r, 200, 225) < 0.01 && px(r, 200, 60) < 0.01 && px(r, 100, 225) > 0.9, "layer bus blanks every instance");
    // per element (pixel): reverse each event in the slot, another instance untouched
    Event el = fx; el.look = "blank"; el.p = {{"fxScope", 2}, {"fxTarget", blob::targetHash("b")}};
    el.start = -10; el.length = 20;
    r.render({a, b, el});
    CHECK(px(r, 200, 225) < 0.01 && px(r, 200, 60) > 0.9, "per-element effect only on the target's events");
    // blob filter fx on the layer bus
    Event bf = fx; bf.look = "blob"; bf.start = -4; bf.length = 8; bf.p = {{"blob", 1}, {"blobAmount", 1}, {"blobPlacement", 0}, {"blobScope", 0}, {"blobCount", 3}};
    r.render({a, b, bf}); double lf = litFraction(r);
    r.render({a, b}); double l0 = litFraction(r);
    CHECK(lf < l0 * 0.8 && lf > 0, "blob filter fx masks the layer (%.3f of %.3f)", lf, l0);
}

static const char* kEchoScene = R"({"format":"espectres.scene","formatVersion":1,"name":"echo","version":1,"bpm":120,"seed":3,
  "harmony":{"sections":[{"name":"A","meter":{"num":4,"den":4},"scale":{"name":"major","semis":[0,2,4,5,7,9,11],"tonic":0},"steps":[{"degree":0,"length":16}]}]},
  "layers":[{"zoomBeats":16,"blend":"lighten","gain":1}],
  "instances":[
   {"id":"mel","type":"melody","layer":0,"follow":"chord","seq":{"mode":"pattern","pattern":"x...","steps":8},"phrase":{"length":4}},
   {"id":"bass","type":"bass","layer":0,"follow":"chord","seq":{"mode":"pattern","pattern":"x.x.","steps":8},"phrase":{"length":4}},
   {"id":"ech","type":"timefx","layer":0,"follow":"off","params":{"effect":"echo","fxScope":"element","fxTarget":"mel","echoCount":3,"echoDecay":0.5,"echoSpacing":0.25,"slotBeats":4},
    "seq":{"mode":"pattern","pattern":"x","steps":1},"phrase":{"length":4}}],
  "routings":[],"macros":{}})";

static void testElementEcho(){
    Engine E; CHECK(E.loadJson(json::parse(kEchoScene)), "echo scene loads");
    for(auto& p : E.problems()) std::printf("  %s\n", p.c_str());
    for(double t = 0; t <= 4; t += 0.25) E.update(t);
    int mel = 0, copies = 0, bassCopies = 0; bool levels = true;
    std::map<uint64_t, double> melLevel;
    for(auto& s : E.events(0)) if(s.e.instance == "mel" && !s.e.p.count("fxCopy")){ mel++; melLevel[s.e.id] = s.e.level; }
    for(auto& s : E.events(0)) if(s.e.p.count("fxCopy")){
        if(s.e.instance == "mel") copies++; else bassCopies++;
        int i = (int)s.e.param("fxCopy", 0);
        bool found = false;
        for(auto& kv : melLevel) if(std::fabs(kv.second * std::pow(0.5, i) - s.e.level) < 1e-9) found = true;
        levels = levels && found;
    }
    CHECK(mel > 0 && copies == mel * 3, "echo copies: %d mel events, %d copies", mel, copies);
    CHECK(bassCopies == 0, "no copies of other instances");
    CHECK(levels, "echo copies fade");
    E.setMuted("ech", true);
    int vis = 0; for(auto& ve : E.visible(0, E.viewFor(0, 4))) vis += ve.ev->e.p.count("fxCopy") ? 1 : 0;
    CHECK(vis == 0, "muting the effect hides its copies");
}

static void demos(){
    std::system((std::string("mkdir -p '") + kPpm + "' '" + kPng + "'").c_str());
    for(int pre = 1; pre <= 5; pre++){
        Renderer r = makeR(960, 540, 16);
        r.layers = {LayerDef{16, "lighten", 1}, LayerDef{16, "lighten", 1}};
        std::vector<Event> ev;
        // a texture band and a melody, both masked by the preset (phrase scope)
        Event tex; tex.kind = EventKind::Sweep; tex.look = "nebula"; tex.instance = "tex"; tex.start = -10; tex.length = 40; tex.seed = 5; tex.id = 1;
        tex.pitched = false; tex.p = {{"bandLow", 0.05}, {"bandHigh", 0.95}, {"density", 0.8}, {"fade", 0}, {"grainSize", 8}};
        tex.p["blob"] = pre; tex.p["blobPlacement"] = pre == 5 ? 2 : 0; tex.p["blobCount"] = 6; tex.p["blobScope"] = 1;
        tex.p["blobSeedP"] = blob::seedToP(77 + pre); tex.p["blobPhStart"] = -8; tex.p["blobPhLen"] = 32;
        ev.push_back(tex);
        for(int i = 0; i < 10; i++){
            Event n; n.kind = EventKind::Note; n.look = "note"; n.instance = "mel"; n.layer = 1; n.degree = (i * 3) % 9; n.start = -8 + i * 2; n.length = 1.6;
            n.seed = 100 + i; n.id = 10 + i; n.colourRole = "highlight";
            n.p = {{"thickness", 8}, {"partials", 4}, {"release", 0.3}, {"blob", pre}, {"blobPlacement", 4}, {"blobScope", 0}, {"blobAmount", 1}, {"blobScale", 0.6}};
            ev.push_back(n);
        }
        r.render(ev);
        std::string name = std::string("blob_") + blob::presetNames()[(size_t)pre];
        std::string ppm = std::string(kPpm) + name + ".ppm";
        CHECK(r.writePPM(ppm), "write %s", ppm.c_str());
        std::system(("sips -s format png '" + ppm + "' --out '" + kPng + name + ".png' >/dev/null 2>&1").c_str());
    }
}

int main(){
    installSequencerSchema();
    testPlacements();
    testShape();
    testScopes();
    testMovesWithContent();
    testPresets();
    testInsertAndBus();
    testElementEcho();
    demos();
    {   // scope "earlier" (3): v1 cell order. A (bottom half) is drawn before the blank, B (top half) after:
        // in the slot A is blanked, B stays; outside the slot both stay
        Renderer r = makeR(); r.frame.nowBeat = 0;
        Event a = block(-20, 60, "a"); a.p["heightHigh"] = 0.5; a.p["drawOrder"] = 0; a.id = 1;
        Event b = block(-20, 60, "b"); b.p["heightLow"] = 0.5; b.p["drawOrder"] = 2; b.id = 2;
        Event fx; fx.kind = EventKind::Effect; fx.look = "blank"; fx.instance = "fx"; fx.start = 0; fx.length = 4; fx.id = 3;
        fx.p = {{"fxScope", 3}, {"drawOrder", 1}};
        r.render({b, fx, a});                                   // list order must not matter
        LayerView v = r.viewOf(0); int xin = (int)v.xOf(2), xout = (int)v.xOf(-2);
        CHECK(px(r, xin, 225) < 0.05 && px(r, xin, 75) > 0.5, "earlier: in the slot A blanked (%.2f), B kept (%.2f)", px(r, xin, 225), px(r, xin, 75));
        CHECK(px(r, xout, 225) > 0.5 && px(r, xout, 75) > 0.5, "earlier: outside the slot both kept (%.2f %.2f)", px(r, xout, 225), px(r, xout, 75));
        CHECK(blob::targets(fx, a) && !blob::targets(fx, b), "earlier: targets() follows drawOrder");
        {   // A with its own group (an instance-scope effect elsewhere on it) is still reached by the earlier blank
            Event ins; ins.kind = EventKind::Effect; ins.look = "blank"; ins.instance = "ins"; ins.start = -8; ins.length = 1; ins.id = 4;
            ins.p = {{"fxScope", 1}, {"fxTarget", blob::targetHash("a")}, {"drawOrder", 3}};
            r.render({b, fx, a, ins});
            CHECK(px(r, xin, 225) < 0.05 && px(r, xin, 75) > 0.5, "earlier + own group: A blanked (%.2f), B kept (%.2f)", px(r, xin, 225), px(r, xin, 75));
            CHECK(px(r, xout, 225) > 0.5, "earlier + own group: A kept outside the slot (%.2f)", px(r, xout, 225));
            CHECK(px(r, (int)v.xOf(-7.5), 225) < 0.05, "own group: its instance effect still applies (%.2f)", px(r, (int)v.xOf(-7.5), 225));
        }
        fx.p["fxScope"] = 0;                                      // layer scope: both blanked
        r.render({b, fx, a});
        CHECK(px(r, xin, 225) < 0.05 && px(r, xin, 75) < 0.05, "layer scope: both blanked (%.2f %.2f)", px(r, xin, 225), px(r, xin, 75));
    }
    std::printf("test_blob: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
