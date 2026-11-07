// GPU test of Forms Box's post-processing shader (formsPost.h) against the patch's own effect
// shaders (bin/data/Effects: HSLFilter → PixelStretch → opacity → Levels), in an offscreen
// OpenGL 4.1 core context (CGL). Also checks the header line.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <string>
#include <vector>
#include <fstream>
#include <sstream>

static std::string readFile(const std::string& p){ std::ifstream f(p); std::stringstream s; s << f.rdbuf(); return s.str(); }
static int fails = 0;

static GLuint compile(GLenum type, const std::string& src, const char* name){
    GLuint s = glCreateShader(type);
    const char* c = src.c_str();
    glShaderSource(s, 1, &c, nullptr);
    glCompileShader(s);
    GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if(!ok){ char log[4096]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); printf("COMPILE FAIL %s:\n%s\n", name, log); fails++; }
    return s;
}
static const char* VERT = "#version 410\nuniform mat4 modelViewProjectionMatrix;\nlayout(location = 0) in vec4 position;\nvoid main(){ gl_Position = modelViewProjectionMatrix * position; }\n";
static GLuint program(const std::string& frag, const char* name){
    GLuint p = glCreateProgram();
    glAttachShader(p, compile(GL_VERTEX_SHADER, VERT, "vert"));
    glAttachShader(p, compile(GL_FRAGMENT_SHADER, frag, name));
    glBindAttribLocation(p, 0, "position");
    glBindFragDataLocation(p, 0, "out_color");
    glLinkProgram(p);
    GLint ok = 0; glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if(!ok){ char log[4096]; glGetProgramInfoLog(p, sizeof(log), nullptr, log); printf("LINK FAIL %s: %s\n", name, log); fails++; }
    return p;
}
static GLuint tex(int w, int h, const unsigned char* px, GLenum fmt = GL_RGBA8){
    GLuint t; glGenTextures(1, &t); glBindTexture(GL_TEXTURE_2D, t);
    glTexImage2D(GL_TEXTURE_2D, 0, fmt, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    return t;
}
struct Target { GLuint fbo, t; };
static Target target(int w, int h, GLenum fmt = GL_RGBA8){
    Target r; r.t = tex(w, h, nullptr, fmt);
    glGenFramebuffers(1, &r.fbo); glBindFramebuffer(GL_FRAMEBUFFER, r.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, r.t, 0);
    return r;
}
static GLuint vao, dummy;
static void draw(GLuint prog, GLuint src, Target& dst, int w, int h){
    glBindFramebuffer(GL_FRAMEBUFFER, dst.fbo);
    glViewport(0, 0, w, h);
    glUseProgram(prog);
    float I[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    glUniformMatrix4fv(glGetUniformLocation(prog, "modelViewProjectionMatrix"), 1, GL_FALSE, I);
    glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, src);
    glUniform1i(glGetUniformLocation(prog, "tSource"), 0);
    // the patch's *Tex inputs: unconnected = a 1x1 texture (unit 1)
    glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, dummy);
    for(const char* n : {"HueTex", "SatTex", "LigTex", "MinTex", "MaxTex", "LowTex", "HighTex", "GammaTex"}){
        GLint l = glGetUniformLocation(prog, n); if(l >= 0) glUniform1i(l, 1);
    }
    glBindVertexArray(vao);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}
static std::vector<unsigned char> readBack(Target& t, int w, int h){
    std::vector<unsigned char> px(w * h * 4);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
    return px;
}
static bool loadBMP(const std::string& p, int& w, int& h, std::vector<unsigned char>& rgba){
    std::ifstream f(p, std::ios::binary); if(!f) return false;
    std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
    if(b.size() < 54 || b[0] != 'B' || b[1] != 'M') return false;
    auto u32 = [&](int o){ return (unsigned)(b[o] | b[o+1] << 8 | b[o+2] << 16 | b[o+3] << 24); };
    unsigned off = u32(10); w = (int)u32(18); int hh = (int)u32(22); int bpp = b[28] | b[29] << 8;
    bool flip = hh > 0; h = std::abs(hh);
    int bytes = bpp / 8, stride = (w * bytes + 3) & ~3;
    rgba.assign(w * h * 4, 255);
    for(int y = 0; y < h; y++) for(int x = 0; x < w; x++){
        int sy = flip ? y : h - 1 - y;   // GL row 0 = bottom = BMP's first row when flipped
        const unsigned char* s = &b[off + sy * stride + x * bytes];
        unsigned char* d = &rgba[(y * w + x) * 4];
        d[0] = s[2]; d[1] = s[1]; d[2] = s[0];
    }
    return true;
}

