// Density (the node's): bends every cell's chance in selectItems. Headless:
//   0 gives exactly the selections of the call without it (same rolls), -1 nothing, +1 every cell
//   that is on, cells at 0 never play, and the mean number of instruments grows with the value.
// Build: clang++ -std=c++17 -O1 -I tests/stub -I src/formsBox -I <OF>/libs/json/include tests/test_density.cpp
#include "formsEngine.h"
#include <cstdio>
using namespace forms;
int main(){
    LayerDef l; l.globalProb = 100; l.groupProb = {1}; l.rows = 96;
    float probs[] = {100, 75, 50, 25, 10, 0};
    for(int i = 0; i < 6; i++){ Cell c; c.item = "i" + std::to_string(i); c.prob = probs[i]; c.group = 0; l.cells.push_back(c); }
    int fail = 0;
    { Rng a(11), b(11); for(int k = 0; k < 2000; k++) if(selectItems(l, a) != selectItems(l, b, nullptr, 0)){ fail++; break; } }
    printf("density 0 identical: %s\n", fail ? "NO" : "yes");
    float last = -1;
    for(float d : {-1.0f, -0.5f, -0.25f, 0.0f, 0.25f, 0.5f, 1.0f}){
        Rng r(5); long n = 0, zero = 0; const int N = 20000;
        for(int k = 0; k < N; k++){ auto v = selectItems(l, r, nullptr, d); n += v.size(); for(auto& s : v) if(s == "i5") zero++; }
        float mean = n / (float)N, expect = 0;
        for(float p : probs) expect += densityProb(p, d) / 100;
        printf("density %+5.2f  instruments per image %.3f (expected %.3f)  the 0%% cell played %ld\n", d, mean, expect, zero);
        if(zero || std::fabs(mean - expect) > 0.03f || mean < last) fail++;
        last = mean;
        if(d == -1 && n != 0) fail++;
        if(d == 1 && n != 5L * N) fail++;
    }
    printf(fail ? "FAIL\n" : "ok\n");
    return fail;
}
