// Forms GL Box's immediate mode, headless (formsLiveEdit.h): real page requests from every scene;
// for each one, one of its instruments is edited (a numeric parameter moved) and the request patched.
// Checks: only that instrument's items change, only in the edited key; the rest of the page is
// identical; the patched request renders; a structural edit replaces that instrument's items only;
// a scene's palette change reaches the request; every stack: a structural edit replaces all of its
// items (also those of the presets it plays), nothing else.
// Build: clang++ -std=c++17 -O2 -I tests/stub -I src/formsBox -I src/formsGL -I <OF>/libs/json/include
//        tests/formsGL/test_live_edit.cpp -lz
// Run:   ./a.out <renderer folder copy>
#include "formsEngine.h"
#include "formsLiveEdit.h"
#include "formsGLSketch.h"
#include <cstdio>
using namespace forms;

// a numeric param of the preset to move: a plain number, or a {min,max} range
static bool editOne(Preset& p, std::string& key){
    for(auto& [k, v] : p.params.items()){
        if(v.is_number()){ v = v.get<double>() * 0.5 + 1.0; key = k; return true; }
        if(v.is_object() && v.contains("min") && v.contains("max") && v["min"].is_number() && v["max"].is_number()){
            double a = v["min"].get<double>(), b = v["max"].get<double>();
            v["min"] = a + (b - a) * 0.25; v["max"] = a + (b - a) * 0.5 + 0.001; key = k; return true;
        }
    }
    return false;
}

