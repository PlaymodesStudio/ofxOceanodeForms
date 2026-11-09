//
//  formsLayerFx.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Effects on each layer's buffer before the composite and before the sound reads it (so what is
//  heard follows what is seen):
//    H blur   a Gaussian blur along x (sigma in px)
//    V blur   a Gaussian blur along y (sigma in px)
//    Trail    a tail behind what moves: the images scroll right to left, so each thing leaves a
//             trail to its right, fading exponentially (like a reverb's decay): a pixel shows the
//             brightest of what passed through it, times decay^distance. Length in px (Forms Box
//             gives it in beats, per layer); level: how bright the tail starts.
//  Nothing is read from outside the image: blurs repeat the edge pixel, the trail starts at the
//  left edge. The buffers hold premultiplied colour and alpha: every channel is treated alike.
//  The trail is exact and cheap: log2(length) passes of "max(self, decay^s * neighbour at -s)"
//  for s = 1, 2, 4, ... give max over every distance k of decay^k * pixel(x - k).
//

#pragma once

#include "ofMain.h"
#include <cmath>
#include <string>

namespace forms {

struct LayerFxSettings {
    float hBlur = 0, vBlur = 0;     // sigma, px
    float trailPx = 0;              // px until the tail is down to 1/1000
    float trailLevel = 0.8f;        // 0..1
    float maskAmount=0, maskWidth=4, maskHeight=.25f, maskContrast=1, maskThreshold=0, maskBlur=0, maskSpeed=0;
    int maskSeed=1; bool maskInvert=false;
    // Musical coordinates: anchor beat at headerX, pixels per beat of this layer.
    float maskBeat=0, maskHeaderX=0, maskPixelsPerBeat=1, maskPhase=0;
    bool any() const { return hBlur > 0.05f || vBlur > 0.05f || trailPx >= 1 || maskAmount > 0; }
};

// The shaders (also read by tests/test_layer_fx.cpp)
inline const char* layerFxVert(){ return R"(
#version 410
uniform mat4 modelViewProjectionMatrix;
layout(location = 0) in vec4 position;
void main(){ gl_Position = modelViewProjectionMatrix * position; }
)"; }

// Separable Gaussian, linear-sampling pairs (about sigma*2.5 taps). Only taps inside the image
// count, and the weights are renormalized over them: nothing comes from outside, and what is near
// an edge keeps its brightness (it doesn't fade into a border that isn't there).
inline const char* layerFxBlurFrag(){ return R"(
#version 410
uniform sampler2D tex;
uniform vec2 size;
uniform vec2 dir;      // (1,0) or (0,1)
uniform float sigma;
out vec4 out_color;
void main(){
    vec2 p = gl_FragCoord.xy;
    float len = dot(size, dir);          // the image's extent along the blur
    float pos = dot(p, dir);             // this pixel's centre along it
    int r = int(ceil(sigma * 2.5));
    vec4 sum = texture(tex, p / size);
    float wsum = 1.0;
    float k = -0.5 / (sigma * sigma);
    for(int i = 1; i <= r; i += 2){
        float w1 = exp(k * float(i * i));
        float w2 = (i + 1 <= r) ? exp(k * float((i + 1) * (i + 1))) : 0.0;
        float w = w1 + w2;
        float o = (float(i) * w1 + float(i + 1) * w2) / w;
        // a pair whose farther pixel is outside: its inside pixel alone, at its own weight
        for(int sgn = -1; sgn <= 1; sgn += 2){
            float far = pos + float(sgn) * float(i + 1), near = pos + float(sgn) * float(i);
            if(far >= 0.0 && far <= len){ sum += texture(tex, (p + dir * o * float(sgn)) / size) * w; wsum += w; }
            else if(near >= 0.0 && near <= len){ sum += texture(tex, (p + dir * float(i) * float(sgn)) / size) * w1; wsum += w1; }
        }
    }
    out_color = sum / wsum;
}
)"; }

// One trail pass: max(self, decay * the pixel `stepPx` to the left)
inline const char* layerFxTrailFrag(){ return R"(
#version 410
uniform sampler2D tex;
uniform float stepPx;
uniform float decay;   // decay per px, to the power stepPx
out vec4 out_color;
void main(){
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = texelFetch(tex, p, 0);
    int x = p.x - int(stepPx);
    if(x >= 0) c = max(c, decay * texelFetch(tex, ivec2(x, p.y), 0));
    out_color = c;
}
)"; }

// The trail's end: the original, with the tail at its level
inline const char* layerFxMixFrag(){ return R"(
#version 410
uniform sampler2D tex;     // the trail (max over distances, the pixel itself included)
uniform sampler2D orig;
uniform float level;
out vec4 out_color;
void main(){
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 o = texelFetch(orig, p, 0);
    out_color = max(o, level * texelFetch(tex, p, 0));
}
)"; }

inline const char* layerFxMaskFrag(){ return R"(
#version 410
uniform sampler2D tex;
uniform vec2 size;
uniform vec4 geom; // beat at header, header x, pixels per beat, evolution phase
uniform vec4 mask; // amount, width in beats, height fraction, contrast
uniform float threshold;
uniform bool invert;
uniform uint seed;
uniform bool maskOnly;
out vec4 out_color;
uint hash(uvec3 p){
    uint h=p.x*374761393u+p.y*668265263u+p.z*2246822519u+seed*3266489917u;
    h=(h^(h>>13u))*1274126177u; return h^(h>>16u);
}
float grad(ivec3 p,vec3 d){
    uint h=hash(uvec3(p))&15u;
    float u=h<8u?d.x:d.y;
    float v=h<4u?d.y:(h==12u||h==14u?d.x:d.z);
    return ((h&1u)==0u?u:-u)+((h&2u)==0u?v:-v);
}
float perlin(vec3 p){
    ivec3 i=ivec3(floor(p));vec3 f=fract(p),u=f*f*f*(f*(f*6.0-15.0)+10.0);
    float a=mix(grad(i,f),grad(i+ivec3(1,0,0),f-vec3(1,0,0)),u.x);
    float b=mix(grad(i+ivec3(0,1,0),f-vec3(0,1,0)),grad(i+ivec3(1,1,0),f-vec3(1,1,0)),u.x);
    float c=mix(grad(i+ivec3(0,0,1),f-vec3(0,0,1)),grad(i+ivec3(1,0,1),f-vec3(1,0,1)),u.x);
    float d=mix(grad(i+ivec3(0,1,1),f-vec3(0,1,1)),grad(i+ivec3(1,1,1),f-vec3(1,1,1)),u.x);
    return mix(mix(a,b,u.y),mix(c,d,u.y),u.z);
}
void main(){
    vec2 p=gl_FragCoord.xy;
    float beat=geom.x+(p.x-geom.y)/max(.0001,geom.z);
    vec3 q=vec3(beat/max(.0001,mask.y),(p.y/size.y)/max(.0001,mask.z),geom.w)+vec3(17.31,43.73,9.19);
    float v=pow(clamp(.5+.5*perlin(q),0.0,1.0),max(.01,mask.w));
    if(invert)v=1.0-v;
    if(threshold>0.0)v=step(threshold,v);
    // Premultiplied colour and alpha must be attenuated together.
    out_color=maskOnly?vec4(v):texelFetch(tex,ivec2(p),0)*mix(1.0,v,clamp(mask.x,0.0,1.0));
}
)"; }

inline const char* layerFxMaskApplyFrag(){ return R"(
#version 410
uniform sampler2D tex;
uniform sampler2D maskTex;
uniform float amount;
out vec4 out_color;
void main(){
    ivec2 p=ivec2(gl_FragCoord.xy);
    float v=texelFetch(maskTex,p,0).r;
    out_color=texelFetch(tex,p,0)*mix(1.0,v,clamp(amount,0.0,1.0));
}
)"; }

class LayerFx {
public:
    GLint internalFormat = GL_RGBA; // Export opts into floating-point blur/trail intermediates.
    bool ok = false, tried = false;

