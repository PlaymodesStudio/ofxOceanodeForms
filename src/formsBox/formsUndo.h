//
//  formsUndo.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport: undo / redo, compare with saved)
//
//  Undo for the Forms Box editor: snapshots of what the window edits (scenes, instrument presets,
//  setlist, scale and palette library), taken when an edit ends (mouse released, field left), so a
//  slider drag is one step. Undo puts the snapshot back in memory and marks what changed unsaved
//  (Save writes it as before). Scenes or presets that appear or disappear on undo are written or
//  removed on disk at once, since creating and deleting them are disk operations in the editor.
//  Compare with saved: what an item's unsaved edits change against its file.
//  Plain C++ and nlohmann::json (tests/test_v1_backports.cpp).
//

#pragma once

#include "formsEngine.h"
#include <deque>
#include <set>

namespace forms {

struct UndoState {
    std::map<std::string, std::pair<std::string, std::string>> scenes;   // name -> (bank, json dump)
    std::map<std::string, std::string> presets;                          // ref -> json dump
    std::string setlist, scales, palettes;
    bool operator==(const UndoState& o) const { return scenes == o.scenes && presets == o.presets && setlist == o.setlist && scales == o.scales && palettes == o.palettes; }
    bool operator!=(const UndoState& o) const { return !(*this == o); }
};

inline UndoState captureState(const Engine& e){
    UndoState s;
    for(auto& [n, sc] : e.scenes) s.scenes[n] = {sc.bank, json(sc).dump()};
    for(auto& [r, p] : e.presets) s.presets[r] = json(p).dump();
    s.setlist = json(e.setlist).dump();
    json sl = json::array();
    for(auto& d : e.scaleLib){ json j = d; j["source"] = d.source; sl.push_back(j); }   // catalog entries keep their source
    s.scales = sl.dump();
    s.palettes = json(e.paletteLib).dump();
    return s;
}

class UndoStack {
public:
    Engine* engine = nullptr;
    std::set<std::string>* dirtyScenes = nullptr;
    std::set<std::string>* dirtyPresets = nullptr;
    bool* setlistDirty = nullptr;
    bool* libraryDirty = nullptr;
    size_t maxSteps = 100;

    // The state now is the start (after a load or reload): history cleared
    void reset(){ undoStack.clear(); redoStack.clear(); labels.clear(); redoLabels.clear(); current = captureState(*engine); started = true; }

    // An edit may have ended: keep a step if the state changed. busy: a control is held (later)
    bool track(bool busy, const std::string& label = "edit"){
        if(!engine) return false;
        if(!started){ reset(); return false; }
        if(busy) return false;
        UndoState now = captureState(*engine);
        if(now == current) return false;
        undoStack.push_back(std::move(current));
        labels.push_back(label);
        current = std::move(now);
        redoStack.clear(); redoLabels.clear();
        while(undoStack.size() > maxSteps){ undoStack.pop_front(); labels.pop_front(); }
        return true;
    }
    // A named one-shot change (assistant Keep, harmony write...): one step
    void commit(const std::string& label){ track(false, label); }

    bool canUndo() const { return !undoStack.empty(); }
    bool canRedo() const { return !redoStack.empty(); }
    std::string undoLabel() const { return labels.empty() ? "" : labels.back(); }
    std::string redoLabel() const { return redoLabels.empty() ? "" : redoLabels.back(); }

    bool undo(){
        track(false);   // an edit not tracked yet becomes its own step first
        if(undoStack.empty()) return false;
        UndoState to = std::move(undoStack.back()); undoStack.pop_back();
        std::string l = labels.back(); labels.pop_back();
        apply(current, to);
        redoStack.push_back(std::move(current)); redoLabels.push_back(l);
        current = std::move(to);
        return true;
    }
    bool redo(){
        if(redoStack.empty()) return false;
        UndoState to = std::move(redoStack.back()); redoStack.pop_back();
        std::string l = redoLabels.back(); redoLabels.pop_back();
        apply(current, to);
        undoStack.push_back(std::move(current)); labels.push_back(l);
        current = std::move(to);
        return true;
    }

private:
    UndoState current;
    bool started = false;
    std::deque<UndoState> undoStack, redoStack;
    std::deque<std::string> labels, redoLabels;

