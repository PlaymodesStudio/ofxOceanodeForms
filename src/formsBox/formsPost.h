//
//  formsPost.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Post-processing of Forms Box's composite, in one shader pass (what the patch did with
//  HSLFilter → PixelStretch → Ranger/Mixer (opacity) → Levels → Macro_12 + Mixer_1 (the line)):
//    1. stretch: everything left of the header shows the header column
//    2. hue / saturation / value (HSV)
//    3. opacity (the header's beat-repeat fade and strobe gate)
//    4. levels: (c - min) / (max - min), gamma, then low..high
//    5. the header line: a vertical line at the header, with its own thickness, colour,
//       softness and easing (it can glide behind a jumping header)
//    6. edge feathers (a separate, last pass: Post::feather, see formsFeather.h), called by the
//       node after its background so the edges fade toward the background colour
//  The settings are plain members (the node registers them as controls). If the shader fails to
//  compile the composite passes through unchanged.
//

#pragma once

#include "ofMain.h"
#include <cmath>
#include <string>
#include "formsFeather.h"

namespace forms {

struct Post {
    // settings
    bool enabled = true;
    bool stretch = true;
    float hue = 0, sat = 1, val = 1;
    bool useOpacity = true;
    float levelMin = 0, levelMax = 0.617f, gamma = 1, low = 0, high = 1;
    bool line = true;
    float lineWidth = 4.8f;        // px (at the output size)
    float lineSoft = 0;            // px of soft edge on each side (horizontal)
    float lineSoftV = 0;           // px over which the line fades in from the top and bottom edges
                                   // (inside the image: nothing falls outside its height)
    float lineAlpha = 1;
    ofFloatColor lineColor = ofFloatColor(1, 1, 1);
    float lineEase = 0;            // seconds to cover ~63% of a jump (0: follows the header exactly)
    int lineEaseCurve = 0;         // 0 exponential, 1 linear glide (constant speed), 2 smooth (critically damped)
    float lineOffset = 0;          // px added to the header position

    Feather edges;                 // edge feathers (all widths 0: off)

    ofFbo fbo, featherFbo;
    float lineX = -1, lineV = 0;   // eased line position (px) and speed (smooth curve)
    bool ok = false, tried = false;

    // Eases the line toward the header. Called once per frame.
    void stepLine(float target, float dt, float width){
        target += lineOffset;
        if(lineX < 0 || lineEase <= 0 || dt <= 0 || std::abs(target - lineX) > width * 0.75f){
            // first frame, no easing, or a wrap of the scroll: jump
            lineX = target; lineV = 0;
            return;
        }
        if(lineEaseCurve == 1){
            float speed = width / std::max(0.01f, lineEase * 4);   // px per second
            float d = target - lineX, step = speed * dt;
            lineX = std::abs(d) <= step ? target : lineX + (d > 0 ? step : -step);
        }else if(lineEaseCurve == 2){
            float w = 2.0f / std::max(0.001f, lineEase);           // critically damped spring
            float x = lineX - target;
            float a = -w * w * x - 2 * w * lineV;
            lineV += a * dt;
            lineX += lineV * dt;
        }else{
            float k = 1 - std::exp(-dt / std::max(0.001f, lineEase));
            lineX += (target - lineX) * k;
        }
    }

    // The final image: `src` processed into this->fbo. headerX in px of src; opacity 0..1.
    const ofTexture* process(const ofTexture& src, float headerX, float opacity, float dt){
        int w = (int)src.getWidth(), h = (int)src.getHeight();
        if(w < 1 || h < 1) return &src;
        stepLine(headerX, dt, (float)w);
        if(!enabled) return &src;
        setupShader();
        if(!ok) return &src;
        if(!fbo.isAllocated() || (int)fbo.getWidth() != w || (int)fbo.getHeight() != h){
            ofFboSettings st; st.width = w; st.height = h; st.internalformat = GL_RGBA;
            fbo.allocate(st);
        }
        fbo.begin();
        ofClear(0, 0, 0, 255);
        ofPushStyle();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        shader.begin();
        shader.setUniformTexture("tSource", src, 0);
        shader.setUniform2f("size", (float)w, (float)h);
        shader.setUniform1f("stretchX", stretch ? std::max(0.0f, headerX) : -1.0f);
        shader.setUniform3f("hsv", hue, sat, val);
        shader.setUniform1f("opacity", useOpacity ? std::max(opacity, 0.001f) : 1.0f);
        shader.setUniform4f("levels", levelMin, std::max(levelMin + 1e-4f, levelMax), std::max(0.01f, gamma), 0);
        shader.setUniform2f("lowHigh", low, high);
        shader.setUniform4f("lineGeom", line ? lineX : -1e6f, std::max(0.0f, lineWidth), std::max(0.0f, lineSoft), line ? lineAlpha : 0.0f);
        shader.setUniform3f("lineColor", lineColor.r, lineColor.g, lineColor.b);
        shader.setUniform1f("lineSoftV", std::max(0.0f, std::min(lineSoftV, h * 0.5f)));
        ofSetColor(255);
        ofDrawRectangle(0, 0, w, h);
        shader.end();
        ofPopStyle();
        fbo.end();
        return &fbo.getTexture();
    }

