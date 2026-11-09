//
//  v2DrawGL.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  GPU path of the v2 drawers for the app (openFrameworks, OpenGL 3.2 core / GLSL 150).
//  Approximates the CPU reference (v2Draw.h), not pixel for pixel:
//    - events: one instanced quad per event into one RGBA16F FBO per layer; the fragment shader
//      draws the look procedurally. Lighten-type looks use GL_MAX, additive looks GL_ONE/ONE.
//      Transpose slots shift note degrees before packing (as the CPU does).
//    - drawn modulation: 8 samples along each event of pitch offset (px) and level gain, baked on
//      the CPU; animated values are resolved on the CPU per frame.
//    - layer-bus effects: one full-screen pass per slot (remaps, echo, shuffle, tears, gains,
//      saturation), with the amount shape (steady / rise / fall / swell / lfo).
//    - cover below: one mask per (source layer, cover settings) group like the CPU: draw, dilate,
//      trail, feather, then multiply (eraser) or blacken the target layers.
//    - blob masks / effect scopes (5.12): instances with a blob mask or targeted by an instance /
//      element scope effect are drawn into their own FBO, get their effects there (element scope:
//      the effect over each target event's span), then a blob mask texture (quarter resolution,
//      filled on the CPU by v2Blob.h, linear filtering) multiplies them before a GL_MAX merge onto
//      the layer. Element echo / repeat arrive as engine copies; those effect events are skipped.
//    - retune (5.13): the note's morph offset is added to its pitch when packing.
//    - 3D (5.14): each event's placement (path offset, perspective scale toward the vanishing
//      point, fog) moves its quad in the vertex stage; the fragment maps gl_FragCoord back to flat
//      space. No depth of field on the GPU. With 3D on, a flat pass renders the score target first:
//      column() always reads score space (as the CPU), the 3D pass is display only.
//    - compositor: premultiplied, lighten = GL_MAX, add = ONE/ONE, normal = ONE/ONE_MINUS_SRC_ALPHA.
//
//  Orientation: texture row 0 = top of the image (y down), the same convention oF uses for FBO
//  contents, so ofTexture::draw / ofFbo::draw show it upright. The passes use gl_FragCoord and
//  texelFetch only (no oF matrices).
//  GL objects: own VAOs with explicit attribute locations (0 = corner, 1..12 = per instance,
//  divisor 1), one persistent instance buffer grown on demand, FBO ping-pong by index (no ofFbo
//  swaps), column read back through two PBOs (one frame late), sync fallback.
//  The headless tests never include this file; compare with the CPU in the node ("Compare GPU/CPU").
//

#pragma once

#include "v2Draw.h"
#include <cstring>
#include <map>

namespace forms2 {
namespace drawgl {

using draw::Frame;

// ---------------------------------------------------------------- shaders
namespace glsl {

static const char* quadVert = R"(#version 150
in vec4 position;                 // corner 0..1
in vec4 iRect;                    // quad bounds px: x0 y0 x1 y1 (y down)
in vec4 iTime;                    // xStart px, px per beat (with depth), length beats, tail beats
in vec4 iCol;                     // rgb, level
in vec4 iKind;                    // kind, look id, seed 0..1, extra
in vec4 iGeo;
in vec4 iGeo2;
in vec4 iEnv;
in vec4 iFine0; in vec4 iFine1;   // drawn pitch offset px (up), 8 samples along the event
in vec4 iLvl0;  in vec4 iLvl1;    // drawn level gain, 8 samples
in vec4 iPlace;                   // 3D (5.14): scale, translate x y (px' = px * s + t), fog gain
uniform vec2 uSize;
flat out vec4 vPlace;
out vec4 vTime; flat out vec4 vCol; flat out vec4 vKind; flat out vec4 vGeo; flat out vec4 vGeo2; flat out vec4 vEnv;
flat out vec4 vF0; flat out vec4 vF1; flat out vec4 vL0; flat out vec4 vL1;
void main(){
    vec2 p = mix(iRect.xy, iRect.zw, position.xy) * iPlace.x + iPlace.yz;
    vPlace = iPlace;
    // y down px -> row 0 at the bottom of clip space = texture row 0 = image top
    gl_Position = vec4(p.x / uSize.x * 2.0 - 1.0, p.y / uSize.y * 2.0 - 1.0, 0.0, 1.0);
    vTime = iTime; vCol = iCol; vKind = iKind; vGeo = iGeo; vGeo2 = iGeo2; vEnv = iEnv;
    vF0 = iFine0; vF1 = iFine1; vL0 = iLvl0; vL1 = iLvl1;
}
)";

static const char* eventFrag = R"(#version 150
uniform vec2 uSize;
uniform int uMaskOnly;            // 1: write cover coverage only
uniform vec2 uPxScale;            // target px -> frame px (about 2 for the half-resolution cover masks)
in vec4 vTime; flat in vec4 vCol; flat in vec4 vKind; flat in vec4 vGeo; flat in vec4 vGeo2; flat in vec4 vEnv;
flat in vec4 vF0; flat in vec4 vF1; flat in vec4 vL0; flat in vec4 vL1;
flat in vec4 vPlace;
out vec4 outColor;

float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(vec2 p, float s){
    vec2 i = floor(p), f = fract(p); f = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash(i + s), hash(i + vec2(1, 0) + s), f.x), mix(hash(i + vec2(0, 1) + s), hash(i + vec2(1, 1) + s), f.x), f.y);
}
float gshape(int s, float t){   // glide shapes: 0 linear 1 ease 2 exp 3 sCurve
    t = clamp(t, 0.0, 1.0); if(s == 0) return t;
    if(s >= 4){                    // snap: v1 sigmoid, s = 4 + tension * 1000
        float k = clamp(float(s - 4) / 1000.0, 0.0, 0.999), n = 2.0 * t - 1.0;
        return (n * (1.0 + k) / (1.0 - k + 2.0 * k * abs(n)) + 1.0) / 2.0;
    }
    if(s == 2) return (exp(3.0 * t) - 1.0) / (exp(3.0) - 1.0);
    float q = t * t * (3.0 - 2.0 * t); return s == 3 ? q * q * (3.0 - 2.0 * q) : q;
}
float u2(float t, float w){ return clamp(t / max(1e-6, w), 0.0, 1.0); }
float sample8(vec4 a, vec4 b, float u){
    float f = clamp(u, 0.0, 1.0) * 7.0; int i = int(floor(f)); float t = f - float(i);
    float s[8] = float[8](a.x, a.y, a.z, a.w, b.x, b.y, b.z, b.w);
    return mix(s[min(i, 7)], s[min(i + 1, 7)], t);
}
float line(float y, float yc, float ht, float sf){
    float sc = 1.0; if(ht < 0.5){ sc = ht / 0.5; ht = 0.5; }
    float d = abs(y - yc); return sc * clamp(1.0 - (d - ht) / max(0.6, sf), 0.0, 1.0);
}
float band(float y, float yT, float yB, float sf){
    if(yT > yB){ float s = yT; yT = yB; yB = s; }
    sf = max(0.5, sf); return clamp(min(y - yT, yB - y) / sf + 1.0, 0.0, 1.0);
}
// v1 techniques (5.15): power envelopes, cos band profile, smooth row noise
float v1env(int sh, float ph, float pw){
    ph = clamp(ph, 0.0, 1.0); if(sh == 0 || pw <= 0.0) return 1.0;
    float e = sh == 1 ? 1.0 - ph : (sh == 2 ? ph : (1.0 - cos(ph * 6.2831853)) * 0.5);
    return pow(max(e, 0.0), pw);
}
float hitEnv(float u, float w, float dec){ float k = max(0.0, w / max(1e-6, dec) - 1.0); return k <= 0.0 ? 1.0 : pow(max(0.0, 1.0 - clamp(u, 0.0, 1.0)), k); }
float profile(float v, float pw){ return pw <= 0.0 ? 1.0 : pow(max(0.0, (1.0 - cos(clamp(v, 0.0, 1.0) * 6.2831853)) * 0.5), pw); }
float rowNoise(float row1080, float scale, float contrast, float s){
    float x = row1080 * scale + 17.3, i = floor(x), f = fract(x); f = f * f * (3.0 - 2.0 * f);
    float n = 0.5 + 1.2 * (mix(hash(vec2(i, s)), hash(vec2(i + 1.0, s)), f) - 0.5);
    return min(1.0, pow(max(0.0, n * 1.33), contrast));
}
float disc(vec2 p, vec2 c, float r){ return clamp(r + 0.5 - length(p - c), 0.0, 1.0); }
// per-partial noise, bit-exact with draw::partialHash (uint mix on a 24-bit seed from iKind.z)
float pHash(uint s, uint k){ uint h = s ^ (k * 0x9E3779B9u); h ^= h >> 16; h *= 0x7FEB352Du; h ^= h >> 15; h *= 0x846CA68Bu; h ^= h >> 16; return float(h & 0xFFFFFFu) * (1.0 / 16777216.0); }
// one partial line with its detune voices (max)
float voicesLine(float y, float yP, float ap, float ht, int voices, float detPx){
    float a = 0.0;
    for(int v = 0; v < 3; v++){
        if(v >= voices) break;
        float off = voices == 1 ? 0.0 : (voices == 2 ? (v == 1 ? 0.5 : -0.5) : float(v) - 1.0);
        a = max(a, min(1.0, ap) * line(y, yP - off * detPx, ht, 0.2 * ht + 0.5));
    }
    return a;
}
float segD(vec2 p, vec2 a, vec2 b){
    vec2 d = b - a; float t = clamp(dot(p - a, d) / max(1e-9, dot(d, d)), 0.0, 1.0);
    return length(p - a - t * d);
}

