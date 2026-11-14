//
//  formsEditorMenus.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Right-click menus on parameters: what the cards leave out of sight. A scene field or sound
//  setting: back to its default, an exact value, copy / paste, the same value on every scene, a
//  live fader. An instrument parameter: fixed value, random range, list of options, exact value,
//  copy / paste, back to the default.
//

#pragma once

namespace forms {

// A typed number that applies when the field is left or Enter is pressed. ImGui's InputFloat / InputInt
// assert on ImGuiInputTextFlags_EnterReturnsTrue (1.91+), and without it they return on every keystroke.
// The typed value is kept while the field is active, since the caller's value is rebuilt every frame.
inline bool exactFloatInput(const char* label, float current, float& out, const char* fmt = "%.4f"){
    static ImGuiID activeId = 0; static float typed = 0;
    ImGuiID id = ImGui::GetID(label);
    float f = activeId == id ? typed : current;
    ImGui::InputFloat(label, &f, 0, 0, fmt);
    if(ImGui::IsItemActive()){ activeId = id; typed = f; }
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    if(done) out = activeId == id ? typed : f;
    if(!ImGui::IsItemActive() && activeId == id) activeId = 0;
    return done;
}
inline bool exactIntInput(const char* label, int current, int& out){
    static ImGuiID activeId = 0; static int typed = 0;
    ImGuiID id = ImGui::GetID(label);
    int i = activeId == id ? typed : current;
    ImGui::InputInt(label, &i, 0, 0);
    if(ImGui::IsItemActive()){ activeId = id; typed = i; }
    bool done = ImGui::IsItemDeactivatedAfterEdit();
    if(done) out = activeId == id ? typed : i;
    if(!ImGui::IsItemActive() && activeId == id) activeId = 0;
    return done;
}

// A scene field (BPM, root, bass degree, palette, scale...): value and default as JSON, `set`
// writes a value into a scene. Call right after the field's control.
inline void Editor::sceneFieldMenu(Scene& s, const char* id, const json& value, const json& def,
                                   const std::function<void(Scene&, const json&)>& set, bool& changed){
    if(!ImGui::BeginPopupContextItem(id)) return;
    ImGui::TextDisabled("%s", id);
    ImGui::Separator();
    if(ImGui::MenuItem(("Reset to default (" + def.dump() + ")").c_str())){ set(s, def); changed = true; }
    if(ImGui::MenuItem("Copy value")){ clipValue = value; clipKey = id; }
    ImGui::BeginDisabled(clipKey != id);
    if(ImGui::MenuItem(("Paste" + (clipKey == id ? " " + clipValue.dump() : std::string())).c_str())){ set(s, clipValue); changed = true; }
    ImGui::EndDisabled();
    ImGui::Separator();
    if(ImGui::MenuItem("Apply to every scene")){
        for(auto& [n, sc] : engine->scenes){ set(sc, value); dirty.insert(n); }
        changed = true;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Every scene gets this value (each one marked as edited; save them in Compose)");
    if(!engine->setlist.empty() && ImGui::MenuItem("Apply to the setlist's scenes")){
        for(auto& e : engine->setlist){ auto it = engine->scenes.find(e.scene); if(it != engine->scenes.end()){ set(it->second, value); dirty.insert(e.scene); } }
        changed = true;
    }
    ImGui::EndPopup();
}

// A performance setting (the scene's sound): v is the scene's value for p.key
inline void Editor::perfParamMenu(Scene& s, const PerfParam& p, json& v, bool& changed){
    if(!ImGui::BeginPopupContextItem("##perfMenu")) return;
    float zz = z();
    ImGui::TextDisabled("%s  (%s)", p.label, p.key);
    ImGui::Separator();
    if(ImGui::MenuItem(("Reset to default (" + p.def.dump() + ")").c_str())){ v = p.def; changed = true; }
    if(p.type == 'f' || p.type == 'i'){
        ImGui::SetNextItemWidth(120 * zz);
        if(p.type == 'f'){
            float f = v.is_number() ? v.get<float>() : p.def.get<float>();
            if(exactFloatInput("exact value", f, f)){ v = std::max(p.min, std::min(p.max, f)); changed = true; }
        }else{
            int i = v.is_number() ? (int)std::lround(v.get<float>()) : p.def.get<int>();
            if(exactIntInput("exact value", i, i)){ v = std::max((int)p.min, std::min((int)p.max, i)); changed = true; }
        }
    }
    if(ImGui::MenuItem("Copy value")){ clipValue = v; clipKey = p.key; }
    ImGui::BeginDisabled(clipKey != p.key);
    if(ImGui::MenuItem(("Paste" + (clipKey == p.key ? " " + clipValue.dump() : std::string())).c_str())){ v = clipValue; changed = true; }
    ImGui::EndDisabled();
    ImGui::Separator();
    if(ImGui::MenuItem("Apply to every scene")){
        for(auto& [n, sc] : engine->scenes){ sc.performance[p.key] = v; dirty.insert(n); }
        changed = true;
    }
    if(!engine->setlist.empty() && ImGui::MenuItem("Apply to the setlist's scenes")){
        for(auto& e : engine->setlist){ auto it = engine->scenes.find(e.scene); if(it != engine->scenes.end()){ it->second.performance[p.key] = v; dirty.insert(e.scene); } }
        changed = true;
    }
    if(p.output && p.type == 'f'){
        ImGui::Separator();
        if(ImGui::BeginMenu("Drive with a live fader")){
            for(int i = 0; i < 8; i++){
                auto& f = perform.faders[i];
                const PerfParam* cur = PerformState::param(f.key);
                std::string lab = "Fader " + ofToString(i + 1) + "  (" + (cur ? cur->label : f.key.c_str()) + ")";
                if(ImGui::MenuItem(lab.c_str(), nullptr, f.key == p.key)) f.key = p.key;
            }
            ImGui::EndMenu();
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The fader (Stage) multiplies this setting while playing, in every scene");
    }
    ImGui::EndPopup();
}

// An instrument parameter (a preset's params): v is the current value (number, {min,max}, {options},
// string, bool, array); erase = back to the schema default
inline void Editor::instrumentParamMenu(const ParamDef& p, json& v, bool& changed, bool& erase){
    if(!ImGui::BeginPopupContextItem("##paramMenu")) return;
    float zz = z();
    ImGui::TextDisabled("%s  (%s)", p.label.c_str(), p.key.c_str());
    ImGui::Separator();
    if(ImGui::MenuItem(("Back to the default (" + p.def.dump() + ")").c_str())) erase = true;
    if(p.isNumber()){
        bool fixed = v.is_number(), range = v.is_object() && v.contains("min"), list = v.is_object() && v.contains("options");
        float x = fixed ? v.get<float>() : range ? v.value("min", p.min) : list && !v["options"].empty() && v["options"][0].is_number() ? v["options"][0].get<float>() : (p.def.is_number() ? p.def.get<float>() : p.min);
        if(ImGui::MenuItem("Fixed value (the same in every image)", nullptr, fixed) && !fixed){ v = p.type == "int" ? json((int)std::lround(x)) : json(x); changed = true; }
        if(ImGui::MenuItem("Random range (a new value per image)", nullptr, range) && !range){
            float span = (p.max - p.min) * 0.1f;
            if(p.type == "int") span = std::max(1.0f, std::round(span));
            v = json{{"min", x}, {"max", std::min(p.max, x + span)}}; changed = true;
        }
        if(ImGui::MenuItem("List of options (one of them per image)", nullptr, list) && !list){ v = json{{"options", json::array({p.type == "int" ? json((int)std::lround(x)) : json(x)})}}; changed = true; }
        ImGui::SetNextItemWidth(120 * zz);
        if(p.type == "int"){
            int i = (int)std::lround(x);
            if(exactIntInput(fixed ? "exact value" : "exact value (makes it fixed)", i, i)){ v = std::max((int)p.min, std::min((int)p.max, i)); changed = true; }
        }else{
            float f = x;
            if(exactFloatInput(fixed ? "exact value" : "exact value (makes it fixed)", f, f)){ v = std::max(p.min, std::min(p.max, f)); changed = true; }
        }
    }else if(p.type == "enum"){
        bool all = v.is_object() && v.contains("options") && v["options"].size() == p.options.size();
        if(ImGui::MenuItem("Any option (one per image)", nullptr, all) && !all){ v = json{{"options", p.options}}; changed = true; }
        if(ImGui::BeginMenu("Only one")){
            for(auto& o : p.options) if(ImGui::MenuItem(o.c_str(), nullptr, v.is_string() && v == o)){ v = o; changed = true; }
            ImGui::EndMenu();
        }
    }else if(p.type == "bool"){
        bool either = v.is_object();
        if(ImGui::MenuItem("Either (per image)", nullptr, either) && !either){ v = json{{"either", true}}; changed = true; }
    }
    ImGui::Separator();
    if(ImGui::MenuItem("Copy value")){ clipValue = v; clipKey = p.key; }
    ImGui::BeginDisabled(clipKey != p.key);
    if(ImGui::MenuItem(("Paste" + (clipKey == p.key ? " " + clipValue.dump() : std::string())).c_str())){ v = clipValue; changed = true; }
    ImGui::EndDisabled();
    ImGui::EndPopup();
}

// ---- the instrument menu (see Editor::instrumentMenu)
inline void Editor::instrumentMenuItems(const std::string& ref, const std::string& scene, int layer, int cell){
    bool isPreset = engine->presets.count(ref) > 0;
    auto sit = engine->scenes.find(scene);
    bool inCell = sit != engine->scenes.end() && layer >= 0 && layer < (int)sit->second.layers.size() && cell >= 0;
    std::string title = shortName(ref) + (inCell ? "  ·  " + scene + ", layer " + ofToString(layer + 1) : std::string());
    ImGui::TextDisabled("%s", title.c_str());
    ImGui::Separator();
    if(instMS && toggleInstMS){
        int st = instMS(ref);
        if(ImGui::MenuItem("Mute", nullptr, (st & 1) != 0)) toggleInstMS(ref, 0);
        if(ImGui::MenuItem("Solo", nullptr, (st & 2) != 0)) toggleInstMS(ref, 1);
    }
    if(isPreset && ImGui::MenuItem("Edit instrument")){ selPreset = ref; composeMode = 1; showTab = "Compose"; }
    ImGui::Separator();
    if(inCell){
        const Scene& s = sit->second;
        for(int k = 0; k < (int)s.layers.size(); k++){
            if(k == layer) continue;
            std::string lab = "Move to layer " + ofToString(k + 1) + " (" + barsText(s.layers[k].bars) + " bars)";
            if(ImGui::MenuItem(lab.c_str())) instAction = {1, scene, ref, layer, cell, k};
        }
        if(ImGui::MenuItem("Move to a new layer")) instAction = {1, scene, ref, layer, cell, (int)s.layers.size()};
        ImGui::Separator();
        if(ImGui::MenuItem("Remove from this layer")) instAction = {3, scene, ref, layer, cell, -1};
    }else{
        // from the library: into the scene being edited
        auto to = engine->scenes.find(selected);
        if(to == engine->scenes.end()) ImGui::TextDisabled("(no scene selected to send it to)");
        else if(ImGui::BeginMenu(("Send to " + selected).c_str())){
            const Scene& s = to->second;
            for(int k = 0; k < (int)s.layers.size(); k++){
                std::string lab = "Layer " + ofToString(k + 1) + " (" + barsText(s.layers[k].bars) + " bars)";
                if(ImGui::MenuItem(lab.c_str())) instAction = {2, selected, ref, -1, -1, k};
            }
            if(ImGui::MenuItem("A new layer")) instAction = {2, selected, ref, -1, -1, (int)s.layers.size()};
            ImGui::EndMenu();
        }
    }
    if(isPreset){
        if(!inCell) ImGui::Separator();
        if(ImGui::MenuItem("Delete instrument from the library...")) instDeleteAsk = ref;
    }
}

inline bool Editor::instrumentMenu(const char* id, const std::string& ref, const std::string& scene, int layer, int cell){
    if(ref.empty()) return false;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);   // a dimmed (off) row still opens a readable menu
    bool open = ImGui::BeginPopupContextItem(id);
    if(open){ instrumentMenuItems(ref, scene, layer, cell); ImGui::EndPopup(); }
    ImGui::PopStyleVar();
    return open;
}

inline void Editor::applyInstAction(){
    InstAction a = instAction;
    instAction = InstAction();
    if(a.kind == 0) return;
    auto sit = engine->scenes.find(a.scene);
    if(sit == engine->scenes.end()) return;
    Scene& s = sit->second;
    if(a.kind == 1){
        // only if the cell still holds that instrument (the list may have changed since the click)
        if(a.layer < 0 || a.layer >= (int)s.layers.size() || a.cell < 0 || a.cell >= (int)s.layers[a.layer].cells.size() || s.layers[a.layer].cells[a.cell].item != a.ref) return;
        if(moveSceneCellToLayer(a.scene, a.layer, a.cell, a.target)){ dirty.insert(a.scene); engine->note("moved " + a.ref + " to layer " + ofToString(a.target + 1) + " of " + a.scene); }
    }else if(a.kind == 2){
        if(a.target < 0 || a.target > (int)s.layers.size()) return;
        if(a.target == (int)s.layers.size()){
            LayerDef nl; nl.bars = 8; nl.blend = "lighten"; nl.gain = 1; nl.rows = 96;
            if(!s.layers.empty()) nl.groupProb = s.layers[0].groupProb;
            s.layers.push_back(nl);
        }
        if(engine->addItemToScene(s, a.target, a.ref)){ dirty.insert(a.scene); engine->note("added " + a.ref + " to " + a.scene + ", layer " + ofToString(a.target + 1)); }
    }else if(a.kind == 3){
        if(a.layer < 0 || a.layer >= (int)s.layers.size() || a.cell < 0 || a.cell >= (int)s.layers[a.layer].cells.size() || s.layers[a.layer].cells[a.cell].item != a.ref) return;
        s.layers[a.layer].cells[a.cell].item.clear();
        s.layers[a.layer].cells[a.cell].prob = 0;
        dirty.insert(a.scene);
        engine->note("removed " + a.ref + " from " + a.scene + ", layer " + ofToString(a.layer + 1));
    }
}

// Deleting a preset removes its file: always asked, with what still uses it
inline void Editor::drawInstDeleteModal(){
    if(!instDeleteAsk.empty() && !ImGui::IsPopupOpen("Delete instrument?")) ImGui::OpenPopup("Delete instrument?");
    if(!ImGui::BeginPopupModal("Delete instrument?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) return;
    std::string ref = instDeleteAsk;
    bool close = false;
    if(!engine->presets.count(ref)) close = true;
    else{
        ImGui::Text("Delete %s from the library?", ref.c_str());
        ImGui::TextDisabled("Its preset file is removed. This cannot be undone.");
        int used = engine->scenesUsing(ref), inStacks = engine->stacksUsing(ref);
        if(used) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%d scenes use it; their cells will render nothing.", used);
        if(inStacks) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%d stacks play it as a layer; those layers will be skipped.", inStacks);
        if(ImGui::Button("Delete")){
            engine->deletePreset(ref);
            dirtyPresets.erase(ref);
            std::error_code ec; fs::remove(thumbs.dir / (ref + ".png"), ec);
            thumbs.forget(ref);
            if(selPreset == ref) selPreset.clear();
            if(sceneInstrumentEditRef == ref) sceneInstrumentEditRef.clear();
            close = true;
        }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) close = true;
    }
    if(close){ instDeleteAsk.clear(); ImGui::CloseCurrentPopup(); }
    ImGui::EndPopup();
}

} // namespace forms
