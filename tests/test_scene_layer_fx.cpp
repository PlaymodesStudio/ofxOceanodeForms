#include "formsCore.h"
#include <iostream>
using namespace forms;
int main(){
    int bad = 0; auto ok = [&](bool c, const char* m){ if(!c){ bad++; std::cout << "FAIL " << m << "\n"; } };
    LayerDef l; json j = l; ok(!j.contains("fx"), "no fx: not written");
    LayerFxValues v = LayerFxValues::from(json(), 2); ok(v.maskSeed == 3 && !v.any(), "defaults");
    ok(v.toJson(2).empty(), "defaults write nothing");
    v.trail = 1.21f; v.trailLevel = 0.4f; v.maskAmount = 1; v.maskAnimate = true; v.maskSeed = 3776;
    l.fx = v.toJson(2); j = l; ok(j.contains("fx") && j["fx"].size() == 5, "five values written");
    LayerDef back = json::parse(j.dump()).get<LayerDef>();
    ok(LayerFxValues::from(back.fx, 2) == v, "round trip through text");
    ok(LayerFxValues::from(json{{"trail", 99}, {"maskSeed", -4}}, 0).trail == 16, "clamped");
    std::cout << (bad ? "FAILED" : "all passed") << "\n"; return bad;
}