void main(){
    vec2 px = (gl_FragCoord.xy * uPxScale - vPlace.yz) / vPlace.x;          // y down (row 0 = top); flat space
    int kind = int(vKind.x + 0.5), look = int(vKind.y + 0.5);
    float t = (px.x - vTime.x) / max(1e-6, vTime.y);            // beats into the event
    float len = vTime.z, u = clamp(t / max(1e-6, len), 0.0, 1.0);
    float lvl = vCol.a * sample8(vL0, vL1, u);
    float dy = sample8(vF0, vF1, u);
    float a = 0.0;
    bool discs = kind == 1 && (look == 4 || look == 6 || look == 7);
    if(t < 0.0 && !discs && kind != 4) discard;
    if(kind == 0){            // Note: geo = yFund, thickness, partials, partialDecay
                              // geo2 = pxPerSemi, detuneSemis, voices + 4 * (chance% + 101 * (partialDetune cents + 201 * partialEnvPower x 2)), partialSpread
                              // env = attack, release, partialBright, partialThin; kind.w = sub
                              // look 0 note, 1 ribbon, 2 additive
        // env.z = partialCut, env.w = envShape * 20 + envPower
        int esh = int(floor(vEnv.w / 20.0 + 1e-3)); float epw = vEnv.w - float(esh) * 20.0;
        float env = v1env(esh, u, epw);
        if(vEnv.x > 0.0) env *= clamp(t / vEnv.x, 0.0, 1.0);
        if(t > len) env = vEnv.y > 0.0 ? v1env(esh, 1.0, epw) * max(0.0, 1.0 - (t - len) / vEnv.y) : 0.0;
        float yF = vGeo.x - dy, th = vGeo.y * 0.5;
        if(look == 1) th *= 0.6 + 0.8 * sin(3.14159265 * clamp(t / max(1e-6, len + vEnv.y), 0.0, 1.0));
        int enc = int(vGeo2.z + 0.5), parts = int(vGeo.z + 0.5);
        int voices = enc - (enc / 4) * 4, qc = (enc / 4) - ((enc / 4) / 101) * 101, qdk = enc / 404, qd = qdk - (qdk / 201) * 201;
        float pch = float(qc) / 100.0, pdetSemi = float(qd) / 100.0;   // partial chance, random flattening per partial (semitones)
        float pEnvPow = float(qdk / 201) / 2.0;                         // v1 lfo power (halves): brightness gain per partial to 1 + ph1 x this
        // kind.w packs the sub level (hundredths) with the overlap gain (quarters above 1): sub + 101 x stack
        float subLvl = mod(vKind.w, 101.0) / 100.0, kStack = 1.0 + floor(vKind.w / 101.0 + 0.5) * 0.25;
        float gLvl = clamp(sample8(vL0, vL1, u), 0.0, 1.0);
        uint ps = uint(vKind.z * 16777216.0);
        // partials sit on a harmonic series in log frequency: only those within reach of this row
        // are evaluated (p found analytically), instead of looping over all of them
        float K = vGeo2.x * 12.0, sp = vGeo2.w, detPx = vGeo2.y * vGeo2.x;
        float win = max(th, 0.5) + max(0.6, 0.2 * th + 0.5) + abs(detPx) + 1.0 + pdetSemi * vGeo2.x;
        if(subLvl > 0.0) a = voicesLine(px.y, yF + K, subLvl, th, voices, detPx);   // sub line an octave below
        int pa = 0, pb = 0;
        if(abs(sp) > 1e-6){
            float q1 = (exp2(clamp((yF - px.y - win) / K, -30.0, 30.0)) - 1.0) / sp;
            float q2 = (exp2(clamp((yF - px.y + win) / K, -30.0, 30.0)) - 1.0) / sp;
            pa = int(ceil(max(0.0, min(q1, q2)))); pb = int(floor(min(float(parts), max(q1, q2))));
        }
        for(int i = 0; i < 12; i++){                               // the 12 lowest (brightest) in reach
            int p = pa + i; if(p > pb) break;
            if(pch < 1.0 && pHash(ps, uint(p) * 2u + 1u) >= pch) continue;   // not drawn (partialChance)
            float yP = yF - K * log2(1.0 + float(p) * sp);
            if(pdetSemi > 0.0) yP += pHash(ps, uint(p) * 2u + 2u) * pdetSemi * vGeo2.x;   // flattened (partialDetune)
            if(yP < -4.0) continue;                                // above the top
            float ph1 = (float(p) + 1.0) / (float(parts) + 1.0);
            float ap = pow(max(0.0, 1.0 - ph1), max(0.0, vEnv.z)) * pow(max(0.0, 1.0 - vGeo.w), float(p));
            if(look == 2) ap = 0.5 * ap + 0.5 * (1.0 - ph1);
            if(pEnvPow > 0.0) ap *= pow(gLvl, ph1 * pEnvPow);
            a = max(a, voicesLine(px.y, yP, ap, th, voices, detPx));
        }
        a *= env;
        if(kStack > 1.0) a = 1.0 - pow(max(0.0, 1.0 - a), kStack);   // v1 overlapping paint
    } else if(kind == 1){     // Hit: geo = yTop, yBot, decay, width; geo2 = sweep, grain, softness, tone (dot: cy)
                              // env.x = dot size px
        float yT = vGeo.x, yB = vGeo.y, sf = vGeo2.z * 8.0 + 0.5, ppb = vTime.y;
        float dsz = vEnv.x, rad = max(0.6, dsz * 0.5);
        if(look == 7){        // dot: one disc
            a = disc(px, vec2(vTime.x, vGeo2.w), rad);
        } else if(look == 6){ // dotGrid
            float st = max(dsz * 3.0, 4.0);
            vec2 l = vec2(px.x - vTime.x, px.y - yT);
            vec2 g = clamp(floor(l / st + 0.5), vec2(0.0), vec2(floor(vGeo.w * ppb / st), floor((yB - yT) / st)));
            a = disc(l, g * st, rad) * exp(-g.x * st / ppb / max(0.005, vGeo.z));
        } else if(look == 4){ // speckle (approximate: sparse hashed cells, fading)
            float tt = max(t, 0.0);
            a = band(px.y, yT, yB, 0.5) * step(0.93 - 0.1 * vGeo2.y, hash(floor(vec2(tt * ppb, px.y) / max(2.0, dsz)) + vKind.z))
              * exp(-tt / max(0.005, vGeo.z)) * step(0.0, t) * step(t, vGeo.w);
        } else {
            if(t > vGeo.w) discard;
            // env.y contrast, env.z noise size px@1080, env.w band shape
            float env = hitEnv(t / max(1e-6, vGeo.w), vGeo.w, vGeo.z);
            float cx = floor(t * 64.0), r1080 = (px.y - yT) * 1080.0 / uSize.y;
            if(look == 1 || look == 10){                              // kick / taiko: v1 taikoSweep wedge
                float kp = max(0.25, (vGeo.w / max(1e-6, vGeo.z) - 1.0) * 0.5);
                float yE = yT + pow(u2(t, vGeo.w), 0.25) * vGeo2.x * (yB - yT);
                a = band(px.y, yE, yB, 0.5) * pow(clamp((px.y - yE) / max(1.0, yB - yE), 0.0, 1.0), kp * 0.5);
            } else if(look == 11 || look == 12){                      // sine / ring
                float fall = 1.0 - exp(-t / max(0.01, vGeo.z * (1.2 - vGeo2.x)));
                float yc = mix(yT, yB, fall * vGeo2.x + (1.0 - vGeo2.x) * 0.5);
                float hlf = look == 10 ? (yB - yT) * 0.5 * (0.4 + 0.6 * env) : (yB - yT) * (0.08 + 0.25 * env);
                if(look == 11) hlf = (yB - yT) * 0.06;
                float ht = max(0.5, hlf);
                if(look == 12) a = max(line(px.y, yc - hlf, 1.0, 1.0), line(px.y, yc + hlf, 1.0, 1.0));
                else a = line(px.y, yc, ht, vGeo2.z * ht + 1.0) * (1.0 - vGeo2.y * hash(vec2(cx, 9.0) + vKind.z));
                if(look == 10) a = max(a, line(px.y, yB - 0.02 * uSize.y, 2.0, 2.0));
            } else if(look == 2){ // noise band / snare: v1 spectral rows
                float m2 = (yT + yB) * 0.5, hh = (yB - yT) * 0.5;
                float n = rowNoise(r1080, 1.0 / max(1.0, vEnv.z), vEnv.y, vKind.z * 97.0);
                float g = (1.0 - vGeo2.y) + vGeo2.y * n;
                if(vGeo2.w > 0.0) g = max(g, vGeo2.w * exp(-pow((px.y - m2) / max(1.0, hh * 0.1), 2.0)));
                a = band(px.y, yT, yB, 0.5) * g;
            } else if(look == 8){ // burst: v1 klank
                a = 0.75 * band(px.y, yT, yB, vGeo2.z * 4.0 + 0.5);
            } else if(look == 3){ // gradient: v1 spectralGradient (narrowing cos profile)
                float m2 = (yT + yB) * 0.5, hh = (yB - yT) * 0.5 * (1.0 - 0.5 * u2(t, vGeo.w));
                a = band(px.y, m2 - hh, m2 + hh, 0.5) * profile((px.y - (m2 - hh)) / max(1.0, 2.0 * hh), vEnv.w);
            } else if(look == 5){ // tick / line: a short vertical stroke
                a = band(px.y, yT, yB, vGeo2.z * 4.0 + 0.5) * step(t * ppb, max(1.0, dsz * 0.5));
                env = 1.0;
            } else if(look == 9){ // lfoBand
                float hc = (yT + yB) * 0.5 - (yB - yT) * 0.4 * sin(6.2831853 * t / max(0.05, vGeo.w * 0.5));
                a = line(px.y, hc, max(1.0, (yB - yT) * 0.1), 2.0);
            } else {              // block: v1 gradblock (cos profile)
                a = band(px.y, yT, yB, sf) * profile((px.y - yT) / max(1.0, yB - yT), vEnv.w);
                if(vGeo2.y > 0.0) a *= (1.0 - vGeo2.y) + vGeo2.y * rowNoise(r1080, 1.0 / max(1.0, vEnv.z), vEnv.y, vKind.z * 97.0);
            }
            a *= env;
        }
    } else if(kind == 2){     // Mass: geo = yFrom, yTo, strands, lineWidth; geo2 = spread px, wobble, fade, softness; kind.w = glide shape
        float x1p = vTime.x + len * vTime.y, X = floor(px.x);
        float cov = clamp(min(X + 1.0, x1p + 0.5) - max(X, vTime.x + 0.5), 0.0, 1.0);   // v1 strokes end on pixel centres
        if(cov <= 0.0) discard;
        int gsh = vKind.w >= 3.75 ? 4 + int(clamp(vKind.w - 4.0, 0.0, 0.999) * 1000.0 + 0.5) : int(vKind.w + 0.5);   // snap: 4 + tension/1000 code
        float yc = mix(vGeo.x, vGeo.y, gshape(gsh, u)) - dy;
        float du = 1.0 / max(1.0, len * vTime.y);
        float sdy = (vGeo.y - vGeo.x) * (gshape(gsh, min(1.0, u + du)) - gshape(gsh, max(0.0, u - du)));
        float slopeW = sqrt(1.0 + 0.25 * sdy * sdy);           // a sloped stroke covers more px per column
        int n = int(vGeo.z + 0.5);
        float f = vGeo2.z > 0.0 ? min(clamp(u / vGeo2.z, 0.0, 1.0), clamp((1.0 - u) / vGeo2.z, 0.0, 1.0)) : 1.0;
        for(int s = 0; s < 64; s++){
            if(s >= n) break;
            float so = n == 1 ? 0.0 : float(s) / float(n - 1) - 0.5;
            float w = vGeo2.y * 0.05 * uSize.y * (vnoise(vec2(t * 2.0 + float(s) * 13.1, float(s)), vKind.z * 40.0) - 0.5) * 2.0;
            a = max(a, line(px.y, yc + so * vGeo2.x + w, vGeo.w * 0.5 * slopeW, vGeo2.w * vGeo.w + 0.5));   // v1: solid strokes
        }
        a = min(a, 1.0) * f * cov;
    } else if(kind == 3){     // Sweep: geo = yBotFrom, yTopFrom, yBotTo, yTopTo; geo2 = levelFrom, levelTo, softness, density (texture if > 0)
                              // env = fade, contrast, grainSize px, cell (0 cloud 1 nebula 2 grain 3 stripes); kind.w = curve
        if(t > len) discard;
        float uc = u;                                            // v1: heights linear, level^curve
        float yB = mix(vGeo.x, vGeo.z, uc) - dy, yT = mix(vGeo.y, vGeo.w, uc) - dy;
        float f = vEnv.x > 0.0 ? min(clamp(u / vEnv.x, 0.0, 1.0), clamp((1.0 - u) / vEnv.x, 0.0, 1.0)) : 1.0;
        a = band(px.y, yT, yB, vGeo2.z * 0.25 * abs(yB - yT) + 0.5) * pow(max(0.0, mix(vGeo2.x, vGeo2.y, uc)), max(0.1, vKind.w)) * f;
        if(vGeo2.w <= 0.0) a *= profile((yB - px.y) / max(1.0, yB - yT), vKind.y);
        if(vGeo2.w > 0.0){
            float gs = max(0.5, vEnv.z);
            vec2 c = vec2(t * vTime.y, px.y) / gs;
            int cell = int(vEnv.w + 0.5);
            float n;
            if(cell == 3) n = 0.5 + 0.5 * sin(c.y * 1.7);
            else if(cell == 4){ a *= rowNoise((px.y - yT) * 1080.0 / uSize.y, 1.0 / max(1.0, gs * 3.0), 4.0 * vEnv.y, vKind.z * 97.0); n = -1.0; }
            else if(cell == 2) n = hash(floor(c) + vKind.z);
            else { n = vnoise(c * 0.3, vKind.z * 17.0); if(cell == 1) n = n * n * 1.5; }
            if(n >= 0.0) a *= clamp((n - (1.0 - vGeo2.w)) * 2.0 * vEnv.y + 0.5 * vGeo2.w, 0.0, 1.0);
        }
    } else if(kind == 4){     // Shape: geo = cy, halfH, lenPx, rotation rad; geo2 = filled, stroke, softness, shape id
                              // ids: 0 line 1 rect 2 ellipse 3 triangle 4 curve 5 letter (X)
        float hw = vGeo.z * 0.5;
        vec2 d0 = vec2(px.x - (vTime.x + hw), px.y - vGeo.x);
        float cr = cos(vGeo.w), sr = sin(vGeo.w);
        vec2 q = vec2(d0.x * cr + d0.y * sr, -d0.x * sr + d0.y * cr);
        int sh = int(vGeo2.w + 0.5); float hh = vGeo.y, d;
        if(sh == 2) d = (length(q / vec2(max(hw, 1e-3), max(hh, 1e-3))) - 1.0) * min(hw, hh);
        else if(sh == 0) d = segD(q, vec2(-hw, 0.0), vec2(hw, 0.0));
        else if(sh == 3){
            float m = min(segD(q, vec2(-hw, hh), vec2(hw, hh)), min(segD(q, vec2(hw, hh), vec2(0.0, -hh)), segD(q, vec2(0.0, -hh), vec2(-hw, hh))));
            float ty = (q.y + hh) / max(1e-3, 2.0 * hh);
            bool inside = q.y <= hh && q.y >= -hh && abs(q.x) <= hw * ty;
            d = inside ? -m : m;
        } else if(sh == 4){
            float tt = clamp((q.x + hw) / max(1e-3, 2.0 * hw), 0.0, 1.0);
            d = (q.x < -hw || q.x > hw) ? 1e9 : abs(q.y + hh * sin(3.14159265 * tt) - hh * 0.5) * 0.8;
        } else if(sh == 5) d = min(segD(q, vec2(-hw, -hh), vec2(hw, hh)), segD(q, vec2(-hw, hh), vec2(hw, -hh)));
        else { vec2 e = abs(q) - vec2(hw, hh); d = length(max(e, 0.0)) + min(max(e.x, e.y), 0.0); }
        float sf = max(0.7, vGeo2.z * 8.0);
        a = vGeo2.x > 0.5 ? clamp(0.5 - d / sf, 0.0, 1.0) : clamp((vGeo2.y * 0.5 + 0.5 - abs(d)) / max(0.7, sf * 0.5), 0.0, 1.0);
    }
    a = clamp(a * lvl, 0.0, 1.0);
    if(a <= 0.0) discard;
    if(uMaskOnly == 1){ float m = min(1.0, a / 0.02); outColor = vec4(m); }   // CPU threshold
    else outColor = vec4(vCol.rgb * a, a) * vPlace.w;                          // premultiplied, fog
}
)";

// full-screen passes share this vertex shader (position = corner 0..1)
static const char* fullVert = R"(#version 150
in vec4 position;
void main(){ gl_Position = vec4(position.xy * 2.0 - 1.0, 0.0, 1.0); }
)";

// layer-bus effect over the slot's columns (x px, y down)
static const char* effectFrag = R"(#version 150
uniform sampler2D uSrc;
uniform vec2 uSize;
uniform float uX0, uX1;           // slot in px
uniform int uMode;                // 0 reverse 1 repeat 2 stutter 3 freeze 4 blank 5 echo 6 shuffle 7 tears 8 saturation
                                  // 10 band 11 notch 12 fadeIn 13 fadeOut 14 gate 15 mask 16 vignette
