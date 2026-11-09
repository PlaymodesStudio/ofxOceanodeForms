// test_gl_equiv.cpp — GPU event pass vs the CPU reference, headless (macOS CGL, OpenGL 3.2 core)
// Renders example / stress frames per layer with the real packEvent() and the event shaders of
// v2DrawGL.h (read from the file at run time) into RGBA16F, and compares with draw::Renderer
// (events only: effects, covers and blob masks are stripped from both sides). Optionally also
// runs a reference copy of v2DrawGL.h (-ref=<old v2DrawGL.h>) with full-height quads and compares.
// Plus an effect-pass check: effectFrag (with packEffect / noiseLattice) vs applyEffectSpan on a
// real layer canvas for page / noise / tile (and a few older modes). Masks / covers / composite are not covered.
//   build: clang++ -std=c++17 -O2 -DV2AUDIO_NO_OF -DGL_SILENCE_DEPRECATION -I src/formsV2 -I src/formsGL -I src/formsBox
//          -I <of>/libs/json/include tests/v2/test_gl_equiv.cpp -framework OpenGL -o test_gl_equiv
#define FORMS2_HEADLESS
#include "v2Engine.h"
#include "v2DrawGL.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>

using namespace forms2;

static std::string readFile(const std::string& p){ std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
// the raw string of `static const char* <name> = R"(...)";` in a source file
static std::string shaderOf(const std::string& src, const std::string& name){
    size_t a = src.find("static const char* " + name + " = R\"(");
    if(a == std::string::npos) return "";
    a = src.find("R\"(", a) + 3;
    size_t b = src.find(")\";", a);
    return src.substr(a, b - a);
}

struct Prog {
    GLuint id = 0;
    bool build(const std::string& vs, const std::string& fs){
        auto comp = [](GLenum t, const std::string& s) -> GLuint {
            GLuint sh = glCreateShader(t); const char* c = s.c_str(); glShaderSource(sh, 1, &c, nullptr); glCompileShader(sh);
            GLint ok = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
            if(!ok){ char log[4096]; glGetShaderInfoLog(sh, 4096, nullptr, log); std::printf("shader error: %s\n", log); return 0; }
            return sh;
        };
        GLuint v = comp(GL_VERTEX_SHADER, vs), f = comp(GL_FRAGMENT_SHADER, fs);
        if(!v || !f) return false;
        id = glCreateProgram(); glAttachShader(id, v); glAttachShader(id, f);
        glBindAttribLocation(id, 0, "position");
        for(int a = 0; a < 12; a++) glBindAttribLocation(id, (GLuint)(1 + a), drawgl::kInstNames[a]);
        glLinkProgram(id);
        GLint ok = 0; glGetProgramiv(id, GL_LINK_STATUS, &ok);
        if(!ok){ char log[4096]; glGetProgramInfoLog(id, 4096, nullptr, log); std::printf("link error: %s\n", log); return false; }
        return true;
    }
};

struct GL {
    CGLContextObj ctx = nullptr;
    GLuint vao = 0, quad = 0, inst = 0, fbo = 0, tex = 0;
    int W = 0, H = 0;
    bool init(int w, int h){
        CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
        CGLPixelFormatObj pf = nullptr; GLint n = 0;
        if(CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError || !pf) return false;
        if(CGLCreateContext(pf, nullptr, &ctx) != kCGLNoError) return false;
        CGLDestroyPixelFormat(pf); CGLSetCurrentContext(ctx);
        W = w; H = h;
        glGenTextures(1, &tex); glBindTexture(GL_TEXTURE_2D, tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, W, H, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glGenFramebuffers(1, &fbo); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) return false;
        const float corners[8] = {0, 0, 1, 0, 0, 1, 1, 1};
        glGenBuffers(1, &quad); glBindBuffer(GL_ARRAY_BUFFER, quad); glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
        glGenBuffers(1, &inst);
        glGenVertexArrays(1, &vao); glBindVertexArray(vao);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        glBindBuffer(GL_ARRAY_BUFFER, inst);
        for(int a = 0; a < 12; a++){
            glEnableVertexAttribArray(1 + a);
            glVertexAttribPointer(1 + a, 4, GL_FLOAT, GL_FALSE, sizeof(drawgl::Inst), (const void*)(size_t)(a * 16));
            glVertexAttribDivisor(1 + a, 1);
        }
        glViewport(0, 0, W, H);
        return true;
    }
    void draw(Prog& p, const std::vector<drawgl::Inst>& v){
        if(v.empty()) return;
        glBindBuffer(GL_ARRAY_BUFFER, inst);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(drawgl::Inst)), v.data(), GL_STREAM_DRAW);
        glUseProgram(p.id);
        glUniform2f(glGetUniformLocation(p.id, "uSize"), (float)W, (float)H);
        glUniform1i(glGetUniformLocation(p.id, "uMaskOnly"), 0);
        glUniform2f(glGetUniformLocation(p.id, "uPxScale"), 1.f, 1.f);
        glBindVertexArray(vao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)v.size());
    }
    // one layer: GL_MAX instances then additive ones, read back (row 0 = top)
    std::vector<float> layer(Prog& p, const std::vector<drawgl::Inst>& mx, const std::vector<drawgl::Inst>& ad, double& ms){
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE);
        glFinish();
        auto t = std::chrono::steady_clock::now();
        glBlendEquation(GL_MAX); draw(p, mx);
        glBlendEquation(GL_FUNC_ADD); draw(p, ad);
        glFinish();
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t).count();
        std::vector<float> px((size_t)W * H * 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px.data());
        return px;
    }
};