    // Edge feathers, last: `img` fades toward `target` (the background; black when none) near
    // each edge. Widths all 0 or no shader: returns &img untouched. Any node can call it after
    // process() (and after its own background pass).
    const ofTexture* feather(const ofTexture& img, ofFloatColor target = ofFloatColor(0, 0, 0)){
        int w = (int)img.getWidth(), h = (int)img.getHeight();
        if(!edges.active() || w < 1 || h < 1) return &img;
        setupFeather();
        if(!fOk) return &img;
        if(!featherFbo.isAllocated() || (int)featherFbo.getWidth() != w || (int)featherFbo.getHeight() != h){
            ofFboSettings st; st.width = w; st.height = h; st.internalformat = GL_RGBA;
            featherFbo.allocate(st);
        }
        featherFbo.begin();
        ofClear(0, 0, 0, 255);
        ofPushStyle();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        fShader.begin();
        fShader.setUniformTexture("tImage", img, 0);
        fShader.setUniform2f("size", (float)w, (float)h);
        fShader.setUniform4f("width", edges.width[0], edges.width[1], edges.width[2], edges.width[3]);
        fShader.setUniform4f("curve", edges.curve[0], edges.curve[1], edges.curve[2], edges.curve[3]);
        fShader.setUniform3f("target", target.r, target.g, target.b);
        ofSetColor(255);
        ofDrawRectangle(0, 0, w, h);
        fShader.end();
        ofPopStyle();
        featherFbo.end();
        return &featherFbo.getTexture();
    }

