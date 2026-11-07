//
//  formsLiveEdit.h
//  ofxOceanodeForms — ESPECTRES 2027 (Forms GL Box: audition and immediate modes)
//
//  Re-rendering pages after an edit, without the rest of the page changing:
//    usesPreset      does a page's recipe (its sidecar / request) draw an instrument?
//    patchForPreset  the recipe with that instrument's edit applied: only the parameters that
//                    changed take their new values (every other value the page drew stays); a change
//                    of structure (stack layers, linked picks, per-bar) re-resolves its items
//    patchForScene   the recipe with the scene's palette and scale as they are now
//    filterItems     a page without its muted instruments (or with only the soloed ones)
//  Headless (tests/formsGL/test_live_edit.cpp).
//

#pragma once

#include "formsEngine.h"
#include <set>

namespace forms {

// The refs a preset's items carry in a request: its own, and (a stack) those of the presets its
// layers play, at any depth
inline void collectRefs(const Engine& e, const Preset& p, std::set<std::string>& out, int depth = 0){
    out.insert(p.ref());
    if(!p.isStack() || depth > 8) return;
    for(auto& l : p.layers){
        std::string r = l.is_object() ? l.value("preset", std::string()) : std::string();
        if(r.empty() || out.count(r)) continue;
        auto it = e.presets.find(r);
        if(it != e.presets.end()) collectRefs(e, it->second, out, depth + 1);
    }
}
inline std::set<std::string> itemRefs(const Engine& e, const Preset& p){ std::set<std::string> s; collectRefs(e, p, s); return s; }

inline bool usesPreset(const json& recipe, const std::set<std::string>& refs){
    for(auto& it : recipe.value("items", json::array()))
        if(it.is_object() && refs.count(it.value("preset", std::string()))) return true;
    return false;
}
inline bool usesPreset(const json& recipe, const std::string& ref){ return usesPreset(recipe, std::set<std::string>{ref}); }

// The parameter keys whose definition differs between two versions of a preset; "" in the set
// means the structure changed (layers, linked picks, per-bar, the generator)
inline std::set<std::string> changedKeys(const Preset& a, const Preset& b){
    std::set<std::string> out;
    json ja = a, jb = b;
    for(const char* k : {"gen", "layers", "pick", "linked", "perBar"})
        if(ja.value(k, json()) != jb.value(k, json())) out.insert("");
    json pa = ja.value("params", json::object()), pb = jb.value("params", json::object());
    for(auto& [k, v] : pa.items()) if(!pb.contains(k) || pb[k] != v) out.insert(k);
    for(auto& [k, v] : pb.items()) if(!pa.contains(k)) out.insert(k);
    return out;
}

// recipe: a page's sidecar or request; before / after: the instrument before and after the edit;
// seed: for the draws of re-resolved values (the page's own seed, so the page stays itself)
inline json patchForPreset(Engine& e, const json& recipe, const Preset& before, const Preset& after, unsigned seed){
    json req = recipe;
    req.erase("result");
    // its items: tagged with its ref or (a stack) with the refs of the presets it plays
    std::set<std::string> refs = itemRefs(e, before);
    for(auto& r : itemRefs(e, after)) refs.insert(r);
    auto mine = [&](const json& it){ return it.is_object() && refs.count(it.value("preset", std::string())) > 0; };
    auto keys = changedKeys(before, after);
    if(keys.empty()) return req;
    json fresh = e.presetItemsSeeded(after, seed);
    json items = req.value("items", json::array());
    if(keys.count("")){
        // structure: this instrument's items all anew, where its first item was
        json out = json::array();
        bool placed = false;
        for(auto& it : items){
            if(mine(it)){
                if(!placed){ for(auto& f : fresh) out.push_back(f); placed = true; }
                continue;
            }
            out.push_back(it);
        }
        if(!placed) for(auto& f : fresh) out.push_back(f);
        req["items"] = out;
        return req;
    }
    // values: each of its items takes the changed keys from the fresh item of the same generator,
    // in order (the generator's own params or its process slot, wherever the key lives)
    size_t fi = 0;
    for(auto& it : items){
        if(!mine(it)) continue;
        while(fi < fresh.size() && fresh[fi].value("gen", std::string()) != it.value("gen", std::string())) fi++;
        if(fi >= fresh.size()) break;
        const json& f = fresh[fi++];
        for(auto& k : keys){
            for(const char* slot : {"params", "process"}){
                if(f.contains(slot) && f[slot].contains(k)){
                    if(!it.contains(slot)) it[slot] = json::object();
                    it[slot][k] = f[slot][k];
                }else if(it.contains(slot) && it[slot].contains(k) && !(f.contains("params") && f["params"].contains(k)) && !(f.contains("process") && f["process"].contains(k))){
                    it[slot].erase(k);   // the key left the preset: the default again
                }
            }
        }
    }
    req["items"] = items;
    return req;
}

// Instrument mute / solo: what a page is drawn with (its recipe stays whole). The sets hold item refs,
// already expanded with itemRefs (a stack: its own and the presets it plays). Muted wins over soloed;
// items without a preset (legacy methods, the test underlay) stay under mute and go under solo.
struct ItemFilter {
    std::set<std::string> mute, solo;
    bool active() const { return !mute.empty() || !solo.empty(); }
    bool keeps(const json& it) const {
        std::string r = it.is_object() ? it.value("preset", std::string()) : std::string();
        if(!r.empty() && mute.count(r)) return false;
        if(!solo.empty() && (r.empty() || !solo.count(r))) return false;
        return true;
    }
};
inline json filterItems(const json& req, const ItemFilter& f){
    if(!f.active()) return req;
    json out = req, items = json::array();
    for(auto& it : req.value("items", json::array())) if(f.keeps(it)) items.push_back(it);
    out["items"] = items;
    out["itemFilter"] = {{"mute", f.mute}, {"solo", f.solo}};   // the sidecar says what was left out
    return out;
}

// The recipe with its scene's palette and scale as they are now (false: nothing to change)
inline bool patchForScene(Engine& e, json& req, const Scene& s){
    const ScaleDef* sd = e.findScale(s.scale);
    const PaletteDef* pd = e.findPalette(s.palette);
    json before = req;
    req.erase("result");
    req["scale"] = sd ? sd->legacyIndex : -1;
    req["palette"] = pd ? pd->legacyIndex : -1;
    if(sd){ req["scaleName"] = sd->name; req["scaleNotes"] = sd->notes; }
    if(pd){ req["paletteName"] = pd->name; req["paletteColors"] = pd->colors; }
    json b = before; b.erase("result");
    return b != req;
}

} // namespace forms
