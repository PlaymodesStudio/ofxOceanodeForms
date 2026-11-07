// Edge feathers (formsFeather.h): all widths 0 leave the image bit-identical, each edge fades
// only its side, edges combine, and the fade goes toward the background colour.
// clang++ -std=c++17 -I src/formsBox tests/test_feather.cpp -o /tmp/tf && /tmp/tf
#include "formsFeather.h"
#include <cstdio>
#include <cstring>
#include <vector>
#include <random>
static int fails = 0;
#define CHECK(c, m) do{ if(!(c)){ printf("FAIL %s\n", m); fails++; } }while(0)
int main(){
    const int W = 64, H = 48;
    std::mt19937 rng(3); std::uniform_real_distribution<float> U(0, 1);
    std::vector<float> img(W * H * 3); for(auto& v : img) v = U(rng);
    auto run = [&](const forms::Feather& f, float bg){
        std::vector<float> o(img);
        if(!f.active()) return o;   // the pass is skipped
        for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
            float k = forms::featherFactor(f, (x + 0.5f) / W, (y + 0.5f) / H);
            for(int c = 0; c < 3; c++){ float& p = o[(y * W + x) * 3 + c]; p = forms::featherOver(p, k, bg); }
        }
        return o;
    };
    // 1. all zero: identity (even with curves changed)
    forms::Feather z; for(auto& k : z.curve) k = 3;
    CHECK(!z.active(), "zero inactive");
    auto o = run(z, 0.5f);
    CHECK(std::memcmp(o.data(), img.data(), o.size() * 4) == 0, "zero == identity");
    for(int i = 0; i < 1000; i++){ float c = U(rng); CHECK(forms::featherOver(c, 1, U(rng)) == c, "factor 1 exact"); }
    // 2. each edge alone fades only its side
    for(int e = 0; e < 4; e++){
        forms::Feather f; f.width[e] = 0.25f;
        auto r = run(f, 0);
        bool sideOk = true, otherOk = true;
        for(int y = 0; y < H; y++) for(int x = 0; x < W; x++){
            float u = (x + 0.5f) / W, v = (y + 0.5f) / H;
            float d = e == 0 ? u : e == 1 ? 1 - u : e == 2 ? v : 1 - v;
            int i = (y * W + x) * 3;
            bool same = r[i] == img[i] && r[i + 1] == img[i + 1] && r[i + 2] == img[i + 2];
            if(d >= 0.25f && !same) otherOk = false;
            if(d < 0.2f && same && img[i] > 0.01f) sideOk = false;
        }
        CHECK(otherOk, "untouched beyond the width"); CHECK(sideOk, "faded inside the width");
    }
    // edge pixel ~ target, monotonic
    CHECK(forms::featherEdge(0, 0.2f, 1) == 0, "edge = target");
    CHECK(forms::featherEdge(0.05f, 0.2f, 1) < forms::featherEdge(0.1f, 0.2f, 1), "monotonic");
    CHECK(forms::featherEdge(0.05f, 0.2f, 3) < forms::featherEdge(0.05f, 0.2f, 1), "curve > 1 darker");
    // 3. independent edges combine (product)
    forms::Feather a; a.width[0] = 0.3f; a.width[2] = 0.2f; a.curve[2] = 2;
    forms::Feather l; l.width[0] = 0.3f; forms::Feather t; t.width[2] = 0.2f; t.curve[2] = 2;
    float u = 0.1f, v = 0.05f;
    CHECK(std::abs(forms::featherFactor(a, u, v) - forms::featherFactor(l, u, v) * forms::featherFactor(t, u, v)) < 1e-6f, "product");
    CHECK(forms::featherFactor(a, 0.9f, 0.5f) == 1, "right half untouched by left/top");
    // 4. toward the background
    CHECK(forms::featherOver(0.8f, 0, 0.3f) == 0.3f, "edge = background");
    CHECK(forms::featherOver(0.8f, 0, 0) == 0, "no background: black");
    float m = forms::featherOver(0.8f, 0.5f, 0.2f); CHECK(std::abs(m - 0.5f) < 1e-6f, "halfway");
    printf(fails ? "%d FAILED\n" : "all feather tests passed\n", fails);
    return fails != 0;
}
