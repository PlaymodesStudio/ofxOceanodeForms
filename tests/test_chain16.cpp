// The live path of one layer at 16 bits per channel, offscreen (GL 4.1 core, CGL): a page drawn by
// the renderer (16-bit straight alpha) -> GL_RGBA16 texture -> premultiplied into a GL_RGBA16 layer
// buffer -> the trail passes of formsLayerFx.h on GL_RGBA16 intermediates -> read back as 16 bits.
// Checks, on the faint part of every lit row (0.1% .. 6% of full scale, where 8 bits had 1..15
// levels): the brightness changes almost every pixel (no plateaus) and the hue stays the note's.
// The same page through an 8-bit chain (GL_RGBA8, the former formats) is measured for comparison.
// Build: clang++ -std=c++17 -O2 -I src/formsGL -I <OF>/libs/json/include tests/test_chain16.cpp -framework OpenGL
// Run:   ./a.out <generators.json> <request.json> src/formsBox/formsLayerFx.h [trail px = 285] [trail level = 0.41]
#define GL_SILENCE_DEPRECATION
#include "formsGLSketch.h"
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <functional>
#include <sstream>
using namespace formsgl;
static int fails = 0;
static std::string shaderFrom(const std::string& file, const std::string& fn){
    std::ifstream f(file); std::stringstream ss; ss << f.rdbuf(); std::string s = ss.str();
    size_t i = s.find("inline const char* " + fn + "()"); i = s.find("R\"(", i) + 3;
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
struct Target { GLuint fbo = 0, t = 0; };
static Target target(int w, int h, GLenum internal, GLenum type, const void* px = nullptr){
    Target r; glGenTextures(1, &r.t); glBindTexture(GL_TEXTURE_2D, r.t);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, internal, w, h, 0, GL_RGBA, type, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glGenFramebuffers(1, &r.fbo); glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.t, 0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){ printf("FAIL: framebuffer of format 0x%x is not complete\n", internal); fails++; }
    return r;
}
static GLuint vao;
static void run(GLuint prog, Target& dst, int w, int h, const std::function<void()>& uniforms){
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo); glViewport(0, 0, w, h); glDisable(GL_BLEND); glUseProgram(prog);
    float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL_FALSE, I);
    uniforms(); glBindVertexArray(vao); glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static void bindTex(GLuint prog, const char* name, GLuint t, int unit){
    glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, t); glUniform1i(glGetUniformLocation(prog, name), unit);
}
struct Stats { long px = 0, changes = 0; int longest = 0; double hueDev = 0; long huePx = 0; int levels = 0; };
// out: w x h RGBA, 16-bit values (premultiplied). The faint, lit part of every row.
static Stats measure(const std::vector<uint16_t>& o, int w, int h){
    Stats s; std::vector<char> seen(65536, 0);
    for(int y = 0; y < h; y++){
        int run = 0;
        for(int x = 1; x < w; x++){
            const uint16_t* p = &o[((size_t)y * w + x) * 4], *q = p - 4;
            bool faint = p[0] >= 66 && p[0] <= 3932 && q[0] >= 66 && q[0] <= 3932 && p[2] * 4 < p[0];   // the yellow notes
            if(!faint){ run = 0; continue; }
            s.px++; if(!seen[p[0]]){ seen[p[0]] = 1; s.levels++; }
            if(p[0] != q[0]){ s.changes++; run = 0; } else { run++; s.longest = std::max(s.longest, run + 1); }
            if(p[0] >= 600){ s.hueDev = std::max(s.hueDev, std::fabs(p[1] / (double)p[0] - 192.0 / 255.0)); s.huePx++; }
        }
    }
    return s;
}
int main(int argc, char** argv){
    if(argc < 4){ printf("usage: a.out generators.json request.json formsLayerFx.h [trailPx] [level]\n"); return 2; }
    Schemas schemas; if(!schemas.load(argv[1])){ printf("cannot read %s\n", argv[1]); return 2; }
    std::ifstream in(argv[2]); json req; in >> req;
    auto r = Sketch(schemas).render(req);
    if(!r.error.empty()){ printf("render: %s\n", r.error.c_str()); return 2; }
    const int W = r.w, H = r.h;
    float L = argc > 4 ? (float)atof(argv[4]) : 285.0f, level = argc > 5 ? (float)atof(argv[5]) : 0.41f;
    CGLPixelFormatAttribute at[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf; GLint npf; CGLChoosePixelFormat(at, &pf, &npf); CGLContextObj ctx; CGLCreateContext(pf, nullptr, &ctx); CGLSetCurrentContext(ctx);
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    float quad[] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo); glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    std::string vert = shaderFrom(argv[3], "layerFxVert");
    GLuint trail = program(vert, shaderFrom(argv[3], "layerFxTrailFrag")), mix = program(vert, shaderFrom(argv[3], "layerFxMixFrag"));
    // straight alpha -> premultiplied, as the scrollers' blend (SRC_ALPHA, ONE_MINUS_SRC_ALPHA over a cleared buffer)
    GLuint premul = program(vert, "#version 410\nuniform sampler2D tex; out vec4 out_color;\nvoid main(){ vec4 c = texelFetch(tex, ivec2(gl_FragCoord.xy), 0); out_color = vec4(c.rgb * c.a, c.a); }\n");
    std::vector<uint8_t> r8(r.rgba.size()); for(size_t i = 0; i < r8.size(); i++) r8[i] = (uint8_t)((r.rgba[i] + 128) / 257);
    Stats res[2];
    for(int bits : {16, 8}){
        GLenum fmt = bits == 16 ? GL_RGBA16 : GL_RGBA8, type = bits == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_BYTE;
        Target page = target(W, H, fmt, type, bits == 16 ? (const void*)r.rgba.data() : (const void*)r8.data());
        Target layer = target(W, H, fmt, type), a = target(W, H, fmt, type), b = target(W, H, fmt, type), d = target(W, H, fmt, type);
        run(premul, layer, W, H, [&]{ bindTex(premul, "tex", page.t, 0); });
        float perPx = std::exp(-6.9078f / L); int steps = (int)std::ceil(std::log2(std::max(2.0f, L * 1.5f)));
        Target* srcT = &layer;
        for(int i = 0; i < steps; i++){
            Target* dst = (srcT == &a) ? &b : &a; float st = (float)(1 << i);
            run(trail, *dst, W, H, [&]{ bindTex(trail, "tex", srcT->t, 0); glUniform1f(glGetUniformLocation(trail, "stepPx"), st); glUniform1f(glGetUniformLocation(trail, "decay"), std::pow(perPx, st)); });
            srcT = dst;
        }
        run(mix, d, W, H, [&]{ bindTex(mix, "tex", srcT->t, 0); bindTex(mix, "orig", layer.t, 1); glUniform1f(glGetUniformLocation(mix, "level"), level); });
        std::vector<uint16_t> o((size_t)W * H * 4);
        glBindFramebuffer(GL_FRAMEBUFFER, d.fbo); glPixelStorei(GL_PACK_ALIGNMENT, 1); glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_SHORT, o.data());
        if(glGetError() != GL_NO_ERROR){ printf("FAIL: GL error in the %d-bit chain\n", bits); fails++; }
        Stats s = measure(o, W, H); res[bits == 16 ? 0 : 1] = s;
        printf("%2d-bit chain: %ld faint pixels; brightness changes on %.1f%% of them; longest flat run %d px; %d distinct levels; hue off by at most %.4f (of %ld px)\n",
               bits, s.px, s.px ? 100.0 * s.changes / s.px : 0.0, s.longest, s.levels, s.hueDev, s.huePx);
        if(getenv("CHAIN_DUMP")){ FILE* f = fopen((std::string(getenv("CHAIN_DUMP")) + "/chain" + std::to_string(bits) + ".raw").c_str(), "wb"); fwrite(o.data(), 2, o.size(), f); fclose(f); printf("  dumped %dx%d rgba16\n", W, H); }
    }
    const Stats& s = res[0];
    if(s.px < 1000){ printf("FAIL: the page has no faint gradient to judge (%ld px)\n", s.px); fails++; }
    if(s.px && (double)s.changes / s.px < 0.9){ printf("FAIL: plateaus in the 16-bit chain\n"); fails++; }
    if(s.longest > 8){ printf("FAIL: a flat run of %d px in the 16-bit chain\n", s.longest); fails++; }
    if(s.hueDev > 0.01){ printf("FAIL: the hue drifts in the 16-bit chain (%.4f)\n", s.hueDev); fails++; }
    printf(fails ? "FAILED (%d)\n" : "ALL OK (0 failures)\n", fails);
    return fails ? 1 : 0;
}
