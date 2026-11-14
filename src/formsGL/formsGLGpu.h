//
//  formsGLGpu.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  Worker GPU contexts (macOS CGL): full-resolution texture uploads shared with the app (16 bits
//  per channel: GL_RGBA16 / GL_R16), and the opt-in approximate noise mask on 16-bit integer pixels.
//  The exact kick rasterizer (formsGLExactRaster.h) reproduced Java2D's 8-bit arithmetic through a
//  table of 256 alpha levels; the canvas is 16 bits per channel since 2026-11-14, so it is not used
//  (kickSweep returns false: the CPU draws the kick). The UI polls upload fences with zero timeout and attaches finished
//  textures directly, preserving every source pixel without a large upload on the scrolling thread.
//  Other drawing keeps the CPU reference. The old approximate noise shader is opt-in only.
//  The includer provides the OpenGL declarations (the app: openFrameworks' GLEW); without them (the
//  headless tests) <OpenGL/gl3.h> is used.
//

#pragma once

#include "formsGLSketch.h"
#include <OpenGL/OpenGL.h>
#if !defined(__glew_h__) && !defined(__GLEW_H__)
#include <OpenGL/gl3.h>
#endif
#include <string>
#include "formsGLSharedTexture.h"

namespace formsgl {

class GpuEffectsCGL : public GpuEffects {
public:
    explicit GpuEffectsCGL(CGLContextObj shared = nullptr) : sharedContext(shared) {}
    ~GpuEffectsCGL() override {
        if(ctx){
            CGLContextObj prev = CGLGetCurrentContext();   // whatever thread this runs on, its context is put back
            CGLSetCurrentContext(ctx);
            if(prog) glDeleteProgram(prog);
            if(vao) glDeleteVertexArrays(1, &vao);
            if(fbo) glDeleteFramebuffers(1, &fbo);
            GLuint t[3] = {texIn, texOut, texPerm};
            glDeleteTextures(3, t);
            CGLSetCurrentContext(prev == ctx ? nullptr : prev);
            CGLDestroyContext(ctx);
        }
    }
    bool ok() { return init(); }
    std::string error;

