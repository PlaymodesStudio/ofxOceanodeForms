// Which real requests Forms GL Box would draw itself, and why the others go to Processing.
// Run: ./a.out <generators.json> <request.json>...
#include "formsGLSketch.h"
#include <cstdio>
using namespace formsgl;
int main(int argc, char** argv){
    Schemas sc; sc.load(argv[1]);
    Sketch sk(sc);
    std::map<std::string, int> why; int here = 0, n = 0;
    std::map<std::string, int> gens;
    for(int i = 2; i < argc; i++){
        std::ifstream in(argv[i]); json r; try { in >> r; } catch(...) { continue; }
        if(!r.contains("items")) continue;
        n++;
        for(auto& it : r["items"]) gens[it.contains("gen") ? it.value("gen", std::string()) : "method:" + it.value("method", std::string()).substr(0, 3)]++;
        std::string w = sk.whyNot(r);
        if(w.empty()) here++; else why[w]++;
    }
    printf("%d requests: %d drawn here (%.0f%%)\n", n, here, 100.0 * here / std::max(1, n));
    std::vector<std::pair<int, std::string>> v; for(auto& [k, c] : why) v.push_back({c, k});
    std::sort(v.rbegin(), v.rend());
    for(int i = 0; i < (int)v.size() && i < 10; i++) printf("  %4d  %s\n", v[i].first, v[i].second.c_str());
    printf("items by generator:"); for(auto& [k, c] : gens) printf(" %s %d", k.c_str(), c); printf("\n");
}
