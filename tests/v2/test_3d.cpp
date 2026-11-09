//
//  test_3d.cpp — optional 3D view (ROADMAP 5.14): converging paths and perspective depth.
//  The score canvases (column(), what the sound reads) stay flat; display() is the 3D view.
//  Writes demo frames (swarm, starfield in perspective, layer planes) to notes/v2_3d as PNG.
//

#include "v2Engine.h"
#include "v2Draw.h"
#include <cstdio>
#include <string>

using namespace forms2;
using namespace forms2::draw;

static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if(!(c)){ fails++; std::printf("FAIL %s:%d: ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while(0)

static const char* kScratch = "/private/tmp/claude-501/-Users-santiagovilanova-PM-Dropbox-PLAYMODES-STUDIO-ESPECTRES-ESPECTRES-1920/eb0128dc-9f9e-4c55-a6fe-018c614158e7/scratchpad/v2/3d/";
static const char* kPng = "/Users/santiagovilanova/PM Dropbox/PLAYMODES_STUDIO/ESPECTRES/ESPECTRES_2027/notes/v2_3d/";

static Renderer makeR(int w = 400, int h = 300, int layers = 1){
    Renderer r; r.frame.width = w; r.frame.height = h; r.frame.headerX = w / 2.0;
    r.layers.assign((size_t)layers, LayerDef{16, "lighten", 1});
    return r;
}
// a small square-ish block centred on height h
static Event dot(double start, double h, int layer = 0){
    Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = start; e.length = 0.25; e.layer = layer;
    e.pitched = false; e.p = {{"heightLow", h - 0.03}, {"heightHigh", h + 0.03}, {"width", 0.25}, {"decay", 1000}, {"softness", 0}};
    return e;
}
struct Box { int x0 = 1 << 30, y0 = 1 << 30, x1 = -1, y1 = -1; double cx = 0, cy = 0, sum = 0; float peak = 0;
    bool empty() const { return x1 < 0; } int w() const { return x1 - x0 + 1; } int h() const { return y1 - y0 + 1; } };
static Box boxOf(const Canvas& c, float thr = 0.02f){
    Box b;
    for(int y = 0; y < c.h; y++) for(int x = 0; x < c.w; x++){
        const float* p = c.at(x, y); float v = std::max({p[0], p[1], p[2]});
        if(v <= thr) continue;
        b.x0 = std::min(b.x0, x); b.x1 = std::max(b.x1, x); b.y0 = std::min(b.y0, y); b.y1 = std::max(b.y1, y);
        b.cx += (x + 0.5) * v; b.cy += (y + 0.5) * v; b.sum += v; b.peak = std::max(b.peak, v);
    }
    if(b.sum > 0){ b.cx /= b.sum; b.cy /= b.sum; }
    return b;
}
static bool sameCanvas(const Canvas& a, const Canvas& b){ return a.w == b.w && a.h == b.h && a.px == b.px; }
static bool sameCol(const Renderer& a, const Renderer& b, int x){ return a.column(x) == b.column(x); }

// ---------------------------------------------------------------- flat default
static void testFlatDefault(){
    std::vector<Event> ev{dot(1, 0.5), dot(3, 0.3), dot(-1, 0.7)};
    Renderer a = makeR(); a.frame.nowBeat = 0; a.render(ev);
    CHECK(!a.displayIs3D() && &a.display() == &a.composite(), "no 3D settings: display is the flat composite");
    // explicit flat path values are flat too
    for(auto& e : ev) e.p.insert({{"pathKind", 0}, {"pathAngle", 0}, {"pathCurve", 0}, {"pathSpeed", 1}});
    Renderer b = makeR(); b.frame.nowBeat = 0; b.render(ev);
    CHECK(!b.displayIs3D() && sameCanvas(a.composite(), b.composite()), "explicit right / 0 / 0 / 1 path = flat, identical");
    CHECK(a.rgb8() == b.rgb8(), "rgb8 identical");
}

// ---------------------------------------------------------------- paths
static void testPaths(){
    const char* names[] = {"right", "top", "bottom", "point", "spiral", "curve"};
    for(int kind = 0; kind < 6; kind++){
        Event e = dot(4, 0.5);
        e.p["pathKind"] = kind;
        if(kind == 0) e.p["pathAngle"] = 30;          // a tilted "right" so it is not flat
        e.p["pathOriginX"] = 0.8; e.p["pathOriginY"] = 0.9;
        Renderer flat = makeR(), r = makeR();
        Event f = dot(4, 0.5);
        // at its beat: exactly the flat render (display and column)
        flat.frame.nowBeat = 4; flat.render({f});
        r.frame.nowBeat = 4; r.render({e});
        CHECK(r.displayIs3D(), "%s: 3D", names[kind]);
        CHECK(sameCanvas(r.display(), flat.composite()), "%s: at its beat the display equals the flat frame", names[kind]);
        CHECK(sameCol(r, flat, 200), "%s: header column at the crossing equals flat", names[kind]);
        // before its beat: from the stated origin, right of the playhead, score column untouched
        for(double back : {0.5, 1.0, 2.0}){
            flat.frame.nowBeat = r.frame.nowBeat = 4 - back;
            flat.render({f}); r.render({e});
            Box fb = boxOf(flat.composite()), db = boxOf(r.display());
            CHECK(!db.empty(), "%s back %g: visible", names[kind], back);
            CHECK(sameCanvas(r.composite(), flat.composite()), "%s back %g: score canvas (sound) unchanged", names[kind], back);
            CHECK(db.x0 >= 200, "%s back %g: stays right of the playhead (x0 %d)", names[kind], back, db.x0);
            double dy = db.cy - fb.cy;                 // + = below (y down)
            if((kind == 4 || kind == 5) && back < 2) continue;   // quadratic near the playhead
            switch(kind){
                case 0: case 1: case 5: CHECK(dy < -2, "%s back %g: comes from above (dy %.1f)", names[kind], back, dy); break;
                case 2: CHECK(dy > 2, "bottom back %g: comes from below (dy %.1f)", back, dy); break;
                case 3: {                                // toward the origin (x 200 + 0.8 * 200, y 0.1 * 300)
                    double Ox = 360, Oy = 30;
                    CHECK(std::hypot(db.cx - Ox, db.cy - Oy) < std::hypot(fb.cx - Ox, fb.cy - Oy) - 2, "point back %g: nearer the origin", back);
                    break; }
                case 4: CHECK(std::fabs(dy) > 2 || std::fabs(db.cx - fb.cx) > 2, "spiral back %g: off the flat path", back); break;
            }
        }
        // just before: close to the flat position (continuous at the crossing)
        flat.frame.nowBeat = r.frame.nowBeat = 4 - 0.02; flat.render({f}); r.render({e});
        Box fb = boxOf(flat.composite()), db = boxOf(r.display());
        CHECK(std::hypot(db.cx - fb.cx, db.cy - fb.cy) < 2.5, "%s: continuous near the crossing (%.2f px)", names[kind], std::hypot(db.cx - fb.cx, db.cy - fb.cy));
    }
    // speed: a faster path starts farther out
    Event e = dot(4, 0.5); e.p["pathSpeed"] = 2;
    Renderer r = makeR(); r.frame.nowBeat = 3; r.render({e});
    Box b = boxOf(r.display());
    CHECK(b.x0 >= 200 + 2 * 25 - 2 && b.x0 <= 200 + 2 * 25 + 2, "speed 2: one beat ahead at 2 beats of distance (x0 %d)", b.x0);
}

// ---------------------------------------------------------------- perspective
static void testPerspective(){
    Renderer flat = makeR(), r = makeR();
    r.frame.td.perspective = true; r.frame.td.fog = 1;
    Event e = dot(6, 0.8);
    flat.frame.nowBeat = r.frame.nowBeat = 0;
    flat.render({e}); r.render({e});
    Box fb = boxOf(flat.composite()), db = boxOf(r.display(), 0.005f);
    auto pl = r.placeOf(e);
    CHECK(pl.s < 1 && pl.gain < 1, "far ahead: scaled %.3f, fog %.3f", pl.s, pl.gain);
    CHECK(db.h() < fb.h() && db.w() <= fb.w(), "far ahead: smaller (%dx%d vs %dx%d)", db.w(), db.h(), fb.w(), fb.h());
    CHECK(db.peak < fb.peak - 0.05f, "far ahead: dimmer (%.2f vs %.2f)", db.peak, fb.peak);
    CHECK(std::hypot(db.cx - pl.vx, db.cy - pl.vy) < std::hypot(fb.cx - pl.vx, fb.cy - pl.vy), "far ahead: nearer the vanishing point");
    CHECK(sameCanvas(r.composite(), flat.composite()), "perspective: score canvas unchanged");
    // farther = smaller
    Event e2 = dot(12, 0.8);
    CHECK(r.placeOf(e2).s < pl.s, "farther ahead is smaller");
    // at its beat: exactly flat size and position, header column equal
    flat.frame.nowBeat = r.frame.nowBeat = 6; flat.render({e}); r.render({e});
    CHECK(sameCanvas(r.display(), flat.composite()), "at the crossing: display == flat");
    CHECK(sameCol(r, flat, 200), "at the crossing: header column == flat");
    // per-event depth is z: deeper (larger depth multiplier) recedes faster
    Event d2 = dot(6, 0.8); d2.depth = 2; r.frame.nowBeat = 0;
    CHECK(r.placeOf(d2).s < r.placeOf(e).s, "depth 2 farther than depth 1");
    // depth of field: blurred far, softer peak
    Renderer bl = makeR(); bl.frame.td.perspective = true; bl.frame.td.fog = 0; bl.frame.td.dof = 1; bl.frame.nowBeat = 0;
    bl.render({e});
    CHECK(bl.placeOf(e).blur > 0, "dof: blur radius %d", bl.placeOf(e).blur);
    // a busy frame: the score column at the header equals flat on every frame
    std::vector<Event> many;
    for(int i = 0; i < 40; i++){ Event m = dot(i * 0.37, 0.1 + 0.02 * i); m.depth = 0.5 + 0.05 * i; m.p["pathKind"] = i % 6; many.push_back(m); }
    bool allSame = true, displayColOk = true;
    for(double t = 0; t < 12; t += 0.13){
        Renderer a = makeR(), b = makeR(); b.frame.td.perspective = true; b.frame.td.dof = 0.5;
        a.frame.nowBeat = b.frame.nowBeat = t;
        std::vector<Event> fl = many; for(auto& m : fl) m.p.erase("pathKind");
        a.render(fl); b.render(many);
        if(!sameCol(a, b, 200) || !sameCanvas(a.composite(), b.composite())) allSame = false;
        // the 3D view at the playhead column: every lit pixel has a lit flat pixel within 2 rows
        for(int y = 0; y < 300; y++){
            const float* q = b.display().at(200, y);
            if(std::max({q[0], q[1], q[2]}) <= 0.05f) continue;
            bool near = false;
            for(int yy = std::max(0, y - 2); yy <= std::min(299, y + 2); yy++){ const float* f = a.composite().at(200, yy); if(std::max({f[0], f[1], f[2]}) > 0.f) near = true; }
            if(!near){ displayColOk = false; }
        }
    }
    CHECK(displayColOk, "3D view at the playhead column stays within 2 rows of the flat column");
    CHECK(allSame, "40 events, paths + perspective: header column and score canvas equal flat on every frame");
}

// ---------------------------------------------------------------- layer planes and camera dolly
static void testPlanes(){
    Renderer r = makeR(400, 300, 3);
    r.frame.td.perspective = true; r.frame.td.fog = 0;
    r.frame.td.planeZ = {{0, 1.5, 3, 0}};
    std::vector<Event> ev{dot(4, 0.8, 0), dot(4, 0.8, 1), dot(4, 0.8, 2)};
    r.frame.nowBeat = 0;
    double s0 = r.placeOf(ev[0]).s, s1 = r.placeOf(ev[1]).s, s2 = r.placeOf(ev[2]).s;
    CHECK(s0 > s1 && s1 > s2, "farther planes smaller (%.3f %.3f %.3f)", s0, s1, s2);
    r.render(ev);
    Box b0 = boxOf(r.displayLayer(0)), b2 = boxOf(r.displayLayer(2));
    auto p0 = r.placeOf(ev[0]);
    CHECK(std::hypot(b2.cx - p0.vx, b2.cy - p0.vy) < std::hypot(b0.cx - p0.vx, b0.cy - p0.vy), "far plane nearer the vanishing point");
    // dolly in: every plane grows; the far plane moves more in screen (parallax with perspective)
    auto head = [&](const Event& e){ auto p = r.placeOf(e); return std::make_pair(p.mapX(r.viewOf(e.layer).xOf(e.start)), p.s); };
    auto n0 = head(ev[0]), f0 = head(ev[2]);
    r.frame.td.camera = 1;
    auto n1 = head(ev[0]), f1 = head(ev[2]);
    CHECK(n1.second > n0.second && f1.second > f0.second, "dolly in: planes grow");
    double sepBefore = n0.first - f0.first, sepAfter = n1.first - f1.first;
    CHECK(std::fabs(sepAfter - sepBefore) > 1, "dolly: planes shift relative to each other (%.1f -> %.1f px)", sepBefore, sepAfter);
    // per-layer camera (layer routings): only that layer moves
    r.frame.td.camera = 0; r.frame.td.layerCamera = {{0, 0, 1, 0}};
    CHECK(r.placeOf(ev[0]).s == s0 && r.placeOf(ev[2]).s > s2, "layer camera moves its own plane only");
    // at the crossing every plane is flat
    r.frame.nowBeat = 4;
    CHECK(r.placeOf(ev[0]).identity() && r.placeOf(ev[2]).identity(), "planes flat at the crossing");
}

// ---------------------------------------------------------------- engine: per-event random paths
static const char* kSwarm = R"({
  "format": "espectres.scene", "formatVersion": 1, "name": "swarm", "bpm": 110, "seed": 77,
  "layers": [{"zoomBeats": 16}],
  "instances": [{
    "id": "sw", "type": "hats", "layer": 0, "follow": "off",
    "params": {"pathKind": "curve", "pathSpeed": 1.2},
    "seq": {"mode": "probability", "density": 0.8, "steps": 16}, "phrase": {"length": 4},
    "routings": [
      {"source": "random1", "dest": "pathAngle", "amount": 1, "level": "event", "min": -70, "max": 70, "sourceParams": {"scope": "event"}},
      {"source": "random2", "dest": "pathCurve", "amount": 1, "level": "event", "min": -2, "max": 2, "sourceParams": {"scope": "event"}}
    ]
  }],
  "routings": [], "macros": {}
})";
static std::vector<Event> playSwarm(){
    Engine en; en.loadJson(json::parse(kSwarm));
    for(double t = 0; t <= 8; t += 0.25) en.update(t);
    std::vector<Event> v; for(auto& s : en.events(0)) v.push_back(s.e);
    return v;
}
static void testEngineSwarm(){
    installSequencerSchema();
    SceneDoc doc = sceneDocFromJson(json::parse(kSwarm));
    auto probs = validate(doc);
    for(auto& p : probs) std::printf("  swarm: %s\n", p.c_str());
    CHECK(probs.empty(), "swarm scene validates");
    auto a = playSwarm(), b = playSwarm();
    CHECK(!a.empty() && a.size() == b.size(), "swarm events %zu", a.size());
    bool same = a.size() == b.size(), varied = false;
    for(size_t i = 0; i < std::min(a.size(), b.size()); i++){
        if(a[i].p.at("pathAngle") != b[i].p.at("pathAngle") || a[i].p.at("pathCurve") != b[i].p.at("pathCurve")) same = false;
        if(i && a[i].p.at("pathAngle") != a[0].p.at("pathAngle")) varied = true;
        if(a[i].p.at("pathKind") != 5 || a[i].p.at("pathSpeed") != 1.2) same = false;
    }
    CHECK(same, "per-event random paths deterministic (same seed, same paths)");
    CHECK(varied, "per-event random paths differ between events");
    // a plain instance has no path keys (flat events unchanged)
    json plain = json::parse(kSwarm); plain["instances"][0].erase("routings"); plain["instances"][0]["params"] = json::object();
    Engine ep; ep.loadJson(plain); for(double t = 0; t <= 4; t += 0.25) ep.update(t);
    bool none = true; for(auto& s : ep.events(0)) for(auto& kv : s.e.p) if(kv.first.compare(0, 4, "path") == 0) none = false;
    CHECK(none && !ep.events(0).empty(), "no path params: events carry no path keys");
}