uniform vec4 uP;
uniform float uAmount;
uniform int uShape;               // steady rise fall swell lfo
uniform float uRateFrac;
uniform float uSeed;
uniform float uPerm[32];
uniform sampler2D uLat;           // noise: CPU value lattice (exact) when uLatN.x > 0
uniform ivec2 uLatN;
uniform float uCell;
// batched pass (uN > 0): uN slots applied in order on the same source, each as the single pass
// above would (the caller puts slots that read other columns in a batch only when they overlap no
// other slot; gains may overlap, echoes may overlap echoes). One pass per batch instead of one per event.
uniform int uN;
uniform vec4 uS[64];              // x0, x1, mode, shape
uniform vec4 uSP[64];             // p
uniform vec4 uSQ[64];             // amount, rateFrac, seed, 0
out vec4 outColor;
float hash(vec2 p){ return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
vec4 col(float x, float y){ if(x < 0.0 || x >= uSize.x) return vec4(0.0); return texelFetch(uSrc, ivec2(int(floor(x)), int(y)), 0); }
float shapeAmt(float u, int shape, float rateFrac){
    if(shape == 1) return u;
    if(shape == 2) return 1.0 - u;
    if(shape == 3) return sin(3.14159265 * u);
    if(shape == 4) return 0.5 + 0.5 * sin(6.2831853 * u / max(0.01, rateFrac));
    return 1.0;
}
// one slot over the pixel's current value c (reads other columns from the source)
vec4 slot(vec4 c, vec2 fc, float X, float x0, float x1, int mode, vec4 P, float amount, int shape, float rateFrac, float seed){
    float a0 = floor(x0), a1 = ceil(x1) - 1.0;
    if(mode == 5){                // echo: copies of the slot to the right (also outside the slot)
        int n = int(P.x + 0.5);
        for(int i = 1; i <= 16; i++){
            if(i > n) break;
            float x = X - floor(float(i) * P.z + 0.5);
            if(x < max(0.0, a0) || x > a1) continue;
            c = max(c, col(x + 0.5, fc.y) * pow(P.y, float(i)));
        }
        return c;
    }
    if(X < a0 || X > a1) return c;
    float w = max(1e-3, x1 - x0), lx = fc.x - x0, u = clamp(lx / w, 0.0, 1.0);
    float h = 1.0 - fc.y / uSize.y;   // height 0 bottom
    if(mode == 0) c = col(x0 + x1 - fc.x, fc.y);
    else if(mode == 1 || mode == 2) c = col(x0 + mod(lx, w / max(1.0, P.x)), fc.y);
    else if(mode == 3) c = col(x0, fc.y);
    else if(mode == 4) c = vec4(0.0);
    else if(mode == 6){
        int n = int(P.x + 0.5); float un = u * float(n); int i = min(n - 1, int(un));
        c = col(x0 + (uPerm[i] + (un - float(i))) * w / float(n), fc.y);
    } else if(mode == 19){        // tile: mean of n source columns
        int n = int(P.x + 0.5); float sx = x0 + mod(lx * float(n), w) - 0.5 * float(n);
        vec4 acc = vec4(0.0);
        for(int k = 0; k < 64; k++){ if(k >= n) break; acc += col(floor(sx + float(k) + 0.5) + 0.5, fc.y); }
        c = acc / float(max(n, 1));
    } else if(mode == 7){
        float off = (hash(vec2(floor(fc.y / 16.0), seed)) - 0.5) * 2.0 * P.x * w * 0.25;
        c = col(fc.x + off, fc.y);
    } else {
        float amt = amount * shapeAmt(u, shape, rateFrac);
        if(mode == 8){
            float dr = 1.0 + P.x * amt;
            c.rgb = min(vec3(c.a), tanh(c.rgb * dr) / tanh(dr) * max(1.0, c.a));
        } else {
            float f = max(1e-4, P.z * 0.5);
            float inB = min(smoothstep(0.0, 1.0, (h - P.x) / f + 0.5), smoothstep(0.0, 1.0, (P.y - h) / f + 0.5));
            float g = 1.0;
            if(mode == 10) g = 1.0 - amt * (1.0 - inB);
            else if(mode == 11) g = 1.0 - amt * inB;
            else if(mode == 12) g = 1.0 - amt * (1.0 - u);
            else if(mode == 13) g = 1.0 - amt * u;
            else if(mode == 14) g = mod(lx, P.x) / P.x < P.y ? 1.0 : 1.0 - amt;
            else if(mode == 15) g = 1.0 - amt * (P.w > 0.5 ? inB : 1.0 - inB);
            else if(mode == 16) g = 1.0 - amt * (1.0 - smoothstep(0.0, 1.0, min(u, 1.0 - u) / max(1e-3, P.x)));
            else if(mode == 17) g = 1.0 - amt * (1.0 - (P.x <= 0.0 ? 1.0 : pow(0.5 - 0.5 * cos(6.2831853 * u), P.x)));
            else if(mode == 18){      // v1 noise mask: (noise x gain)^contrast, inverted / thresholded
                vec2 q = vec2(lx, floor(fc.y)) / uCell, i = floor(q), f = q - i; f = f * f * (3.0 - 2.0 * f);
                float a, b, cc, d;
                if(uLatN.x > 0){
                    ivec2 j = ivec2(i); ivec2 m = uLatN - 1;
                    a = texelFetch(uLat, clamp(j, ivec2(0), m), 0).r; b = texelFetch(uLat, clamp(j + ivec2(1, 0), ivec2(0), m), 0).r;
                    cc = texelFetch(uLat, clamp(j + ivec2(0, 1), ivec2(0), m), 0).r; d = texelFetch(uLat, clamp(j + ivec2(1, 1), ivec2(0), m), 0).r;
                } else { a = hash(i + seed); b = hash(i + vec2(1, 0) + seed); cc = hash(i + vec2(0, 1) + seed); d = hash(i + vec2(1, 1) + seed); }
                float nv = pow(clamp(mix(mix(a, b, f.x), mix(cc, d, f.x), f.y) * P.x, 0.0, 1.0), P.y);
                if(P.z > 0.5) nv = 1.0 - nv;
                if(P.w > 0.0) nv = nv >= P.w ? 1.0 : 0.0;
                g = 1.0 - amt + amt * nv;
            }
            c *= g;
        }
    }
    return c;
}
void main(){
    vec2 fc = gl_FragCoord.xy;
    float X = floor(fc.x);
    vec4 c = col(fc.x, fc.y);
    if(uN > 0){
        for(int i = 0; i < 64; i++){
            if(i >= uN) break;
            c = slot(c, fc, X, uS[i].x, uS[i].y, int(uS[i].z + 0.5), uSP[i], uSQ[i].x, int(uS[i].w + 0.5), uSQ[i].y, uSQ[i].z);
        }
    } else c = slot(c, fc, X, uX0, uX1, uMode, uP, uAmount, uShape, uRateFrac, uSeed);
    outColor = c;
}
)";

// mask passes (half resolution, single channel): 0 dilate H, 1 dilate V, 2 trail (decays to the
// right), 3 blur H, 4 blur V. Loop bounds come from the radius (no fixed 65-tap loop).
static const char* maskFrag = R"(#version 150
uniform sampler2D uSrc;
uniform vec2 uSize;
uniform int uPass;
uniform int uRadius;
uniform float uReleasePx;
out vec4 outColor;
float m(ivec2 p){ if(p.x < 0 || p.y < 0 || p.x >= int(uSize.x) || p.y >= int(uSize.y)) return 0.0; return texelFetch(uSrc, p, 0).r; }
void main(){
    ivec2 p = ivec2(gl_FragCoord.xy);
    float v = 0.0;
    if(uPass == 0 || uPass == 1){
        ivec2 d = uPass == 0 ? ivec2(1, 0) : ivec2(0, 1);
        for(int i = -uRadius; i <= uRadius; i++) v = max(v, m(p + d * i));
    } else if(uPass == 2){
        v = m(p);
        if(uReleasePx > 0.0){
            int n = int(min(32.0, ceil(uReleasePx)));            // up to 32 taps (sampled for long releases)
            float stepPx = uReleasePx / float(n);
            for(int i = 1; i <= n; i++){
                float dx = float(i) * stepPx;
                v = max(v, m(p - ivec2(int(dx + 0.5), 0)) * (1.0 - dx / uReleasePx));
            }
        }
    } else {
        ivec2 d = uPass == 3 ? ivec2(1, 0) : ivec2(0, 1);
        for(int i = -uRadius; i <= uRadius; i++) v += m(p + d * i);
        v /= float(2 * uRadius + 1);
    }
    outColor = vec4(v);
}
)";

// apply a cover mask in place through blending: the output is (0, 0, 0, k); eraser blends with
// (ZERO, ONE_MINUS_SRC_ALPHA) -> c * (1 - k); black with (ONE, ONE_MINUS_SRC_ALPHA) -> rgb * (1 - k),
// a + (1 - a) k. The half-resolution mask is read with linear filtering.
static const char* coverFrag = R"(#version 150
uniform sampler2D uMask;
uniform vec2 uSize;
uniform float uAmount;
out vec4 outColor;
void main(){
    float k = clamp(uAmount * texture(uMask, gl_FragCoord.xy / uSize).r, 0.0, 1.0);
    outColor = vec4(0.0, 0.0, 0.0, k);
}
)";

// multiply by a (low resolution) blob gain texture, row 0 = image top
static const char* blobFrag = R"(#version 150
uniform sampler2D uSrc;
uniform sampler2D uMask;
uniform vec2 uSize;
out vec4 outColor;
void main(){
    vec4 c = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0);
    outColor = c * clamp(texture(uMask, gl_FragCoord.xy / uSize).r, 0.0, 1.0);
}
)";

// composite one layer (premultiplied) with gain; blending is set by the caller
static const char* compFrag = R"(#version 150
uniform sampler2D uSrc;
uniform float uGain;
out vec4 outColor;
void main(){
    vec4 c = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0);
    outColor = vec4(c.rgb * uGain, min(c.a, 1.0));
}
)";

// composite up to 4 layers in one pass (premultiplied): per layer lighten (max), add or normal,
// in order, as the blended passes did
static const char* compAllFrag = R"(#version 150
uniform sampler2D uL0; uniform sampler2D uL1; uniform sampler2D uL2; uniform sampler2D uL3;
uniform int uN;
uniform ivec4 uMode;              // 0 lighten 1 add 2 normal 3 multiply 4 darken
uniform ivec4 uAfb;               // alpha from brightness per layer (5.18)
uniform vec4 uGain;
out vec4 outColor;
vec4 blendL(vec4 c, vec4 s, int mode, float g, int afb){
    s = vec4(s.rgb * g, min(s.a, 1.0));
    if(afb == 1) s.a = min(1.0, max(s.r, max(s.g, s.b)));
    if(mode == 1) return min(c + s, vec4(1.0));
    if(mode == 0) return max(c, s);
    vec3 rest = s.rgb * (1.0 - c.a) + c.rgb * (1.0 - s.a);
    float a = s.a + c.a * (1.0 - s.a);
    if(mode == 3) return vec4(s.rgb * c.rgb + rest, a);
    if(mode == 4) return vec4(min(s.rgb * c.a, c.rgb * s.a) + rest, a);
    return vec4(s.rgb + c.rgb * (1.0 - s.a), a);
}
void main(){
    ivec2 p = ivec2(gl_FragCoord.xy);
    vec4 c = vec4(0.0);
    c = blendL(c, texelFetch(uL0, p, 0), uMode.x, uGain.x, uAfb.x);
    if(uN > 1) c = blendL(c, texelFetch(uL1, p, 0), uMode.y, uGain.y, uAfb.y);
    if(uN > 2) c = blendL(c, texelFetch(uL2, p, 0), uMode.z, uGain.z, uAfb.z);
    if(uN > 3) c = blendL(c, texelFetch(uL3, p, 0), uMode.w, uGain.w, uAfb.w);
    outColor = c;
}
)";

// display copy over the background colour (the score / column targets stay without it)
// uImg: the image shown (the post's output, or the score itself); uSrc: the score, for its alpha,
// stretched left of uStretchX as the post stretches the image (< 0: no stretch)
static const char* bgFrag = R"(#version 150
uniform sampler2D uSrc;
uniform sampler2D uImg;
uniform vec3 uBg;
uniform vec2 uSize;
uniform float uStretchX, uOpacity;
out vec4 outColor;
void main(){
    vec3 c = texelFetch(uImg, ivec2(gl_FragCoord.xy), 0).rgb;
    float a;
    if(uStretchX >= 0.0 && gl_FragCoord.x <= uStretchX){
        vec2 q = vec2(uStretchX, gl_FragCoord.y) - 0.5; ivec2 i = ivec2(floor(q)); vec2 f = q - floor(q); ivec2 m = ivec2(uSize) - 1;
        float a0 = mix(texelFetch(uSrc, clamp(i, ivec2(0), m), 0).a, texelFetch(uSrc, clamp(i + ivec2(1, 0), ivec2(0), m), 0).a, f.x);
        float a1 = mix(texelFetch(uSrc, clamp(i + ivec2(0, 1), ivec2(0), m), 0).a, texelFetch(uSrc, clamp(i + ivec2(1, 1), ivec2(0), m), 0).a, f.x);
        a = mix(a0, a1, f.y);
    } else a = texelFetch(uSrc, ivec2(gl_FragCoord.xy), 0).a;
    a = clamp(a * uOpacity, 0.0, 1.0);
    outColor = vec4(min(c + uBg * (1.0 - a), vec3(1.0)), 1.0);
}
)";

} // namespace glsl

