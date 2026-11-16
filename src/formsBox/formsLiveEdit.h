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
//    patchSelection  the instruments after an edit of the layer's cells (only what changed changes)
//    filterItems     a page without its muted instruments (or with only the soloed ones)
//  Headless (tests/formsGL/test_live_edit.cpp).
//

#pragma once

#include "formsEngine.h"
#include <set>
#include <map>
#include <algorithm>

namespace forms {

// Watch the preset data rather than a GUI selection: scene editors, nested stack
// editors and the Instruments tab all edit this same map. Submit one preset per
// redraw batch so edits to two instruments cannot overwrite each other's recipes.
class PresetEditTracker {
public:
    bool next(const std::map<std::string, Preset>& presets, bool enabled, bool busy,
              Preset& before, Preset& after){
        if(!initialized){ applied=presets; initialized=true; return false; }
        for(auto it=applied.begin();it!=applied.end();)
            if(!presets.count(it->first)) it=applied.erase(it); else ++it;
        for(auto& [ref,preset]:presets){
            auto result=applied.try_emplace(ref,preset);
            if(result.second) continue;
            auto& old=result.first->second;
            if(old.gen==preset.gen && old.params==preset.params && old.layers==preset.layers &&
               old.pick==preset.pick && old.linked==preset.linked && old.perBar==preset.perBar) continue;
            if(!enabled){ old=preset; continue; }
            if(busy) continue; // retain the previous version until the renderer is free
            before=old; after=preset; old=preset;
            return true;
        }
        return false;
    }
private:
    bool initialized=false;
    std::map<std::string, Preset> applied;
};

// The refs a preset's items carry in a request: its own, and (a stack) those of the presets its
// layers play, at any depth
inline void collectRefs(const Engine& e, const Preset& p, std::set<std::string>& out, int depth = 0){
    out.insert(p.ref());
    if(!p.isStack() || depth > 8) return;
    for(auto& l : p.layers){
        std::string r = l.is_object() ? l.value("preset", std::string()) : std::string();
        if(r.empty() || out.count(r)) continue;
        const Preset* np = e.presetFor(r);   // a scene's own version, when one is set (Engine::OwnScope)
        if(np) collectRefs(e, *np, out, depth + 1);
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

// A page's instruments after an edit of its layer's cells (chances, items, solo flags, rows): only
// what the edit changes changes. Each instrument's chance before and after (itemChances; 0 when no
// eligible cell names it): one the page has stays while its roll, taken as uniform below the old chance, is
// still under the new one; one it lacks comes while its roll, taken above the old chance, is under the
// new one. So raising a chance only adds, lowering only removes, and a small change flips only what
// is near the threshold. The roll is a stable number for (page seed, layer, instrument). Instruments
// that are kept keep their drawn items; added ones are resolved anew (seeded) and drawn on top.
// Frozen layers and the layer's group / page chances are not touched.
inline double stableUnit(unsigned seed, int layer, const std::string& item){
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](unsigned char c){ h ^= c; h *= 1099511628211ull; };
    for(int k = 0; k < 4; k++) mix((unsigned char)(seed >> (8 * k)));
    mix((unsigned char)layer);
    for(unsigned char c : item) mix(c);
    return (double)(h >> 11) * (1.0 / 9007199254740992.0);
}
// an instrument's chance to be on a page: every eligible cell naming it rolls on its own, so it is
// 1 - (1 - p1)(1 - p2)... (in %)
inline std::map<std::string, float> itemChances(const LayerDef& l){
    std::map<std::string, double> miss;
    bool anySolo = std::any_of(l.cells.begin(), l.cells.end(), [](const Cell& c){ return c.solo; });
    int n = std::min<int>(l.rows, (int)l.cells.size());
    for(int i = 0; i < n; i++){
        const Cell& c = l.cells[i];
        if(c.item.empty() || c.item == "none" || (anySolo && !c.solo)) continue;
        double p = std::max(0.0f, std::min(100.0f, c.prob)) / 100.0;
        auto it = miss.find(c.item);
        if(it == miss.end()) miss[c.item] = 1 - p; else it->second *= 1 - p;
    }
    std::map<std::string, float> out;
    for(auto& [k, m] : miss) out[k] = (float)(100.0 * (1 - m));
    return out;
}
inline json patchSelection(Engine& e, const json& recipe, const LayerDef& before, const LayerDef& after, unsigned pageSeed, int layer){
    json req = recipe;
    req.erase("result");
    auto pb = itemChances(before), pa = itemChances(after);
    // A gain edit keeps the same selected instrument, but changes how it is composited.
    // The list shows one row per instrument, so use that instrument's first cell, matching
    // sceneUses() and its displayed gain control.
    std::map<std::string, float> changedGains;
    std::set<std::string> seenGainItems;
    auto cellGains = [](const LayerDef& l){
        std::map<std::string, float> out;
        int n = std::min<int>(l.rows, (int)l.cells.size());
        for(int i = 0; i < n; i++){
            const Cell& c = l.cells[i];
            if(!c.item.empty() && c.item != "none") out.emplace(c.item, c.gain);
        }
        return out;
    };
    auto oldGains = cellGains(before), newGains = cellGains(after);
    for(auto& [item, gain] : newGains){
        if(seenGainItems.insert(item).second && oldGains.count(item) && oldGains[item] != gain)
            changedGains[item] = gain;
    }
    // the refs an item list entry belongs to (a stack: its own and the presets it plays)
    std::map<std::string, std::set<std::string>> refsOf;
    auto owner = [&](const std::string& item) -> const std::set<std::string>& {
        auto f = refsOf.find(item);
        if(f != refsOf.end()) return f->second;
        std::set<std::string> r;
        if(isPresetRef(item)){ auto it = e.presets.find(item); if(it != e.presets.end()) r = itemRefs(e, it->second); else r.insert(item); }
        return refsOf[item] = r;
    };
    auto owns = [&](const json& it, const std::string& item){
        if(!it.is_object() || it.value("layerEffect",false)) return false;
        if(it.contains("method")) return it["method"] == item;
        return owner(item).count(it.value("preset", std::string())) > 0;
    };
    json items = req.value("items", json::array());
    std::set<std::string> all;
    for(auto& [k, v] : pb) all.insert(k);
    for(auto& [k, v] : pa) all.insert(k);
    std::set<std::string> drop;
    std::vector<std::string> add;
    for(auto& item : all){
        float oldP = pb.count(item) ? pb[item] : 0, newP = pa.count(item) ? pa[item] : 0;
        if(oldP == newP) continue;
        bool present = false;
        for(auto& it : items) if(owns(it, item)){ present = true; break; }
        double u = stableUnit(pageSeed, layer, item);
        if(present){ if(!(newP > 0 && u * oldP < newP)) drop.insert(item); }
        else if(newP > oldP && oldP + u * (100 - oldP) < newP) add.push_back(item);
    }
    bool gainChanged = false;
    if(!changedGains.empty()){
        for(auto& it : items){
            if(!it.is_object() || it.value("layerEffect",false)) continue;
            for(auto& [item, gain] : changedGains){
                bool match = it.contains("method") ? it["method"] == item : owner(item).count(it.value("preset", std::string())) > 0;
                if(!match) continue;
                if(gain == 1.0f) it.erase("gain");
                else it["gain"] = gain;
                gainChanged = true;
                break;
            }
        }
        if(gainChanged) req["items"] = items;
    }
    if(drop.empty() && add.empty()) return req;
    json out = json::array();
    for(auto& it : items){
        bool dropped = false;
        for(auto& d : drop) if(owns(it, d)){ dropped = true; break; }
        if(!dropped) out.push_back(it);
    }
    // added ones in cell order, resolved for this page
    std::vector<std::string> ordered;
    for(auto& c : after.cells) if(std::find(add.begin(), add.end(), c.item) != add.end() && std::find(ordered.begin(), ordered.end(), c.item) == ordered.end()) ordered.push_back(c.item);
    e.itemBars = e.layerBars(after);
    for(auto& item : ordered){
        float gain = newGains.count(item) ? newGains[item] : 1.0f;
        if(!isPresetRef(item)){
            json method = {{"method", item}};
            if(gain != 1.0f) method["gain"] = gain;
            out.push_back(std::move(method));
            continue;
        }
        const Preset* pi = e.presetFor(item);
        if(!pi) continue;
        for(auto x : e.presetItemsSeeded(*pi, (unsigned)(stableUnit(pageSeed, layer, item) * 4294967295.0))){
            if(gain != 1.0f) x["gain"] = gain;
            out.push_back(std::move(x));
        }
    }
    json orderedItems=json::array();
    for(auto& it:out)if(!it.value("layerEffect",false))orderedItems.push_back(it);
    for(auto& it:out)if(it.value("layerEffect",false))orderedItems.push_back(it);
    req["items"] = orderedItems;
    return req;
}

// Density (the node's) on a page already drawn (Immediate): the layer's cells with their chances bent
// (densityProb), and the page's instruments at a new density. The page keeps, in its recipe, what it
// was first drawn with ("densityBase": that density and those items); every new value is worked out
// from there with patchSelection, so the result depends only on the value: going down removes
// instruments, going up adds them, and coming back to a value gives the same page again (the first
// density: the page as it was drawn). Any other edit of the page drops the base (it starts again
// from the page as it is then).
inline LayerDef withDensity(LayerDef l, float density){
    for(auto& c : l.cells) c.prob = densityProb(c.prob, density);
    return l;
}
inline json patchDensity(Engine& e, const json& recipe, const LayerDef& layer, float density, unsigned pageSeed, int li){
    json req = recipe;
    req.erase("result");
    json base = req.contains("densityBase") && req["densityBase"].is_object() ? req["densityBase"]
              : json{{"d", req.value("chanceDensity", 0.0f)}, {"items", req.value("items", json::array())}};
    const float d0 = base.value("d", 0.0f);
    json from = req;
    from["items"] = base.value("items", json::array());
    req["items"] = patchSelection(e, from, withDensity(layer, d0), withDensity(layer, density), pageSeed, li)["items"];
    if(density != 0) req["chanceDensity"] = density; else req.erase("chanceDensity");
    if(density != d0) req["densityBase"] = base; else req.erase("densityBase");
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
        if(it.is_object() && it.value("layerEffect",false)) return true;
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

// The recipe with its scene's palette, scale and note grid as they are now (false: nothing to change)
inline bool patchForScene(Engine& e, json& req, const Scene& s){
    const ScaleDef es = e.effectiveScale(s);
    const ScaleDef* sd = es.name.empty() ? nullptr : &es;
    const PaletteDef* pd = e.findPalette(s.palette);
    json before = req;
    req.erase("result");
    req["scale"] = sd ? sd->legacyIndex : -1;
    req["palette"] = pd ? pd->legacyIndex : -1;
    req["notegrid"] = s.notegrid;
    req["bassDegree"] = s.bassDegree;
    req["lineWeight"] = s.lineWeight;
    if(s.hueDeviation > 0) req["hueDeviation"] = s.hueDeviation; else req.erase("hueDeviation");
    if(s.satDeviation > 0) req["satDeviation"] = s.satDeviation; else req.erase("satDeviation");
    if(s.lightDeviation > 0) req["lightDeviation"] = s.lightDeviation; else req.erase("lightDeviation");
    {   // the layer's blocking switch as it is now
        int li = req.value("layer", 1) - 1;
        if(li >= 0 && li < (int)s.layers.size() && !s.layers[li].blocking) req["blocking"] = false; else req.erase("blocking");
    }
    if(sd){ req["scaleName"] = sd->name; req["scaleNotes"] = sd->notes; }
    if(pd){ req["paletteName"] = pd->name; req["paletteColors"] = pd->colors; }
    json b = before; b.erase("result");
    return b != req;
}

} // namespace forms