int main(int, char** argv){
    Engine e; e.rendererFolder = argv[1]; e.loadAll(); e.useSetlist = false; e.newSession(77);
    formsgl::Schemas sc; if(!sc.loadEmbedded()){ printf("schemas\n"); return 1; }
    int pages = 0, edited = 0, differs = 0, fails = 0, renders = 0, structural = 0, scenesOk = 0;
    for(auto& [name, s] : e.scenes){
        e.manualScene = name;
        json req = readJsonFile(e.makeRequest(true));
        pages++;
        // the first non-stack instrument of the page that has a numeric parameter
        for(auto& it : req["items"]){
            std::string ref = it.value("preset", std::string());
            auto pi = e.presets.find(ref);
            if(pi == e.presets.end() || pi->second.isStack()) continue;
            Preset before = pi->second, after = before;
            std::string key;
            if(!editOne(after, key)) continue;
            unsigned seed = req.value("seed", 0u) ^ (unsigned)std::hash<std::string>()(ref);
            json patched = patchForPreset(e, req, before, after, seed);
            // compare item by item
            auto& a = req["items"]; auto& b = patched["items"];
            bool ok = a.size() == b.size();
            for(size_t i = 0; ok && i < a.size(); i++){
                if(a[i].value("preset", std::string()) != ref){ if(a[i] != b[i]){ ok = false; printf("  %s: other item %zu changed\n", name.c_str(), i); } continue; }
                for(const char* slot : {"params", "process"}){
                    json x = a[i].value(slot, json::object()), y = b[i].value(slot, json::object());
                    x.erase(key); y.erase(key);
                    if(x != y){ ok = false; printf("  %s: %s %s changed beyond '%s'\n", name.c_str(), ref.c_str(), slot, key.c_str()); }
                }
            }
            json ra = req, rb = patched; ra.erase("items"); rb.erase("items"); ra.erase("result"); rb.erase("result");
            if(ra != rb){ ok = false; printf("  %s: request fields changed\n", name.c_str()); }
            if(!ok){ fails++; break; }
            edited++;
            if(patched["items"] != req["items"]) differs++;
            // it renders (small, for time)
            json small = patched; small["width"] = 480; small["height"] = 270;
            formsgl::substituteUnderlay(small);
            formsgl::Sketch sk(sc);
            auto r = sk.render(small);
            if(r.error.empty()) renders++; else { fails++; printf("  %s: render: %s\n", name.c_str(), r.error.c_str()); }
            // structural: per-bar on -> that instrument's items replaced, others identical
            Preset st = before; st.perBar = before.perBar ? 0 : 2;
            json ps = patchForPreset(e, req, before, st, seed);
            json keepA = json::array(), keepB = json::array();
            for(auto& x : req["items"]) if(x.value("preset", std::string()) != ref) keepA.push_back(x);
            bool hasRef = false;
            for(auto& x : ps["items"]){ if(x.value("preset", std::string()) != ref) keepB.push_back(x); else hasRef = true; }
            if(keepA == keepB && hasRef) structural++; else { fails++; printf("  %s: structural patch touched others\n", name.c_str()); }
            break;
        }
        // scene: another palette -> the request's palette fields follow, the rest stays
        if(e.paletteLib.size() > 1){
            Scene s2 = s;
            for(auto& pd : e.paletteLib) if(pd.name != s.palette){ s2.palette = pd.name; break; }
            json r2 = req;
            bool changed = patchForScene(e, r2, s2);
            const PaletteDef* pd = e.findPalette(s2.palette);
            if(changed && pd && r2["paletteName"] == pd->name && r2["items"] == req["items"]) scenesOk++;
            else { fails++; printf("  %s: scene patch\n", name.c_str()); }
        }
    }
    // stacks (plain layers carry the stack's ref, preset layers the inner preset's): every stack in a
    // request between two other instruments; a structural edit replaces all its items (no leftovers,
    // no duplicates), a value edit keeps their number; the other instruments stay identical
    int stacks = 0, stackOk = 0, stackValue = 0;
    const Preset* other = nullptr;
    for(auto& [r, p] : e.presets) if(!p.isStack() && !e.onlyEffects(p)){ other = &p; break; }
    for(auto& [ref, p] : e.presets){
        if(!p.isStack() || !other) continue;
        stacks++;
        auto refs = itemRefs(e, p);
        json req = readJsonFile(e.makeRequest(true));
        json items = json::array();
        json o1 = e.presetItemsSeeded(*other, 5), mineA = e.presetItemsSeeded(p, 7), o2 = e.presetItemsSeeded(*other, 9);
        for(auto& x : o1) items.push_back(x);
        for(auto& x : mineA) items.push_back(x);
        for(auto& x : o2) items.push_back(x);
        req["items"] = items;
        bool selfOther = refs.count(other->ref()) > 0;
        auto split = [&](const json& rq, json& others, int& n){
            others = json::array(); n = 0;
            for(auto& x : rq["items"]) (refs.count(x.value("preset", std::string())) ? (void)n++ : others.push_back(x));
        };
        Preset st = p; st.perBar = p.perBar ? 0 : 2;
        json ps = patchForPreset(e, req, p, st, 7);
        json othA, othB; int nA = 0, nB = 0;
        split(req, othA, nA); split(ps, othB, nB);
        int nFresh = (int)e.presetItemsSeeded(st, 7).size();
        bool ok = selfOther || (othA == othB && nB == nFresh);
        Preset va = p; std::string key;
        if(editOne(va, key)){
            json pv = patchForPreset(e, req, p, va, 7);
            int nV = 0; json othV; split(pv, othV, nV);
            if(!selfOther && (othV != othA || nV != nA)) ok = false; else stackValue++;
        }
        if(ok) stackOk++; else { fails++; if(fails < 12) printf("  stack %s: others %s, items %d -> %d (fresh %d)\n", ref.c_str(), othA == othB ? "same" : "CHANGED", nA, nB, nFresh); }
    }
    printf("%d stacks: %d ok (%d with a value edit)\n", stacks, stackOk, stackValue);
    // mute / solo (filterItems): a request with two instruments, a stack playing presets, a legacy
    // method item; mute drops exactly the muted refs, solo keeps exactly the soloed ones, muted wins
    {
        int fOk = 0, fAll = 0;
        auto check = [&](bool c, const char* what){ fAll++; if(c) fOk++; else { fails++; printf("  filter: %s\n", what); } };
        const Preset* stack = nullptr;
        for(auto& [r, p] : e.presets) if(p.isStack() && itemRefs(e, p).size() > 1){ stack = &p; break; }
        const Preset *a = nullptr, *b = nullptr;
        for(auto& [r, p] : e.presets) if(!p.isStack()){ if(!a) a = &p; else if(p.gen != a->gen){ b = &p; break; } }
        if(a && b && stack){
            json req = readJsonFile(e.makeRequest(true));
            json items = json::array();
            for(auto& x : e.presetItemsSeeded(*a, 1)) items.push_back(x);
            for(auto& x : e.presetItemsSeeded(*b, 2)) items.push_back(x);
            for(auto& x : e.presetItemsSeeded(*stack, 3)) items.push_back(x);
            items.push_back({{"method", "t3_clouds"}});
            req["items"] = items;
            auto count = [](const json& r, const std::set<std::string>& refs){ int n = 0; for(auto& it : r["items"]) if(refs.count(it.value("preset", std::string()))) n++; return n; };
            auto methods = [](const json& r){ int n = 0; for(auto& it : r["items"]) if(it.contains("method")) n++; return n; };
            auto sRefs = itemRefs(e, *stack);
            int nA = count(req, {a->ref()}), nB = count(req, {b->ref()}), nS = count(req, sRefs);
            ItemFilter none;
            check(filterItems(req, none) == req, "inactive filter changes nothing");
            ItemFilter m; m.mute = {a->ref()};
            json rm = filterItems(req, m);
            check(count(rm, {a->ref()}) == 0 && count(rm, {b->ref()}) == nB && count(rm, sRefs) == nS && methods(rm) == 1, "mute drops only the muted instrument");
            ItemFilter ms; ms.mute = sRefs;
            json rms = filterItems(req, ms);
            check(count(rms, sRefs) == 0 && count(rms, {a->ref()}) == nA, "muting a stack drops the presets it plays too");
            ItemFilter so; so.solo = {b->ref()};
            json rso = filterItems(req, so);
            check(count(rso, {b->ref()}) == nB && (int)rso["items"].size() == nB, "solo keeps only the soloed (no methods)");
            ItemFilter both; both.solo = {a->ref(), b->ref()}; both.mute = {a->ref()};
            json rb = filterItems(req, both);
            check(count(rb, {a->ref()}) == 0 && count(rb, {b->ref()}) == nB, "muted wins over soloed");
            check(rm.contains("itemFilter") && !filterItems(req, none).contains("itemFilter"), "the filter is recorded only when active");
        }else{ fails++; printf("  filter: no stack / instruments to test with\n"); }
        printf("mute / solo: %d / %d checks ok\n", fOk, fAll);
    }
    // cell edits (patchSelection) on every scene's first layer: an untouched layer changes nothing;
    // raising a present instrument's chance keeps the page; lowering one to 0 removes exactly it;
    // raising an absent one to 100 adds it (and nothing else); 100 -> 50 keeps about half
    {
        int sOk = 0, sAll = 0, halfKept = 0, halfOf = 0;
        auto check = [&](bool c, const std::string& what){ sAll++; if(c) sOk++; else { fails++; if(fails < 20) printf("  selection: %s\n", what.c_str()); } };
        for(auto& [name, sc] : e.scenes){
            if(sc.layers.empty() || sc.layers[0].fixed.is_object()) continue;
            e.manualScene = name;
            json req = readJsonFile(e.makeRequest(true));
            unsigned seed = req.value("seed", 0u);
            const LayerDef& L = sc.layers[0];
            check(patchSelection(e, req, L, L, seed, 0)["items"] == req["items"], name + ": no edit, same items");
            auto chances = itemChances(L);
            auto ownsAny = [&](const json& r, const std::string& item){
                std::set<std::string> refs;
                if(isPresetRef(item)){ auto it = e.presets.find(item); if(it != e.presets.end()) refs = itemRefs(e, it->second); }
                for(auto& it : r["items"]) if(it.contains("method") ? it["method"] == item : refs.count(it.value("preset", std::string()))) return true;
                return false;
            };
            for(auto& [item, p] : chances){
                bool has = ownsAny(req, item);
                LayerDef up = L, zero = L, full = L;
                for(auto& c : up.cells) if(c.item == item) c.prob = std::min(100.0f, c.prob + 10);
                for(auto& c : zero.cells) if(c.item == item) c.prob = 0;
                for(auto& c : full.cells) if(c.item == item) c.prob = 100;
                if(has){
                    check(patchSelection(e, req, L, up, seed, 0)["items"] == req["items"], name + " " + item + ": raising a present one keeps the page");
                    json rz = patchSelection(e, req, L, zero, seed, 0);
                    check(!ownsAny(rz, item), name + " " + item + ": 0 removes it");
                    if(p == 100){ LayerDef half = L; for(auto& c : half.cells) if(c.item == item) c.prob = 50; halfOf++; if(ownsAny(patchSelection(e, req, L, half, seed, 0), item)) halfKept++; }
                }else if(p < 100){
                    json rf = patchSelection(e, req, L, full, seed, 0);
                    check(ownsAny(rf, item), name + " " + item + ": 100 adds it");
                    json rest = rf; json keep = json::array();
                    for(auto& it : rf["items"]) if(!ownsAny(json{{"items", json::array({it})}}, item)) keep.push_back(it);
                    check(keep == req["items"], name + " " + item + ": adding leaves the rest as drawn");
                }
            }
            json rl = req; rl["notegrid"] = 1; Scene s2 = sc; s2.notegrid = sc.notegrid + 7;
            patchForScene(e, rl, s2);
            check(rl["notegrid"] == s2.notegrid, name + ": note grid patched");
        }
        printf("cell edits: %d / %d checks ok; 100%% -> 50%% kept %d of %d\n", sOk, sAll, halfKept, halfOf);
    }
    printf("%d pages, %d with an instrument edited (%d where the edited value changed the page, %d rendered, %d structural ok), %d scene patches ok, %d failures\n",
           pages, edited, differs, renders, structural, scenesOk, fails);
    return fails ? 1 : 0;
}