// ---------------------------------------------------------------- demo frames
static void save(const Renderer& r, const std::string& name){
    std::string ppm = std::string(kScratch) + name + ".ppm";
    CHECK(r.writePPM(ppm), "write %s", ppm.c_str());
    std::system(("sips -s format png '" + ppm + "' --out '" + std::string(kPng) + name + ".png' >/dev/null 2>&1").c_str());
}
static void demos(){
    std::system(("mkdir -p '" + std::string(kScratch) + "' '" + std::string(kPng) + "'").c_str());
    const int W = 960, H = 540;
    // swarm: many origins, random path per event
    {
        Renderer r = makeR(W, H); r.frame.headerX = 240; r.frame.nowBeat = 0;
        std::vector<Event> ev;
        for(int i = 0; i < 160; i++){
            Event e = dot(-1 + i * 0.1, 0.1 + 0.8 * hash01(5, i));
            e.p["pathKind"] = (double)(i % 6); e.p["pathAngle"] = -70 + 140 * hash01(6, i); e.p["pathCurve"] = -2 + 4 * hash01(7, i);
            e.p["pathOriginX"] = hash01(8, i); e.p["pathOriginY"] = hash01(9, i); e.p["pathTurns"] = 0.5 + 2 * hash01(10, i);
            e.colourRole = i % 3 == 0 ? "accent" : (i % 3 == 1 ? "highlight" : "neutral");
            ev.push_back(e);
        }
        r.render(ev); save(r, "swarm_many_origins");
        r.frame.nowBeat = 4; r.render(ev); save(r, "swarm_many_origins_b4");
    }
    // starfield in perspective, random depth
    {
        Renderer r = makeR(W, H); r.frame.headerX = 200; r.frame.nowBeat = 0;
        r.frame.td.perspective = true; r.frame.td.fog = 0.6; r.frame.td.dof = 0.4; r.frame.td.vanishX = 0.7; r.frame.td.vanishY = 0.55;
        std::vector<Event> ev;
        for(int i = 0; i < 400; i++){
            Event e = dot(-2 + i * 0.12, 0.03 + 0.94 * hash01(11, i));
            e.depth = 0.3 + 2.5 * hash01(12, i);
            e.p["heightLow"] = e.p["heightLow"] + 0.02; e.p["heightHigh"] = e.p["heightHigh"] - 0.02;
            ev.push_back(e);
        }
        r.render(ev); save(r, "starfield_perspective");
    }
    // layers as planes: three layers of notes, camera dolly
    for(double cam : {0.0, 1.5}){
        Renderer r = makeR(W, H, 3); r.frame.headerX = 240; r.frame.nowBeat = 0;
        r.frame.td.perspective = true; r.frame.td.fog = 0.3; r.frame.td.planeZ = {{0, 1.5, 3, 0}}; r.frame.td.camera = cam;
        std::vector<Event> ev;
        const char* roles[] = {"neutral", "highlight", "accent"};
        for(int L = 0; L < 3; L++) for(int i = 0; i < 40; i++){
            Event e; e.kind = EventKind::Hit; e.look = "block"; e.layer = L; e.start = -2 + i * 0.75 + L * 0.25; e.length = 0.5;
            double h = 0.15 + 0.7 * hash01(20 + L, i);
            e.pitched = false; e.p = {{"heightLow", h - 0.04}, {"heightHigh", h + 0.04}, {"width", 0.5}, {"decay", 1000}, {"softness", 0.2}};
            e.colourRole = roles[L];
            ev.push_back(e);
        }
        r.render(ev); save(r, cam == 0 ? "layer_planes" : "layer_planes_dolly");
    }
}

int main(){
    testFlatDefault();
    testPaths();
    testPerspective();
    testPlanes();
    testEngineSwarm();
    demos();
    std::printf("test_3d: %d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