int main(int argc, char** argv){
    std::string effects = argv[1], postFrag = argv[2];
    CGLPixelFormatAttribute at[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf; GLint npf; CGLChoosePixelFormat(at, &pf, &npf);
    CGLContextObj ctx; CGLCreateContext(pf, nullptr, &ctx); CGLSetCurrentContext(ctx);
    printf("GL %s / %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));

    // image: a real rendered frame if given, else a synthetic one
    int W = 1920, H = 1080;
    std::vector<unsigned char> img;
    if(argc > 3 && loadBMP(argv[3], W, H, img)) printf("image %s %dx%d\n", argv[3], W, H);
    else {
        img.assign(W * H * 4, 255);
        for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
            unsigned char* d = &img[(y * W + x) * 4];
            d[0] = (x * 7 + y * 3) % 256; d[1] = (unsigned char)(127 + 127 * std::sin(x * 0.01 + y * 0.02)); d[2] = (y * 255) / H;
        }
        printf("synthetic image %dx%d\n", W, H);
    }
    glGenVertexArrays(1, &vao); glBindVertexArray(vao);
    float quad[] = {-1,-1,0,1, 1,-1,0,1, -1,1,0,1, 1,1,0,1};
    GLuint vbo; glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(quad), quad, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 0, nullptr);
    unsigned char one[4] = {0, 0, 0, 255};
    dummy = tex(1, 1, one);
    GLuint src = tex(W, H, img.data());

    GLuint hsl = program(readFile(effects + "/HSLFilter.glsl"), "HSLFilter");
    GLuint stretch = program(readFile(effects + "/PixelStretch.glsl"), "PixelStretch");
    GLuint levels = program(readFile(effects + "/Levels.glsl"), "Levels");
    GLuint opac = program("#version 410\nuniform sampler2D tSource; uniform float op; out vec4 out_color;\n"
                          "void main(){ vec4 c = texelFetch(tSource, ivec2(gl_FragCoord.xy), 0); out_color = vec4(c.rgb * max(op, 0.001), 1.0); }\n", "opacity");
    GLuint post = program(readFile(postFrag), "formsPost");
    if(fails){ printf("%d shader failures\n", fails); return 1; }

    Target a = target(W, H, GL_RGBA32F), b = target(W, H, GL_RGBA32F), c = target(W, H, GL_RGBA32F), d = target(W, H), mine = target(W, H);
    struct Case { float hue, sat, val, x, op, mn, mx, gamma, lo, hi; };
    Case cases[] = {
        {0, 1, 1, 418.9f, 1, 0, 0.617f, 1, 0, 1},             // the patch's settings
        {0.31f, 0.8f, 1.2f, 1003.4f, 0.7f, 0.05f, 0.617f, 1.3f, 0.02f, 0.95f},
        {0.77f, 1.5f, 0.6f, 0, 0.25f, 0.1f, 0.9f, 0.6f, 0, 1},
        {0.5f, 0, 1, 1919, 1, 0, 1, 1, 0.2f, 0.8f},
    };
    for(auto& k : cases){
        // the patch: HSLFilter → PixelStretch → opacity → Levels
        glUseProgram(hsl);
        glUniform1f(glGetUniformLocation(hsl, "Hue"), k.hue); glUniform1f(glGetUniformLocation(hsl, "Sat"), k.sat); glUniform1f(glGetUniformLocation(hsl, "Lig"), k.val);
        draw(hsl, src, a, W, H);
        glUseProgram(stretch); glUniform1f(glGetUniformLocation(stretch, "x"), k.x);
        draw(stretch, a.t, b, W, H);
        glUseProgram(opac); glUniform1f(glGetUniformLocation(opac, "op"), k.op);
        draw(opac, b.t, c, W, H);
        glUseProgram(levels);
        glUniform1f(glGetUniformLocation(levels, "Min"), k.mn); glUniform1f(glGetUniformLocation(levels, "Max"), k.mx);
        glUniform1f(glGetUniformLocation(levels, "Gamma"), k.gamma); glUniform1f(glGetUniformLocation(levels, "Low"), k.lo);
        glUniform1f(glGetUniformLocation(levels, "High"), k.hi);
        draw(levels, c.t, d, W, H);
        auto ref = readBack(d, W, H);
        // Forms Box: one pass, line off
        glUseProgram(post);
        glUniform2f(glGetUniformLocation(post, "size"), W, H);
        glUniform1f(glGetUniformLocation(post, "stretchX"), k.x);
        glUniform3f(glGetUniformLocation(post, "hsv"), k.hue, k.sat, k.val);
        glUniform1f(glGetUniformLocation(post, "opacity"), std::max(k.op, 0.001f));
        glUniform4f(glGetUniformLocation(post, "levels"), k.mn, k.mx, k.gamma, 0);
        glUniform2f(glGetUniformLocation(post, "lowHigh"), k.lo, k.hi);
        glUniform4f(glGetUniformLocation(post, "lineGeom"), -1e6f, 4.8f, 0, 0);
        glUniform3f(glGetUniformLocation(post, "lineColor"), 1, 1, 1);
        draw(post, src, mine, W, H);
        auto got = readBack(mine, W, H);
        double sum = 0; int mx = 0, over2 = 0, overStretch = 0;
        int xs = (int)std::ceil(k.x);
        for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) for(int ch = 0; ch < 3; ch++){
            int i = (y * W + x) * 4 + ch, dd = std::abs(ref[i] - got[i]);
            sum += dd; mx = std::max(mx, dd);
            if(dd > 2){ if(x <= xs + 1) overStretch++; else over2++; }
        }
        printf("case hue %.2f sat %.2f val %.2f x %.1f op %.2f levels %.3f..%.3f g %.2f: mean |diff| %.4f, max %d, >2 levels: %d px-ch right of the header, %d in the stretched part\n",
               k.hue, k.sat, k.val, k.x, k.op, k.mn, k.mx, k.gamma, sum / (W * H * 3.0), mx, over2, overStretch);
        if(mx > 1) { fails++; printf("  FAIL: differs from the patch chain\n"); }
    }
    // the header line: 4.8 px white at x = 800.3, added
    glUseProgram(post);
    glUniform1f(glGetUniformLocation(post, "stretchX"), -1);
    glUniform3f(glGetUniformLocation(post, "hsv"), 0, 1, 0);       // value 0: black image
    glUniform1f(glGetUniformLocation(post, "opacity"), 1);
    glUniform4f(glGetUniformLocation(post, "levels"), 0, 1, 1, 0);
    glUniform2f(glGetUniformLocation(post, "lowHigh"), 0, 1);
    glUniform4f(glGetUniformLocation(post, "lineGeom"), 800.3f, 4.8f, 0, 1);
    glUniform3f(glGetUniformLocation(post, "lineColor"), 1, 0.5f, 0.25f);
    draw(post, src, mine, W, H);
    auto ln = readBack(mine, W, H);
    float total = 0; int first = -1, last = -1;
    for(int x = 0; x < W; x++){ int v = ln[(500 * W + x) * 4]; if(v){ total += v / 255.0f; if(first < 0) first = x; last = x; } }
    printf("line: lit from x=%d to %d, total width %.2f px (want 4.8), colour %d %d %d at the centre\n", first, last, total,
           ln[(500 * W + 800) * 4], ln[(500 * W + 800) * 4 + 1], ln[(500 * W + 800) * 4 + 2]);
    if(std::abs(total - 4.8f) > 0.3f || first < 795 || last > 805) { fails++; printf("  FAIL: line\n"); }
    // soft line
    glUniform4f(glGetUniformLocation(post, "lineGeom"), 800.3f, 4.8f, 6, 1);
    draw(post, src, mine, W, H);
    ln = readBack(mine, W, H);
    int softFirst = -1, softLast = -1;
    for(int x = 0; x < W; x++) if(ln[(500 * W + x) * 4]){ if(softFirst < 0) softFirst = x; softLast = x; }
    printf("soft line: lit from %d to %d\n", softFirst, softLast);
    if(softLast - softFirst < 14) { fails++; printf("  FAIL: soft edge\n"); }
    // vertical softness: 100 px fade in from the top and bottom edges, inside the image
    glUniform4f(glGetUniformLocation(post, "lineGeom"), 800.3f, 4.8f, 0, 1);
    glUniform3f(glGetUniformLocation(post, "lineColor"), 1, 1, 1);
    glUniform1f(glGetUniformLocation(post, "lineSoftV"), 100);
    draw(post, src, mine, W, H);
    ln = readBack(mine, W, H);
    auto rowV = [&](int y){ return ln[(y * W + 800) * 4]; };   // GL row y (0 = bottom)
    printf("soft V: row 0 %d, row 25 %d, row 50 %d, row 100 %d, middle %d, row H-1 %d\n", rowV(0), rowV(25), rowV(50), rowV(100), rowV(H / 2), rowV(H - 1));
    if(rowV(0) > 3 || rowV(H - 1) > 3 || rowV(H / 2) < 250 || rowV(100) < 250 || !(rowV(25) < rowV(50) && rowV(50) < rowV(100))) { fails++; printf("  FAIL: soft V\n"); }
    glUniform1f(glGetUniformLocation(post, "lineSoftV"), 0);
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
