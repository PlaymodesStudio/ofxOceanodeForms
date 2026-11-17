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
    {
        LayerFxValues e = LayerFxValues::from(json(), 0); e.echo = 0.75f; ok(e.any(), "echo alone counts as an effect");
        json w = e.toJson(0); ok(w.size() == 1 && w.contains("echo"), "echo: only the time written at default feedback / mix / filters");
        e.echoFeedback = 0.8f; e.echoMix = 0.3f; e.echoLow = 0.6f; e.echoHigh = 0.2f;
        ok(LayerFxValues::from(json::parse(e.toJson(0).dump()), 0) == e, "echo round trip");
        e.echoMix = 0; ok(!e.any(), "echo at mix 0 is off");
        LayerFxValues c = LayerFxValues::from(json{{"echo", 99}, {"echoFeedback", 2}}, 0); ok(c.echo == 8 && c.echoFeedback == 0.95f, "echo clamped");
        LayerFxValues old = LayerFxValues::from(json{{"trail", 1}}, 0); ok(old.echo == 0 && !old.echoOn(), "scenes saved before the echo: none");
    }
    std::cout << (bad ? "FAILED" : "all passed") << "\n"; return bad;
}