// ---------------------------------------------------------------- per-instance data
struct Inst { float v[12][4]; };   // iRect iTime iCol iKind iGeo iGeo2 iEnv iFine0 iFine1 iLvl0 iLvl1 iPlace
static const char* kInstNames[12] = {"iRect", "iTime", "iCol", "iKind", "iGeo", "iGeo2", "iEnv", "iFine0", "iFine1", "iLvl0", "iLvl1", "iPlace"};

// pack an event into instance attributes (resolves animated values and samples drawn ones)
inline bool packEvent(const draw::Renderer& ref, int H, const Event& e, Inst& in){
    const Frame& f = ref.frame;
    LayerView v = ref.viewOf(e.layer);
    double depth = draw::eventDepth(e, f);
    draw::Ctx k{e, f, v}; k.depth = depth;
    double ppb = v.pxPerBeat * depth;
    double x0 = v.xOf(e.start, depth), tail = draw::tailBeats(e);
    double x1 = v.xOf(e.start + e.length + tail, depth);
    auto col = f.paper.colourOf(e.colourRole);
    double pxPerSemi = H / std::max(1e-6, f.space.highSemi - f.space.lowSemi);
    std::memset(&in, 0, sizeof(in));
    double rx0 = x0 - 2, rx1 = x1 + 2;                 // widened per kind below
    double ry0 = 0, ry1 = H, shR = 0;                  // vertical extent, tightened per kind below
    in.v[1][0] = (float)x0; in.v[1][1] = (float)ppb; in.v[1][2] = (float)e.length; in.v[1][3] = (float)tail;
    in.v[2][0] = col[0]; in.v[2][1] = col[1]; in.v[2][2] = col[2]; in.v[2][3] = (float)k.levelBase();
    in.v[3][0] = (float)(int)e.kind; in.v[3][2] = (float)draw::hash01(e.seed, 1);
    // drawn samples: pitch offset (px, up) and level gain
    double fineS = k.base("pitchFine", 0);
    for(int s = 0; s < 8; s++){
        double u = s / 7.0;
        double fine = k.at("pitchFine", fineS, u) - fineS;
        in.v[7 + s / 4][s % 4] = (float)(fine / 100.0 * pxPerSemi + k.at("height", 0, u) * H);
        double lb = in.v[2][3];
        in.v[9 + s / 4][s % 4] = lb > 1e-9 ? (float)(k.levelAt(lb, u) / lb) : 1.f;
    }
    auto Y = [&](double h){ return (float)v.yOf(h); };
    double sz = k.base("size", 1);
    switch(e.kind){
        case EventKind::Note: {
            in.v[3][1] = e.look == "ribbon" ? 1.f : (e.look == "additive" ? 2.f : 0.f);
            // sub level (hundredths) + 101 x overlap gain above 1 in quarters (alphaStack 1..8 -> 0..28)
            in.v[3][3] = (float)(std::lround(draw::clamp01(k.base("sub", 0)) * 100) + 101 * std::lround((std::clamp(k.base("alphaStack", 1), 1.0, 8.0) - 1) * 4));
            in.v[1][2] = (float)(e.length * k.base("noteLength", 1));
            double semis = (e.p.count("glideToSemis") ? e.param("glideToSemis", 0) : f.scale.semitonesOf(e.degree))   /* key-change bend target */ + fineS / 100.0 + draw::retuneOffset(e, f.nowBeat);
            double h = e.pitched ? f.space.heightOf(semis) : e.height;
            int partials = (int)std::lround(k.base("partials", 4) * draw::clamp01(k.base("partialCount", 1)));
            in.v[4][0] = Y(h + k.base("height", 0)); in.v[4][1] = (float)(k.base("thickness", 2) * sz); in.v[4][2] = (float)std::clamp(partials, 0, 160); in.v[4][3] = (float)k.base("partialDecay", 0);
            // geo2.z packs the detune voices with the partial chance (%) and the partial detune (cents): voices + 4 * (qc + 101 * qd)
            int qc = (int)std::lround(draw::clamp01(k.base("partialChance", 1)) * 100), qd = (int)std::lround(std::clamp(k.base("partialDetune", 0), 0.0, 200.0));
            int qk = (int)std::lround(std::clamp(k.base("partialEnvPower", 0), 0.0, 64.0) * 2);   // 0..128, in halves
            in.v[5][0] = (float)pxPerSemi; in.v[5][1] = (float)(k.base("detune", 0) / 100.0); in.v[5][2] = (float)(std::clamp((int)std::lround(k.base("detuneVoices", 1)), 1, 3) + 4 * (qc + 101 * (qd + 201 * qk))); in.v[5][3] = (float)k.base("partialSpread", 1);
            in.v[6][0] = e.param("tied", 0) > 0.5 ? 0.f : (float)k.base("attack", 0); in.v[6][1] = (float)k.base("release", 0); in.v[6][2] = (float)k.base("partialCut", 1);
            in.v[6][3] = (float)(std::clamp((int)std::lround(k.base("envShape", 0)), 0, 3) * 20 + std::clamp(k.base("envPower", 1), 0.0, 16.0));
            break; }
        case EventKind::Hit: {
            static const std::map<std::string, int> ids{{"kick", 1}, {"taiko", 10}, {"sine", 11}, {"ring", 12}, {"noiseBand", 2}, {"snare", 2}, {"burst", 8},
                                                        {"gradient", 3}, {"speckle", 4}, {"tick", 5}, {"line", 5}, {"dotGrid", 6}, {"dot", 7}, {"lfoBand", 9}};
            auto it = ids.find(e.look); int id = it == ids.end() ? 0 : it->second;
            in.v[3][1] = (float)id;
            double lo = e.param("heightLow", e.param("bandLow", std::min(e.height, e.height2)));
            double hi = e.param("heightHigh", e.param("bandHigh", std::max(e.height, e.height2)));
            if(hi - lo < 0.01){ lo -= 0.05; hi += 0.05; }
            double hOff = k.base("height", 0); lo += hOff; hi += hOff;
            double mid = (lo + hi) / 2; lo = mid + (lo - mid) * sz; hi = mid + (hi - mid) * sz;
            double width = k.base("width", e.length), dot = k.base("dotSize", 3) * sz;
            bool noisy = e.look == "noiseBand" || e.look == "snare";
            in.v[4][0] = Y(hi); in.v[4][1] = Y(lo); in.v[4][2] = (float)std::max(0.005, k.base("decay", 0.3)); in.v[4][3] = (float)width;
            in.v[5][0] = (float)k.base("sweep", 0.5); in.v[5][1] = (float)k.base("grain", noisy ? 1 : 0); in.v[5][2] = (float)k.base("softness", 0.2); in.v[5][3] = (float)k.base("tone", 0);
            in.v[6][0] = (float)dot; in.v[6][1] = (float)k.base("contrast", 4); in.v[6][2] = (float)k.base("grainSize", 20); in.v[6][3] = (float)k.base("bandShape", 0);
            if(id == 7){   // dot: its height as the CPU picks it
                Rng r(e.seed ^ 0xD07);
                in.v[5][3] = Y(draw::lerp(mid, lo + r.uniform() * (hi - lo), k.base("scatter", 0.3)));
            }
            double rad = std::max(0.6, dot * 0.5) + 2;
            rx0 = x0 - rad - 2; rx1 = std::max(x1, x0 + width * ppb) + rad + 2;
            break; }
        case EventKind::Mass:
            in.v[4][0] = Y(e.param("heightFrom", e.height) + k.base("height", 0)); in.v[4][1] = Y(e.param("heightTo", e.height2) + k.base("height", 0));
            in.v[4][2] = (float)std::clamp((int)std::lround(k.base("strands", 8)), 1, 64); in.v[4][3] = (float)(k.base("lineWidth", 1.5) * sz * e.param("widthScale", 1));
            in.v[3][3] = (float)std::clamp((int)e.param("glideShape", 1), 0, 4);
            if(in.v[3][3] == 4.f) in.v[3][3] = 4.f + (float)std::clamp(e.param("glideTension", 0.95), 0.0, 0.999);   // snap: tension in the fraction
            in.v[5][0] = (float)(k.base("spread", 0.1) * H); in.v[5][1] = (float)k.base("wobble", 0.1); in.v[5][2] = (float)k.base("fade", 0); in.v[5][3] = (float)k.base("softness", 0.1);
            break;
        case EventKind::Sweep: {
            bool tex = e.p.count("bandLow") || e.p.count("density") || e.look == "noise" || e.look == "cloud" || e.look == "grain" || e.look == "nebula" || e.look == "stripes";
            double bF, bT, tF, tT;
            if(tex){ bF = bT = e.param("bandLow", std::min(e.height, e.height2)); tF = tT = e.param("bandHigh", std::max(e.height, e.height2)); }
            else { bF = e.param("bottomFrom", e.height); bT = e.param("bottomTo", e.height); tF = e.param("topFrom", e.height2); tT = e.param("topTo", e.height2); }
            double hOff = k.base("height", 0);
            in.v[4][0] = Y(bF + hOff); in.v[4][1] = Y(tF + hOff); in.v[4][2] = Y(bT + hOff); in.v[4][3] = Y(tT + hOff);
            in.v[5][0] = (float)e.param("levelFrom", 1); in.v[5][1] = (float)e.param("levelTo", 1); in.v[5][2] = (float)k.base("softness", 0.4); in.v[5][3] = tex ? (float)std::max(1e-3, k.base("density", 0.5)) : 0.f;
            in.v[3][3] = (float)std::max(0.1, e.param("curve", 1));
            std::string cell = e.look.empty() ? "cloud" : e.look;
            in.v[6][0] = (float)k.base("fade", tex ? 0.3 : 0); in.v[6][1] = (float)k.base("contrast", 1); in.v[6][2] = (float)k.base("grainSize", 6);
            in.v[6][3] = cell == "nebula" ? 1.f : (cell == "noise" ? 4.f : cell == "grain" ? 2.f : (cell == "stripes" ? 3.f : 0.f));
            in.v[3][1] = (float)k.base("bandShape", 0);
            break; }
        case EventKind::Shape: {
            static const std::map<std::string, int> ids{{"line", 0}, {"rect", 1}, {"ellipse", 2}, {"triangle", 3}, {"curve", 4}, {"letter", 5}};
            std::string sh = e.look.empty() ? "rect" : e.look;
            auto it = ids.find(sh);
            double hc = k.base("heightCenter", e.pitched ? f.space.heightOf(f.scale.semitonesOf(e.degree)) : e.height) + k.base("height", 0);
            double lenPx = k.base("shapeLength", e.length) * ppb, hh = k.base("heightSize", 0.2) * sz * H / 2, sw = k.base("strokeWidth", 2);
            bool filled = k.base("filled", 1) > 0.5 && sh != "line" && sh != "curve" && sh != "letter";
            in.v[4][0] = Y(hc); in.v[4][1] = (float)hh; in.v[4][2] = (float)lenPx; in.v[4][3] = (float)(k.base("rotation", 0) * M_PI / 180);
            in.v[5][0] = filled ? 1.f : 0.f; in.v[5][1] = (float)sw; in.v[5][2] = (float)k.base("softness", 0.1); in.v[5][3] = it == ids.end() ? 1.f : (float)it->second;
            double R = std::hypot(lenPx / 2, hh) + sw + 4 + k.base("softness", 0.1) * 8, cx = x0 + lenPx / 2;
            rx0 = cx - R; rx1 = cx + R;
            break; }
        default: return false;
    }
    // tight vertical extent (conservative): what the fragment shader can draw, so the quad stays small
    {
        double dmin = 0, dmax = 0;                         // drawn pitch offsets (the shader subtracts them)
        for(int s = 0; s < 8; s++){ double d = in.v[7 + s / 4][s % 4]; dmin = std::min(dmin, d); dmax = std::max(dmax, d); }
        auto span = [&](double a, double b, double m, bool fine){
            ry0 = std::min(a, b) - m - (fine ? dmax : 0); ry1 = std::max(a, b) + m - (fine ? dmin : 0);
        };
        const float* g = in.v[4]; const float* g2 = in.v[5];
        switch(e.kind){
            case EventKind::Note: {
                double K = g2[0] * 12.0, sp = g2[3], parts = g[2], th = g[1] * 0.5 * (in.v[3][1] == 1.f ? 1.4 : 1.0);
                double m = std::max(th, 0.5) + std::max(0.6, 0.2 * th + 0.5) + std::abs(g2[1] * g2[0]) + 2;
                double arg = 1 + parts * sp;
                if(arg <= 1e-9){ ry0 = 0; ry1 = H; break; }          // degenerate spread: keep full height
                double yTop = g[0] - K * std::log2(arg), yBot = g[0] + (in.v[3][3] > 0 ? K : 0.0);
                span(std::min(yTop, g[0] + 0.0), std::max(yBot, yTop), m, true);
                break; }
            case EventKind::Hit: {
                int id = (int)in.v[3][1];
                double m = g2[2] * 8 + 0.5 + std::max(0.6, in.v[6][0] * 0.5) + 4;
                if(id >= 9 && id <= 12) m += 0.5 * std::abs(g[1] - g[0]) + 0.02 * H;   // sine / ring / taiko / lfo swing
                if(id == 7) m += std::abs(g2[3] - 0.5 * (g[0] + g[1]));                 // dot height (in band, kept safe)
                span(g[0], g[1], m, false);
                break; }
            case EventKind::Mass:
                span(g[0], g[1], std::abs(g2[0]) * 0.5 + std::abs(g2[1]) * 0.05 * H + g[3] * (0.5 * std::sqrt(1 + 0.25 * (g[1] - g[0]) * (g[1] - g[0])) + std::abs(g2[3])) + 2, true);
                break;
            case EventKind::Sweep: {
                double lo = std::min({g[0], g[1], g[2], g[3]}), hi = std::max({g[0], g[1], g[2], g[3]});
                span(lo, hi, std::abs(g2[2]) * 0.25 * (hi - lo) + 2, true);
                break; }
            case EventKind::Shape: span(g[0], g[0], shR, false); break;
            default: break;
        }
        ry0 = std::max(0.0, std::floor(ry0)); ry1 = std::min((double)H, std::ceil(ry1));
        if(ry1 <= ry0) return false;                       // entirely above or below the frame
    }
    in.v[0][0] = (float)rx0; in.v[0][1] = (float)ry0; in.v[0][2] = (float)rx1; in.v[0][3] = (float)ry1;
    // 3D placement (identity when flat): px' = px * s + (s * off + V * (1 - s))
    auto pl = ref.placeOf(e);
    in.v[11][0] = (float)pl.s; in.v[11][1] = (float)(pl.s * pl.ox + pl.vx * (1 - pl.s)); in.v[11][2] = (float)(pl.s * pl.oy + pl.vy * (1 - pl.s)); in.v[11][3] = (float)pl.gain;
    return true;
}


