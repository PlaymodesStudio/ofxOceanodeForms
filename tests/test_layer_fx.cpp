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
    GLuint echo = program(vert, shaderFrom(hdr, "layerFxEchoFrag"));
    GLuint mask = program(vert, shaderFrom(hdr, "layerFxMaskFrag"));
    GLuint maskApply = program(vert, shaderFrom(hdr,"layerFxMaskApplyFrag"));
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
    // ---- Echo: dots at x = 20 on several rows, a fractional delay (33.4 px), feedback 0.6, mix 0.7.
    // Every repeat on round(k * delay) (no drift), at mix * feedback^(k-1) * filter^k; nothing
    // elsewhere; alpha alike. Then the filters: open = no change by row; low-pass / high-pass fade
    // smoothly (no step between neighbouring rows) and more on every repeat.
    {
        auto src = img([](int x, int y, int){ return (x == 20 && y % 10 == 5) ? 255 : 0; });
        Target s = target(W, H, src.data()), d = target(W, H);
        const float D = 33.4f, FB = 0.6f, MIX = 0.7f;
        auto render = [&](float low, float high){
            run(echo, d, W, H, [&]{ bindTex(echo, "tex", s.t, 0); glUniform1f(glGetUniformLocation(echo, "height"), H); glUniform1f(glGetUniformLocation(echo, "delayPx"), D);
                glUniform1f(glGetUniformLocation(echo, "feedback"), FB); glUniform1f(glGetUniformLocation(echo, "level"), MIX); glUniform2f(glGetUniformLocation(echo, "filt"), low, high); });
            return readBack(d, W, H);
        };
        auto sm = [](double a, double b, double x){ double t = std::min(1.0, std::max(0.0, (x - a) / (b - a))); return t * t * (3 - 2 * t); };
        auto gain = [&](int y, double low, double high){ double v = 1.0 - (y + 0.5) / H, w = 0.35, lo = -w + (1 + w) * low, hi = (1 + w) * high; return (1 - sm(lo, lo + w, v)) * sm(hi - w, hi, v); };
        auto check = [&](const char* name, double low, double high, double tol){
            auto o = render((float)low, (float)high);
            double worst = 0; int stray = 0, alpha = 0;
            for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
                int v = o[(y * W + x) * 4]; double ref = 0;
                if(y % 10 == 5){
                    if(x == 20) ref = 255;
                    double g = gain(y, low, high), a = MIX * g;
                    for(int k = 1; k <= 128; k++){ int xx = 20 + (int)std::floor(k * D + 0.5); if(xx == x) ref = std::max(ref, 255 * a); a *= FB * g; }
                }
                worst = std::max(worst, std::fabs(v - ref));
                if(ref == 0 && v) stray++;
                if(v != o[(y * W + x) * 4 + 3]) alpha++;
            }
            printf("Echo %s: worst vs CPU %.2f levels, lit where nothing should be %d, alpha differs %d\n", name, worst, stray, alpha);
            if(worst > tol || stray || alpha){ fails++; printf("  FAIL: echo %s\n", name); }
            return o;
        };
        auto open = check("open (33.4 px, feedback 0.6, mix 0.7)", 1, 0, 1.5);
        printf("  repeats 1, 2, 3, 6 at x = 53, 87, 120, 220: %d %d %d %d (want %.0f %.0f %.0f %.0f)\n", open[(95 * W + 53) * 4], open[(95 * W + 87) * 4], open[(95 * W + 120) * 4], open[(95 * W + 220) * 4],
               255 * MIX, 255 * MIX * FB, 255 * MIX * FB * FB, 255 * MIX * std::pow(FB, 5));
        auto lp = check("low-pass 0.5", 0.5, 0, 1.5);
        auto hp = check("high-pass 0.5", 1, 0.5, 1.5);
        // smooth: the first repeat's level from row to row (10 rows apart) never jumps; the low-pass keeps the
        // bottom (large y) and silences the top, the high-pass the other way
        int jumpL = 0, jumpH = 0;
        for(int y = 15; y < H; y += 10){ jumpL = std::max(jumpL, std::abs((int)lp[(y * W + 53) * 4] - (int)lp[((y - 10) * W + 53) * 4])); jumpH = std::max(jumpH, std::abs((int)hp[(y * W + 53) * 4] - (int)hp[((y - 10) * W + 53) * 4])); }
        printf("  low-pass 0.5, first repeat: top row %d, bottom row %d, largest step between rows %d of 178; high-pass 0.5: top %d, bottom %d, largest step %d\n",
               lp[(5 * W + 53) * 4], lp[(195 * W + 53) * 4], jumpL, hp[(5 * W + 53) * 4], hp[(195 * W + 53) * 4], jumpH);
        if(lp[(5 * W + 53) * 4] != 0 || lp[(195 * W + 53) * 4] < 177 || hp[(195 * W + 53) * 4] != 0 || hp[(5 * W + 53) * 4] < 177 || jumpL > 45 || jumpH > 45){ fails++; printf("  FAIL: echo filters\n"); }
        // closed filters: no echo at all, the original untouched
        auto shut = render(0, 0); int lit = 0; for(int y = 0; y < H; y++) for(int x = 0; x < W; x++) if(x != 20 && shut[(y * W + x) * 4]) lit++;
        printf("  low-pass 0: pixels lit besides the original %d, original %d\n", lit, shut[(95 * W + 20) * 4]);
        if(lit || shut[(95 * W + 20) * 4] != 255){ fails++; printf("  FAIL: echo closed\n"); }
    }
    // Whole-layer mask: premultiplied colour/alpha, bypass, inversion, evolution,
    // threshold, and attachment to musical time while the image scrolls.
    {
        auto src=img([](int,int,int c){return c==0?64:c==1?96:128;});
        Target input=target(W,H,src.data()),output=target(W,H);
        auto renderMask=[&](float amount,bool inv,float phase,float beat,float threshold,int seed){
            run(mask,output,W,H,[&]{
                bindTex(mask,"tex",input.t,0);
                glUniform2f(glGetUniformLocation(mask,"size"),W,H);
                glUniform4f(glGetUniformLocation(mask,"geom"),beat,0,50,phase);
                glUniform4f(glGetUniformLocation(mask,"mask"),amount,4,.25,1);
                glUniform1f(glGetUniformLocation(mask,"threshold"),threshold);
                glUniform1i(glGetUniformLocation(mask,"invert"),inv);
                glUniform1i(glGetUniformLocation(mask,"maskOnly"),0);
                glUniform1ui(glGetUniformLocation(mask,"seed"),seed);
            });return readBack(output,W,H);
        };
        auto bypass=renderMask(0,false,0,0,0,1);if(bypass!=src){++fails;printf("FAIL mask bypass\n");}
        auto normal=renderMask(1,false,0,0,0,1),inverse=renderMask(1,true,0,0,0,1);
        int worst=0;for(size_t i=0;i<normal.size();++i){
            worst=std::max(worst,std::abs(normal[i]+inverse[i]-src[i]));
            if(normal[i]>src[i])++fails;
        }
        if(worst>1){++fails;printf("FAIL mask complement\n");}
        for(size_t i=0;i<normal.size();i+=4)if(std::abs(normal[i]*2-normal[i+3])>1 || std::abs(normal[i+1]*4-normal[i+3]*3)>4)++fails;
        if(normal!=renderMask(1,false,0,0,0,1)){++fails;printf("FAIL mask deterministic pause\n");}
        if(normal==renderMask(1,false,.25,0,0,1) || normal==renderMask(1,false,0,0,0,2)){++fails;printf("FAIL mask evolution / seed\n");}
        auto shifted=renderMask(1,false,0,1,0,1);
        for(int y=0;y<H;++y)for(int x=0;x<W-50;++x)for(int c=0;c<4;++c)
            if(std::abs(shifted[(y*W+x)*4+c]-normal[(y*W+x+50)*4+c])>1)++fails;
        auto hard=renderMask(1,false,0,0,.5,1);
        for(size_t i=0;i<hard.size();++i)if(hard[i]!=0 && hard[i]!=src[i])++fails;
        // Threshold -> mask-only Gaussian -> multiplication. A thin source line
        // must stay thin: blurring the masked image instead would spread its ink.
        {
            auto line=img([](int x,int,int){return x==200?128:0;});
            Target lineInput=target(W,H,line.data()),maskA=target(W,H),maskB=target(W,H),softened=target(W,H);
            run(mask,maskA,W,H,[&]{
                bindTex(mask,"tex",input.t,0);glUniform2f(glGetUniformLocation(mask,"size"),W,H);
                glUniform4f(glGetUniformLocation(mask,"geom"),0,0,50,0);glUniform4f(glGetUniformLocation(mask,"mask"),1,4,.25,1);
                glUniform1f(glGetUniformLocation(mask,"threshold"),.5);glUniform1i(glGetUniformLocation(mask,"invert"),0);
                glUniform1ui(glGetUniformLocation(mask,"seed"),1);glUniform1i(glGetUniformLocation(mask,"maskOnly"),1);
            });
            auto hardOnly=readBack(maskA,W,H);
            for(auto v:hardOnly)if(v!=0 && v!=255)++fails;
            for(int axis=0;axis<2;++axis){
                Target& in=axis==0?maskA:maskB;Target& out=axis==0?maskB:maskA;
                run(blur,out,W,H,[&]{bindTex(blur,"tex",in.t,0);glUniform2f(glGetUniformLocation(blur,"size"),W,H);glUniform2f(glGetUniformLocation(blur,"dir"),axis==0?1:0,axis==0?0:1);glUniform1f(glGetUniformLocation(blur,"sigma"),5);});
            }
            run(maskApply,softened,W,H,[&]{bindTex(maskApply,"tex",lineInput.t,0);bindTex(maskApply,"maskTex",maskA.t,1);glUniform1f(glGetUniformLocation(maskApply,"amount"),1);});
            auto soft=readBack(softened,W,H);int intermediate=0;
            for(int y=0;y<H;++y)for(int x=0;x<W;++x){
                int a=soft[(y*W+x)*4+3];if(x!=200 && a)++fails;
                if(x==200 && a>0 && a<128)++intermediate;
                for(int c=0;c<4;++c)if(soft[(y*W+x)*4+c]!=a)++fails;
            }
            if(intermediate==0){++fails;printf("FAIL post-threshold mask blur has no soft edges\n");}
            run(maskApply,softened,W,H,[&]{bindTex(maskApply,"tex",lineInput.t,0);bindTex(maskApply,"maskTex",maskA.t,1);glUniform1f(glGetUniformLocation(maskApply,"amount"),0);});
            if(readBack(softened,W,H)!=line){++fails;printf("FAIL blurred mask amount bypass\n");}
            glUseProgram(mask);glUniform1i(glGetUniformLocation(mask,"maskOnly"),0);
            printf("Post-threshold mask blur: %d soft edge pixels, no spread of source ink, alpha and zero-amount bypass checked\n",intermediate);
        }
        // Threefold resolution has pixel centres at the same musical coordinates.
        std::vector<unsigned char> hi((size_t)W*H*9*4);
        for(size_t i=0;i<hi.size();++i)hi[i]=src[i%4];
        Target hiInput=target(W*3,H*3,hi.data()),hiOutput=target(W*3,H*3);
        run(mask,hiOutput,W*3,H*3,[&]{
            bindTex(mask,"tex",hiInput.t,0);glUniform2f(glGetUniformLocation(mask,"size"),W*3,H*3);
            glUniform4f(glGetUniformLocation(mask,"geom"),0,0,150,0);glUniform4f(glGetUniformLocation(mask,"mask"),1,4,.25,1);
            glUniform1f(glGetUniformLocation(mask,"threshold"),0);glUniform1i(glGetUniformLocation(mask,"invert"),0);glUniform1ui(glGetUniformLocation(mask,"seed"),1);
        });
        auto high=readBack(hiOutput,W*3,H*3);
        for(int y=0;y<H;++y)for(int x=0;x<W;++x)for(int c=0;c<4;++c)
            if(std::abs(normal[(y*W+x)*4+c]-high[((y*3+1)*(W*3)+x*3+1)*4+c])>1)++fails;
        printf("Perlin: bypass, premultiplied alpha, inversion, deterministic pause, evolution, seed, beat-aligned scrolling and hard threshold and resolution invariance checked\n");
    }
    // Per-image volume boost before blur/trail and sonification, with straight
    // input alpha and a premultiplied destination. Both texture targets compile.
    {
        std::string gainHeader=hdr.substr(0,hdr.find_last_of('/')+1)+"formsLayerGain.h";
        auto gainVert=shaderFrom(gainHeader,"layerGainVert"),gainBody=shaderFrom(gainHeader,"layerGainBody");
        GLuint boost=program(gainVert,"#version 410\nuniform sampler2D tex;\n"+gainBody);
        program(gainVert,"#version 410\nuniform sampler2DRect tex;\n"+gainBody);
        float uv[]={0,0,1,0,0,1,1,1};GLuint uvBuffer;glGenBuffers(1,&uvBuffer);glBindBuffer(GL_ARRAY_BUFFER,uvBuffer);glBufferData(GL_ARRAY_BUFFER,sizeof(uv),uv,GL_STATIC_DRAW);
        glEnableVertexAttribArray(3);glVertexAttribPointer(3,2,GL_FLOAT,GL_FALSE,0,nullptr);
        auto src=img([](int x,int,int c){return c==3?(x%3==0?64:128):c==0?64:c==1?128:255;});
        Target input=target(W,H,src.data()),dst=target(W,H);
        for(float gain:{.5f,1.f,1.5f,2.f}){
            glBindFramebuffer(GL_FRAMEBUFFER,dst.fbo);glClearColor(0,0,0,0);glClear(GL_COLOR_BUFFER_BIT);
            glEnable(GL_BLEND);glBlendFuncSeparate(GL_SRC_ALPHA,GL_ONE_MINUS_SRC_ALPHA,GL_ONE,GL_ONE_MINUS_SRC_ALPHA);
            run(boost,dst,W,H,[&]{bindTex(boost,"tex",input.t,0);glUniform1f(glGetUniformLocation(boost,"gainAmount"),gain);});glDisable(GL_BLEND);
            auto out=readBack(dst,W,H);
            for(size_t i=0;i<src.size();i+=4){
                float alpha=std::min(1.f,src[i+3]/255.f*gain);
                for(int c=0;c<4;++c){float expected=c==3?alpha*255:std::min(alpha,src[i+c]/255.f*src[i+3]/255.f*gain)*255;
                    if(std::abs(out[i+c]-expected)>1.5f)++fails;
                }
            }
        }
        glDisableVertexAttribArray(3);
        printf("Layer volume: 0.5 / 1 / 1.5 / 2, saturation and premultiplied alpha checked; both texture target shaders compiled\n");
    }
    printf("%s (%d failures)\n", fails ? "FAILED" : "ALL OK", fails);
    return fails ? 1 : 0;
}