    bool kickSweep(Canvas& canvas, float x, float top, float width, float power, int cap) override {
        return false;   // 16-bit canvas: the CPU draws it (see the note at the top)
    }
    uint64_t exactDraws() const { return 0; }
    bool enableExactRaster = true;
    double exactPrepareMs() const { return 0; }
    double exactRasterMs() const { return 0; }
    // 16 bits per channel: RGBA (straight alpha) or one channel (GL_RED: a cover mask).
    // eightBit: the texture is made with 8 bits per channel (the node's 8-bit live buffers option);
    // the 16-bit values are rounded to 8 here, on the worker.
    std::shared_ptr<SharedTexture> upload(const std::vector<uint16_t>& rgba, int w, int h,
                                        const std::shared_ptr<SharedTexturePool>& pool, GLenum format=GL_RGBA, bool eightBit=false){
        size_t channels=format==GL_RED?1:4;
        if(!pool || !init() || w <= 0 || h <= 0 || rgba.size() < (size_t)w * h * channels) return {};
        CGLSetCurrentContext(ctx);
        GLenum target = pool->target();
        GLint maxTexture = 0;
        glGetIntegerv(target == GL_TEXTURE_RECTANGLE ? GL_MAX_RECTANGLE_TEXTURE_SIZE : GL_MAX_TEXTURE_SIZE, &maxTexture);
        if(w > maxTexture || h > maxTexture) return {};
        auto t = std::make_shared<SharedTexture>(); t->pool = pool; t->w = w; t->h = h; t->target = target;
        t->internalFormat=eightBit?(format==GL_RED?GL_R8:GL_RGBA8):(format==GL_RED?GL_R16:GL_RGBA16);
        glGenTextures(1, &t->id);
        glActiveTexture(GL_TEXTURE0); glBindTexture(target, t->id);
        glTexParameteri(target, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(target, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(target, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
        if(eightBit){
            std::vector<uint8_t> b8((size_t)w*h*channels);
            for(size_t i=0;i<b8.size();++i) b8[i]=(uint8_t)((rgba[i]+128u)/257u);
            glPixelStorei(GL_UNPACK_ALIGNMENT, channels==1?1:4);
            glTexImage2D(target, 0, t->internalFormat, w, h, 0, format, GL_UNSIGNED_BYTE, b8.data());
        }else{
            glPixelStorei(GL_UNPACK_ALIGNMENT, channels==1?2:4);
            glTexImage2D(target, 0, t->internalFormat, w, h, 0, format, GL_UNSIGNED_SHORT, rgba.data());
        }
        if(glGetError() != GL_NO_ERROR){ error = "shared texture upload failed"; return {}; }
        t->fence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);
        if(!t->fence){ error = "shared texture fence allocation failed"; return {}; }
        glFlush(); // make the texture and fence visible to the app's context
        return t;
    }

    bool mask(std::vector<Canvas::Px>& px, int W, int H, const SketchPRNG& prng, float ox, float oy, float cellX, float cellY,
              float gain, float con, float out, bool invert, float threshold, float amount, bool canvasMode) override {
        // The historical shader's floating-point noise differs by up to a few levels.
        // Exact rendering always retains the CPU noise path.
        if(!allowApproximateNoise) return false;
        if(!init()) return false;
        CGLSetCurrentContext(ctx);
        GLint maxTex = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        if(W > maxTex || H > maxTex) return false;
        // the permutation table (512 ints) as a 512 x 1 integer texture
        int perm[512];
        for(int i = 0; i < 512; i++) perm[i] = prng.perm()[i];
        glBindTexture(GL_TEXTURE_2D, texPerm);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32I, 512, 1, 0, GL_RED_INTEGER, GL_INT, perm);
        glUseProgram(prog);
        glActiveTexture(GL_TEXTURE0); glBindTexture(GL_TEXTURE_2D, texIn);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, texPerm);
        glUniform1i(glGetUniformLocation(prog, "img"), 0);
        glUniform1i(glGetUniformLocation(prog, "perm"), 1);
        float gx[16], gy[16];
        for(int i = 0; i < 16; i++){ gx[i] = prng.gradX(i); gy[i] = prng.gradY(i); }
        glUniform1fv(glGetUniformLocation(prog, "gx"), 16, gx);
        glUniform1fv(glGetUniformLocation(prog, "gy"), 16, gy);
        glUniform1f(glGetUniformLocation(prog, "bound"), prng.bound());
        glUniform2f(glGetUniformLocation(prog, "o"), ox, oy);
        glUniform2f(glGetUniformLocation(prog, "cell"), cellX, cellY);
        glUniform4f(glGetUniformLocation(prog, "shape"), gain, con, out, threshold);
        glUniform1f(glGetUniformLocation(prog, "amount"), amount);
        glUniform1i(glGetUniformLocation(prog, "invert"), invert ? 1 : 0);
        glUniform1i(glGetUniformLocation(prog, "canvasMode"), canvasMode ? 1 : 0);
        glBindVertexArray(vao);
        // in strips of at most ~4 million pixels (larger integer textures come back empty on this
        // driver); the strip's first row goes to the shader so the noise is the same as in one piece
        int rowsPer = std::max(1, std::min(H, 4000000 / std::max(1, W)));
        for(int y0 = 0; y0 < H; y0 += rowsPer){
            int h = std::min(rowsPer, H - y0);
            Canvas::Px* strip = px.data() + (size_t)y0 * W;
            // the image: 0xAAAARRRRGGGGBBBB words = B, G, R, A 16-bit values
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, texIn);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16UI, W, h, 0, GL_BGRA_INTEGER, GL_UNSIGNED_SHORT, strip);
            glBindTexture(GL_TEXTURE_2D, texOut);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16UI, W, h, 0, GL_BGRA_INTEGER, GL_UNSIGNED_SHORT, nullptr);
            glBindTexture(GL_TEXTURE_2D, texIn);
            glBindFramebuffer(GL_FRAMEBUFFER, fbo);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texOut, 0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){ error = "framebuffer"; glBindFramebuffer(GL_FRAMEBUFFER, 0); return false; }
            glViewport(0, 0, W, h);
            glUniform1i(glGetUniformLocation(prog, "rowOffset"), y0);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            glReadPixels(0, 0, W, h, GL_BGRA_INTEGER, GL_UNSIGNED_SHORT, strip);
        }
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        GLenum e = glGetError();
        if(e != GL_NO_ERROR){ error = "GL error " + std::to_string(e); return false; }
        return true;
    }

private:
    CGLContextObj sharedContext = nullptr;
public:
    bool allowApproximateNoise = false;
private:
    CGLContextObj ctx = nullptr;
    GLuint prog = 0, vao = 0, fbo = 0, texIn = 0, texOut = 0, texPerm = 0;
    bool tried = false, good = false;

    bool init(){
        if(tried) return good;
        tried = true;
        CGLPixelFormatAttribute attrs[] = {kCGLPFAOpenGLProfile, (CGLPixelFormatAttribute)kCGLOGLPVersion_3_2_Core, kCGLPFAAccelerated, (CGLPixelFormatAttribute)0};
        CGLPixelFormatObj pf = nullptr; GLint n = 0;
        if(sharedContext){
            pf = CGLGetPixelFormat(sharedContext);
            if(pf) CGLRetainPixelFormat(pf);
        }else if(CGLChoosePixelFormat(attrs, &pf, &n) != kCGLNoError) pf = nullptr;
        if(!pf){ error = "no pixel format"; return false; }
        CGLError ce = CGLCreateContext(pf, sharedContext, &ctx);
        CGLDestroyPixelFormat(pf);
        if(ce != kCGLNoError || !ctx){ error = "no context"; return false; }
        CGLSetCurrentContext(ctx);
        const char* vs = R"(#version 410
void main(){ vec2 p = vec2((gl_VertexID == 1) ? 3.0 : -1.0, (gl_VertexID == 2) ? 3.0 : -1.0); gl_Position = vec4(p, 0.0, 1.0); }
)";
        // Processes.pde noise mask with PRNG.pde perlin(); integer image in, integer image out
        const char* fs = R"(#version 410
