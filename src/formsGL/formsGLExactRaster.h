#pragma once

// NOT INCLUDED since 2026-11-14: the canvas holds 16 bits per channel (formsGLCanvas.h), and this
// rasterizer reproduces the former 8-bit arithmetic (256 alpha levels, uint32 pixels). Kept for
// reference; it does not compile against the 16-bit canvas.

// Exact GPU rasterization of the kick sweep. All pow/envelope calculations stay on the CPU:
// each column stores the first sample reaching each of the 256 alpha levels. The GPU finds
// that level with integer searches and reproduces Canvas's Java2D integer source-over.
// No shader pow, normalized framebuffer blending, filtering or approximate noise is used.
// The caller owns the current GL context and must call release() before destroying it.

#include "formsGLCanvas.h"
#include <stdexcept>
#include <string>
#include <chrono>

namespace formsgl {

class ExactRasterGL {
public:
    std::string error;
    uint64_t draws = 0;
    double prepareMs = 0, rasterMs = 0;
    void release(){
        if(program) glDeleteProgram(program);
        if(vao) glDeleteVertexArrays(1, &vao);
        if(fbo) glDeleteFramebuffers(1, &fbo);
        glDeleteTextures(5, textures);
        program = vao = fbo = 0;
        for(auto& t : textures) t = 0;
        ready = false;
    }

