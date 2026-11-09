// GPU test of all pixel-stretch blend modes and matching background coverage.
// Build: clang++ -std=c++17 tests/test_stretch_blend.cpp -framework OpenGL
// Run: ./a.out src/formsBox/formsPost.h src/formsBox/formsBackground.h
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <cmath>
#include <cassert>
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


static std::string fragment(const std::string& path,const std::string& marker){
    std::ifstream in(path); std::stringstream ss; ss<<in.rdbuf(); auto s=ss.str();
    auto p=s.find(marker); assert(p!=std::string::npos);
    if(marker=="void setupShader()") p=s.find("std::string frag",p);
    p=s.find("R\"(",p)+3; return s.substr(p,s.find(")\"",p)-p);
}
static float blend(float base,float stretched,int mode){
    switch(mode){
        case 1:return std::min(1.f,base+stretched);
        case 2:return std::max(base,stretched);
        case 3:return 1-(1-base)*(1-stretched);
        case 4:return base*stretched;
        case 5:return std::min(base,stretched);
        default:return stretched;
    }
}
int main(int argc,char** argv){
    assert(argc==3);
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,kCGLPFAAccelerated,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf=nullptr; GLint count=0; CGLContextObj ctx=nullptr;
    assert(CGLChoosePixelFormat(attrs,&pf,&count)==kCGLNoError);
    assert(CGLCreateContext(pf,nullptr,&ctx)==kCGLNoError); CGLSetCurrentContext(ctx);
    glGenVertexArrays(1,&vao); glBindVertexArray(vao);
    float quad[]={-1,-1,0,1,1,-1,0,1,-1,1,0,1,1,1,0,1};
    GLuint vbo; glGenBuffers(1,&vbo); glBindBuffer(GL_ARRAY_BUFFER,vbo); glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0); glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,nullptr);
    std::string vert="#version 410\nuniform mat4 modelViewProjectionMatrix; layout(location=0) in vec4 position; void main(){gl_Position=modelViewProjectionMatrix*position;}";
    auto post=program(vert,fragment(argv[1],"void setupShader()"));
    auto bg=program(vert,fragment(argv[2],"backgroundFragmentShader()"));
    assert(!fails);
    const int W=16,H=4; std::vector<unsigned char> pixels(W*H*4);
    for(int y=0;y<H;++y) for(int x=0;x<W;++x) for(int c=0;c<4;++c) pixels[(y*W+x)*4+c]=20+(x*11+y*17+c*31)%200;
    auto source=target(W,H,pixels.data()), out=target(W,H), background=target(W,H);
    for(int enabled=0;enabled<2;++enabled) for(int mode=0;mode<6;++mode){
        run(post,out,W,H,[&]{
            bindTex(post,"tSource",source.t,0); glUniform2f(glGetUniformLocation(post,"size"),W,H);
            glUniform1f(glGetUniformLocation(post,"stretchX"),enabled ? 8.5f : -1);
            glUniform1i(glGetUniformLocation(post,"stretchBlend"),mode);
            glUniform3f(glGetUniformLocation(post,"hsv"),0,1,1); glUniform1f(glGetUniformLocation(post,"opacity"),1);
            glUniform4f(glGetUniformLocation(post,"levels"),0,1,1,0); glUniform2f(glGetUniformLocation(post,"lowHigh"),0,1);
            glUniform4f(glGetUniformLocation(post,"lineGeom"),-100,0,0,0);
        });
        auto actual=readBack(out,W,H);
        run(bg,background,W,H,[&]{
            bindTex(bg,"tImage",out.t,0); bindTex(bg,"tCover",source.t,1); glUniform2f(glGetUniformLocation(bg,"size"),W,H);
            glUniform1f(glGetUniformLocation(bg,"stretchX"),enabled ? 8.5f : -1); glUniform1i(glGetUniformLocation(bg,"stretchBlend"),mode);
            glUniform1f(glGetUniformLocation(bg,"opacity"),1); glUniform3f(glGetUniformLocation(bg,"bg"),.3f,.2f,.1f); glUniform1i(glGetUniformLocation(bg,"paper"),0);
        });
        auto withBg=readBack(background,W,H);
        for(int y=0;y<H;++y) for(int x=0;x<W;++x){
            int i=(y*W+x)*4,h=(y*W+8)*4; bool stretched=enabled && x<=8;
            float coverage=stretched ? blend(pixels[i+3]/255.f,pixels[h+3]/255.f,mode) : pixels[i+3]/255.f;
            for(int c=0;c<3;++c){
                float expected=stretched ? blend(pixels[i+c]/255.f,pixels[h+c]/255.f,mode) : pixels[i+c]/255.f;
                if(std::abs(actual[i+c]-expected*255)>2){ ++fails; }
                float expectedBg=std::min(1.f,expected+(.3f-.1f*c)*(1-coverage));
                if(std::abs(withBg[i+c]-expectedBg*255)>2){ ++fails; }
            }
        }
    }
    assert(glGetError()==GL_NO_ERROR);
    printf("Stretch blend: %s (%d failures); 6 modes, stretch on/off, untouched right side and background coverage\n",fails ? "FAIL" : "PASS",fails);
    return fails ? 1 : 0;
}