    // The CPU version of one pixel (the same maths as the shader), for the tests and for the
    // sound (which reads the column before levels, like the patch did)
    static void hsvAdjust(float& r, float& g, float& b, float hue, float sat, float val){
        float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn;
        float hh = 0;
        if(d > 1e-10f){
            if(mx == r) hh = std::fmod((g - b) / d, 6.0f);
            else if(mx == g) hh = (b - r) / d + 2;
            else hh = (r - g) / d + 4;
            hh /= 6; if(hh < 0) hh += 1;
        }
        float s = mx > 1e-10f ? d / mx : 0, v = mx;
        hh = std::fmod(hh + hue, 1.0f); if(hh < 0) hh += 1;
        s *= sat; v *= val;
        auto f = [&](float n){ float k = std::fmod(n + hh * 6, 6.0f); return v - v * s * std::max(0.0f, std::min(std::min(k, 4 - k), 1.0f)); };
        r = f(5); g = f(3); b = f(1);
    }

private:
    ofShader shader, fShader;
    bool fOk = false, fTried = false;
    void setupFeather(){
        if(fTried) return;
        fTried = true;
        std::string vert = "#version 410\nuniform mat4 modelViewProjectionMatrix;\nlayout(location = 0) in vec4 position;\nvoid main(){ gl_Position = modelViewProjectionMatrix * position; }\n";
        std::string frag = R"(
#version 410
uniform sampler2D tImage;
uniform vec2 size;
uniform vec4 width;   // left, right, top, bottom (fractions)
uniform vec4 curve;
uniform vec3 target;
out vec4 out_color;
float edge(float d, float w, float k){
    if(w <= 0.0) return 1.0;
    float t = clamp(d / w, 0.0, 1.0);
    t = t * t * (3.0 - 2.0 * t);
    return pow(t, max(0.05, k));
}
void main(){
    vec3 c = texelFetch(tImage, ivec2(gl_FragCoord.xy), 0).rgb;
    float u = gl_FragCoord.x / size.x;
X
    float f = edge(u, width.x, curve.x) * edge(1.0 - u, width.y, curve.y) *
              edge(v, width.z, curve.z) * edge(1.0 - v, width.w, curve.w);
    out_color = vec4(mix(target, c, f), 1.0);
}
)";
        fOk = fShader.setupShaderFromSource(GL_VERTEX_SHADER, vert) && fShader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
        if(fOk){ fShader.bindDefaults(); fOk = fShader.linkProgram(); }
        if(!fOk) ofLogError("Forms Box") << "edge feather shader failed: no feathers";
    }
    void setupShader(){
        if(tried) return;
        tried = true;
        std::string vert = R"(
#version 410
uniform mat4 modelViewProjectionMatrix;
layout(location = 0) in vec4 position;
void main(){ gl_Position = modelViewProjectionMatrix * position; }
)";
        std::string frag = R"(
#version 410
uniform sampler2D tSource;
uniform vec2 size;
uniform float stretchX;
uniform vec3 hsv;
uniform float opacity;
uniform vec4 levels;      // min, max, gamma
uniform vec2 lowHigh;
uniform vec4 lineGeom;    // x, width, soft, alpha
uniform vec3 lineColor;
uniform float lineSoftV;  // px: fade in from the top and bottom edges
out vec4 out_color;

vec3 rgb2hsv(vec3 c){
    vec4 K = vec4(0.0, -1.0 / 3.0, 2.0 / 3.0, -1.0);
    vec4 p = mix(vec4(c.bg, K.wz), vec4(c.gb, K.xy), step(c.b, c.g));
    vec4 q = mix(vec4(p.xyw, c.r), vec4(c.r, p.yzx), step(p.x, c.r));
    float d = q.x - min(q.w, q.y);
    float e = 1.0e-10;
    return vec3(abs(q.z + (q.w - q.y) / (6.0 * d + e)), d / (q.x + e), q.x);
}
vec3 hsv2rgb(vec3 c){
    vec4 K = vec4(1.0, 2.0 / 3.0, 1.0 / 3.0, 3.0);
    vec3 p = abs(fract(c.xxx + K.xyz) * 6.0 - K.www);
    return c.z * mix(K.xxx, clamp(p - K.xxx, 0.0, 1.0), c.y);
}
// hue / saturation / value (HSLFilter) of one texel
vec3 colour(ivec2 p){
    vec3 h = rgb2hsv(texelFetch(tSource, clamp(p, ivec2(0), ivec2(size) - 1), 0).rgb);
    h.x = mod(h.x + hsv.x, 1.0);
    h.y *= hsv.y;
    h.z *= hsv.z;
    return hsv2rgb(h);
}
void main(){
    vec3 c;
    // 1-2. colour, then stretch (PixelStretch): left of the header shows the header column,
    // interpolated between the two texels around it after the colour change (the patch's order)
    if(stretchX >= 0.0 && gl_FragCoord.x <= stretchX){
        vec2 q = vec2(stretchX, gl_FragCoord.y) - 0.5;
        ivec2 i = ivec2(floor(q));
        vec2 f = q - floor(q);
        vec3 a = mix(colour(i), colour(i + ivec2(1, 0)), f.x);
        vec3 b = mix(colour(i + ivec2(0, 1)), colour(i + ivec2(1, 1)), f.x);
        c = mix(a, b, f.y);
    } else c = colour(ivec2(gl_FragCoord.xy));
    // 3. opacity
    c *= opacity;
    // 4. levels
    c = min(max(c - vec3(levels.x), vec3(0.0)) / vec3(levels.y - levels.x), vec3(1.0));
    c = pow(c, vec3(1.0 / levels.z));
    c = mix(vec3(lowHigh.x), vec3(lowHigh.y), c);
    // 5. the header line (added, like Mixer_1)
    if(lineGeom.w > 0.0){
        float d = abs(gl_FragCoord.x - lineGeom.x);
        float hw = lineGeom.y * 0.5;
        float a = lineGeom.z > 0.0 ? 1.0 - smoothstep(hw, hw + lineGeom.z, d)
                                   : clamp(hw + 0.5 - d, 0.0, 1.0);   // 1 px anti-aliased edge
        // vertical softness: towards the image's top and bottom edges, inside them
        if(lineSoftV > 0.0){
            float dv = min(gl_FragCoord.y, size.y - gl_FragCoord.y);
            a *= smoothstep(0.0, lineSoftV, dv);
        }
        c = min(c + lineColor * a * lineGeom.w, vec3(1.0));
    }
    out_color = vec4(c, 1.0);
}
)";
        ok = shader.setupShaderFromSource(GL_VERTEX_SHADER, vert) &&
             shader.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
        if(ok){ shader.bindDefaults(); ok = shader.linkProgram(); }
        if(!ok) ofLogError("Forms Box") << "post-processing shader failed: the composite passes through";
    }
};

} // namespace forms