    bool kick(Canvas& canvas, float x, float top, float width, float power, int cap){
        auto started = std::chrono::steady_clock::now();
        if(!error.empty()) return false; // a failed accelerator stays on the safe CPU fallback
        if(!std::isfinite(x) || !std::isfinite(top) || !std::isfinite(width) ||
           !std::isfinite(power) || width <= 0 || power <= 0) return false;
        if(cap == Canvas::SQUARE) return true;
        if(!init()) return false;
        GLint maxTex = 0; glGetIntegerv(GL_MAX_TEXTURE_SIZE, &maxTex);
        if(canvas.w > maxTex) return false;
        const float snap = Canvas::snapOf(cap);
        // Include neighbouring samples at float rounding boundaries. Discard offscreen columns
        // before preparing the tables (end-anchored and burst cells may extend outside the page).
        const int base = (int)std::floor(x + snap);
        int first = std::max(0, -base - 2);
        int end = std::min((int)std::ceil(width), canvas.w - base + 2);
        int columns = end - first;
        if(columns <= 0) return true;
        if(columns > maxTex) return false;
        int x0 = std::clamp((int)std::floor((float)first + x + snap), 0, canvas.w);
        int x1 = std::clamp((int)std::floor((float)(end - 1) + x + snap) + 1, 0, canvas.w);
        if(x1 <= x0) return true;
        std::vector<uint32_t> thresholds((size_t)columns * 256);
        std::vector<float> geometry((size_t)columns * 2);
        const float H = canvas.h - top;
        Canvas::parallelFor((size_t)columns, [&](size_t a, size_t b){
            for(size_t k = a; k < b; k++){
                float i = (float)(first + (int)k), ph = i / width;
                float env = (float)std::pow((double)(1 - i / width), (double)(power * 2));
                float y = (float)std::pow((double)ph, 0.25) * H + top;
                float h = canvas.h - y;
                int count = std::max(0, (int)std::ceil(h));
                geometry[k * 2] = y; geometry[k * 2 + 1] = h;
                auto alpha = [&](int j){
                    float v = (float)std::pow((double)(j / h), (double)(power / 2));
                    float af = env * 255 * v;
                    return (int)(std::clamp(af / 255.0f, 0.0f, 1.0f) * 255);
                };
                int maximum = count > 0 ? alpha(count - 1) : 0, previous = 0;
                thresholds[k * 256] = 0;
                for(int level = 1; level < 256; level++){
                    int j = count;
                    if(level <= maximum){
                        // An inverse envelope gives a close guess. Correct with the original
                        // float expression, so the table is exact even at quantization boundaries.
                        double guess = h * std::pow(level / (double)(env * 255), 2.0 / power);
                        j = std::clamp((int)std::ceil(guess), previous, count - 1);
                        while(j < count && alpha(j) < level) j++;
                        while(j > previous && alpha(j - 1) >= level) j--;
                    }
                    thresholds[k * 256 + level] = (uint32_t)j;
                    previous = j;
                }
            }
        }, 512);
        // Alpha zero is a no-op. Tighten the transfer/draw rectangle to samples that actually
        // paint; a long decaying kick often occupies only a small corner of its nominal box.
        int activeX0 = canvas.w, activeX1 = 0, activeY0 = canvas.h, activeY1 = 0;
        for(int k = 0; k < columns; k++){
            float y = geometry[k * 2], h = geometry[k * 2 + 1];
            int n = std::max(0, (int)std::ceil(h)), j = thresholds[(size_t)k * 256 + 1];
            if(j >= n) continue;
            int xx = (int)std::floor((float)(first + k) + x + snap);
            if(xx < 0 || xx >= canvas.w) continue;
            activeX0 = std::min(activeX0, xx); activeX1 = std::max(activeX1, xx + 1);
            activeY0 = std::min(activeY0, std::clamp((int)std::floor((float)j + y + snap), 0, canvas.h));
            activeY1 = std::max(activeY1, std::clamp((int)std::floor((float)(n - 1) + y + snap) + 1, 0, canvas.h));
        }
        x0 = activeX0; x1 = activeX1;
        int W = x1 - x0;
        if(W <= 0 || activeY1 <= activeY0) return true;
        prepareMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
        auto rasterStarted = std::chrono::steady_clock::now();
        // Upload only the small envelope descriptions. Textures are reused between calls.
        glUseProgram(program);
        bind(1, textures[2]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32UI, 256, columns, 0, GL_RED_INTEGER, GL_UNSIGNED_INT, thresholds.data());
        bind(2, textures[3]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RG32F, columns, 1, 0, GL_RG, GL_FLOAT, geometry.data());
        bind(3, textures[4]);
        glUniform1i(uniform("image"), 0); glUniform1i(uniform("thresholds"), 1);
        glUniform1i(uniform("geometry"), 2); glUniform1i(uniform("divideTable"), 3);
        glUniform1i(uniform("firstColumn"), first); glUniform1i(uniform("columns"), columns);
        glUniform1i(uniform("base"), base); glUniform1f(uniform("xStart"), x);
        glUniform1f(uniform("snap"), snap); glUniform1i(uniform("roundCap"), cap == Canvas::ROUND);
        glUniform1i(uniform("layerMode"), canvas.layer); glUniform1i(uniform("addMode"), canvas.addMode);
        glBindVertexArray(vao); glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glDisable(GL_BLEND); glDisable(GL_SCISSOR_TEST); glDisable(GL_DEPTH_TEST);
        // Strip transfers keep the integer targets below the driver's large-texture limit.
        const int rows = std::max(1, std::min(activeY1 - activeY0, 4000000 / W));
        glPixelStorei(GL_UNPACK_ROW_LENGTH, canvas.w);
        glPixelStorei(GL_PACK_ROW_LENGTH, canvas.w);
        bool changed = false;
        for(int y = activeY0; y < activeY1; y += rows){
            int h = std::min(rows, activeY1 - y);
            uint32_t* pixels = canvas.px.data() + (size_t)y * canvas.w + x0;
            bind(0, textures[0]);
            // Keep the native word's byte order end to end (B,G,R,A on this platform).
            // White source-over treats the three colour channels symmetrically, so no swizzle
            // is needed; avoiding BGRA_INTEGER also avoids a driver format-conversion path.
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, W, h, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, pixels);
            glBindTexture(GL_TEXTURE_2D, textures[1]);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8UI, W, h, 0, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, nullptr);
            glBindTexture(GL_TEXTURE_2D, textures[0]);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, textures[1], 0);
            if(glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE){
                error = "exact raster framebuffer"; break;
            }
            glViewport(0, 0, W, h);
            glUniform2i(uniform("origin"), x0, y);
            glDrawArrays(GL_TRIANGLES, 0, 3);
            // Check before writing to CPU memory. A failure after an earlier strip aborts the page;
            // it must never fall back and paint the same kick a second time.
            if(glGetError() != GL_NO_ERROR){ error = "exact raster draw"; break; }
            glReadPixels(0, 0, W, h, GL_RGBA_INTEGER, GL_UNSIGNED_BYTE, pixels);
            changed = true;
            if(glGetError() != GL_NO_ERROR){ error = "exact raster readback"; break; }
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        if(!error.empty()){
            if(changed) throw RasterError(error);
            return false;
        }
        draws++;
        rasterMs += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - rasterStarted).count();
        return true;
    }

private:
    GLuint program = 0, vao = 0, fbo = 0, textures[5] = {};
    bool ready = false, attempted = false;
    GLint uniform(const char* name){ return glGetUniformLocation(program, name); }
    static void bind(int unit, GLuint tex){ glActiveTexture(GL_TEXTURE0 + unit); glBindTexture(GL_TEXTURE_2D, tex); }
    bool init(){
        if(attempted) return ready;
        attempted = true;
        const char* vs = R"GLSL(#version 410
void main(){
    vec2 p = vec2(gl_VertexID == 1 ? 3.0 : -1.0, gl_VertexID == 2 ? 3.0 : -1.0);
    gl_Position = vec4(p, 0, 1);
}
)GLSL";
        const char* fs = R"GLSL(#version 410