static double mad(const std::vector<float>& a, const std::vector<float>& b, double* mx = nullptr){
    double s = 0, m = 0;
    for(size_t i = 0; i < a.size(); i++){ double d = std::fabs(std::min(1.f, a[i]) - std::min(1.f, b[i])); s += d; m = std::max(m, d); }
    if(mx) *mx = m;
    return s / std::max<size_t>(1, a.size());
}
static double meanOf(const std::vector<float>& a){ double s = 0; for(float x : a) s += std::min(1.f, x); return s / std::max<size_t>(1, a.size()); }

int main(int argc, char** argv){
    installSequencerSchema();
    std::string refPath, cur = readFile("src/formsV2/v2DrawGL.h");
    int W = 960, H = 540;
    std::vector<std::string> scenes;
    for(int i = 1; i < argc; i++){ std::string a = argv[i]; if(a.rfind("-ref=", 0) == 0) refPath = a.substr(5); else if(a[0] != '-') scenes.push_back(a); }
    if(scenes.empty()) scenes = {"tests/v2/scenes/stress/stress_medium.json", "tests/v2/scenes/examples/04_starfield_depth.json",
                                 "tests/v2/scenes/examples/09_chord_arp.json", "tests/v2/scenes/examples/12_animations.json",
                                 "tests/v2/scenes/examples/11_modulation_levels.json", "tests/v2/scenes/examples/20_swarm_paths.json",
                                 "tests/v2/scenes/examples/24_mass_and_masks.json"};
    int bgFails = 0;
    {   // background colour is display only: the column reads the premultiplied score (CPU path)
        draw::Renderer r; r.frame.width = 64; r.frame.height = 32; r.frame.headerX = 32; r.layers = {LayerDef{16, "lighten", 1}};
        r.render({});
        auto col = r.column(32); float mx = 0; for(float v : col) mx = std::max(mx, v);
        auto img = drawgl::rgb8OverBackground(r.display(), 1, 1, 1);
        if(mx != 0 || img[0] != 255){ std::printf("FAIL background: empty column max %g, shown %d\n", mx, img[0]); bgFails++; }
        draw::Canvas c; c.resize(1, 1); c.px = {0.5f, 0.5f, 0.5f, 0.5f};      // 50 %% transparent white (premultiplied)
        auto px = drawgl::rgb8OverBackground(c, 0, 0, 0);
        float colv = std::min(1.f, std::max({c.px[0], c.px[1], c.px[2]}));   // what column() reads
        if(std::fabs(colv - 0.5f) > 1e-6 || std::abs(px[0] - 128) > 1){ std::printf("FAIL background: half white column %g shown %d\n", colv, px[0]); bgFails++; }
        // a real half-level white element through the renderer
        Event e; e.kind = EventKind::Hit; e.look = "block"; e.start = 0; e.length = 4; e.pitched = false; e.level = 0.5; e.colourRole = "white";
        e.p = {{"heightLow", 0.2}, {"heightHigh", 0.8}, {"width", 4}, {"decay", 1000}, {"softness", 0}};
        r.render({e}); auto c2 = r.column(34); float m2 = 0; for(float v : c2) m2 = std::max(m2, v);
        std::printf("background check: empty column max %g, white bg shown %d; half-transparent white column %.3f (renderer block level 0.5: %.3f)\n", mx, img[0], colv, m2);
    }
    GL gl; if(!gl.init(W, H)){ std::printf("SKIP: no OpenGL context\n"); return 0; }
    Prog cp, rp; bool haveRef = false;
    if(!cp.build(shaderOf(cur, "quadVert"), shaderOf(cur, "eventFrag"))){ std::printf("FAIL: current shaders\n"); return 1; }
    // the other shaders of the file must compile too
    for(auto n : {"effectFrag", "maskFrag", "coverFrag", "blobFrag", "compFrag", "compAllFrag", "bgFrag"}){
        std::string fs = shaderOf(cur, n); if(fs.empty()) continue;
        Prog p; if(!p.build(shaderOf(cur, "fullVert"), fs)){ std::printf("FAIL: %s\n", n); return 1; }
    }
    if(!refPath.empty()){ std::string r = readFile(refPath); haveRef = rp.build(shaderOf(r, "quadVert"), shaderOf(r, "eventFrag")); }
    int fails = bgFails;
    double tCur = 0, tRef = 0, sumCpu = 0, sumRef = 0, sumRefCpu = 0; int nLayers = 0;
    for(auto& path : scenes){
        Engine en;
        if(!en.loadText(readFile(path))){ std::printf("%s: load failed\n", path.c_str()); fails++; continue; }
        for(double beat : {8.25, 17.6, 31.1}){
            std::vector<LayerDef> layers = en.scene().layers; layers.resize(4);
            std::vector<LayerView> views;
            for(int l = 0; l < 4; l++){ layers[(size_t)l].zoomBeats = en.layerZoom(l, beat); views.push_back(en.viewFor(l, beat, W, H, W * 0.5)); }
            en.update(beat, views);
            std::vector<Event> evs; std::unordered_map<uint64_t, std::shared_ptr<const EventMod>> mods;
            std::vector<VisibleEvent> vis;
            for(int l = 0; l < 4; l++){
                vis.clear(); en.visibleInto(l, views[(size_t)l], vis);
                for(auto& ve : vis){
                    const Event& e = ve.ev->e;
                    if(e.kind == EventKind::Effect || blob::enabled(e)) continue;   // event pass only
                    evs.push_back(e); evs.back().p.erase("coverEnable");
                    mods[e.id] = en.modOf(*ve.ev);
                }
            }
            draw::Frame f; f.width = W; f.height = H; f.headerX = W * 0.5; f.nowBeat = beat;
            auto ha = en.harmonyAt(beat); if(ha.scale) f.scale = *ha.scale;
            f.mod.drawn = [&](const Event& e, const std::string& d, double u, double b){ auto it = mods.find(e.id); return it == mods.end() ? b : it->second->drawn(d, b, u); };
            f.mod.animated = [&](const Event& e, const std::string& d, Beat now, double b){ auto it = mods.find(e.id); return it == mods.end() ? b : it->second->animated(d, b, now); };
            draw::Renderer cpu; cpu.frame = f; cpu.layers = layers; cpu.render(evs);
            draw::Renderer ref; ref.frame = f; ref.layers = layers;
            for(int L = 0; L < 4; L++){
                std::vector<drawgl::Inst> mx, ad, mxF, adF;
                for(auto& e : evs) if(std::clamp(e.layer, 0, 3) == L && ref.shown(e)){
                    drawgl::Inst i; if(!drawgl::packEvent(ref, H, e, i)) continue;
                    bool add = e.look == "additive";
                    (add ? ad : mx).push_back(i);
                    i.v[0][1] = 0; i.v[0][3] = (float)H;                         // full height (the old quads)
                    (add ? adF : mxF).push_back(i);
                }
                if(mx.empty() && ad.empty()) continue;
                double ms = 0;
                auto g = gl.layer(cp, mx, ad, ms); tCur += ms;
                auto gF = gl.layer(cp, mxF, adF, ms);
                double mTight = 0, dCpu = mad(g, cpu.layer(L).px), dFull = mad(g, gF, &mTight);
                sumCpu += dCpu; nLayers++;
                std::printf("%-48s beat %5.2f L%d inst %5zu  mean %.4f  |gpu-cpu| %.4f  |tight-full| %.6f (max %.3f)",
                            path.substr(path.rfind('/') + 1).c_str(), beat, L, mx.size() + ad.size(), meanOf(g), dCpu, dFull, mTight);
                if(dFull > 1e-4 || mTight > 0.05){ std::printf("  FAIL tight quads cut drawing"); fails++; }
                if(haveRef){
                    auto r = gl.layer(rp, mxF, adF, ms); tRef += ms;
                    double mr = 0, dR = mad(g, r, &mr), dRC = mad(r, cpu.layer(L).px);
                    sumRef += dR; sumRefCpu += dRC;
                    std::printf("  |new-old| %.5f (max %.3f)  |old-cpu| %.4f", dR, mr, dRC);
                }
                std::printf("\n");
            }
        }
    }
    // ---- effect pass: GPU effectFrag vs CPU applyEffectSpan on scene 24's layers at beat 8.25
    {
        Prog fx; if(!fx.build(shaderOf(cur, "fullVert"), shaderOf(cur, "effectFrag"))){ std::printf("FAIL: effectFrag\n"); return 1; }
        Engine en; en.loadText(readFile("tests/v2/scenes/examples/24_mass_and_masks.json"));
        double beat = 8.25;
        std::vector<LayerDef> layers = en.scene().layers; layers.resize(4);
        std::vector<LayerView> views; for(int l = 0; l < 4; l++) views.push_back(en.viewFor(l, beat, W, H, W * 0.5));
        en.update(beat, views);
        std::vector<Event> evs; std::vector<VisibleEvent> vis;
        for(int l = 0; l < 2; l++){ vis.clear(); en.visibleInto(l, views[(size_t)l], vis); for(auto& ve : vis) if(ve.ev->e.kind != EventKind::Effect && !blob::enabled(ve.ev->e)) evs.push_back(ve.ev->e); }
        draw::Frame f; f.width = W; f.height = H; f.headerX = W * 0.5; f.nowBeat = beat;
        draw::Renderer cpu; cpu.frame = f; cpu.layers = layers; cpu.render(evs);
        GLuint src = 0; glGenTextures(1, &src); GLuint lat = 0; glGenTextures(1, &lat);
        GLuint vao2 = 0; glGenVertexArrays(1, &vao2); glBindVertexArray(vao2); glBindBuffer(GL_ARRAY_BUFFER, gl.quad);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr);
        struct T { const char* look; std::map<std::string, double> p; };
        std::vector<T> tests = {
            {"page", {{"pagePower", 1}}}, {"page", {{"pagePower", 3}, {"amount", 0.7}}},
            {"noise", {{"noiseScale", 0.2}}}, {"noise", {{"noiseScale", 0.05}, {"noiseContrast", 2}, {"maskInvert", 1}}}, {"noise", {{"noiseScale", 0.1}, {"noiseThreshold", 0.5}}},
            {"tile", {{"repeats", 2}}}, {"tile", {{"repeats", 4}}}, {"reverse", {}}, {"band", {{"shape", 3}}}};
        double sNew = 0, sSkip = 0; int nNew = 0;
        for(int L = 0; L < 2; L++) for(auto& t : tests){
            const draw::Canvas& in = cpu.layer(L);
            Event e; e.kind = EventKind::Effect; e.look = t.look; e.layer = L; e.seed = 77 + L; e.p = t.p;
            LayerView v = cpu.viewOf(L); e.start = v.nowBeat + (200 - v.headerX) / v.pxPerBeat; e.length = 500 / v.pxPerBeat;
            draw::Canvas c = in; draw::applyEffect(c, e, v);
            int mode = 0; float pp[4], perm[32];
            if(!drawgl::packEffect(e, v, mode, pp, perm)){ std::printf("FAIL effect %s not packed\n", t.look); fails++; continue; }
            float x0 = (float)v.xOf(e.start), x1 = (float)v.xOf(e.start + e.length);
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, in.px.data());   // row 0 = top, as the app's FBOs
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            std::vector<float> lb; int lnx = 0, lny = 0; float cell = 1;
            if(mode == 18) cell = drawgl::noiseLattice(e, x1 - x0, H, lb, lnx, lny);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, lat);
            if(lnx > 0) glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, lnx, lny, 0, GL_RED, GL_FLOAT, lb.data());
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
            glActiveTexture(GL_TEXTURE0);
            glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo); glDisable(GL_BLEND);
            glUseProgram(fx.id);
            auto U = [&](const char* n){ return glGetUniformLocation(fx.id, n); };
            glUniform1i(U("uSrc"), 0); glUniform1i(U("uLat"), 1);
            glUniform2f(U("uSize"), (float)W, (float)H); glUniform1f(U("uX0"), x0); glUniform1f(U("uX1"), x1);
            glUniform1i(U("uMode"), mode); glUniform4f(U("uP"), pp[0], pp[1], pp[2], pp[3]);
            glUniform1f(U("uAmount"), (float)e.param("amount", 1)); glUniform1i(U("uShape"), std::clamp((int)e.param("shape", 0), 0, 4));
            glUniform1f(U("uRateFrac"), (float)(e.param("gateRate", 0.5) / std::max(1e-6, e.length)));
            glUniform1f(U("uSeed"), (float)draw::hash01(e.seed, 77) * 100.f); glUniform1fv(U("uPerm"), 32, perm);
            glUniform1f(U("uCell"), cell); glUniform2i(U("uLatN"), lnx, lny);
            glBindVertexArray(vao2); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            std::vector<float> g((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, g.data());
            double d = mad(g, c.px), dSkip = mad(in.px, c.px);
            bool isNew = mode >= 17;
            if(isNew){ sNew += d; sSkip += dSkip; nNew++; }
            std::printf("effect %-8s L%d mode %2d  |gpu-cpu| %.5f  (effect skipped: %.5f)%s\n", t.look, L, mode, d, dSkip, d > 0.01 ? "  FAIL" : "");
            if(d > 0.01) fails++;
        }
        std::printf("effects page/noise/tile: mean |gpu-cpu| %.5f, before (GPU skipped them) %.5f\n", sNew / std::max(1, nNew), sSkip / std::max(1, nNew));
        // in-place narrow passes (GLRenderer::applyEffect): a pass scissored to fxColumns() over a
        // target already holding the input must equal the full-screen pass, for wide and 16th slots
        std::vector<T> ip = {{"mask", {}}, {"notch", {{"bandLow", 0.3}, {"bandHigh", 0.6}}}, {"band", {{"shape", 3}}}, {"gate", {}}, {"blank", {}},
                             {"stutter", {}}, {"repeat", {}}, {"reverse", {}}, {"shuffle", {}}, {"freeze", {}}, {"echo", {{"echoCount", 4}}},
                             {"tears", {{"tearAmount", 1}}}, {"tile", {{"repeats", 4}}}, {"noise", {{"noiseScale", 0.1}}}, {"vignette", {}}, {"page", {}}};
        int ipN = 0, ipFail = 0; double ipCols = 0;
        for(int L = 0; L < 2; L++) for(double slotPx : {500.0, 15.0}) for(auto& t : ip){
            const draw::Canvas& in = cpu.layer(L);
            Event e; e.kind = EventKind::Effect; e.look = t.look; e.layer = L; e.seed = 91 + L; e.p = t.p;
            LayerView v = cpu.viewOf(L); e.start = v.nowBeat + (300 - v.headerX) / v.pxPerBeat; e.length = slotPx / v.pxPerBeat;
            int mode = 0; float pp[4], perm[32];
            if(!drawgl::packEffect(e, v, mode, pp, perm)) continue;
            float x0 = (float)v.xOf(e.start), x1 = (float)v.xOf(e.start + e.length);
            int c0 = 0, c1 = 0;
            if(!drawgl::fxColumns(e, v, x0, x1, W, c0, c1)) continue;
            glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, in.px.data());
            std::vector<float> lb; int lnx = 0, lny = 0; float cell = 1;
            if(mode == 18) cell = drawgl::noiseLattice(e, x1 - x0, H, lb, lnx, lny);
            glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, lat);
            if(lnx > 0) glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, lnx, lny, 0, GL_RED, GL_FLOAT, lb.data());
            glActiveTexture(GL_TEXTURE0);
            glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo); glDisable(GL_BLEND);
            glUseProgram(fx.id);
            auto U = [&](const char* n){ return glGetUniformLocation(fx.id, n); };
            auto pass = [&](float a, float b){
                glUniform1i(U("uSrc"), 0); glUniform1i(U("uLat"), 1);
                glUniform2f(U("uSize"), (float)W, (float)H); glUniform1f(U("uX0"), a); glUniform1f(U("uX1"), b);
                glUniform1i(U("uMode"), mode); glUniform4f(U("uP"), pp[0], pp[1], pp[2], pp[3]);
                glUniform1f(U("uAmount"), (float)e.param("amount", 1)); glUniform1i(U("uShape"), std::clamp((int)e.param("shape", 0), 0, 4));
                glUniform1f(U("uRateFrac"), (float)(e.param("gateRate", 0.5) / std::max(1e-6, e.length)));
                glUniform1f(U("uSeed"), (float)draw::hash01(e.seed, 77) * 100.f); glUniform1fv(U("uPerm"), 32, perm);
                glUniform1f(U("uCell"), cell); glUniform2i(U("uLatN"), lnx, lny);
                glBindVertexArray(vao2); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            };
            std::vector<float> full((size_t)W * H * 4), inPlace((size_t)W * H * 4);
            pass(x0, x1); glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, full.data());
            pass(-200.f, -100.f);                                   // the target holds the input
            glEnable(GL_SCISSOR_TEST); glScissor(c0, 0, c1 - c0, H); pass(x0, x1); glDisable(GL_SCISSOR_TEST);
            glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, inPlace.data());
            double mx = 0, d = mad(full, inPlace, &mx);
            ipN++; ipCols += c1 - c0;
            if(mx > 1e-6){ ipFail++; fails++; std::printf("FAIL in-place %-8s L%d slot %3.0f px cols [%d,%d): |full-inplace| %.6f max %.4f\n", t.look, L, slotPx, c0, c1, d, mx); }
        }
        std::printf("in-place effect passes: %d checked, %d differ from the full pass; mean columns per pass %.0f of %d\n", ipN, ipFail, ipCols / std::max(1, ipN), W);
        // batched passes (GLRenderer::fxBatchPass): N slots in one pass with uN / uS / uSP / uSQ must equal
        // the N single passes applied one after another. Non-overlapping readers, overlapping gains,
        // overlapping echoes, and a mix, each on both layers.
        struct BT { const char* name; std::vector<std::pair<const char*, double>> slots; };   // look, x0 px (slot 60 px)
        std::vector<BT> bt = {
            {"readers apart", {{"stutter", 100}, {"repeat", 300}, {"reverse", 500}, {"freeze", 700}, {"tears", 900}, {"tile", 1100}}},
            {"gains overlapping", {{"blank", 100}, {"band", 130}, {"gate", 300}, {"notch", 320}, {"mask", 340}, {"vignette", 600}, {"page", 620}, {"fadeIn", 640}, {"fadeOut", 660}}},
            {"echoes overlapping", {{"echo", 100}, {"echo", 140}, {"echo", 400}}},
            {"mixed apart", {{"blank", 100}, {"stutter", 300}, {"band", 500}, {"gate", 520}, {"repeat", 700}, {"echo", 1500}}}};
        int btN = 0, btFail = 0;
        for(int L = 0; L < 2; L++) for(auto& t : bt){
            const draw::Canvas& in = cpu.layer(L);
            LayerView v = cpu.viewOf(L);
            std::vector<Event> es; std::vector<int> modes; std::vector<std::array<float, 4>> ps;
            for(auto& sl : t.slots){
                Event e; e.kind = EventKind::Effect; e.look = sl.first; e.layer = L; e.seed = 123 + (uint64_t)es.size(); e.p = {{"echoCount", 3}, {"tearAmount", 1}, {"repeats", 3}};
                e.start = v.nowBeat + (sl.second - v.headerX) / v.pxPerBeat; e.length = 60 / v.pxPerBeat;
                int mode = 0; float pp[4], perm[32];
                if(!drawgl::packEffect(e, v, mode, pp, perm)) continue;
                es.push_back(e); modes.push_back(mode); ps.push_back({pp[0], pp[1], pp[2], pp[3]});
            }
            auto upload = [&](const std::vector<float>& px){ glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, W, H, 0, GL_RGBA, GL_FLOAT, px.data()); };
            glBindFramebuffer(GL_FRAMEBUFFER, gl.fbo); glDisable(GL_BLEND); glUseProgram(fx.id);
            auto U = [&](const char* n){ return glGetUniformLocation(fx.id, n); };
            float perm0[32] = {0};
            glUniform1i(U("uSrc"), 0); glUniform1i(U("uLat"), 1); glUniform2f(U("uSize"), (float)W, (float)H); glUniform1fv(U("uPerm"), 32, perm0); glUniform1f(U("uCell"), 1); glUniform2i(U("uLatN"), 0, 0);
            // sequential: each single pass reads the previous result
            std::vector<float> cur = in.px, out((size_t)W * H * 4);
            for(size_t i = 0; i < es.size(); i++){
                const Event& e = es[i];
                upload(cur);
                glUniform1i(U("uN"), 0);
                glUniform1f(U("uX0"), (float)v.xOf(e.start)); glUniform1f(U("uX1"), (float)v.xOf(e.start + e.length));
                glUniform1i(U("uMode"), modes[i]); glUniform4f(U("uP"), ps[i][0], ps[i][1], ps[i][2], ps[i][3]);
                glUniform1f(U("uAmount"), 1.f); glUniform1i(U("uShape"), 0); glUniform1f(U("uRateFrac"), (float)(0.5 / e.length));
                glUniform1f(U("uSeed"), (float)draw::hash01(e.seed, 77) * 100.f);
                glBindVertexArray(vao2); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
                glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, out.data()); cur = out;
            }
            // batched: one pass
            std::vector<float> S4, SP, SQ;
            for(size_t i = 0; i < es.size(); i++){
                const Event& e = es[i];
                S4.insert(S4.end(), {(float)v.xOf(e.start), (float)v.xOf(e.start + e.length), (float)modes[i], 0.f});
                SP.insert(SP.end(), {ps[i][0], ps[i][1], ps[i][2], ps[i][3]});
                SQ.insert(SQ.end(), {1.f, (float)(0.5 / e.length), (float)draw::hash01(e.seed, 77) * 100.f, 0.f});
            }
            upload(in.px);
            glUniform1i(U("uN"), (int)es.size());
            glUniform4fv(U("uS"), (int)es.size(), S4.data()); glUniform4fv(U("uSP"), (int)es.size(), SP.data()); glUniform4fv(U("uSQ"), (int)es.size(), SQ.data());
            glBindVertexArray(vao2); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            std::vector<float> batched((size_t)W * H * 4); glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, batched.data());
            glUniform1i(U("uN"), 0);
            double mx = 0, d = mad(cur, batched, &mx), dIn = mad(in.px, batched);
            btN++;
            // must equal the sequence (to one RGBA16F ulp: the sequence rounds between passes, the batch
            // multiplies overlapping gains in one go) and must have done something
            bool bad = mx > 1e-3 || dIn < 1e-7;
            if(bad){ btFail++; fails++; }
            std::printf("batched %-20s L%d %zu slots  |sequential-batched| %.7f max %.5f  (vs input %.5f)%s\n", t.name, L, es.size(), d, mx, dIn, bad ? "  FAIL" : "");
        }
        std::printf("batched effect passes: %d checked, %d differ from the sequential passes\n", btN, btFail);
    }
    std::printf("layers %d: mean |gpu-cpu| %.4f", nLayers, sumCpu / std::max(1, nLayers));
    if(haveRef) std::printf("  mean |new-old| %.5f  mean |old-cpu| %.4f  event pass ms: new %.1f old %.1f", sumRef / std::max(1, nLayers), sumRefCpu / std::max(1, nLayers), tCur, tRef);
    else std::printf("  event pass ms %.1f", tCur);
    std::printf("\n%s (%d failures)\n", fails ? "FAILED" : "ok", fails);
    return fails ? 1 : 0;
}
