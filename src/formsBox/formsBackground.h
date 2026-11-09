//
//  formsBackground.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The background colour of Forms Box's final image (Output only; the sound never sees it).
//  out = post(composite) + background x (1 - coverage), coverage = the layers' alpha stacked
//  "over" (after the same stretch and header opacity as the image). Black background: the pass
//  is skipped, so Output is exactly as before. The GL pass is in formsBox.h (applyBackground).
//

#pragma once

#include <algorithm>

namespace forms {

// Is the background black (the pass is skipped)?
inline bool backgroundIsBlack(float r, float g, float b){ return r <= 0 && g <= 0 && b <= 0; }

// The layers' coverage, stacked: a + d x (1 - a) (as the coverage pass blends)
inline float coverageOver(float dst, float a){ return a + dst * (1 - a); }

// One channel of the final pixel (the same maths as the shader)
inline float backgroundOver(float c, float coverage, float bg){
    if(bg <= 0) return c;   // black: unchanged (the pass does not run)
    return std::min(1.0f, c + bg * (1 - std::min(1.0f, std::max(0.0f, coverage))));
}

// Paper view: hue and saturation kept, HSL lightness inverted (L' = 1 - L). With S and H fixed the
// chroma is the same, so every channel moves by (1 - max - min).
inline void paperInk(float& r, float& g, float& b){
    float d = 1 - std::max(r, std::max(g, b)) - std::min(r, std::min(g, b));
    r = std::min(1.0f, std::max(0.0f, r + d)); g = std::min(1.0f, std::max(0.0f, g + d)); b = std::min(1.0f, std::max(0.0f, b + d));
}
// One channel of a paper-view pixel: the ink over the background by its coverage
inline float paperOver(float ink, float coverage, float bg){
    float a = std::min(1.0f, std::max(0.0f, coverage));
    return ink * a + bg * (1 - a);
}

inline const char* backgroundFragmentShader(){ return R"(
#version 410
uniform sampler2D tImage;
uniform sampler2D tCover;
uniform vec2 size;
uniform float stretchX;   // < 0: no stretch
uniform int stretchBlend;
uniform float opacity;
uniform vec3 bg;
uniform int paper;
out vec4 out_color;
void main(){
    vec3 c = texelFetch(tImage, ivec2(gl_FragCoord.xy), 0).rgb;
    float a;
    if(stretchX >= 0.0 && gl_FragCoord.x <= stretchX){
        // the header column, interpolated as the post's stretch does
        vec2 q = vec2(stretchX, gl_FragCoord.y) - 0.5;
        ivec2 i = ivec2(floor(q)); vec2 f = q - floor(q);
        ivec2 m = ivec2(size) - 1;
        float a0 = mix(texelFetch(tCover, clamp(i, ivec2(0), m), 0).a, texelFetch(tCover, clamp(i + ivec2(1, 0), ivec2(0), m), 0).a, f.x);
        float a1 = mix(texelFetch(tCover, clamp(i + ivec2(0, 1), ivec2(0), m), 0).a, texelFetch(tCover, clamp(i + ivec2(1, 1), ivec2(0), m), 0).a, f.x);
        a = mix(a0, a1, f.y);
        float base = texelFetch(tCover, ivec2(gl_FragCoord.xy), 0).a;
        if(stretchBlend == 1) a = min(base + a, 1.0);
        else if(stretchBlend == 2) a = max(base, a);
        else if(stretchBlend == 3) a = 1.0 - (1.0 - base) * (1.0 - a);
        else if(stretchBlend == 4) a = base * a;
        else if(stretchBlend == 5) a = min(base, a);
    } else a = texelFetch(tCover, ivec2(gl_FragCoord.xy), 0).a;
    a = clamp(a * opacity, 0.0, 1.0);
    if(paper == 1){
        // paper view: lightness inverted (hue, saturation kept), as ink over the background
        vec3 ink = clamp(c + (1.0 - max(c.r, max(c.g, c.b)) - min(c.r, min(c.g, c.b))), 0.0, 1.0);
        out_color = vec4(ink * a + bg * (1.0 - a), 1.0);
    } else out_color = vec4(min(c + bg * (1.0 - a), vec3(1.0)), 1.0);
}
)";
}

} // namespace forms
