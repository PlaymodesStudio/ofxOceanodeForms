// GPU test of the layer effects (formsLayerFx.h: H blur, V blur, Trail) in an offscreen GL 4.1
// core context, against CPU references. The shader sources are read from formsLayerFx.h.
// Build: clang++ -std=c++17 tests/test_layer_fx.cpp -framework OpenGL && ./a.out src/formsBox/formsLayerFx.h
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <functional>
#include <sstream>
#include <string>
#include <vector>

static int fails = 0;
static std::string shaderFrom(const std::string& file, const std::string& fn){
    std::ifstream f(file); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
    size_t i = s.find("inline const char* " + fn + "()");
    i = s.find("R\"(", i) + 3;
    return s.substr(i, s.find(")\"", i) - i);
}
static GLuint compile(GLenum type, const std::string& src){
    GLuint s = glCreateShader(type); const char* c = src.c_str(); glShaderSource(s, 1, &c, nullptr); glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); printf("COMPILE FAIL: %s\n", log); fails++; }
    return s;
}
static GLuint program(const std::string& vert, const std::string& frag){
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, vert)); glAttachShader(p, compile(GL_FRAGMENT_SHADER, frag));
    glBindAttribLocation(p, 0, "position"); glBindFragDataLocation(p, 0, "out_color"); glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok); if(!ok){ printf("LINK FAIL\n"); fails++; }
    return p;
}
struct Target { GLuint fbo, t; };
static GLuint tex(int w, int h, const unsigned char* px){
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
static Target target(int w, int h, const unsigned char* px = nullptr){
    Target r; r.t = tex(w, h, px); glGenFramebuffers(1, &r.fbo); glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.t, 0); return r;
}
static GLuint vao;
static void run(GLuint prog, Target& dst, int w, int h, const std::function<void()>& uniforms){
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo); glViewport(0, 0, w, h); glUseProgram(prog);
    float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL_FALSE, I);
    uniforms(); glBindVertexArray(vao); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static std::vector<unsigned char> readBack(Target& t, int w, int h){
    std::vector<unsigned char> px(w * h * 4); glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data()); return px;
}
static void bindTex(GLuint prog, const char* name, GLuint t, int unit){
    glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); glUniform1i(glGetUniformLocation(prog, name), unit);
}