uniform usampler2D img;
uniform isampler2D perm;
uniform float gx[16];
uniform float gy[16];
uniform float bound;
uniform vec2 o;
uniform vec2 cell;      // width, height of a noise cell (px)
uniform vec4 shape;    // gain, contrast, out, threshold
uniform float amount;
uniform int invert;
uniform int canvasMode;
uniform int rowOffset;
out uvec4 color;
int P(int i){ return texelFetch(perm, ivec2(i, 0), 0).r; }
int incr(int a){ return a > 254 ? 0 : a + 1; }
float fadeF(float t){ return t * t * t * (t * (t * 6.0 - 15.0) + 10.0); }
float lerpV(float a, float b, float t){ return a + fadeF(t) * (b - a); }
float perlin(float x, float y){
    int i = int(x) & 255, j = int(y) & 255;
    int tl = P(P(P(i) + j)) & 15, tr = P(P(P(incr(i)) + j)) & 15;
    int bl = P(P(P(i) + incr(j))) & 15, br = P(P(P(incr(i)) + incr(j))) & 15;
    float u = x - float(int(x)), v = y - float(int(y));
    float a = gx[tl] * u + gy[tl] * v, b = gx[tr] * (u - 1.0) + gy[tr] * v;
    float c = gx[bl] * u + gy[bl] * (v - 1.0), d = gx[br] * (u - 1.0) + gy[br] * (v - 1.0);
    float top = lerpV(a, b, u), bottom = lerpV(c, d, u);
    return (lerpV(top, bottom, v) + bound) / (bound + bound);
}
uint scaleK(uint c, float k){ return uint(clamp(floor(float(c) * k + 0.5), 0.0, 65535.0)); }
void main(){
    ivec2 q = ivec2(gl_FragCoord.xy);
    uvec4 c = texelFetch(img, q, 0);   // r, g, b, a (uploaded and read back as BGRA bytes)
    uint lvl = canvasMode == 1 ? max(c.b, max(c.g, c.r)) : c.a;
    if(lvl == 0u){ color = c; return; }
    float x = float(q.x), y = float(q.y + rowOffset);
    float nv = clamp(pow(clamp(perlin(o.x + x / cell.x, o.y + y / cell.y) * shape.x, 0.0, 1.0), shape.y) * shape.z, 0.0, 1.0);
    if(invert == 1) nv = 1.0 - nv;
    if(shape.w > 0.0) nv = nv >= shape.w ? 1.0 : 0.0;
    float k = 1.0 - amount + amount * nv;
    if(canvasMode == 1) color = uvec4(scaleK(c.r, k), scaleK(c.g, k), scaleK(c.b, k), 65535u);
    else color = uvec4(c.r, c.g, c.b, scaleK(c.a, k));
}
)";
        auto compile = [&](GLenum type, const char* src) -> GLuint {
            GLuint sh = glCreateShader(type);
            glShaderSource(sh, 1, &src, nullptr);
            glCompileShader(sh);
            GLint okc = 0; glGetShaderiv(sh, GL_COMPILE_STATUS, &okc);
            if(!okc){ char log[2048]; glGetShaderInfoLog(sh, 2048, nullptr, log); error = std::string("shader: ") + log; glDeleteShader(sh); return 0; }
            return sh;
        };
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if(!v || !f){ CGLSetCurrentContext(nullptr); return false; }
        prog = glCreateProgram();
        glAttachShader(prog, v); glAttachShader(prog, f);
        glLinkProgram(prog);
        glDeleteShader(v); glDeleteShader(f);
        GLint linked = 0; glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        if(!linked){ error = "link"; CGLSetCurrentContext(nullptr); return false; }
        glGenVertexArrays(1, &vao);
        glGenFramebuffers(1, &fbo);
        GLuint t[3]; glGenTextures(3, t); texIn = t[0]; texOut = t[1]; texPerm = t[2];
        for(GLuint tx : {texIn, texOut, texPerm}){
            glBindTexture(GL_TEXTURE_2D, tx);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        good = true;
        return true;
    }
};

} // namespace formsgl
