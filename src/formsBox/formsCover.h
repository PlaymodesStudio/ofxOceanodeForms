#pragma once

// Shared cover contract for the page renderer, live compositor and exports.
// Pixels are measured at 1920 x 1080; release is measured in beats.
#include <nlohmann/json.hpp>
#include <algorithm>
#include <memory>
#include <string>
#include <vector>
#include <cstdint>
class ofTexture;

namespace formscover {
using json = nlohmann::json;
struct Settings {
    bool enabled = false;
    float growRight = 2, growHeight = 2, feather = 4, release = .25f, amount = 1;
    std::string mode = "eraser", target = "allBelow";
    bool operator==(const Settings& s) const {
        return enabled==s.enabled && growRight==s.growRight && growHeight==s.growHeight && feather==s.feather && release==s.release &&
            amount==s.amount && mode==s.mode && target==s.target;
    }
};
inline Settings read(const json& j){
    Settings s;
    if(!j.is_object()) return s;
    auto number = [&](const char* k, float d, float hi){ auto p=j.find(k); return p!=j.end() && p->is_number() ? std::clamp(p->get<float>(),0.f,hi) : d; };
    auto en=j.find("coverEnable"); s.enabled=en!=j.end() && (en->is_boolean() ? en->get<bool>() : en->is_number() && en->get<double>()>=.5);
    // Keep the original key for saved presets; its horizontal growth is now rightward only.
    s.growRight=number("coverDilate",2,32);
    s.growHeight=number("coverGrowHeight",s.growRight,32); s.feather=number("coverFeather",4,32);
    s.release=number("coverRelease",.25f,8); s.amount=number("coverAmount",1,1);
    if(j.value("coverMode",std::string("eraser"))=="black") s.mode="black";
    std::string t=j.value("coverLayers",std::string("allBelow"));
    if(t=="layerBelow" || t=="all") s.target=t;
    return s;
}
inline json write(const Settings& s){
    return {{"coverEnable",s.enabled},{"coverDilate",s.growRight},{"coverGrowHeight",s.growHeight},{"coverFeather",s.feather},
            {"coverRelease",s.release},{"coverAmount",s.amount},{"coverMode",s.mode},{"coverLayers",s.target}};
}
inline json schema(){
    return json::array({
        {{"key","coverEnable"},{"type","bool"},{"label","Cover enabled"},{"group","cover"},{"default",false}},
        {{"key","coverDilate"},{"type","float"},{"label","Grow right (px)"},{"group","cover"},{"min",0},{"max",32},{"default",2}},
        {{"key","coverGrowHeight"},{"type","float"},{"label","Grow height (px)"},{"group","cover"},{"min",0},{"max",32},{"default",2}},
        {{"key","coverFeather"},{"type","float"},{"label","Feather (px)"},{"group","cover"},{"min",0},{"max",32},{"default",4}},
        {{"key","coverRelease"},{"type","float"},{"label","Release (beats)"},{"group","cover"},{"min",0},{"max",8},{"default",.25}},
        {{"key","coverAmount"},{"type","float"},{"label","Amount"},{"group","cover"},{"min",0},{"max",1},{"default",1}},
        {{"key","coverMode"},{"type","enum"},{"label","Mode"},{"group","cover"},{"default","eraser"},{"options",{"eraser","black"}}},
        {{"key","coverLayers"},{"type","enum"},{"label","Acts on"},{"group","cover"},{"default","allBelow"},{"options",{"allBelow","layerBelow","all"}}}
    });
}
// "blocking" (on unless a false is stored): whether an instrument's black background ("blank what
// is underneath") is drawn at all. Off = it neither blanks its own image nor erases the layers below.
// Read from an instrument's params, a scene layer or a render request.
inline bool blocking(const json& j){
    if(!j.is_object()) return true;
    auto p=j.find("blocking");
    if(p==j.end()) return true;
    return p->is_boolean() ? p->get<bool>() : p->is_number() ? p->get<double>()>=.5 : true;
}
inline json blockingSchema(const std::string& group){
    return {{"key","blocking"},{"type","bool"},{"group",group},{"default",true},{"basic",true},
            {"label","blocking: black background under it (off = never blanks; the other instruments and layers stay visible)"}};
}
// source / dest are positions in the visual stack, not the layer's immutable number.
inline bool affects(const Settings& s, int source, int dest){
    return s.enabled && s.amount>0 && (s.target=="all" ? dest!=source : s.target=="layerBelow" ? dest==source-1 : dest<source);
}
struct Mask {
    Settings settings;
    int w=0,h=0;
    std::shared_ptr<const std::vector<uint8_t>> alpha;
    std::shared_ptr<ofTexture> texture;
    std::string png;
};
inline void readMasks(std::vector<Mask>& masks, const json& side, const std::string& folder){
    masks.clear();
    if(!side.contains("result")) return;
    for(auto& m:side["result"].value("coverMasks",json::array())){
        Mask out; out.settings=read(m); out.png=folder+"/"+m.value("png",std::string());
        if(out.settings.enabled) masks.push_back(std::move(out));
    }
}
} // namespace formscover
