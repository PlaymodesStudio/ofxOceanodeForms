//
//  formsFeather.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Edge feathers: a soft vignette per screen edge (left, right, top, bottom), each with its own
//  width (fraction of the screen, 0 = off) and curve (exponent of the fade, 1 = smoothstep).
//  The pixel fades toward a target colour (the background, black when none):
//    out = mix(target, c, f),  f = fL x fR x fT x fB
//  All widths 0: the pass is skipped (the image is exactly as before). Pure maths, no GL:
//  the shader in formsPost.h (Post::feather) does the same.
//

#pragma once

#include <algorithm>
#include <cmath>

namespace forms {

struct Feather {
    float width[4] = {0, 0, 0, 0};   // left, right, top, bottom: fraction of the width / height
    float curve[4] = {1, 1, 1, 1};   // exponent applied after smoothstep (>1 harder, <1 softer)
    bool active() const { return width[0] > 0 || width[1] > 0 || width[2] > 0 || width[3] > 0; }
};

// One edge: d = distance from the edge as a fraction (0..1), w = width, k = curve
inline float featherEdge(float d, float w, float k){
    if(w <= 0) return 1;
    float t = std::min(1.0f, std::max(0.0f, d / w));
    t = t * t * (3 - 2 * t);
    return std::pow(t, std::max(0.05f, k));
}

// The factor at pixel centre (u, v) in 0..1 (v = 0 at the top)
inline float featherFactor(const Feather& f, float u, float v){
    return featherEdge(u, f.width[0], f.curve[0]) * featherEdge(1 - u, f.width[1], f.curve[1]) *
           featherEdge(v, f.width[2], f.curve[2]) * featherEdge(1 - v, f.width[3], f.curve[3]);
}

// One channel of the final pixel
inline float featherOver(float c, float factor, float target){ return c * factor + target * (1 - factor); }   // as GLSL mix

} // namespace forms