// a layer-bus effect as effectFrag uniforms: mode, uP, perm; false if the GPU has no pass for it
inline bool packEffect(const Event& e, const LayerView& v, int& mode, float p[4], float perm[32]){
    static const std::map<std::string, int> modes{{"reverse", 0}, {"repeat", 1}, {"stutter", 2}, {"freeze", 3}, {"blank", 4},
        {"echo", 5}, {"shuffle", 6}, {"tears", 7}, {"saturation", 8},
        {"band", 10}, {"notch", 11}, {"fadeIn", 12}, {"fadeOut", 13}, {"gate", 14}, {"mask", 15}, {"vignette", 16},
        {"page", 17}, {"noise", 18}, {"tile", 19}};
    auto it = modes.find(e.look); if(it == modes.end()) return false;
    mode = it->second;
    for(int i = 0; i < 4; i++) p[i] = 0;
    for(int i = 0; i < 32; i++) perm[i] = 0;
    if(mode == 1 || mode == 19) p[0] = (float)std::max(1L, std::lround(e.param("repeats", 2)));
    else if(mode == 2) p[0] = (float)std::max(2L, std::lround(e.param("stutterDiv", 8)));
    else if(mode == 5){ p[0] = (float)std::clamp((int)std::lround(e.param("echoCount", 3)), 1, 16); p[1] = (float)e.param("echoDecay", 0.5); p[2] = (float)(e.param("echoSpacing", 0.5) * v.pxPerBeat); }
    else if(mode == 6){
        int n = std::clamp((int)std::lround(e.param("shuffleSlices", 4)), 2, 32);
        std::vector<int> pm((size_t)n); for(int i = 0; i < n; i++) pm[(size_t)i] = i;
        Rng r(e.seed ^ 0x5F); for(int i = n - 1; i > 0; i--) std::swap(pm[(size_t)i], pm[(size_t)r.below(i + 1)]);
        for(int i = 0; i < n; i++) perm[i] = (float)pm[(size_t)i];
        p[0] = (float)n;
    }
    else if(mode == 7) p[0] = (float)e.param("tearAmount", 0.5);
    else if(mode == 8) p[0] = (float)e.param("drive", 1);
    else if(mode == 14){ p[0] = (float)(std::max(0.01, e.param("gateRate", 0.5)) * v.pxPerBeat); p[1] = (float)e.param("gateDuty", 0.5); }
    else if(mode == 16) p[0] = (float)e.param("vignetteSize", 0.3);
    else if(mode == 17) p[0] = (float)std::max(0.0, e.param("pagePower", 1));
    else if(mode == 18){ p[0] = (float)e.param("noiseGain", 1); p[1] = (float)e.param("noiseContrast", 1); p[2] = (float)e.param("maskInvert", 0); p[3] = (float)e.param("noiseThreshold", 0); }
    else { p[0] = (float)e.param("bandLow", 0.2); p[1] = (float)e.param("bandHigh", 0.8); p[2] = (float)e.param("feather", 0.1); p[3] = (float)e.param("maskInvert", 0); }
    return true;
}

// columns [c0, c1) an effect pass reads or writes on a W-wide target (effectFrag passes every other
// pixel through, so a pass scissored to them over a copy of them equals the full-screen pass);
// false when the pass draws nothing
inline bool fxColumns(const Event& e, const LayerView& v, float x0, float x1, int W, int& c0, int& c1){
    int mode = 0; float pp[4], perm[32];
    if(!packEffect(e, v, mode, pp, perm)) return false;
    if(x1 < 0 || x0 >= W || x1 - x0 < 1) return false;
    double lo = std::floor(x0), hi = std::ceil(x1);
    if(mode == 5) hi = W;                                                        // echo: copies to the right
    if(mode == 7){ double ext = std::fabs(pp[0]) * (x1 - x0) * 0.25 + 2; lo -= ext; hi += ext; }   // tears
    if(mode == 19){ double ext = std::fabs(pp[0]) + 2; lo -= ext; hi += ext; }   // tile
    c0 = std::clamp((int)lo - 2, 0, W); c1 = std::clamp((int)hi + 2, 0, W);
    return c1 > c0;
}
// noise effect: cell size (px) and the CPU value lattice over the slot (as noise2), so the GPU
// matches the CPU exactly; empty when too large (the shader then hashes)
inline float noiseLattice(const Event& e, double slotPx, int H, std::vector<float>& lat, int& nx, int& ny){
    double cell = std::max(1.0, e.param("noiseScale", 0.2) * H);
    nx = (int)std::ceil(slotPx / cell) + 2; ny = (int)std::ceil(H / cell) + 2;
    if((size_t)nx * ny > 262144){ lat.clear(); nx = ny = 0; return (float)cell; }
    lat.resize((size_t)nx * ny);
    for(int y = 0; y < ny; y++) for(int x = 0; x < nx; x++) lat[(size_t)y * nx + x] = (float)draw::hash01(e.seed, x, y);
    return (float)cell;
}

// shown image over a background colour (premultiplied canvas: rgb + bg * (1 - a)), 8-bit RGB.
// Display only: the sonification reads the score before this (background never sounds).
inline std::vector<uint8_t> rgb8OverBackground(const draw::Canvas& s, float br, float bgc, float bb){
    std::vector<uint8_t> o((size_t)s.w * s.h * 3);
    const float bg[3] = {br, bgc, bb};
    for(size_t i = 0; i < (size_t)s.w * s.h; i++){
        float k = 1.f - std::clamp(s.px[i * 4 + 3], 0.f, 1.f);
        for(int c = 0; c < 3; c++) o[i * 3 + c] = (uint8_t)std::lround(std::clamp(s.px[i * 4 + c] + bg[c] * k, 0.f, 1.f) * 255);
    }
    return o;
}

} // namespace drawgl
} // namespace forms2

#ifndef FORMS2_HEADLESS

#include "ofMain.h"

namespace forms2 {
namespace drawgl {

class GLRenderer {
public:
    GLRenderer() = default;
    GLRenderer(const GLRenderer&) = delete;
    GLRenderer& operator=(const GLRenderer&) = delete;
    ~GLRenderer(){ releaseGL(); }

    // (re)allocates the FBOs at w x h; shaders and buffers are made once
    void setup(int w, int h){
        W = std::max(1, w); H = std::max(1, h);
        ofFbo::Settings s; s.width = W; s.height = H; s.internalformat = GL_RGBA16F; s.textureTarget = GL_TEXTURE_2D;
        s.minFilter = GL_NEAREST; s.maxFilter = GL_NEAREST; s.useDepth = false; s.useStencil = false; s.numSamples = 0;
        for(auto& f : pool) f.allocate(s);
        MW = std::max(1, (W + 1) / 2); MH = std::max(1, (H + 1) / 2);
        ofFbo::Settings ms = s; ms.width = MW; ms.height = MH; ms.internalformat = GL_R16F; ms.minFilter = GL_LINEAR; ms.maxFilter = GL_LINEAR;
        ms.wrapModeHorizontal = GL_CLAMP_TO_EDGE; ms.wrapModeVertical = GL_CLAMP_TO_EDGE;
        maskA.allocate(ms); maskB.allocate(ms); out.allocate(s); disp.allocate(s); grpA.allocate(s); grpB.allocate(s); score.allocate(s); for(auto& f : scoreLayer) f.allocate(s);
        for(int i = 0; i < 4; i++) lay[i] = i;
        scr = 4;
        if(!glReady) setupGL();
        // column PBOs sized for this height
        for(int i = 0; i < 2; i++){
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[i]);
            glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)H * 4 * sizeof(float), nullptr, GL_STREAM_READ);
            pboValid[i] = false;
            for(int L = 0; L < 4; L++){
                glBindBuffer(GL_PIXEL_PACK_BUFFER, lpbo[L][i]);
                glBufferData(GL_PIXEL_PACK_BUFFER, (GLsizeiptr)H * 4 * sizeof(float), nullptr, GL_STREAM_READ);
                lpboValid[L][i] = false;
            }
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
    }
    int width() const { return W; }
    int height() const { return H; }