    void apply(const UndoState& from, const UndoState& to){
        Engine& e = *engine;
        // scenes
        for(auto& [n, v] : from.scenes) if(!to.scenes.count(n)){ e.deleteScene(n); if(dirtyScenes) dirtyScenes->erase(n); }
        for(auto& [n, v] : to.scenes){
            auto f = from.scenes.find(n);
            if(f != from.scenes.end() && f->second == v) continue;
            Scene s;
            try { s = json::parse(v.second).get<Scene>(); } catch(...) { continue; }
            s.name = n; s.bank = v.first;
            bool appears = f == from.scenes.end();
            if(f != from.scenes.end() && f->second.first != v.first && e.scenes.count(n)){
                // moved between banks: the file follows
                e.deleteScene(n);
                appears = true;
            }
            e.scenes[n] = s;
            if(appears) e.saveScene(s);
            else if(dirtyScenes) dirtyScenes->insert(n);
        }
        // presets
        bool presetsChanged = false;
        for(auto& [r, v] : from.presets) if(!to.presets.count(r)){ e.deletePreset(r); if(dirtyPresets) dirtyPresets->erase(r); presetsChanged = true; }
        for(auto& [r, v] : to.presets){
            auto f = from.presets.find(r);
            if(f != from.presets.end() && f->second == v) continue;
            Preset p;
            try { p = json::parse(v).get<Preset>(); } catch(...) { continue; }
            if(f == from.presets.end()) e.savePreset(p);
            else { e.presets[r] = p; if(dirtyPresets) dirtyPresets->insert(r); }
            presetsChanged = true;
        }
        if(presetsChanged) e.refreshPresetMethods();
        if(from.setlist != to.setlist){
            try { e.setlist = json::parse(to.setlist).get<std::vector<SetlistEntry>>(); } catch(...) {}
            if(setlistDirty) *setlistDirty = true;
        }
        if(from.scales != to.scales || from.palettes != to.palettes){
            try {
                json sl = json::parse(to.scales);
                e.scaleLib.clear();
                for(auto& j : sl){ ScaleDef d = j.get<ScaleDef>(); d.source = j.value("source", std::string()); e.scaleLib.push_back(d); }
                e.paletteLib = json::parse(to.palettes).get<std::vector<PaletteDef>>();
            } catch(...) {}
            if(libraryDirty) *libraryDirty = true;
        }
    }
};

// ---------------------------------------------------------------- compare with saved

// One line per difference ("path: saved -> now"), at most `limit`
inline std::vector<std::string> diffLines(const json& saved, const json& now, size_t limit = 40){
    std::vector<std::string> out;
    json patch = json::diff(saved, now);
    for(auto& op : patch){
        if(out.size() >= limit){ out.push_back("..."); break; }
        std::string path = op.value("path", std::string());
        std::string kind = op.value("op", std::string());
        std::string was;
        try { json::json_pointer ptr(path); if(saved.contains(ptr)) was = saved.at(ptr).dump(); } catch(...) {}
        std::string val = op.contains("value") ? op["value"].dump() : "";
        if(was.size() > 60) was = was.substr(0, 57) + "...";
        if(val.size() > 60) val = val.substr(0, 57) + "...";
        if(kind == "replace") out.push_back(path + ":  " + was + "  ->  " + val);
        else if(kind == "add") out.push_back(path + ":  added " + val);
        else if(kind == "remove") out.push_back(path + ":  removed (was " + was + ")");
        else out.push_back(kind + " " + path);
    }
    return out;
}

// What a scene's file holds (null if none)
inline json savedScene(Engine& e, const Scene& s){
    try { return json(readJsonFile(e.scenePath(s)).get<Scene>()); } catch(...) { return json(); }
}
inline json savedPreset(Engine& e, const Preset& p){
    try { return json(Engine::readPresetFile(e.presetPath(p))); } catch(...) { return json(); }
}
inline json savedSetlist(Engine& e){
    try { return json(readJsonFile(e.setlistPath()).value("entries", std::vector<SetlistEntry>())); } catch(...) { return json(); }
}

} // namespace forms