int main(int, char** argv){
    std::string hdr = argv[1];
    CGLPixelFormatAttribute at[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf; GLint npf; CGLChoosePixelFormat(at, &pf, &npf); CGLContextObj ctx; CGLCreateContext(pf, nullptr, &ctx); CGLSetCurrentContext(ctx);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    float quad[] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    std::string vert = shaderFrom(hdr, "layerFxVert");
    GLuint blur = program(vert, shaderFrom(hdr, "layerFxBlurFrag"));
    GLuint trail = program(vert, shaderFrom(hdr, "layerFxTrailFrag"));
    GLuint mix = program(vert, shaderFrom(hdr, "layerFxMixFrag"));
    if(fails){ printf("shader failures\n"); return 1; }
    const int W = 400, H = 200;
    auto img = [&](std::function<int(int, int, int)> f){ std::vector<unsigned char> v(W * H * 4); for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) for(int c = 0; c < 4; c++) v[(y * W + x) * 4 + c] = (unsigned char)f(x, y, c); return v; };

    // ---- H blur: a 1-px vertical line at x = 200, sigma 5
    {
        auto src = img([](int x, int, int){ return x == 200 ? 255 : 0; });
        Target s = target(W, H, src.data()), d = target(W, H);
        run(blur, d, W, H, [&]{ bindTex(blur, "tex", s.t, 0); glUniform2f(glGetUniformLocation(blur, "size"), W, H); glUniform2f(glGetUniformLocation(blur, "dir"), 1, 0); glUniform1f(glGetUniformLocation(blur, "sigma"), 5); });
        auto o = readBack(d, W, H);
        double sum = 0, worst = 0, norm = 0;
        for(int k = -20; k <= 20; k++) norm += std::exp(-0.5 * k * k / 25.0);
        for(int x = 170; x <= 230; x++){
            double v = o[(100 * W + x) * 4], ref = 255 * std::exp(-0.5 * (x - 200) * (x - 200) / 25.0) / norm;
            sum += v; worst = std::max(worst, std::fabs(v - ref));
        }
        bool sym = true; for(int k = 1; k < 15; k++) if(std::abs((int)o[(100 * W + 200 + k) * 4] - (int)o[(100 * W + 200 - k) * 4]) > 1) sym = false;
        printf("H blur: peak %d (want %.1f), energy %.1f (want 255), worst vs CPU %.2f levels, symmetric %s\n", o[(100 * W + 200) * 4], 255 / norm, sum, worst, sym ? "yes" : "no");
        if(worst > 2.5 || std::fabs(sum - 255) > 8 || !sym){ fails++; printf("  FAIL: H blur\n"); }
    }
    // ---- V blur at the edges: a band touching the top and bottom stays uniform (edges repeat)
    {
        auto src = img([](int x, int, int){ return (x >= 100 && x < 110) ? 200 : 0; });
        Target s = target(W, H, src.data()), d = target(W, H);
        run(blur, d, W, H, [&]{ bindTex(blur, "tex", s.t, 0); glUniform2f(glGetUniformLocation(blur, "size"), W, H); glUniform2f(glGetUniformLocation(blur, "dir"), 0, 1); glUniform1f(glGetUniformLocation(blur, "sigma"), 12); });
        auto o = readBack(d, W, H);
        int lo = 255, hi = 0, outside = 0;
        for(int y = 0; y < H; y++){ int v = o[(y * W + 105) * 4]; lo = std::min(lo, v); hi = std::max(hi, v); if(o[(y * W + 120) * 4]) outside++; }
        printf("V blur (sigma 12) on a full-height band: %d..%d down its column (want 200), pixels lit beside it %d\n", lo, hi, outside);
        if(lo < 199 || hi > 201 || outside){ fails++; printf("  FAIL: V blur edges\n"); }
        // a dot 3 px from the top: its blur stays inside, the energy kept (the edge repeats)
        auto dot = img([](int x, int y, int){ return (x == 50 && y == H - 3) ? 255 : 0; });
        Target s2 = target(W, H, dot.data()), d2 = target(W, H);
        run(blur, d2, W, H, [&]{ bindTex(blur, "tex", s2.t, 0); glUniform2f(glGetUniformLocation(blur, "size"), W, H); glUniform2f(glGetUniformLocation(blur, "dir"), 0, 1); glUniform1f(glGetUniformLocation(blur, "sigma"), 6); });
        auto o2 = readBack(d2, W, H);
        double e = 0; for(int y = 0; y < H; y++) e += o2[(y * W + 50) * 4];
        printf("V blur of a dot 3 px from the top: column total %.1f (want about 255: nothing lost past the edge)\n", e);
        if(e < 200 || e > 300){ fails++; printf("  FAIL: V blur near the edge\n"); }
    }
    // ---- Trail: a dot at x = 100, 60 px long, level 0.8; alpha alike
    {
        auto src = img([](int x, int y, int){ return (x == 100 && y == 80) ? 255 : 0; });
        Target s = target(W, H, src.data()), a = target(W, H), b = target(W, H), d = target(W, H);
        float L = 60, perPx = std::exp(-6.9078f / L);
        int steps = (int)std::ceil(std::log2(std::max(2.0f, L * 1.5f)));
        Target* srcT = &s;
        for(int i = 0; i < steps; i++){
            Target* dst = (srcT == &a) ? &b : &a;
            float st = (float)(1 << i);
            run(trail, *dst, W, H, [&]{ bindTex(trail, "tex", srcT->t, 0); glUniform1f(glGetUniformLocation(trail, "stepPx"), st); glUniform1f(glGetUniformLocation(trail, "decay"), std::pow(perPx, st)); });
            srcT = dst;
        }
        run(mix, d, W, H, [&]{ bindTex(mix, "tex", srcT->t, 0); bindTex(mix, "orig", s.t, 1); glUniform1f(glGetUniformLocation(mix, "level"), 0.8f); });
        auto o = readBack(d, W, H);
        double worst = 0; int left = 0, otherRows = 0;
        for(int x = 0; x < W; x++){
            int v = o[(80 * W + x) * 4], va = o[(80 * W + x) * 4 + 3];
            double ref = x < 100 ? 0 : x == 100 ? 255 : 0.8 * 255 * std::pow(perPx, x - 100);
            worst = std::max(worst, std::fabs(v - ref));
            if(x < 100 && v) left++;
            if(std::abs(v - va) > 0) worst = std::max(worst, 99.0);   // alpha alike
        }
        for(int y = 0; y < H; y++) if(y != 80) for(int x = 0; x < W; x++) if(o[(y * W + x) * 4]) otherRows++;
        printf("Trail (60 px, level 0.8): at +1 %d (want %.0f), +30 %d (want %.0f), +60 %d (want %.1f); worst vs CPU %.2f levels; lit left of the dot %d, on other rows %d\n",
               o[(80 * W + 101) * 4], 0.8 * 255 * perPx, o[(80 * W + 130) * 4], 0.8 * 255 * std::pow(perPx, 30), o[(80 * W + 160) * 4], 0.8 * 255 * std::pow(perPx, 60), worst, left, otherRows);
        if(worst > 1.5 || left || otherRows){ fails++; printf("  FAIL: trail\n"); }
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