    // draws events of every layer, applies effects and covers, composites into output().
    // With 3D on, a flat pass first fills the score target the column reads (score space, never
    // the 3D view), then the 3D pass fills output() for display.
    void render(const std::vector<Event>& events0, const Frame& f, const std::vector<LayerDef>& layers){
        if(!glReady) return;
        timersFrameStart();
        paper = f.paper; nLayers = std::clamp((int)layers.size(), 1, 4);
        for(int L = 0; L < 4; L++){ layerGain[L] = L < (int)layers.size() ? (float)layers[(size_t)L].gain : 1.f; colCached[L] = false; }
        scoreIsOut = !f.td.any();
        if(!scoreIsOut){
            Frame flat = f; flat.td = draw::ThreeD{};
            tBegin(G_FLAT); innerTiming = false;
            renderPass(events0, flat, layers);
            // copy the flat composite into the score target
            score.begin(); ofClear(0, 0, 0, 0); glDisable(GL_BLEND);
            compShader.begin(); compShader.setUniformTexture("uSrc", out.getTexture(), 0); compShader.setUniform1f("uGain", 1.f);
            fullQuad(); compShader.end();
            score.end();
            for(int L = 0; L < 4; L++){   // flat copies of each layer for layerColumn()
                scoreLayer[L].begin(); ofClear(0, 0, 0, 0);
                compShader.begin(); compShader.setUniformTexture("uSrc", layerFbo(L).getTexture(), 0); compShader.setUniform1f("uGain", 1.f);
                fullQuad(); compShader.end();
                scoreLayer[L].end();
            }
            innerTiming = true; tEnd(G_FLAT);
        }
        renderPass(events0, f, layers);
        showOver(out.getTexture(), -1.f, 1.f);   // display copy over the background colour
    }
    // display(): img (e.g. the post's output of output()) over the background, with output()'s alpha
    // (stretched like the image left of stretchX, < 0 none). Display only.
    ofFbo& showOver(const ofTexture& img, float stretchX, float opacity){
        if(!glReady) return disp;
        disp.begin(); glDisable(GL_BLEND);
        bgShader.begin(); bgShader.setUniformTexture("uSrc", out.getTexture(), 0); bgShader.setUniformTexture("uImg", img, 1);
        bgShader.setUniform3f("uBg", bgCol); bgShader.setUniform2f("uSize", (float)W, (float)H);
        bgShader.setUniform1f("uStretchX", stretchX); bgShader.setUniform1f("uOpacity", opacity);
        fullQuad(); bgShader.end(); disp.end();
        return disp;
    }
    // background colour of display() only; column() / layerColumn() / output() never include it
    void setBackground(float r, float g, float b){ bgCol = glm::vec3(r, g, b); }
    ofFbo& display(){ return disp; }
    // per-pass GPU time (ms, a few frames late) while `timing` is on: layers (events, effects,
    // own groups), covers (masks), composite, flat 3D score pass; -1 = their sum
    enum { G_LAYERS, G_COVERS, G_COMP, G_FLAT, G_N };
    bool timing = false;
    bool timersOk() const { return gqOk; }
    double gpuMs(int k) const { if(k >= 0) return gpuT[k]; double t = 0; for(double x : gpuT) t += x; return t; }
    // the flat score the sound reads (equals output() when no 3D is on)
    ofFbo& scoreTarget(){ return scoreIsOut ? out : score; }

private:
    bool scoreIsOut = true;
    void renderPass(const std::vector<Event>& events0, const Frame& f, const std::vector<LayerDef>& layers){
        ref.frame = f; ref.layers = layers;
        int nl = std::clamp((int)layers.size(), 1, 4);
        // transpose slots act on note events before drawing (as the CPU; scoped ones on targets only)
        std::vector<const Event*> transposes, insts, elems;
        for(auto& e : events0) if(e.kind == EventKind::Effect){
            int sc = blob::scopeOf(e);
            if(e.look == "transpose") transposes.push_back(&e);
            else if(sc == 1) insts.push_back(&e);
            else if(sc == 2 && !blob::isCopyEffect(e)) elems.push_back(&e);
        }
        std::vector<Event> shifted;
        if(!transposes.empty()){
            shifted = events0;
            for(auto& e : shifted){
                if(e.kind == EventKind::Effect || !e.pitched) continue;
                for(auto t : transposes){
                    bool on = blob::scopeOf(*t) == 0 ? t->layer == e.layer : blob::targets(*t, e);
                    if(on && e.start >= t->start && e.start < t->start + t->length) e.degree += (int)std::lround(t->param("transposeDegrees", 2));
                }
            }
        }
        const std::vector<Event>& events = transposes.empty() ? events0 : shifted;

        ofPushStyle(); ofDisableDepthTest();
        glDisable(GL_CULL_FACE);
        coverOf.assign(events.size(), -1); coverInst.clear();   // cover events packed once, reused by applyCovers
        fxPasses = 0;
        tBegin(G_LAYERS);
        for(int L = 0; L < nl; L++){
            // instances with their own FBO: blob-masked or targeted by a scoped effect
            std::vector<std::string> own;
            for(auto& e : events){
                if(e.kind == EventKind::Effect || std::clamp(e.layer, 0, nl - 1) != L) continue;
                bool o = blob::enabled(e);
                for(auto f : insts) if(blob::targets(*f, e)) o = true;
                for(auto f : elems) if(blob::targets(*f, e)) o = true;
                if(o && std::find(own.begin(), own.end(), e.instance) == own.end()) own.push_back(e.instance);
            }
            auto isOwn = [&](const std::string& id){ return std::find(own.begin(), own.end(), id) != own.end(); };
            // effects on the instruments listed before them (scope "earlier"): the layer is drawn in
            // stages by drawOrder and each such effect is applied to the bus when its order is reached
            std::vector<const Event*> early;
            for(auto& e : events0) if(blob::isEarlier(e) && e.look != "transpose" && std::clamp(e.layer, 0, nl - 1) == L) early.push_back(&e);
            std::stable_sort(early.begin(), early.end(), [](const Event* a, const Event* b){ return blob::drawOrderOf(*a) < blob::drawOrderOf(*b); });
            std::map<std::string, double> ownOrder;   // own groups are drawn in their stage too
            for(auto& e : events) if(e.kind != EventKind::Effect && isOwn(e.instance)) ownOrder.emplace(e.instance, blob::drawOrderOf(e));
            for(size_t stage = 0; stage <= early.size(); stage++){
                double lo = stage > 0 ? blob::drawOrderOf(*early[stage - 1]) : -1e300;
                double hi = stage < early.size() ? blob::drawOrderOf(*early[stage]) : 1e300;
                std::vector<Inst>& maxI = stMax; std::vector<Inst>& addI = stAdd;
                maxI.clear(); addI.clear();
                if(stage == 0 || hi > lo)
                for(auto& e : events) if(std::clamp(e.layer, 0, nl - 1) == L && e.kind != EventKind::Effect && !isOwn(e.instance) && ref.shown(e)){
                    if(!early.empty()){ double o = blob::drawOrderOf(e); if(o < lo || o >= hi) continue; }
                    Inst i; if(!pack(e, i)) continue;
                    if(e.param("coverEnable", 0) >= 0.5){ coverOf[(size_t)(&e - events.data())] = (int)coverInst.size(); coverInst.push_back(i); }
                    bool add = e.look == "additive";
                    (add ? addI : maxI).push_back(i);
                }
                if(stage == 0 || !maxI.empty() || !addI.empty()){
                    fxFlush();   // pending effect batch before anything is drawn over it
                    layerFbo(L).begin(); if(stage == 0) ofClear(0, 0, 0, 0);
                    glEnable(GL_BLEND);
                    glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE); drawInstances(maxI, 0);
                    glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); drawInstances(addI, 0);
                    layerFbo(L).end();
                }
                for(auto& id : own){ double o = ownOrder[id]; if(o >= lo && o < hi){ fxFlush(); drawGroup(L, nl, id, events, insts, elems); } }
                if(stage < early.size()) fxQueue(L, *early[stage]);
            }
            for(auto& e : events0) if(std::clamp(e.layer, 0, nl - 1) == L && e.kind == EventKind::Effect && e.look != "transpose" && blob::scopeOf(e) == 0) fxQueue(L, e);
            fxFlush();
        }
        tEnd(G_LAYERS);
        tBegin(G_COVERS);
        applyCovers(events0, nl);
        tEnd(G_COVERS);
        // composite: all layers in one pass (overwrites out, no clear)
        tBegin(G_COMP);
        out.begin();
        glDisable(GL_BLEND);
        compAllShader.begin();
        int mode[4] = {0, 0, 0, 0}, afb[4] = {0, 0, 0, 0}; float gain[4] = {0, 0, 0, 0};
        for(int L = 0; L < nl; L++){
            mode[L] = ref.blendOf(L); gain[L] = (float)layers[(size_t)L].gain; afb[L] = f.paper.alphaFromBrightness[(size_t)L] ? 1 : 0;
            static const char* names[4] = {"uL0", "uL1", "uL2", "uL3"};
            compAllShader.setUniformTexture(names[L], layerFbo(L).getTexture(), L);
        }
        for(int L = nl; L < 4; L++){ static const char* names[4] = {"uL0", "uL1", "uL2", "uL3"}; compAllShader.setUniformTexture(names[L], layerFbo(0).getTexture(), L); }
        compAllShader.setUniform1i("uN", nl);
        glUniform4i(glGetUniformLocation(compAllShader.getProgram(), "uMode"), mode[0], mode[1], mode[2], mode[3]);
        glUniform4i(glGetUniformLocation(compAllShader.getProgram(), "uAfb"), afb[0], afb[1], afb[2], afb[3]);
        compAllShader.setUniform4f("uGain", glm::vec4(gain[0], gain[1], gain[2], gain[3]));
        fullQuad(); compAllShader.end();
        glBlendEquation(GL_FUNC_ADD);
        out.end();
        tEnd(G_COMP);
        ofPopStyle();
    }
public:

    ofFbo& output(){ return out; }
    long effectPasses() const { return fxPasses; }   // effect passes of the last frame (batched or single)
    ofFbo& layer(int i){ return layerFbo(std::clamp(i, 0, 3)); }

    // brightness per row (max of R, G, B) at column x, row 0 = top. Reads only the 1-px column
    // through a PBO; the value returned is the previous frame's (sync read on the first frame).
    // With a non-brightness reading mode on any layer (5.18): max over layers of each layer's reading
    // (as v2Draw.h column()), from the layer columns; never the background.
    std::vector<float> column(int x){
        if(!glReady) return std::vector<float>((size_t)H, 0.f);
        if(paper.anyReading()){
            std::vector<float> col((size_t)H, 0.f);
            for(int L = 0; L < nLayers; L++){ auto c = layerColumn(L, x); for(int y = 0; y < H; y++) col[(size_t)y] = std::max(col[(size_t)y], c[(size_t)y]); }
            return col;
        }
        return readColumn(scoreTarget().getId(), x, pbo, pboValid, pboNext);   // score space, not the 3D view
    }
    // layer L's own flat score buffer (before layer gain and compositing), brightness per row (max
    // of R, G, B, 0..1) at column x. Row 0 = top of the image (texture row 0 is the top, as
    // everywhere in this file). Asynchronous through its own PBO pair: one frame late, sync on the
    // first read. With 3D on it reads the flat copy made by the score pass, never the 3D view.
    std::vector<float> layerColumn(int L, int x){
        L = std::clamp(L, 0, 3);
        if(!glReady) return std::vector<float>((size_t)H, 0.f);
        GLuint id = scoreIsOut ? layerFbo(L).getId() : scoreLayer[L].getId();
        if(colCached[L] && colCacheX[L] == x) return colCache[L];   // read once per frame
        readL = L;
        colCache[L] = readColumn(id, x, lpbo[L], lpboValid[L], lpboNext[L]);
        readL = -1; colCached[L] = true; colCacheX[L] = x;
        return colCache[L];
    }
private:
    std::vector<float> readColumn(GLuint fbo, int x, GLuint* pbo, bool* pboValid, int& pboNext){
        std::vector<float> col((size_t)H, 0.f);
        x = std::clamp(x, 0, W - 1);
        GLint prevRead = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo);
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        bool ok = false;
        int cur = pboNext, prev = 1 - cur;
        glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[cur]);
        glReadPixels(x, 0, 1, H, GL_RGBA, GL_FLOAT, nullptr);   // async into the PBO
        pboValid[cur] = true;
        if(pboValid[prev]){
            glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo[prev]);
            const float* p = (const float*)glMapBufferRange(GL_PIXEL_PACK_BUFFER, 0, (GLsizeiptr)H * 4 * sizeof(float), GL_MAP_READ_BIT);
            if(p){ toColumn(p, col); ok = true; }
            glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
        pboNext = prev;
        if(!ok){   // fallback: synchronous read of the column
            std::vector<float> rgba((size_t)H * 4);
            glReadPixels(x, 0, 1, H, GL_RGBA, GL_FLOAT, rgba.data());
            toColumn(rgba.data(), col);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return col;
    }
public:

    // whole composite as RGBA floats, row 0 = top (synchronous; for the GPU/CPU comparison)
    std::vector<float> readRGBA(){
        std::vector<float> px((size_t)W * H * 4, 0.f);
        if(!glReady) return px;
        GLint prevRead = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevRead);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, out.getId());
        glReadBuffer(GL_COLOR_ATTACHMENT0);
        glPixelStorei(GL_PACK_ALIGNMENT, 4);
        glReadPixels(0, 0, W, H, GL_RGBA, GL_FLOAT, px.data());
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)prevRead);
        return px;
    }