uniform usampler2D image, thresholds, divideTable;
uniform sampler2D geometry;
uniform int firstColumn, columns, base, roundCap, layerMode, addMode;
uniform float xStart, snap;
uniform ivec2 origin;
out uvec4 color;
uint mul8(uint a, uint b){ uint t = a * b + 128u; return (t + (t >> 8)) >> 8; }
uvec3 mulRGB(uvec3 c, uint a){ return uvec3(mul8(c.r, a), mul8(c.g, a), mul8(c.b, a)); }
uvec4 paint(uvec4 d, uint a){
    if(a == 0u) return d;
    if(addMode == 1){
        uint sa = a + (a >= 127u ? 1u : 0u);
        return min(uvec4(255), uvec4(d.rgb + uvec3((255u * sa) >> 8), d.a + a));
    }
    uint ia = 255u - a;
    if(layerMode == 0) return uvec4(uvec3(a) + mulRGB(d.rgb, ia), 255);
    uint da = mul8(ia, d.a), ra = a + da;
    uvec3 rgb = uvec3(a);
    if(da > 0u) rgb += da == 255u ? d.rgb : mulRGB(d.rgb, da);
    if(ra > 0u && ra < 255u){
        rgb = uvec3(texelFetch(divideTable, ivec2(rgb.r, ra), 0).r,
                    texelFetch(divideTable, ivec2(rgb.g, ra), 0).r,
                    texelFetch(divideTable, ivec2(rgb.b, ra), 0).r);
    }
    return uvec4(rgb, ra);
}
int alphaAt(int column, int sampleIndex){
    int lo = 0, hi = 256;
    for(int k = 0; k < 8; k++){
        int mid = (lo + hi) / 2;
        if(texelFetch(thresholds, ivec2(mid, column), 0).r <= uint(sampleIndex)) lo = mid;
        else hi = mid;
    }
    return lo;
}
void main(){
    ivec2 q = ivec2(gl_FragCoord.xy), p = q + origin;
    uvec4 d = texelFetch(image, q, 0);
    // Check adjacent samples explicitly at floating-point snapping boundaries. Their order is
    // the CPU's outer x / inner y order, including the rare case of two samples on one pixel.
    int guessX = p.x - base;
    for(int dx = -2; dx <= 2; dx++){
        int ix = guessX + dx, c = ix - firstColumn;
        if(c < 0 || c >= columns) continue;
        precise float xx = float(ix) + xStart;
        precise float xs = xx + snap;
        if(int(floor(xs)) != p.x) continue;
        vec2 g = texelFetch(geometry, ivec2(c, 0), 0).rg;
        int guessY = p.y - int(floor(g.x + snap)), n = int(ceil(g.y));
        for(int dy = -2; dy <= 2; dy++){
            int j = guessY + dy;
            if(j < 0 || j >= n) continue;
            precise float yy = float(j) + g.x;
            precise float ys = yy + snap;
            if(int(floor(ys)) != p.y) continue;
            uint a = uint(alphaAt(c, j));
            if(roundCap == 1) a = mul8(181u, a);
            d = paint(d, a);
        }
    }
    color = d;
}
)GLSL";
        auto compile = [&](GLenum kind, const char* src){
            GLuint s = glCreateShader(kind); glShaderSource(s, 1, &src, nullptr); glCompileShader(s);
            GLint ok = 0; glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
            if(!ok){ char log[2048]; glGetShaderInfoLog(s, sizeof(log), nullptr, log); error = log; glDeleteShader(s); return GLuint(0); }
            return s;
        };
        GLuint v = compile(GL_VERTEX_SHADER, vs), f = compile(GL_FRAGMENT_SHADER, fs);
        if(!v || !f){ if(v) glDeleteShader(v); if(f) glDeleteShader(f); return false; }
        program = glCreateProgram(); glAttachShader(program, v); glAttachShader(program, f); glLinkProgram(program);
        glDeleteShader(v); glDeleteShader(f);
        GLint linked = 0; glGetProgramiv(program, GL_LINK_STATUS, &linked);
        if(!linked){ char log[2048]; glGetProgramInfoLog(program, sizeof(log), nullptr, log); error = log; return false; }
        glGenVertexArrays(1, &vao); glGenFramebuffers(1, &fbo); glGenTextures(5, textures);
        for(GLuint t : textures){
            glBindTexture(GL_TEXTURE_2D, t);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        }
        std::vector<uint8_t> divide(65536);
        for(int a = 0; a < 256; a++) std::copy_n(Canvas::div8row(a), 256, divide.data() + a * 256);
        bind(3, textures[4]);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8UI, 256, 256, 0, GL_RED_INTEGER, GL_UNSIGNED_BYTE, divide.data());
        ready = glGetError() == GL_NO_ERROR;
        if(!ready) error = "exact raster initialization";
        return ready;
    }
};
} // namespace formsgl
