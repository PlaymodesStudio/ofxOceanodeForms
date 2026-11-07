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

} // namespace forms