private:
    int W = 0, H = 0;
    ofFbo pool[5], maskA, maskB, out, grpA, grpB, score;   // score: flat composite for column() when 3D is on   // grp: one instance's own drawing (5.12)
    ofShader blobShader;
    GLuint blobTex = 0, noiseTex = 0;
    std::vector<float> latBuf;
    std::vector<Inst> grpMax, grpAdd, stMax, stAdd;
    int passCols[2] = {-1, -1};   // columns of an in-place effect pass (applyEffect), -1 none
    std::vector<float> blobGain;   // pool: 4 layers + 1 scratch, ping-pong by index
    int lay[4] = {0, 1, 2, 3}, scr = 4;
    ofShader evShader, fxShader, maskShader, coverShader, compShader, compAllShader, bgShader;
    ofFbo disp;                                        // out over the background (display only)
    glm::vec3 bgCol{0, 0, 0};
    int MW = 1, MH = 1;                                // cover mask size (half resolution)
    std::vector<int> coverOf; std::vector<Inst> coverInst;
    // GPU timer queries: a ring 3 frames deep, read when available
    GLuint gq[3][G_N] = {}; bool gqUsed[3][G_N] = {}; int gqFrame = 0; bool gqOk = false, innerTiming = true; int gqActive = -1;
    double gpuT[G_N] = {0, 0, 0, 0};
    void timersFrameStart(){
        if(!gqOk) return;
        gqFrame = (gqFrame + 1) % 3;
        for(int k = 0; k < G_N; k++) if(gqUsed[gqFrame][k]){
            GLint ready = 0; glGetQueryObjectiv(gq[gqFrame][k], GL_QUERY_RESULT_AVAILABLE, &ready);
            if(ready){ GLuint64 ns = 0; glGetQueryObjectui64v(gq[gqFrame][k], GL_QUERY_RESULT, &ns); gpuT[k] = ns / 1e6; }
            gqUsed[gqFrame][k] = false;   // not ready after 3 frames: dropped
        }
        if(!timing) for(double& x : gpuT) x = 0;
    }
    void tBegin(int k){
        if(!gqOk || !timing || gqActive >= 0 || (!innerTiming && k != G_FLAT)) return;
        glBeginQuery(GL_TIME_ELAPSED, gq[gqFrame][k]); gqUsed[gqFrame][k] = true; gqActive = k;
    }
    void tEnd(int k){ if(gqActive == k){ glEndQuery(GL_TIME_ELAPSED); gqActive = -1; } }
    bool glReady = false;
    GLuint evVao = 0, fullVao = 0, quadBuf = 0, instBuf = 0, pbo[2] = {0, 0}, lpbo[4][2] = {{0, 0}, {0, 0}, {0, 0}, {0, 0}};
    size_t instCap = 0;
    bool pboValid[2] = {false, false}, lpboValid[4][2] = {};
    int pboNext = 0, lpboNext[4] = {0, 0, 0, 0};
    ofFbo scoreLayer[4];              // flat copies of the layers when 3D is on (layerColumn)
    draw::Renderer ref;               // CPU side: views, spans, culling

    ofFbo& layerFbo(int L){ return pool[lay[L]]; }

    // brightness per row; for a layer read (readL >= 0) its reading mode with gain and alpha from brightness
    void toColumn(const float* rgba, std::vector<float>& col){
        if(readL >= 0 && paper.anyReading()){
            int m = paper.reading[(size_t)readL]; bool afb = paper.alphaFromBrightness[(size_t)readL];
            for(int y = 0; y < H; y++){ float q[4]; draw::layerPixel(&rgba[(size_t)y * 4], layerGain[readL], afb, q); col[(size_t)y] = std::min(1.f, draw::readPixel(q, m)); }
            return;
        }
        for(int y = 0; y < H; y++){ const float* p = &rgba[(size_t)y * 4]; col[(size_t)y] = std::min(1.f, std::max({p[0], p[1], p[2]})); }
    }
    draw::Paper paper; int nLayers = 1; float layerGain[4] = {1, 1, 1, 1};
    int readL = -1; bool colCached[4] = {}; int colCacheX[4] = {}; std::vector<float> colCache[4];

    void setupGL(){
        auto build = [](ofShader& sh, const char* vert, const char* frag, bool inst){
            sh.setupShaderFromSource(GL_VERTEX_SHADER, vert);
            sh.setupShaderFromSource(GL_FRAGMENT_SHADER, frag);
            sh.bindAttribute(0, "position");
            if(inst) for(int a = 0; a < 12; a++) sh.bindAttribute((GLuint)(1 + a), kInstNames[a]);
            sh.linkProgram();
        };
        build(evShader, glsl::quadVert, glsl::eventFrag, true);
        build(fxShader, glsl::fullVert, glsl::effectFrag, false);
        build(maskShader, glsl::fullVert, glsl::maskFrag, false);
        build(coverShader, glsl::fullVert, glsl::coverFrag, false);
        build(compShader, glsl::fullVert, glsl::compFrag, false);
        build(compAllShader, glsl::fullVert, glsl::compAllFrag, false);
        build(bgShader, glsl::fullVert, glsl::bgFrag, false);
        {   // timer queries need GL 3.3+ (or ARB_timer_query)
            GLint maj = 0, mnr = 0; glGetIntegerv(GL_MAJOR_VERSION, &maj); glGetIntegerv(GL_MINOR_VERSION, &mnr);
            while(glGetError() != GL_NO_ERROR){}
            if(maj * 10 + mnr >= 33){ glGenQueries(3 * G_N, &gq[0][0]); gqOk = glGetError() == GL_NO_ERROR && gq[0][0]; }
        }
        build(blobShader, glsl::fullVert, glsl::blobFrag, false);
        glGenTextures(1, &blobTex); glGenTextures(1, &noiseTex);
        const float corners[8] = {0, 0, 1, 0, 0, 1, 1, 1};   // triangle strip
        glGenBuffers(1, &quadBuf);
        glBindBuffer(GL_ARRAY_BUFFER, quadBuf);
        glBufferData(GL_ARRAY_BUFFER, sizeof corners, corners, GL_STATIC_DRAW);
        glGenBuffers(1, &instBuf);
        glBindBuffer(GL_ARRAY_BUFFER, instBuf);
        instCap = 256 * sizeof(Inst);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)instCap, nullptr, GL_STREAM_DRAW);
        // full-screen VAO: corners only
        glGenVertexArrays(1, &fullVao);
        glBindVertexArray(fullVao);
        glBindBuffer(GL_ARRAY_BUFFER, quadBuf);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr); glVertexAttribDivisor(0, 0);
        // event VAO: corners + 11 per-instance vec4 (divisor 1) from one interleaved buffer
        glGenVertexArrays(1, &evVao);
        glBindVertexArray(evVao);
        glBindBuffer(GL_ARRAY_BUFFER, quadBuf);
        glEnableVertexAttribArray(0); glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, nullptr); glVertexAttribDivisor(0, 0);
        glBindBuffer(GL_ARRAY_BUFFER, instBuf);
        for(int a = 0; a < 12; a++){
            GLuint loc = (GLuint)(1 + a);
            glEnableVertexAttribArray(loc);
            glVertexAttribPointer(loc, 4, GL_FLOAT, GL_FALSE, sizeof(Inst), (const void*)(size_t)(a * 4 * sizeof(float)));
            glVertexAttribDivisor(loc, 1);
        }
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glGenBuffers(2, pbo);
        for(int L = 0; L < 4; L++) glGenBuffers(2, lpbo[L]);
        glReady = true;
    }
    void releaseGL(){
        if(!glReady) return;
        glDeleteVertexArrays(1, &evVao); glDeleteVertexArrays(1, &fullVao);
        glDeleteBuffers(1, &quadBuf); glDeleteBuffers(1, &instBuf); glDeleteBuffers(2, pbo);
        for(int L = 0; L < 4; L++) glDeleteBuffers(2, lpbo[L]);
        glDeleteTextures(1, &blobTex); glDeleteTextures(1, &noiseTex);
        if(gqOk) glDeleteQueries(3 * G_N, &gq[0][0]);
        glReady = false;
    }

    void fullQuad(){
        glBindVertexArray(fullVao);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        glBindVertexArray(0);
    }

    bool pack(const Event& e, Inst& in){ return packEvent(ref, H, e, in); }

    // upload instances into the persistent buffer (orphaned each call) and draw them
    void drawInstances(const std::vector<Inst>& inst, int maskOnly){
        if(inst.empty()) return;
        size_t bytes = inst.size() * sizeof(Inst);
        glBindBuffer(GL_ARRAY_BUFFER, instBuf);
        if(bytes > instCap) instCap = std::max(bytes, instCap * 2);
        glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)instCap, nullptr, GL_STREAM_DRAW);
        glBufferSubData(GL_ARRAY_BUFFER, 0, (GLsizeiptr)bytes, inst.data());
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        evShader.begin();
        evShader.setUniform2f("uSize", (float)W, (float)H);
        evShader.setUniform1i("uMaskOnly", maskOnly);
        evShader.setUniform2f("uPxScale", maskOnly ? (float)W / MW : 1.f, maskOnly ? (float)H / MH : 1.f);
        glBindVertexArray(evVao);
        glDrawArraysInstanced(GL_TRIANGLE_STRIP, 0, 4, (GLsizei)inst.size());
        glBindVertexArray(0);
        evShader.end();
    }

    // ---- batched effect passes. Consecutive effect events on a layer (bus and "earlier" scope, in
    // their order) go into one full-screen pass each instead of one pass per event: on Apple's GL
    // every pass (FBO switch, blit) costs about the same whatever its width, and a v1 scene shows
    // 200+ gate / blank / band events per frame (DUBTECH: 14 fps). A slot joins the batch when it
    // overlaps no earlier slot of the batch, or both are pointwise gains (they commute), or both are
    // echoes (max). Shuffle (its own permutation), noise (its lattice texture) and blob run alone.
    struct FxSlot { const Event* e; float x0, x1; int c0, c1, mode; bool gain; float p[4]; };
    std::vector<FxSlot> fxBatch; int fxBatchL = -1;
    static bool fxGainMode(int m){ return m == 4 || (m >= 10 && m <= 17); }
    void fxQueue(int L, const Event& e){
        if(e.look == "blob"){ fxFlush(); applyEffect(L, e); return; }
        LayerView v = ref.viewOf(L);
        FxSlot s; s.e = &e; s.x0 = (float)v.xOf(e.start); s.x1 = (float)v.xOf(e.start + e.length);
        float perm[32];
        if(!packEffect(e, v, s.mode, s.p, perm)) return;
        if(!fxColumns(e, v, s.x0, s.x1, W, s.c0, s.c1)) return;
        if(s.mode == 6 || s.mode == 18){ fxFlush(); applyEffect(L, e); return; }
        s.gain = fxGainMode(s.mode);
        bool ok = fxBatchL == L && fxBatch.size() < 64;
        if(ok) for(auto& o : fxBatch) if(o.c1 > s.c0 && s.c1 > o.c0 && !((o.gain && s.gain) || (o.mode == 5 && s.mode == 5))){ ok = false; break; }
        if(!ok) fxFlush();
        fxBatchL = L; fxBatch.push_back(s);
    }
    void fxFlush(){
        if(fxBatch.empty()) return;
        int L = fxBatchL;
        if(fxBatch.size() == 1) applyEffect(L, *fxBatch[0].e);
        else if(fxBatchPass(layerFbo(L), pool[scr], fxBatch)) std::swap(lay[L], scr);
        fxBatch.clear(); fxBatchL = -1;
    }
    std::vector<float> fxS, fxSP, fxSQ;
    bool fxBatchPass(ofFbo& src, ofFbo& dst, const std::vector<FxSlot>& slots){
        int n = (int)std::min<size_t>(slots.size(), 64); if(n == 0) return false;
        fxS.assign((size_t)n * 4, 0.f); fxSP.assign((size_t)n * 4, 0.f); fxSQ.assign((size_t)n * 4, 0.f);
        for(int i = 0; i < n; i++){
            const FxSlot& s = slots[(size_t)i]; const Event& e = *s.e;
            fxS[(size_t)i * 4] = s.x0; fxS[(size_t)i * 4 + 1] = s.x1; fxS[(size_t)i * 4 + 2] = (float)s.mode; fxS[(size_t)i * 4 + 3] = (float)std::clamp((int)e.param("shape", 0), 0, 4);
            for(int k = 0; k < 4; k++) fxSP[(size_t)i * 4 + k] = s.p[k];
            fxSQ[(size_t)i * 4] = (float)e.param("amount", 1); fxSQ[(size_t)i * 4 + 1] = (float)(e.param("gateRate", 0.5) / std::max(1e-6, e.length));
            fxSQ[(size_t)i * 4 + 2] = (float)draw::hash01(e.seed, 77) * 100.f;
        }
        glDisable(GL_BLEND);
        dst.begin();
        fxShader.begin();
        fxShader.setUniformTexture("uSrc", src.getTexture(), 0);
        fxShader.setUniform2f("uSize", (float)W, (float)H);
        fxShader.setUniform1i("uN", n);
        fxShader.setUniform4fv("uS", fxS.data(), n); fxShader.setUniform4fv("uSP", fxSP.data(), n); fxShader.setUniform4fv("uSQ", fxSQ.data(), n);
        fullQuad();
        fxShader.setUniform1i("uN", 0);
        fxShader.end(); dst.end();
        fxPasses++;
        return true;
    }
    long fxPasses = 0;   // effect passes this frame (batched or single), for the timing panel

    // layer-bus effect into the scratch FBO, then the indices swap
    void applyEffect(int L, const Event& e){
        LayerView v = ref.viewOf(L);
        if(e.look == "blob"){ if(blobFxPass(layerFbo(L), pool[scr], e, v)) std::swap(lay[L], scr); return; }
        float x0 = (float)v.xOf(e.start), x1 = (float)v.xOf(e.start + e.length);
        int c0 = 0, c1 = W;
        if(!fxColumns(e, v, x0, x1, W, c0, c1)) return;
        if(c1 - c0 > W / 2){   // wide slot: full-screen pass into the scratch buffer, swapped in
            if(fxPass(layerFbo(L), pool[scr], e, x0, x1, v)) std::swap(lay[L], scr);
            return;
        }
        // narrow slot (e.g. one 16th): copy only the columns the pass reads into the scratch buffer,
        // then run it in place, scissored to those columns (the shader passes the rest through)
        GLboolean sc = glIsEnabled(GL_SCISSOR_TEST); glDisable(GL_SCISSOR_TEST);
        GLint pr = 0, pd = 0; glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &pr); glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &pd);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, layerFbo(L).getId()); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, pool[scr].getId());
        glBlitFramebuffer(c0, 0, c1, H, c0, 0, c1, H, GL_COLOR_BUFFER_BIT, GL_NEAREST);
        glBindFramebuffer(GL_READ_FRAMEBUFFER, (GLuint)pr); glBindFramebuffer(GL_DRAW_FRAMEBUFFER, (GLuint)pd);
        passCols[0] = c0; passCols[1] = c1;   // fxPass scissors to them after binding its target
        fxPass(pool[scr], layerFbo(L), e, x0, x1, v);
        passCols[0] = passCols[1] = -1;
        if(sc) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
    }

    // upload a quarter-resolution gain map into blobTex and multiply src into dst
    void blobMultiply(ofFbo& src, ofFbo& dst, int mw, int mh){
        glDisable(GL_BLEND);
        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, blobTex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, mw, mh, 0, GL_RED, GL_FLOAT, blobGain.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glActiveTexture(GL_TEXTURE0);
        dst.begin(); blobShader.begin();
        blobShader.setUniformTexture("uSrc", src.getTexture(), 0);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, blobTex); glActiveTexture(GL_TEXTURE0);
        blobShader.setUniform1i("uMask", 1);
        blobShader.setUniform2f("uSize", (float)W, (float)H);
        fullQuad(); blobShader.end(); dst.end();
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, 0); glActiveTexture(GL_TEXTURE0);
    }
    // filter fx "blob" over a slot (gain 1 outside it)
    bool blobFxPass(ofFbo& src, ofFbo& dst, const Event& e, const LayerView& v){
        double xs = v.xOf(e.start), xe = v.xOf(e.start + e.length);
        if(xe < 0 || xs >= W || xe - xs < 1) return false;
        int mw = std::max(1, W / 4), mh = std::max(1, H / 4);
        blobGain.assign((size_t)mw * mh, 1.f);
        blob::Source so = blob::sourceOf(e, e.length);
        if(!e.p.count("blobPhStart")){ so.phStart = e.start; so.phLen = e.length; }
        blob::Mask m; m.build(blob::paramsOf(e), so, e.start, e.start + e.length);
        for(int x = 0; x < mw; x++){
            double cx = (x + 0.5) * 4; if(cx < xs || cx >= xe) continue;
            double beat = v.nowBeat + (cx - v.headerX) / v.pxPerBeat;
            for(int y = 0; y < mh; y++) blobGain[(size_t)y * mw + x] = (float)std::clamp(m.gain(beat, 1 - (y + 0.5) / mh, v.pxPerBeat, H, v.nowBeat), 0.0, 1.0);
        }
        blobMultiply(src, dst, mw, mh);
        return true;
    }

    // one effect pass src -> dst over columns [x0, x1) at the view's scale; false if nothing done
    bool fxPass(ofFbo& src, ofFbo& dstFbo, const Event& e, float x0, float x1, const LayerView& v){
        int mode = 0; float pp[4], perm[32];
        if(!packEffect(e, v, mode, pp, perm)) return false;
        if(x1 < 0 || x0 >= W || x1 - x0 < 1) return false;
        glm::vec4 p(pp[0], pp[1], pp[2], pp[3]);
        float cell = 1; int lnx = 0, lny = 0;
        if(mode == 18){   // noise: the CPU lattice as a small float texture
            cell = noiseLattice(e, x1 - x0, H, latBuf, lnx, lny);
            if(lnx > 0){
                glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, noiseTex);
                glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
                glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, lnx, lny, 0, GL_RED, GL_FLOAT, latBuf.data());
                glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST); glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
                glActiveTexture(GL_TEXTURE0);
            }
        }
        glDisable(GL_BLEND);
        ofFbo& dst = dstFbo;
        dst.begin();
        if(passCols[0] >= 0){ glEnable(GL_SCISSOR_TEST); glScissor(passCols[0], 0, passCols[1] - passCols[0], H); }   // in-place pass
        fxShader.begin();
        fxShader.setUniformTexture("uSrc", src.getTexture(), 0);
        fxShader.setUniform2f("uSize", (float)W, (float)H);
        fxShader.setUniform1f("uX0", x0); fxShader.setUniform1f("uX1", x1);
        fxShader.setUniform1i("uMode", mode);
        fxShader.setUniform1i("uN", 0);
        fxShader.setUniform4f("uP", p);
        fxShader.setUniform1f("uAmount", (float)e.param("amount", 1));
        fxShader.setUniform1i("uShape", std::clamp((int)e.param("shape", 0), 0, 4));
        fxShader.setUniform1f("uRateFrac", (float)(e.param("gateRate", 0.5) / std::max(1e-6, e.length)));
        fxShader.setUniform1f("uSeed", (float)draw::hash01(e.seed, 77) * 100.f);
        fxShader.setUniform1fv("uPerm", perm, 32);
        fxShader.setUniform1f("uCell", cell);
        glUniform2i(glGetUniformLocation(fxShader.getProgram(), "uLatN"), lnx, lny);
        glActiveTexture(GL_TEXTURE1); glBindTexture(GL_TEXTURE_2D, noiseTex); glActiveTexture(GL_TEXTURE0);
        fxShader.setUniform1i("uLat", 1);
        fullQuad(); fxShader.end(); dst.end();
        fxPasses++;
        return true;
    }

    // one instance's own drawing: draw, its scoped effects, its blob mask, GL_MAX onto the layer
    void drawGroup(int L, int nl, const std::string& id, const std::vector<Event>& events,
                   const std::vector<const Event*>& insts, const std::vector<const Event*>& elems){
        std::vector<Inst>& maxI = grpMax; std::vector<Inst>& addI = grpAdd; std::vector<const Event*> mine;   // buffers reused
        maxI.clear(); addI.clear();
        double xl = 1e18, xr = -1e18;
        for(auto& e : events) if(std::clamp(e.layer, 0, nl - 1) == L && e.kind != EventKind::Effect && e.instance == id && ref.shown(e)){
            Inst i; if(!pack(e, i)) continue;
            bool add = e.look == "additive";
            (add ? addI : maxI).push_back(i);
            mine.push_back(&e);
            double sc = i.v[11][0], tx = i.v[11][1];
            xl = std::min(xl, i.v[0][0] * sc + tx); xr = std::max(xr, i.v[0][2] * sc + tx);
        }
        if(mine.empty()) return;
        // columns the group can reach: its quads, every effect slot it gets (echo to the right,
        // tears / tile reading around), so every pass is scissored to them
        {
            LayerView v0 = ref.viewOf(L);
            auto addSlot = [&](const Event& f, double x0, double x1){
                double ext = 0.25 * (x1 - x0) * std::max(0.0, f.param("tearAmount", 0.5)) + 8;
                if(f.look == "echo") ext += std::clamp(f.param("echoCount", 3), 1.0, 16.0) * std::fabs(f.param("echoSpacing", 0.5)) * v0.pxPerBeat;
                if(f.look == "tile") ext += f.param("repeats", 2);
                xl = std::min(xl, x0 - ext); xr = std::max(xr, x1 + ext);
            };
            for(auto f : elems) for(auto e : mine) if(blob::targets(*f, *e)){
                LayerView vd = v0; vd.pxPerBeat *= draw::eventDepth(*e, ref.frame);
                addSlot(*f, vd.xOf(e->start), vd.xOf(e->start + e->length + draw::tailBeats(*e)));
            }
            for(auto f : insts) if(f->layer == L) addSlot(*f, v0.xOf(f->start), v0.xOf(f->start + f->length));
        }
        int gx0 = std::clamp((int)std::floor(xl) - 4, 0, W), gx1 = std::clamp((int)std::ceil(xr) + 4, 0, W);
        if(gx1 <= gx0) return;
        glEnable(GL_SCISSOR_TEST); glScissor(gx0, 0, gx1 - gx0, H);
        ofFbo* a = &grpA; ofFbo* b = &grpB;
        a->begin(); glEnable(GL_SCISSOR_TEST); glClearColor(0, 0, 0, 0); glClear(GL_COLOR_BUFFER_BIT);
        glEnable(GL_BLEND);
        glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE); drawInstances(maxI, 0);
        glBlendEquation(GL_FUNC_ADD); glBlendFunc(GL_ONE, GL_ONE); drawInstances(addI, 0);
        a->end();
        LayerView v = ref.viewOf(L);
        for(auto f : elems) for(auto e : mine){
            if(!blob::targets(*f, *e) || e->start < f->start - 1e-9 || e->start >= f->start + f->length) continue;
            double d = draw::eventDepth(*e, ref.frame);
            LayerView vd = v; vd.pxPerBeat = v.pxPerBeat * d;
            double len = e->length + draw::tailBeats(*e);
            Event slot = *f; slot.start = e->start; slot.length = len;
            if(fxPass(*a, *b, slot, (float)vd.xOf(e->start), (float)vd.xOf(e->start + len), vd)) std::swap(a, b);
        }
        for(auto f : insts) if(f->layer == L && (f->param("fxTarget", 0) == 0 || f->param("fxTarget", 0) == blob::targetHash(id))){
            if(f->look == "blob"){ if(blobFxPass(*a, *b, *f, v)) std::swap(a, b); continue; }
            if(fxPass(*a, *b, *f, (float)v.xOf(f->start), (float)v.xOf(f->start + f->length), v)) std::swap(a, b);
        }
        bool masked = false; for(auto e : mine) masked = masked || blob::enabled(*e);
        glDisable(GL_BLEND);
        if(masked){
            // gain texture at quarter resolution: per column, the max gain of the events drawn there
            int mw = std::max(1, W / 4), mh = std::max(1, H / 4);
            blobGain.assign((size_t)mw * mh, 0.f);
            for(auto e : mine){
                double d = draw::eventDepth(*e, ref.frame);
                auto sp = ref.spanOf(*e);
                int c0 = std::max(0, (int)std::floor((sp.first - 64) / 4)), c1 = std::min(mw - 1, (int)std::ceil((sp.second + 64) / 4));
                blob::Mask m; bool on = blob::enabled(*e);
                if(on) m = ref.maskOf(*e);
                for(int x = c0; x <= c1; x++){
                    double beat = v.nowBeat + ((x + 0.5) * 4 - v.headerX) / (v.pxPerBeat * d);
                    for(int y = 0; y < mh; y++){
                        float g = on ? (float)m.gain(beat, 1 - (y + 0.5) / mh, v.pxPerBeat * d, H, ref.frame.nowBeat) : 1.f;
                        float& o = blobGain[(size_t)y * mw + x]; o = std::max(o, g);
                    }
                }
            }
            blobMultiply(*a, *b, mw, mh);
            std::swap(a, b);
        }
        layerFbo(L).begin();
        glEnable(GL_BLEND); glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE);
        compShader.begin(); compShader.setUniformTexture("uSrc", a->getTexture(), 0); compShader.setUniform1f("uGain", 1.f);
        fullQuad(); compShader.end();
        glBlendEquation(GL_FUNC_ADD);
        layerFbo(L).end();
        glDisable(GL_SCISSOR_TEST);
    }

    // one mask pass (half resolution), limited to the columns [sx0, sx1)
    void maskPass(ofFbo& src, ofFbo& dst, int pass, int radius, float releasePx, int sx0, int sx1){
        dst.begin();
        glEnable(GL_SCISSOR_TEST); glScissor(sx0, 0, sx1 - sx0, MH);
        maskShader.begin();
        maskShader.setUniformTexture("uSrc", src.getTexture(), 0);
        maskShader.setUniform2f("uSize", (float)MW, (float)MH);
        maskShader.setUniform1i("uPass", pass); maskShader.setUniform1i("uRadius", radius); maskShader.setUniform1f("uReleasePx", releasePx);
        fullQuad(); maskShader.end();
        glDisable(GL_SCISSOR_TEST);
        dst.end();
    }

    // one mask per (source layer, cover settings), as the CPU groups them. Masks are half resolution
    // and single channel; every pass is scissored to the group's columns; the mask is applied to the
    // target layers in place by blending (no copy).
    void applyCovers(const std::vector<Event>& events, int nl){
        std::map<std::pair<int, draw::CoverKey>, std::vector<Inst>> groups;
        bool reuse = coverOf.size() == events.size();
        for(size_t n = 0; n < events.size(); n++){
            const Event& e = events[n];
            if(e.kind == EventKind::Effect || e.param("coverEnable", 0) < 0.5 || !ref.visible(e)) continue;
            draw::CoverKey key{e.param("coverDilate", 2), e.param("coverFeather", 4), e.param("coverRelease", 0.25), e.param("coverAmount", 1),
                               (int)e.param("coverMode", 0), (int)e.param("coverLayers", 0)};
            auto& g = groups[{std::clamp(e.layer, 0, nl - 1), key}];
            if(reuse && coverOf[n] >= 0){ g.push_back(coverInst[(size_t)coverOf[n]]); continue; }
            Inst i; if(pack(e, i)) g.push_back(i);
        }
        for(auto& g : groups){
            if(g.second.empty()) continue;
            int L = g.first.first; const draw::CoverKey& ck = g.first.second;
            int dil = std::clamp((int)std::lround(ck.dilate), 0, 32);
            int fea = std::clamp((int)std::lround(ck.feather * 0.5), 0, 32);
            float rel = (float)(ck.release * ref.viewOf(L).pxPerBeat);
            // columns the mask can reach (frame px): instance quads (placed) + dilate, feather, release
            double xl = 1e18, xr = -1e18;
            for(auto& i : g.second){
                double sc = i.v[11][0], tx = i.v[11][1];
                xl = std::min(xl, i.v[0][0] * sc + tx); xr = std::max(xr, i.v[0][2] * sc + tx);
            }
            double pad = dil + fea + 4;
            int fx0 = std::clamp((int)std::floor(xl - pad), 0, W), fx1 = std::clamp((int)std::ceil(xr + pad + std::max(0.f, rel)), 0, W);
            if(fx1 <= fx0) continue;
            int sx0 = fx0 * MW / W, sx1 = std::min(MW, (fx1 * MW + W - 1) / W + 1);
            glDisable(GL_SCISSOR_TEST);
            maskB.begin(); ofClear(0, 0, 0, 0); maskB.end();
            maskA.begin(); ofClear(0, 0, 0, 0);
            glEnable(GL_BLEND); glBlendEquation(GL_MAX); glBlendFunc(GL_ONE, GL_ONE);
            drawInstances(g.second, 1);
            maskA.end();
            glDisable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
            // half resolution radii
            int dh = (dil + 1) / 2, fh = (fea + 1) / 2;
            maskPass(maskA, maskB, 0, dh, 0, sx0, sx1); maskPass(maskB, maskA, 1, dh, 0, sx0, sx1);
            maskPass(maskA, maskB, 2, 0, rel * MW / W, sx0, sx1);
            maskPass(maskB, maskA, 3, fh, 0, sx0, sx1); maskPass(maskA, maskB, 4, fh, 0, sx0, sx1);   // result in maskB
            for(int t = 0; t < nl; t++){
                bool on = ck.target == 2 ? t != L : (ck.target == 1 ? t == L - 1 : t < L);
                if(!on) continue;
                ofFbo& dst = layerFbo(t);
                dst.begin();
                glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                if(ck.mode == 1) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA); else glBlendFunc(GL_ZERO, GL_ONE_MINUS_SRC_ALPHA);
                glEnable(GL_SCISSOR_TEST); glScissor(fx0, 0, fx1 - fx0, H);
                coverShader.begin();
                coverShader.setUniformTexture("uMask", maskB.getTexture(), 0);
                coverShader.setUniform2f("uSize", (float)W, (float)H);
                coverShader.setUniform1f("uAmount", (float)ck.amount);
                fullQuad(); coverShader.end();
                glDisable(GL_SCISSOR_TEST); glDisable(GL_BLEND);
                dst.end();
            }
        }
    }
};

} // namespace drawgl
} // namespace forms2

#endif // FORMS2_HEADLESS
