// test_reading.cpp — light backgrounds (ROADMAP 5.18): reading modes, multiply / darken, alpha from brightness
//   build: clang++ -std=c++17 -O2 -DV2AUDIO_NO_OF -I src/formsV2 -I src/formsGL -I src/formsBox -I <of>/libs/json/include tests/v2/test_reading.cpp
#define FORMS2_HEADLESS
#include "v2Draw.h"
#include "v2DrawGL.h"
#include <cstdio>

using namespace forms2;
using namespace forms2::draw;

static int passed = 0, failed = 0;
#define CHECK(c, ...) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d ", __FILE__, __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } }while(0)

// one layer canvas filled with a premultiplied colour
static Renderer one(const float px[4], int reading, bool afb = false, const std::string& blend = "lighten"){
    Renderer r; r.frame.width = 8; r.frame.height = 4; r.frame.headerX = 4;
    r.layers = {LayerDef{16, blend, 1}};
    r.frame.paper.reading[0] = reading; r.frame.paper.alphaFromBrightness[0] = afb;
    r.render({});
    return r;
}
static float readOf(const float s[4], int mode){ float q[4]; layerPixel(s, 1, false, q); return readPixel(q, mode); }

int main(){
    // reading modes on single pixels
    const float clear[4] = {0, 0, 0, 0}, white[4] = {1, 1, 1, 1}, black[4] = {0, 0, 0, 1}, red[4] = {1, 0, 0, 1};
    const float paleInk[4] = {0.8f, 0.8f, 0.85f, 1}, darkInk[4] = {0.1f, 0.1f, 0.12f, 1}, halfBlack[4] = {0, 0, 0, 0.5f};
    for(int m = 0; m < 3; m++) CHECK(readOf(clear, m) == 0, "mode %d: transparent not silent", m);
    CHECK(readOf(white, ReadBrightness) == 1 && readOf(black, ReadBrightness) == 0, "brightness mode");
    CHECK(readOf(black, ReadCoverage) == 1 && readOf(white, ReadCoverage) == 1 && readOf(red, ReadCoverage) == 1, "coverage not colour-independent");
    CHECK(std::fabs(readOf(halfBlack, ReadCoverage) - 0.5f) < 1e-6, "coverage of half-opaque ink");
    CHECK(readOf(darkInk, ReadDarkInk) > readOf(paleInk, ReadDarkInk) + 0.5f, "dark ink not louder than pale ink (%.3f vs %.3f)", readOf(darkInk, ReadDarkInk), readOf(paleInk, ReadDarkInk));
    CHECK(readOf(black, ReadDarkInk) == 1 && readOf(white, ReadDarkInk) == 0, "dark ink extremes");
    CHECK(std::fabs(readOf(halfBlack, ReadDarkInk) - 0.5f) < 1e-6, "dark ink scales with opacity");

    // through the renderer: an empty score (any background) is silent in every mode
    for(int m = 0; m < 3; m++){
        Renderer r = one(clear, m);
        float mx = 0; for(float v : r.column(4)) mx = std::max(mx, v);
        CHECK(mx == 0, "mode %d: empty score column %.3f", m, mx);
        auto shown = drawgl::rgb8OverBackground(r.display(), 1, 1, 1);
        CHECK(shown[0] == 255, "white background not shown");
    }
    // a black block (dark-ink palette role "black") read in each mode
    Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = 0; e.length = 4; e.pitched = false; e.level = 1; e.colourRole = "black";
    e.p = {{"heightLow", 0.2}, {"heightHigh", 0.8}, {"width", 4}, {"decay", 1000}, {"softness", 0}};
    auto peak = [&](int mode, const std::string& role){
        Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32; r.layers = {LayerDef{16, "lighten", 1}};
        r.frame.paper.reading[0] = mode; Event x = e; x.colourRole = role; r.render({x});
        float m = 0; for(float v : r.column(34)) m = std::max(m, v); return m;
    };
    CHECK(peak(ReadBrightness, "black") < 1e-6, "black ink audible in brightness mode");
    CHECK(peak(ReadCoverage, "black") > 0.99f && std::fabs(peak(ReadCoverage, "black") - peak(ReadCoverage, "white")) < 1e-5, "coverage: black ink should sound like white");
    CHECK(peak(ReadDarkInk, "black") > 0.99f && peak(ReadDarkInk, "white") < 1e-5, "dark ink: black loud, white silent");
    // per-layer column follows the layer's mode; all-brightness keeps the composite path
    {
        Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32;
        r.layers = {LayerDef{16, "lighten", 1}, LayerDef{16, "lighten", 1}};
        Event a = e; a.colourRole = "black"; a.layer = 0; Event b = e; b.colourRole = "white"; b.layer = 1; b.p["heightLow"] = 0.85; b.p["heightHigh"] = 0.95;
        r.frame.paper.reading[0] = ReadDarkInk; r.render({a, b});
        auto l0 = r.layerColumn(0, 34), l1 = r.layerColumn(1, 34), c = r.column(34);
        float m0 = 0, m1 = 0, mc = 0; for(size_t y = 0; y < c.size(); y++){ m0 = std::max(m0, l0[y]); m1 = std::max(m1, l1[y]); mc = std::max(mc, c[y]); }
        CHECK(m0 > 0.99f && m1 > 0.99f && mc > 0.99f, "mixed modes: layer columns %.2f %.2f composite %.2f", m0, m1, mc);
    }

    // multiply / darken maths (premultiplied)
    {
        float o[4] = {0.8f, 0.6f, 0.4f, 1}; const float s[4] = {0.5f, 0.5f, 0.5f, 1};
        blendPixel(o, s, 3);
        CHECK(std::fabs(o[0] - 0.4f) < 1e-6 && std::fabs(o[1] - 0.3f) < 1e-6 && std::fabs(o[2] - 0.2f) < 1e-6 && o[3] == 1, "multiply opaque");
        float o2[4] = {0.8f, 0.6f, 0.4f, 1}; blendPixel(o2, s, 4);
        CHECK(std::fabs(o2[0] - 0.5f) < 1e-6 && std::fabs(o2[1] - 0.5f) < 1e-6 && std::fabs(o2[2] - 0.4f) < 1e-6, "darken opaque");
        float o3[4] = {0, 0, 0, 0}; blendPixel(o3, s, 3);
        CHECK(o3[0] == 0.5f && o3[3] == 1, "multiply over empty = the ink");
        float o4[4] = {0.8f, 0.6f, 0.4f, 1}; const float tr[4] = {0, 0, 0, 0}; blendPixel(o4, tr, 3); blendPixel(o4, tr, 4);
        CHECK(o4[0] == 0.8f && o4[1] == 0.6f && o4[2] == 0.4f && o4[3] == 1, "transparent ink changes nothing");
        // half-opaque black ink multiplied over white paper layer: W3C separable formula
        float o5[4] = {1, 1, 1, 1}; const float hb[4] = {0, 0, 0, 0.5f}; blendPixel(o5, hb, 3);
        CHECK(std::fabs(o5[0] - 0.5f) < 1e-6, "half black multiply over white %.3f", o5[0]);
        // through the compositor: layer 1 multiplies layer 0
        Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32;
        r.layers = {LayerDef{16, "lighten", 1}, LayerDef{16, "lighten", 1}};
        r.frame.paper.blend[1] = "multiply";
        Event a = e; a.colourRole = "white"; a.layer = 0; Event b = e; b.colourRole = "bass"; b.layer = 1;
        r.render({a, b});
        const float* p = r.composite().at(34, 16); auto bc = roleColour("bass");
        CHECK(std::fabs(p[0] - bc[0]) < 1e-5 && std::fabs(p[1] - bc[1]) < 1e-5, "multiply over white = the ink colour");
        r.frame.paper.blend[1] = "darken"; r.render({a, b}); p = r.composite().at(34, 16);
        CHECK(std::fabs(p[1] - bc[1]) < 1e-5, "darken over white = the ink colour");
        CHECK(r.blendOf(1) == 4 && r.blendOf(0) == 0, "blend override");
    }

    // alpha from brightness: a layer equals a v1-converted image (alpha = max rgb, colour at full
    // strength) composited over a coloured background
    {
        const float bg[3] = {0.2f, 0.5f, 0.9f};
        int bad = 0, n = 0;
        for(float r0 : {0.f, 0.1f, 0.5f, 1.f}) for(float g0 : {0.f, 0.3f, 0.9f}) for(float b0 : {0.f, 0.7f}) for(float cov : {0.25f, 1.f}){
            float src[4] = {r0 * cov, g0 * cov, b0 * cov, cov};             // v2 premultiplied, coverage alpha
            float q[4]; layerPixel(src, 1, true, q);
            float out[3]; for(int i = 0; i < 3; i++) out[i] = q[i] + bg[i] * (1 - q[3]);
            // v1: image rgb (over black), alpha = brightness, colour normalised to full strength
            float img[3] = {src[0], src[1], src[2]}, A = std::max({img[0], img[1], img[2]});
            for(int i = 0; i < 3; i++){
                float straight = A > 0 ? img[i] / A : 0;
                float v1 = straight * A + bg[i] * (1 - A);
                n++; if(std::fabs(v1 - out[i]) > 1e-5f) bad++;
            }
        }
        CHECK(bad == 0, "alpha from brightness differs from v1 in %d of %d", bad, n);
        // through the renderer and the display over a background
        Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32; r.layers = {LayerDef{16, "normal", 1}};
        r.frame.paper.alphaFromBrightness[0] = true;
        Event a = e; a.colourRole = "bass"; a.level = 0.6; r.render({a});
        const float* p = r.composite().at(34, 16);
        CHECK(std::fabs(p[3] - std::max({p[0], p[1], p[2]})) < 1e-6, "afb: alpha != brightness");
        r.frame.paper.reading[0] = ReadCoverage; r.render({a});
        Renderer rb = r; rb.frame.paper.reading[0] = ReadBrightness; rb.render({a});
        CHECK(std::fabs(r.column(34)[16] - rb.column(34)[16]) < 1e-6, "afb: coverage reading should equal brightness");
        Renderer off; off.frame = r.frame; off.layers = r.layers; off.frame.paper = Paper{}; off.render({a});
        CHECK(std::fabs(off.column(34)[16] - rb.column(34)[16]) < 1e-6, "afb must not change the brightness reading");
    }
    // the dark-ink palette and the "paper" block
    {
        auto d = roleColour("white", true), w = roleColour("white");
        CHECK(std::max({d[0], d[1], d[2]}) < 0.2f && w[0] == 1, "dark-ink palette");
        json j = json::parse(R"({"darkInk": true, "background": [1, 1, 1], "layers": [{"reading": "darkInk", "blend": "multiply"}, {"reading": "coverage", "alphaFromBrightness": true}]})");
        Paper p = paperFromJson(j);
        CHECK(p.darkInk && p.hasBackground && p.background[0] == 1 && p.reading[0] == ReadDarkInk && p.blend[0] == "multiply" && p.reading[1] == ReadCoverage && p.alphaFromBrightness[1] && p.reading[2] == 0, "paperFromJson");
        Paper q = paperFromJson(paperToJson(p));
        CHECK(q.reading == p.reading && q.blend == p.blend && q.alphaFromBrightness == p.alphaFromBrightness && q.darkInk, "paper round trip");
    }
    {   // scene role colours (top-level "colours", e.g. a converted v1 palette)
        auto cs = coloursFromJson(json{{"accent", "#FFC000"}, {"shadow", {0.1, 0.2, 0.3}}, {"bad", "#12"}, {"bass", 7}});
        CHECK(cs.size() == 2 && std::fabs(cs["accent"][1] - 192 / 255.f) < 1e-6 && std::fabs(cs["shadow"][2] - 0.3f) < 1e-6, "colours parsed, bad entries skipped");
        Paper pp; pp.colours = cs;
        CHECK(pp.colourOf("accent")[1] == cs["accent"][1] && pp.colourOf("bass") == roleColour("bass"), "scene colour used, missing role built-in");
        pp.darkInk = true;
        CHECK(pp.colourOf("accent") == roleColour("accent", true), "dark ink keeps its own palette");
        Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32; r.layers = {LayerDef{16, "lighten", 1}};
        Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = 0; e.length = 4; e.pitched = false; e.level = 1; e.colourRole = "accent";
        e.p = {{"heightLow", 0.2}, {"heightHigh", 0.8}, {"width", 4}, {"decay", 1000}, {"softness", 0}};
        r.frame.paper.colours = cs; r.render({e});
        const float* p = r.composite().at(34, 16);
        CHECK(std::fabs(p[0] - 1) < 1e-4 && std::fabs(p[1] - 192 / 255.f) < 1e-4 && p[2] < 1e-4, "renderer draws the scene colour (%.3f %.3f %.3f)", p[0], p[1], p[2]);
    }
    std::printf("test_reading: %d checks, %d failed\n", passed + failed, failed);
    return failed ? 1 : 0;
}
