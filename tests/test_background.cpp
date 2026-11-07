// Background colour of Forms Box's Output (formsBackground.h): black leaves the image exactly as
// it was, a colour shows where nothing is drawn, and the sound's inputs (the layers) are untouched.
// clang++ -std=c++17 -I src/formsBox tests/test_background.cpp -o /tmp/tb && /tmp/tb
#include "formsBackground.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
#include <cmath>
#include <algorithm>
static int fails = 0;
#define CHECK(c, m) do{ if(!(c)){ printf("FAIL %s\n", m); fails++; } }while(0)
int main(){
    std::mt19937 rng(7); std::uniform_real_distribution<float> U(0, 1);
    // a frame: post's colour and the layers' alphas (what the sound reads: a copy to compare)
    const int N = 4096, L = 3;
    std::vector<float> col(N * 3), layers(N * L);
    for(auto& v : col) v = U(rng);
    for(auto& v : layers) v = U(rng) < 0.3f ? 0 : U(rng);
    std::vector<float> layersBefore = layers;
    auto coverage = [&](int i){ float d = 0; for(int l = 0; l < L; l++) d = forms::coverageOver(d, layers[i * L + l]); return d; };
    // 1. black: bit-identical
    bool black = forms::backgroundIsBlack(0, 0, 0);
    CHECK(black, "black is black");
    std::vector<float> out(N * 3);
    for(int i = 0; i < N; i++) for(int k = 0; k < 3; k++) out[i * 3 + k] = forms::backgroundOver(col[i * 3 + k], coverage(i), 0);
    CHECK(std::memcmp(out.data(), col.data(), out.size() * sizeof(float)) == 0, "black background == current output");
    // 2. white over an empty frame: white
    for(int k = 0; k < 3; k++) CHECK(forms::backgroundOver(0, 0, 1) == 1, "white over empty");
    CHECK(!forms::backgroundIsBlack(1, 1, 1), "white is not black");
    // 3. full coverage: the image alone; half: half the background added
    CHECK(forms::backgroundOver(0.3f, 1, 1) == 0.3f, "covered pixel unchanged");
    CHECK(std::abs(forms::backgroundOver(0.2f, 0.5f, 0.4f) - 0.4f) < 1e-6f, "half coverage");
    CHECK(forms::backgroundOver(0.9f, 0, 0.5f) == 1.0f, "clamped");
    // 4. coverage stacking: one layer = its alpha; opaque anywhere = 1
    CHECK(forms::coverageOver(0, 0.25f) == 0.25f, "one layer");
    CHECK(forms::coverageOver(0.4f, 1) == 1, "opaque");
    // 5. a coloured background never touches the layers (the sound's input)
    for(int i = 0; i < N; i++) for(int k = 0; k < 3; k++) out[i * 3 + k] = forms::backgroundOver(col[i * 3 + k], coverage(i), 0.7f);
    CHECK(layers == layersBefore, "layers unchanged");
    // 6. paper view: hue and saturation kept, lightness inverted, transparent = background
    auto hsl = [](float r, float g, float b, float& h, float& s, float& l){
        float mx = std::max(r, std::max(g, b)), mn = std::min(r, std::min(g, b)), d = mx - mn; l = (mx + mn) / 2;
        s = d < 1e-6f ? 0 : d / (1 - std::abs(2 * l - 1)); h = 0;
        if(d > 1e-6f){ if(mx == r) h = std::fmod((g - b) / d + 6, 6.0f); else if(mx == g) h = (b - r) / d + 2; else h = (r - g) / d + 4; }
    };
    for(int i = 0; i < 2000; i++){
        float r = U(rng), g = U(rng), b = U(rng), h0, s0, l0, h1, s1, l1;
        hsl(r, g, b, h0, s0, l0);
        float R = r, G = g, B = b; forms::paperInk(R, G, B); hsl(R, G, B, h1, s1, l1);
        CHECK(std::abs(l1 - (1 - l0)) < 1e-5f, "paper: L' = 1 - L");
        if(s0 > 1e-3f && std::abs(2 * l0 - 1) < 0.99f){ CHECK(std::abs(h1 - h0) < 1e-3f, "paper: hue kept"); CHECK(std::abs(s1 - s0) < 1e-3f, "paper: saturation kept"); }
    }
    { float r = 0.5f, g = 0.5f, b = 1; forms::paperInk(r, g, b); CHECK(r == 0 && g == 0 && b == 0.5f, "paper: bright blue -> deep blue"); }
    { float r = 0, g = 0, b = 0; forms::paperInk(r, g, b); CHECK(r == 1 && g == 1 && b == 1, "paper: black -> white"); }
    CHECK(forms::paperOver(0.2f, 0, 1) == 1, "paper: transparent = background");
    CHECK(forms::paperOver(0.2f, 1, 1) == 0.2f, "paper: opaque = ink");
    printf(fails ? "%d FAILED\n" : "background: all passed\n", fails);
    return fails ? 1 : 0;
}
