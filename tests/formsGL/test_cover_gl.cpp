// GPU checks for the actual Cover shaders: dilation, feather, linear release, erase and black.
// Build: clang++ -std=c++17 tests/formsGL/test_cover_gl.cpp -framework OpenGL
// Run: ./a.out src/formsBox/formsCoverGL.h
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
    p=s.find("R\"(",p)+3; return s.substr(p,s.find(")\"",p)-p);
}

int main(int argc,char** argv){
    assert(argc==2);
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,kCGLPFAAccelerated,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf=nullptr;GLint count=0;CGLContextObj ctx=nullptr;
    assert(CGLChoosePixelFormat(attrs,&pf,&count)==kCGLNoError);
    assert(CGLCreateContext(pf,nullptr,&ctx)==kCGLNoError);CGLSetCurrentContext(ctx);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    float quad[]={-1,-1,0,1,1,-1,0,1,-1,1,0,1,1,1,0,1};
    GLuint vbo;glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(quad),quad,GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);glVertexAttribPointer(0,4,GL_FLOAT,GL_FALSE,0,nullptr);
    std::string vert="#version 410\nuniform mat4 modelViewProjectionMatrix;layout(location=0)in vec4 position;void main(){gl_Position=modelViewProjectionMatrix*position;}";
    GLuint filter=program(vert,fragment(argv[1],"std::string frag="));
    GLuint cover=program(vert,fragment(argv[1],"std::string apply="));
    constexpr int W=64,H=32;
    std::vector<unsigned char> raw(W*H*4,0);
    std::vector<float> cpu(W*H,0);
    for(int y=8;y<12;++y)for(int x=12;x<16;++x){raw[(y*W+x)*4+3]=255;cpu[y*W+x]=1;}
    auto a=target(W,H,raw.data()),b=target(W,H),c=target(W,H);
    auto pass=[&](Target& src,Target& dst,int kind,int radius=0,int offset=0,float release=0){
        glDisable(GL_BLEND);
        run(filter,dst,W,H,[&]{bindTex(filter,"uSrc",src.t,0);glUniform2i(glGetUniformLocation(filter,"uSize"),W,H);
            glUniform1i(glGetUniformLocation(filter,"uPass"),kind);glUniform1i(glGetUniformLocation(filter,"uRadius"),radius);
            glUniform1i(glGetUniformLocation(filter,"uOffset"),offset);glUniform1f(glGetUniformLocation(filter,"uRelease"),release);
            glUniform1i(glGetUniformLocation(filter,"uAlpha"),1);});
    };
    pass(a,b,-1);pass(b,a,0,2);pass(a,b,1,2);
    auto grown=readBack(b,W,H);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){int expected=x>=12&&x<18&&y>=6&&y<14?255:0;assert(grown[(y*W+x)*4]==expected);}
    for(int offset=1;offset<=8;offset*=2){pass(b,a,2,0,offset,8);std::swap(a,b);}
    auto trail=readBack(b,W,H);
    assert(trail[(8*W+17)*4]==255 && std::abs((int)trail[(8*W+21)*4]-128)<=2);
    assert(trail[(8*W+25)*4]==0 && trail[(8*W+11)*4]==0); // later beats only; linear tail reaches zero
    pass(b,a,3,2);pass(a,b,4,2);pass(b,a,3,2);pass(a,b,4,2);
    auto soft=readBack(b,W,H);assert(soft[(5*W+12)*4]>0 && soft[(8*W+12)*4]<255);
    for(int y=0;y<H;++y)for(int x=0;x<12;++x) assert(soft[(y*W+x)*4]==0);
    // Each grow axis works independently; feather alone cannot precede the source.
    auto original=target(W,H,raw.data());
    pass(original,a,-1);pass(a,b,0,4);pass(b,a,1,0);
    auto rightOnly=readBack(a,W,H);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){int expected=x>=12&&x<20&&y>=8&&y<12?255:0;assert(rightOnly[(y*W+x)*4]==expected);}
    pass(original,a,-1);pass(a,b,0,0);pass(b,a,1,3);
    auto heightOnly=readBack(a,W,H);
    for(int y=0;y<H;++y)for(int x=0;x<W;++x){int expected=x>=12&&x<16&&y>=5&&y<15?255:0;assert(heightOnly[(y*W+x)*4]==expected);}
    pass(original,a,-1);pass(a,b,3,2);pass(b,a,4,2);
    auto featherOnly=readBack(a,W,H);
    for(int y=0;y<H;++y)for(int x=0;x<12;++x) assert(featherOnly[(y*W+x)*4]==0);
    // Apply a half-strength mask to a translucent premultiplied destination.
    std::vector<unsigned char> solid(W*H*4,0),mask(W*H*4,255);
    for(int i=0;i<W*H;++i){solid[i*4]=100;solid[i*4+1]=50;solid[i*4+2]=25;solid[i*4+3]=128;}
    auto uniformMask=target(W,H,mask.data());
    for(bool black:{false,true}){
        auto dst=target(W,H,solid.data());
        glEnable(GL_BLEND);glBlendEquation(GL_FUNC_ADD);glBlendFunc(black?GL_ONE:GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);
        run(cover,dst,W,H,[&]{bindTex(cover,"uMask",uniformMask.t,0);glUniform2f(glGetUniformLocation(cover,"uSize"),W,H);glUniform1f(glGetUniformLocation(cover,"uAmount"),.5);});
        auto pixels=readBack(dst,W,H);
        assert(std::abs((int)pixels[0]-50)<=1);assert(std::abs((int)pixels[3]-(black?192:64))<=1);
    }
    assert(glGetError()==GL_NO_ERROR && fails==0);
    std::puts("PASS: right-only grow, independent height grow, causal feather, linear beat release, eraser and black alpha semantics");
}