    // Processes `f` in place
    void process(ofFbo& f, const LayerFxSettings& s){
        if(!s.any() || !f.isAllocated()) return;
        setup();
        if(!ok) return;
        int w = (int)f.getWidth(), h = (int)f.getHeight();
        alloc(a, w, h); alloc(b, w, h);
        ofFbo* cur = &f;
        auto other = [&](ofFbo* c){ return c == &a ? &b : &a; };
        ofPushStyle();
        ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        auto pass = [&](ofShader& sh, ofFbo& dst, const std::function<void()>& uniforms){
            dst.begin();
            ofClear(0, 0, 0, 0);
            sh.begin();
            uniforms();
            ofSetColor(255);
            ofDrawRectangle(0, 0, w, h);
            sh.end();
            dst.end();
        };
        if(s.hBlur > 0.05f){
            ofFbo* dst = other(cur);
            pass(blur, *dst, [&]{ blur.setUniformTexture("tex", cur->getTexture(), 0); blur.setUniform2f("size", w, h); blur.setUniform2f("dir", 1, 0); blur.setUniform1f("sigma", s.hBlur); });
            cur = dst;
        }
        if(s.vBlur > 0.05f){
            ofFbo* dst = cur == &f ? &a : other(cur);
            pass(blur, *dst, [&]{ blur.setUniformTexture("tex", cur->getTexture(), 0); blur.setUniform2f("size", w, h); blur.setUniform2f("dir", 0, 1); blur.setUniform1f("sigma", s.vBlur); });
            cur = dst;
        }
        if(s.trailPx >= 1){
            // keep the input for the mix, then the doubling passes
            alloc(c, w, h);
            ofFbo* orig = cur;
            if(cur != &f){ copy(*cur, c, w, h); orig = &c; }
            float perPx = std::exp(-6.9078f / s.trailPx);   // 1/1000 at trailPx
            int steps = (int)std::ceil(std::log2(std::max(2.0f, s.trailPx * 1.5f)));
            ofFbo* src = cur;
            for(int i = 0; i < steps; i++){
                float st = (float)(1 << i);
                ofFbo* dst = (src == &a) ? &b : &a;
                pass(trail, *dst, [&]{ trail.setUniformTexture("tex", src->getTexture(), 0); trail.setUniform1f("stepPx", st); trail.setUniform1f("decay", std::pow(perPx, st)); });
                src = dst;
            }
            ofFbo* dst = (src == &a) ? &b : &a;
            pass(mix, *dst, [&]{ mix.setUniformTexture("tex", src->getTexture(), 0); mix.setUniformTexture("orig", orig->getTexture(), 1); mix.setUniform1f("level", ofClamp(s.trailLevel, 0, 1)); });
            cur = dst;
        }
        if(s.maskAmount > 0){
            ofFbo* dst=other(cur);
            bool soften=s.maskBlur>.05f;
            if(soften){alloc(maskA,w,h);alloc(maskB,w,h);}
            pass(noiseMask,soften?maskA:*dst,[&]{
                noiseMask.setUniformTexture("tex",cur->getTexture(),0);
                noiseMask.setUniform2f("size",w,h);
                noiseMask.setUniform4f("geom",s.maskBeat,s.maskHeaderX,s.maskPixelsPerBeat,s.maskPhase);
                noiseMask.setUniform4f("mask",s.maskAmount,s.maskWidth,s.maskHeight,s.maskContrast);
                noiseMask.setUniform1f("threshold",s.maskThreshold);
                noiseMask.setUniform1i("maskOnly",soften);
                noiseMask.setUniform1i("invert",s.maskInvert);
                glUniform1ui(glGetUniformLocation(noiseMask.getProgram(),"seed"),(GLuint)s.maskSeed);
            });
            if(soften){
                // Blur the thresholded mask itself, then attenuate the untouched
                // layer. Blurring the already-masked image would smear its notes.
                pass(blur,maskB,[&]{blur.setUniformTexture("tex",maskA.getTexture(),0);blur.setUniform2f("size",w,h);blur.setUniform2f("dir",1,0);blur.setUniform1f("sigma",s.maskBlur);});
                pass(blur,maskA,[&]{blur.setUniformTexture("tex",maskB.getTexture(),0);blur.setUniform2f("size",w,h);blur.setUniform2f("dir",0,1);blur.setUniform1f("sigma",s.maskBlur);});
                pass(maskApply,*dst,[&]{maskApply.setUniformTexture("tex",cur->getTexture(),0);maskApply.setUniformTexture("maskTex",maskA.getTexture(),1);maskApply.setUniform1f("amount",s.maskAmount);});
            }
            cur=dst;
        }
        if(cur != &f) copy(*cur, f, w, h);
        ofPopStyle();
    }

private:
    ofShader blur, trail, mix, plain, noiseMask, maskApply;
    ofFbo a, b, c, maskA, maskB;
    void alloc(ofFbo& x, int w, int h){
        if(x.isAllocated() && (int)x.getWidth() == w && (int)x.getHeight() == h && x.getTexture().getTextureData().glInternalFormat == internalFormat) return;
        ofFboSettings st; st.width = w; st.height = h; st.internalformat = internalFormat;
        x.allocate(st);
    }
    void copy(ofFbo& src, ofFbo& dst, int w, int h){
        dst.begin();
        ofClear(0, 0, 0, 0);
        plain.begin();
        plain.setUniformTexture("tex", src.getTexture(), 0);
        ofSetColor(255);
        ofDrawRectangle(0, 0, w, h);
        plain.end();
        dst.end();
    }
    void setup(){
        if(tried) return;
        tried = true;
        auto make = [](ofShader& sh, const char* frag){
            bool r = sh.setupShaderFromSource(GL_VERTEX_SHADER, layerFxVert()) && sh.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
            if(r){ sh.bindDefaults(); r = sh.linkProgram(); }
            return r;
        };
        static const char* plainFrag = R"(
#version 410
uniform sampler2D tex;
out vec4 out_color;
void main(){ out_color = texelFetch(tex, ivec2(gl_FragCoord.xy), 0); }
)";
        ok = make(blur, layerFxBlurFrag()) && make(trail, layerFxTrailFrag()) && make(mix, layerFxMixFrag()) && make(plain, plainFrag) && make(noiseMask,layerFxMaskFrag()) && make(maskApply,layerFxMaskApplyFrag());
        if(!ok) ofLogError("Forms Box") << "layer effects shaders failed";
    }
};

} // namespace forms
