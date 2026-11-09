//
//  formsEditorInstruments.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Methods browser and the Instruments tab: preset list, stack layers, linked picks, parameter tabs and previews.
//  Methods of forms::Editor (declared in formsEditor.h, which includes this file).
//

#pragma once

namespace forms {

inline void Editor::drawMethodsTab(){
    ImGui::TextDisabled("Every method the renderer offers. Click one to add it to \"%s\".", selected.c_str());
    drawBrowser([this](const std::string& m){ addMethodToScene(m); });
}

inline void Editor::drawPickerPopup(){
    if(openPicker){ ImGui::OpenPopup("Pick an instrument"); openPicker = false; }
    float zz = z();
    ImGui::SetNextWindowSize(ImVec2(900 * zz, 640 * zz), ImGuiCond_Appearing);
    if(ImGui::BeginPopupModal("Pick an instrument", nullptr)){
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::SameLine();
        ImGui::TextDisabled("click a thumbnail");
        drawBrowser([this](const std::string& m){ if(onPick) onPick(m); ImGui::CloseCurrentPopup(); });
        ImGui::EndPopup();
    }
}

inline void Editor::drawBrowser(const std::function<void(const std::string&)>& pick){
    float zz = z();
    ImGui::SetNextItemWidth(220 * zz);
    ImGui::InputTextWithHint("##search", "search", search, sizeof(search));
    // the Processing families (harmony split into its roles, the geometric textures apart), then the
    // other lists; legacy methods go by their prefix, presets by their category
    static const char* fams[] = {"All", "Harmony", "Bass", "Melody", "Chords", "Pads", "Arps", "Rhythm", "Texture", "Geometry", "FX", "Not in any scene", "Instruments"};
    static const char* famCat[] = {"", "h", "bass", "melody", "chords", "pads", "arps", "rhythm", "texture", "geometry", "effects", "", ""};
    const int nFams = 13, FILTER_UNUSED = 11, FILTER_PRESETS = 12;
    for(int i = 0; i < nFams; i++){
        if(i != 7 || true) ImGui::SameLine();
        if(i == 2 || i == 7 || i == 11){ ImGui::TextDisabled("|"); ImGui::SameLine(); }
        if(ImGui::RadioButton(fams[i], familyFilter == i)) familyFilter = i;
    }
    ImGui::SetNextItemWidth(110 * zz);
    ImGui::SliderFloat("##size", &thumbSize, 90, 300, "size %.0f");
    if(!thumbs.any() && !engine->methods.empty() && !fs::exists(thumbs.dir))
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "No thumbnails yet: System → Make thumbnails (a few seconds).");

    ImGui::BeginChild("##grid", ImVec2(0, 0));
    float w = thumbSize * zz, h = w * 9 / 16;
    int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (w + 8 * zz)));
    std::string q = ofToLower(search);
    int n = 0;
    for(auto& [name, fam] : engine->methods){
        if(familyFilter >= 1 && familyFilter <= 10){
            std::string want = famCat[familyFilter];
            if(isPresetRef(name)){
                std::string cat = fam.rfind("cat/", 0) == 0 ? fam.substr(4) : std::string();
                bool ok = familyFilter == 1 ? categoryFamily(cat) == "h" : cat == want;
                if(!ok) continue;
            }else{
                std::string letter = name.rfind("fx", 0) == 0 ? "fx" : name.substr(0, 1);
                std::string wantLetter = familyFilter == 1 ? "h" : categoryFamily(want);
                if(familyFilter >= 2 && familyFilter <= 6) continue;   // a legacy method has no role within harmony
                if(familyFilter == 9) continue;                        // nor is it geometry
                if(letter != wantLetter) continue;
            }
        }
        if(familyFilter == FILTER_UNUSED && engine->scenesUsing(name) > 0) continue;
        if(familyFilter == FILTER_PRESETS && !isPresetRef(name)) continue;
        if(!q.empty() && ofToLower(name).find(q) == std::string::npos) continue;
        if(n % perRow) ImGui::SameLine();
        n++;
        ImGui::PushID(name.c_str());
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        // only load thumbnails that are on screen
        bool visible = ImGui::IsRectVisible(ImVec2(w, h));
        const ofTexture* t = visible ? thumbs.get(name) : nullptr;
        bool clicked = t ? ImGui::ImageButton("##m", texId(*t), ImVec2(w, h)) : ImGui::Button("##m", ImVec2(w, h));
        if(ImGui::IsItemHovered()) methodTooltip(name);
        ImGui::GetWindowDrawList()->AddRectFilled(p, ImVec2(p.x + 4 * zz, p.y + h), ImGui::GetColorU32(familyColor(fam)));
        ImGui::TextColored(familyColor(fam), "%s", shortName(name).c_str());
        ImGui::EndGroup();
        ImGui::PopID();
        if(clicked) pick(name);
    }
    if(!n) ImGui::TextDisabled("Nothing matches.");
    ImGui::EndChild();
}

// A scene's instruments per layer (cells with a chance, once each), and under a stack the presets it
// plays (depth 1, 2...)
inline std::vector<Editor::SceneUse> Editor::sceneUses(const std::string& sceneName, bool includeOff) const {
    std::vector<SceneUse> uses;
    auto sit = engine->scenes.find(sceneName);
    if(sit == engine->scenes.end()) return uses;
    std::function<void(const std::string&, int, float, int, int)> add = [&](const std::string& r, int l, float pb, int d, int cell){
        auto pi = engine->presets.find(r);
        if(pi == engine->presets.end() || d > 4) return;
        uses.push_back({r, l, pb, d, d ? -1 : cell});
        if(pi->second.isStack())
            for(auto& ly : pi->second.layers){
                std::string inner = ly.is_object() ? ly.value("preset", std::string()) : std::string();
                if(!inner.empty()) add(inner, l, pb, d + 1, cell);
            }
    };
    auto& L = sit->second.layers;
    for(int l = 0; l < (int)L.size(); l++){
        std::set<std::string> seen;
        for(int c = 0; c < (int)L[l].cells.size() && c < L[l].rows; c++){
            auto& cell = L[l].cells[c];
            if(cell.item.empty() || cell.item == "none" || (cell.prob <= 0 && !includeOff) || !seen.insert(cell.item).second) continue;
            add(cell.item, l, cell.prob, 0, c);
        }
    }
    return uses;
}

inline bool Editor::cellChanceDrag(const std::string& sceneName, const SceneUse& u, float width){
    auto sit = engine->scenes.find(sceneName);
    if(u.depth || u.cell < 0 || sit == engine->scenes.end() || u.layer >= (int)sit->second.layers.size()) return false;
    auto& cells = sit->second.layers[u.layer].cells;
    if(u.cell >= (int)cells.size()) return false;
    float& prob = cells[u.cell].prob;
    ImGui::SetNextItemWidth(width);
    bool ch = ImGui::DragFloat("##chance", &prob, 0.5f, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Chance per image (page) of this instrument in %s, layer %d.\n"
                                                 "Drag, or double-click to type. Next images; the scene is marked unsaved.", sceneName.c_str(), u.layer + 1);
    if(ch) dirty.insert(sceneName);
    return ch;
}

inline bool Editor::cellGainDrag(const std::string& sceneName, const SceneUse& u, float width){
    auto sit = engine->scenes.find(sceneName);
    if(u.depth || u.cell < 0 || sit == engine->scenes.end() || u.layer >= (int)sit->second.layers.size()) return false;
    auto& cells = sit->second.layers[u.layer].cells;
    if(u.cell >= (int)cells.size()) return false;
    float pct = cells[u.cell].gain * 100;
    ImGui::SetNextItemWidth(width);
    bool ch = ImGui::DragFloat("##gain", &pct, 0.5f, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Volume / opacity of this instrument in %s (100%% = as the preset draws it).\n"
                                                 "Drag, or double-click to type. Next images; the scene is marked unsaved.", sceneName.c_str());
    if(ch){ cells[u.cell].gain = std::clamp(pct / 100.f, 0.f, 1.f); dirty.insert(sceneName); }
    return ch;
}

inline bool Editor::moveSceneCell(const std::string& sceneName, int layer, int from, int to){
    auto sit = engine->scenes.find(sceneName);
    if(sit == engine->scenes.end() || layer < 0 || layer >= (int)sit->second.layers.size()) return false;
    auto& cells = sit->second.layers[layer].cells;
    if(from < 0 || to < 0 || from >= (int)cells.size() || to >= (int)cells.size() || from == to) return false;
    Cell c = cells[from];
    cells.erase(cells.begin() + from);
    cells.insert(cells.begin() + to, c);
    dirty.insert(sceneName);
    return true;
}

inline bool Editor::moveSceneCellToLayer(const std::string& sceneName, int layer, int cell, int target){
    auto sit = engine->scenes.find(sceneName);
    if(sit == engine->scenes.end()) return false;
    Scene& s = sit->second;
    if(layer < 0 || layer >= (int)s.layers.size() || target < 0 || target > (int)s.layers.size() || target == layer) return false;
    if(cell < 0 || cell >= (int)s.layers[layer].cells.size()) return false;
    Cell moved = s.layers[layer].cells[cell];
    if(moved.item.empty()) return false;
    if(target == (int)s.layers.size()){
        LayerDef next;
        next.groupProb = s.layers[layer].groupProb;
        s.layers.push_back(next);
    }
    LayerDef& dest = s.layers[target];
    if(moved.group >= 0 && moved.group >= (int)dest.groupProb.size()) dest.groupProb.resize(moved.group + 1, 1.0f);
    int slot = -1;
    for(int i = 0; i < std::min<int>(dest.rows, (int)dest.cells.size()); ++i)
        if(dest.cells[i].item.empty()){ slot = i; break; }
    if(slot >= 0) dest.cells[slot] = moved;
    else {
        dest.cells.insert(dest.cells.begin() + std::min<int>(dest.rows, (int)dest.cells.size()), moved);
        if((int)dest.cells.size() > dest.rows) dest.rows++;
    }
    s.layers[layer].cells[cell].item.clear();
    s.layers[layer].cells[cell].prob = 0;
    dirty.insert(sceneName);
    return true;
}

inline void Editor::drawInstrumentsTab(){
    float zz = z();
    pollPreview();
    if(engine->gens.empty()){
        ImGui::TextDisabled("No parameterised generators: generators.json is missing or unreadable in the data folder (System → Reload scenes and library).");
        return;
    }
    if(!selPreset.empty() && !engine->presets.count(selPreset)) selPreset.clear();
    if(selPreset.empty() && !engine->presets.empty()) selPreset = engine->presets.begin()->first;
    if(previewScene.empty() || !engine->scenes.count(previewScene)) previewScene = selected;

    // the scene playing (under the header), else the one selected in Scenes; its instruments per
    // layer (cells with a chance), and the presets a stack of it plays
    std::string playing;
    if(engine->schedule && !engine->schedule->layers.empty() && engine->schedule->layers[0].header)
        playing = engine->schedule->layers[0].header->scene;
    bool isPlaying = !playing.empty() && engine->scenes.count(playing);
    std::string sceneHere = isPlaying ? playing : selected;
    std::vector<SceneUse> uses = sceneUses(sceneHere, true);
    std::set<std::string> inScene;
    for(auto& u : uses) if(u.prob > 0 || u.depth) inScene.insert(u.ref);
    // opening the tab, or the playing scene changing: select one of its instruments (unless the
    // selection is already one of them)
    bool opened = ImGui::GetFrameCount() - instrumentsFrame > 1;
    instrumentsFrame = ImGui::GetFrameCount();
    if((opened || sceneHere != playingFor) && !inScene.empty() && !inScene.count(selPreset)){
        for(auto& u : uses) if(inScene.count(u.ref)){ selPreset = u.ref; break; }
    }
    playingFor = sceneHere;

    // --- list: the scene's instruments, then one foldable section per generator (and Stack)
    ImGui::BeginChild("##presetList", ImVec2(instListW * zz, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_ResizeX);
    instListW = std::max(200.f, ImGui::GetWindowWidth() / std::max(0.01f, zz));   // dragged by its right border
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##pfilter", "filter presets", presetFilter, sizeof(presetFilter));
    std::string q = ofToLower(presetFilter);
    if(!sceneHere.empty()){
        ImGui::SetNextItemOpen(true, ImGuiCond_FirstUseEver);
        bool sceneDirty = dirty.count(sceneHere) > 0;
        std::string head = std::string(isPlaying ? "Playing: " : "Scene: ") + sceneHere + (sceneDirty ? " *" : "") + "  (" + std::to_string(inScene.size()) + ")###sceneInstruments";
        bool sceneOpen = ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding | ImGuiTreeNodeFlags_AllowOverlap);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? "The instruments of the scene under the header; right: chance per image and volume; drag a name to reorder; right-click a name to move to another layer" : "Nothing playing: the scene selected in Scenes; right: chance per image and volume; drag a name to reorder; right-click a name to move to another layer");
        if(sceneDirty){
            ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - 40 * zz));
            if(ImGui::SmallButton("Save##sceneChances")){ auto sit2 = engine->scenes.find(sceneHere); if(sit2 != engine->scenes.end()){ engine->saveScene(sit2->second); dirty.erase(sceneHere); } }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Save the scene (its changed chances, volumes, order)");
        }
        if(sceneOpen){
            int lastLayer = -1;
            auto sit = engine->scenes.find(sceneHere);
            bool multi = sit != engine->scenes.end() && sit->second.layers.size() > 1;
            float dragW = 50 * zz, gap = ImGui::GetStyle().ItemSpacing.x;
            {   // column titles over the controls
                ImGui::TextDisabled("arrows or drag to reorder");
                ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - 2 * dragW - gap));
                ImGui::TextDisabled("chance"); ImGui::SameLine(0, 0);
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - dragW));
                ImGui::TextDisabled("volume");
            }
            struct MovePayload { int layer, cell; };
            int moveLayer = -1, moveFrom = -1, moveTo = -1;
            int transferLayer = -1, transferCell = -1, transferTarget = -1;
            for(size_t i = 0; i < uses.size(); i++){
                auto& u = uses[i];
                if(!q.empty() && ofToLower(u.ref).find(q) == std::string::npos) continue;
                if(multi && u.layer != lastLayer){ ImGui::TextDisabled("Layer %d", u.layer + 1); lastLayer = u.layer; }
                auto& p = engine->presets[u.ref];
                ImGui::PushID((int)i);
                instMSButtons(u.ref);
                ImGui::SameLine();
                if(!u.depth && u.cell >= 0 && sit != engine->scenes.end() && u.layer < (int)sit->second.layers.size()){
                    auto& cells = sit->second.layers[u.layer].cells;
                    int prev = -1, next = -1;
                    for(int c = u.cell - 1; c >= 0; c--) if(!cells[c].item.empty() && cells[c].item != "none"){ prev = c; break; }
                    for(int c = u.cell + 1; c < (int)cells.size(); c++) if(!cells[c].item.empty() && cells[c].item != "none"){ next = c; break; }
                    ImGui::BeginDisabled(prev < 0);
                    if(ImGui::SmallButton("^")){ moveLayer = u.layer; moveFrom = u.cell; moveTo = prev; }
                    ImGui::EndDisabled(); ImGui::SameLine(0, 1 * zz);
                    ImGui::BeginDisabled(next < 0);
                    if(ImGui::SmallButton("v")){ moveLayer = u.layer; moveFrom = u.cell; moveTo = next; }
                    ImGui::EndDisabled(); ImGui::SameLine();
                }
                bool off = !u.depth && u.prob <= 0;
                if(off) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
                ImGui::PushStyleColor(ImGuiCol_Text, genColour(p.gen));
                std::string label = (u.depth ? std::string((size_t)u.depth * 2, ' ') + "- " : std::string()) + p.name + (dirtyPresets.count(u.ref) ? "  *" : "") + "##use" + std::to_string(i);
                theme::badge(p.gen);
                float selW = u.depth ? 0.f : std::max(20.f, ImGui::GetContentRegionAvail().x - 2 * dragW - 2 * gap - 38 * zz);
                if(ImGui::Selectable(label.c_str(), selPreset == u.ref, ImGuiSelectableFlags_AllowOverlap, ImVec2(selW, 0))) selPreset = u.ref;
                ImGui::PopStyleColor();
                if(!u.depth && u.cell >= 0 && sit != engine->scenes.end() && ImGui::BeginPopupContextItem("##instrumentLayerMenu")){
                    ImGui::TextDisabled("%s · Layer %d", p.name.c_str(), u.layer + 1);
                    ImGui::Separator();
                    for(int k = 0; k < (int)sit->second.layers.size(); ++k){
                        if(k == u.layer) continue;
                        std::string text = "Move to layer " + std::to_string(k + 1) + " (" + std::to_string(sit->second.layers[k].bars) + " bars)";
                        if(ImGui::MenuItem(text.c_str())){ transferLayer = u.layer; transferCell = u.cell; transferTarget = k; }
                    }
                    if(ImGui::MenuItem("Move to a new layer")){
                        transferLayer = u.layer; transferCell = u.cell; transferTarget = (int)sit->second.layers.size();
                    }
                    ImGui::EndPopup();
                }
                if(ImGui::IsItemHovered() && !ImGui::IsMouseDragging(0)) methodTooltip(u.ref);
                if(!u.depth && u.cell >= 0){   // drag the name onto another instrument of its layer: drawing order
                    if(ImGui::BeginDragDropSource()){
                        MovePayload mp{u.layer, u.cell};
                        ImGui::SetDragDropPayload("FORMS_CELL_MOVE", &mp, sizeof mp);
                        ImGui::Text("Move %s", p.name.c_str());
                        ImGui::EndDragDropSource();
                    }
                    if(ImGui::BeginDragDropTarget()){
                        if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FORMS_CELL_MOVE")){
                            MovePayload mp = *(const MovePayload*)pl->Data;
                            if(mp.layer == u.layer){ moveLayer = u.layer; moveFrom = mp.cell; moveTo = u.cell; }
                        }
                        ImGui::EndDragDropTarget();
                    }
                }
                if(!u.depth){ ImGui::SameLine(); cellChanceDrag(sceneHere, u, dragW); ImGui::SameLine(); cellGainDrag(sceneHere, u, dragW); }
                if(off) ImGui::PopStyleVar();
                ImGui::PopID();
            }
            if(uses.empty()) ImGui::TextDisabled("no instruments (legacy methods only)");
            if(moveFrom >= 0) moveSceneCell(sceneHere, moveLayer, moveFrom, moveTo);   // after the rows (they index the cells)
            if(transferCell >= 0) moveSceneCellToLayer(sceneHere, transferLayer, transferCell, transferTarget);
            ImGui::TreePop();
        }
        ImGui::Separator();
    }
    std::vector<std::pair<std::string, std::string>> sections;   // gen, label
    for(auto& g : engine->gens) sections.push_back({g.name, g.label});
    sections.push_back({"stack", "Stack"});
    for(auto& [gname, glabel] : sections){
        int count = 0;
        for(auto& [r, p] : engine->presets) if(p.gen == gname) count++;
        ImGui::PushID(gname.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, genColour(gname));
        bool selHere = engine->presets.count(selPreset) && engine->presets[selPreset].gen == gname;
        if(!q.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        else if(selHere && justSelected) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
        theme::badge(gname);
        bool open = ImGui::TreeNodeEx("##sec", ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding, "%s  (%d)", glabel.c_str(), count);
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered()){
            const GenDef* gd = engine->findGen(gname);
            std::string d = gd ? gd->description : "Layered presets: several generator items drawn in order";
            ImGui::BeginTooltip(); ImGui::PushTextWrapPos(380 * zz); ImGui::TextUnformatted(d.c_str()); ImGui::PopTextWrapPos(); ImGui::EndTooltip();
        }
        if(open){
            if(ImGui::SmallButton("+ new preset")){
                Preset p; p.gen = gname; p.name = engine->uniquePresetName(gname, "new");
                engine->savePreset(p);
                selPreset = p.ref();
            }
            for(auto& [r, p] : engine->presets){
                if(p.gen != gname) continue;
                if(!q.empty() && ofToLower(p.name).find(q) == std::string::npos) continue;
                std::string label = p.name + (dirtyPresets.count(r) ? "  *" : "") + "##" + r;
                if(ImGui::Selectable(label.c_str(), selPreset == r)) selPreset = r;
                if(ImGui::IsItemHovered()) methodTooltip(r);
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    justSelected = false;
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##presetEdit", ImVec2(0, 0));
    auto it = engine->presets.find(selPreset);
    if(it == engine->presets.end()){
        ImGui::TextDisabled("No preset selected: open a generator on the left and pick one, or + new preset.");
        ImGui::EndChild();
        return;
    }
    std::string ref = it->first;
    Preset& pr = it->second;
    const GenDef* g = engine->findGen(pr.gen);
    bool isDirty = dirtyPresets.count(ref) > 0;
    std::string pendingRename, pendingSelect;
    bool pendingDelete = false, pendingRevert = false;

    // --- header: family chip, name, file actions, used-in
    if(renameFor != ref){ renameFor = ref; snprintf(renameBuf, sizeof(renameBuf), "%s", pr.name.c_str()); useUnderlay = engine->onlyEffects(pr); }
    {
        ImVec4 c = genColour(pr.gen);
        std::string fam = g ? g->label : (pr.isStack() ? "Stack" : pr.gen);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(c.x * 0.35f, c.y * 0.35f, c.z * 0.35f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, c);
        ImGui::Button(fam.c_str());
        ImGui::PopStyleColor(4);
    }
    // the category (the Processing families, harmony by role): what the browser filters and the stripes colour
    ImGui::SameLine();
    {
        std::string cat = presetCategory(pr);
        ImVec4 cc = familyColor("cat/" + cat);
        ImGui::PushStyleColor(ImGuiCol_Text, cc);
        ImGui::SetNextItemWidth(110 * zz);
        if(ImGui::BeginCombo("##category", categoryLabel(cat).c_str())){
            for(auto& c : presetCategories()){
                ImGui::PushStyleColor(ImGuiCol_Text, familyColor("cat/" + c));
                if(ImGui::Selectable(categoryLabel(c).c_str(), c == cat) && c != cat){ pr.category = c; dirtyPresets.insert(ref); engine->refreshPresetMethods(); }
                ImGui::PopStyleColor();
            }
            ImGui::EndCombo();
        }
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Category: Harmony (bass, melody, chords, pads, arps), Rhythm, Texture, Geometry or FX.\nWhat the instrument browser filters by; saved with the preset.");
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(260 * zz);
    ImGui::InputText("##name", renameBuf, sizeof(renameBuf));
    if(ImGui::IsItemDeactivatedAfterEdit() && pr.name != renameBuf) pendingRename = renameBuf;
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rename (scenes that use it follow)");
    ImGui::SameLine();
    if(isDirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.55f, 0.40f, 0.10f, 1));
    if(ImGui::Button(isDirty ? "Save *" : "Save")){ engine->savePreset(pr); dirtyPresets.erase(ref); }
    if(isDirty) ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!isDirty);
    if(ImGui::Button("Revert")) pendingRevert = true;
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Duplicate")){
        Preset c = pr;
        c.name = engine->uniquePresetName(pr.gen, pr.name + " copy");
        engine->savePreset(c);
        pendingSelect = c.ref();
    }
    ImGui::SameLine();
    if(ImGui::Button("Delete")) ImGui::OpenPopup("Delete preset?");
    if(ImGui::BeginPopupModal("Delete preset?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        int used = engine->scenesUsing(ref);
        ImGui::Text("Delete %s?", ref.c_str());
        if(used) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%d scenes use it; their cells will render nothing.", used);
        int inStacks = engine->stacksUsing(ref);
        if(inStacks) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "%d stacks play it as a layer; those layers will be skipped.", inStacks);
        if(ImGui::Button("Delete")){ pendingDelete = true; ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    int usedIn = engine->scenesUsing(ref);
    ImGui::SameLine();
    ImGui::TextDisabled("|  in %d scene%s", usedIn, usedIn == 1 ? "" : "s");
    if(int st = engine->stacksUsing(ref)){ ImGui::SameLine(); ImGui::TextDisabled("+ %d stack%s", st, st == 1 ? "" : "s"); }
    if(!pr.legacy.empty()){
        ImGui::SameLine();
        ImGui::TextDisabled("|  from %s", pr.legacy.c_str());
    }

    // --- live edit: this instrument heard on stage, or edited into the playback
    if(auditionSwitch || immediateSwitch){
        bool any = (auditionSwitch && *auditionSwitch) || (immediateSwitch && *immediateSwitch);
        bool imm = immediateSwitch && *immediateSwitch;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, any ? ImVec4(0.30f, 0.17f, 0.06f, 1) : ImVec4(0.16f, 0.13f, 0.10f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * zz, 4 * zz));
        ImGui::BeginChild("##liveEdit", ImVec2(0, 0), ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImVec4(1, 0.7f, 0.35f, 1), "LIVE EDIT");
        if(auditionSwitch){
            ImGui::SameLine();
            ImGui::Checkbox("Audition", auditionSwitch);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("This instrument's page alone on the stage, under the header, heard: every change\n"
                                                         "is drawn again at once (same seed). The other layers are silent while it is on.\n"
                                                         "Off: the playback is back where it is.");
        }
        ImGui::SameLine();
        ImGui::TextDisabled(imm ? "  Immediate is on (top bar): edits go into the pages on stage"
                                : any ? "  edits are seen and heard at once" : "  off: edits show in the preview below (Immediate: top bar)");
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor();
    }

    // --- preview and use
    bool canPreview = (g || pr.isStack()) && engine->scenes.count(previewScene);
    ImGui::BeginDisabled(!canPreview);
    auto preview = [&](bool reroll){
        json items = json::array();
        if(useUnderlay) for(auto* m : UNDERLAY) items.push_back({{"method", m}});
        { auto sc = engine->scenes.find(previewScene); if(sc != engine->scenes.end() && !sc->second.layers.empty()) engine->itemBars = sc->second.layers[0].bars; }
        if(reroll) engine->previewSeedSet = false;   // a new image, which "Same seed" then keeps
        for(auto& x : engine->presetItemsSeeded(pr, engine->nextPreviewItemsSeed())) items.push_back(x);
        sendPreviews(previewScene, {{ref, items}});
    };
    if(ImGui::Button("Preview", ImVec2(100 * zz, 0))) preview(false);
    ImGui::SameLine();
    if(ImGui::Button("Reroll")) preview(true);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Preview a new image (another seed)");
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::Checkbox("Same seed", &engine->previewLock);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Preview re-renders the last preview's image (same seed, harmony and range draws),\nso a parameter change shows on the same image. Reroll for a new one.");
    if(!canPreview && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Pick a scene to preview with (\"with\", to the right)");
    ImGui::SameLine();
    ImGui::TextDisabled("with");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(200 * zz);
    if(ImGui::BeginCombo("##pscene", previewScene.c_str())){
        for(auto& [n, s] : engine->scenes) if(ImGui::Selectable(n.c_str(), n == previewScene)) previewScene = n;
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("The scene whose bars, harmony, scale and palette the preview uses");
    ImGui::SameLine();
    ImGui::Checkbox("test image underneath", &useUnderlay);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Draw a busy test image first, so effects and blanking have something to act on");
    ImGui::SameLine();
    {
        int li = 0;
        auto si = engine->scenes.find(selected);
        if(si != engine->scenes.end() && sceneLayerFor == selected) li = ofClamp(sceneLayer, 0, std::max(0, (int)si->second.layers.size() - 1));
        std::string lab = "Add to " + selected + (si != engine->scenes.end() && si->second.layers.size() > 1 ? ", layer " + ofToString(li + 1) : std::string());
        if(ImGui::Button(lab.c_str())) addMethodToScene(ref);
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Add this instrument to the selected layer of the scene selected in the Scenes tab");
    if(!g && !pr.isStack()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "The renderer doesn't know the generator '%s'.", pr.gen.c_str());
    ImGui::Separator();

    // --- layered preset: pick the layer to edit; its generator's parameters show below
    json* editParams = &pr.params;
    json* refLayer = nullptr;        // a stack layer that plays another preset
    json* editLinked = nullptr;      // where "linked" lives for what is being edited
    if(pr.isStack()){
        if(ImGui::CollapsingHeader("Cover (whole instrument)"))
            if(drawCoverParams(pr.params)) dirtyPresets.insert(ref);
        if(stackFor != ref){ stackFor = ref; stackLayer = 0; }
        int n = (int)pr.layers.size();
        int moveFrom = -1, moveTo = -1, removeAt = -1;
        for(int i = 0; i < n; i++){
            ImGui::PushID(i);
            std::string lg = pr.layers[i].contains("preset") ? "> " + pr.layers[i].value("preset", std::string("?")) : pr.layers[i].value("gen", std::string("?"));
            if(i) ImGui::SameLine();
            if(ImGui::RadioButton((ofToString(i + 1) + ": " + lg).c_str(), stackLayer == i)) stackLayer = i;
            ImGui::PopID();
        }
        if(n) ImGui::SameLine();
        ImGui::SetNextItemWidth(130 * zz);
        if(ImGui::BeginCombo("##addlayer", "+ layer")){
            for(auto& gg : engine->gens) if(ImGui::Selectable(gg.label.c_str())){
                pr.layers.push_back({{"gen", gg.name}, {"params", json::object()}});
                stackLayer = (int)pr.layers.size() - 1;
                dirtyPresets.insert(ref);
            }
            ImGui::Separator();
            std::string picked = presetMenu("a preset", ref);
            if(!picked.empty()){
                pr.layers.push_back({{"preset", picked}});
                stackLayer = (int)pr.layers.size() - 1;
                dirtyPresets.insert(ref);
            }
            ImGui::EndCombo();
        }
        if(n){
            ImGui::SameLine();
            if(ImGui::SmallButton("<") && stackLayer > 0){ moveFrom = stackLayer; moveTo = stackLayer - 1; }
            ImGui::SameLine();
            if(ImGui::SmallButton(">") && stackLayer < n - 1){ moveFrom = stackLayer; moveTo = stackLayer + 1; }
            ImGui::SameLine();
            if(ImGui::SmallButton("remove layer")) removeAt = stackLayer;
        }
        // stack options: one layer per image, and the selected layer's chance / group
        const char* pickModes[] = {"all layers", "one layer per image", "one group per image"};
        const char* pickKeys[] = {"all", "one", "group"};
        int pm = pr.pick == "one" ? 1 : pr.pick == "group" ? 2 : 0;
        ImGui::SetNextItemWidth(170 * zz);
        if(ImGui::Combo("##pick", &pm, pickModes, 3)){ pr.pick = pickKeys[pm]; dirtyPresets.insert(ref); }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("all: every layer (each with its chance)\none layer: a single layer per image, weighted\none group: the layers of one group per image, weighted by the group's first layer");
        bool one = pm == 1;
        if(stackLayer < n){
            json& L = pr.layers[stackLayer];
            ImGui::SameLine();
            ImGui::TextDisabled("| layer %d:", stackLayer + 1);
            ImGui::SameLine();
            if(pm == 2){
                int grp = L.value("group", 0);
                ImGui::SetNextItemWidth(70 * zz);
                if(ImGui::InputInt("group", &grp)){ grp = std::max(0, grp); if(grp == 0) L.erase("group"); else L["group"] = grp; dirtyPresets.insert(ref); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Layers with the same group number (> 0) play together; a layer without a group is a group of its own");
                ImGui::SameLine();
                float wgt = L.value("weight", 1.0f);
                ImGui::SetNextItemWidth(100 * zz);
                if(ImGui::SliderFloat("weight", &wgt, 0, 10, "%.2f")){ L["weight"] = wgt; dirtyPresets.insert(ref); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("A group's weight is its first layer's");
                ImGui::SameLine();
                float ch = L.value("chance", 100.0f);
                ImGui::SetNextItemWidth(100 * zz);
                if(ImGui::SliderFloat("chance %", &ch, 0, 100, "%.0f")){ L["chance"] = ch; dirtyPresets.insert(ref); }
            }else if(one){
                float wgt = L.value("weight", 1.0f);
                ImGui::SetNextItemWidth(120 * zz);
                if(ImGui::SliderFloat("weight", &wgt, 0, 10, "%.2f")){ L["weight"] = wgt; dirtyPresets.insert(ref); }
            }else{
                float ch = L.value("chance", 100.0f);
                ImGui::SetNextItemWidth(120 * zz);
                if(ImGui::SliderFloat("chance %", &ch, 0, 100, "%.0f")){ L["chance"] = ch; dirtyPresets.insert(ref); }
                ImGui::SameLine();
                int grp = L.value("group", 0);
                ImGui::SetNextItemWidth(70 * zz);
                if(ImGui::InputInt("group", &grp)){ grp = std::max(0, grp); if(grp == 0) L.erase("group"); else L["group"] = grp; dirtyPresets.insert(ref); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Layers with the same group number (> 0) play or not together");
            }
        }
        if(moveFrom >= 0){ std::swap(pr.layers[moveFrom], pr.layers[moveTo]); stackLayer = moveTo; dirtyPresets.insert(ref); }
        if(removeAt >= 0){ pr.layers.erase(pr.layers.begin() + removeAt); stackLayer = std::max(0, stackLayer - 1); dirtyPresets.insert(ref); }
        g = nullptr;
        if(stackLayer < (int)pr.layers.size()){
            json& L = pr.layers[stackLayer];
            if(L.contains("preset")){
                refLayer = &L;
            }else{
                if(!L.contains("params") || !L["params"].is_object()) L["params"] = json::object();
                g = engine->findGen(L.value("gen", std::string()));
                editParams = &L["params"];
                editLinked = &L;
            }
        }
        if(pr.layers.empty()) ImGui::TextDisabled("Add a layer: each layer is one generator, drawn in order.");
    }

    // --- parameters | preview
    if((g || refLayer) && ImGui::BeginTable("##inst", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
        ImGui::TableSetupColumn("params", ImGuiTableColumnFlags_WidthStretch, 1.15f);
        ImGui::TableSetupColumn("preview", ImGuiTableColumnFlags_WidthStretch, 0.6f);   // narrower: room for the list
        ImGui::TableNextColumn();
        ImGui::BeginChild("##params", ImVec2(0, 0));
        bool changed = false;
        if(refLayer){
            std::string cur = refLayer->value("preset", std::string());
            ImGui::TextWrapped("This layer plays the preset below, resolved for each image (its own layers, chances and linked picks included).");
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::BeginCombo("##layerpreset", cur.c_str())){
                std::string picked = presetMenu("change to", ref);
                if(!picked.empty()){ (*refLayer)["preset"] = picked; changed = true; }
                ImGui::EndCombo();
            }
            if(!engine->presets.count(cur)) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Preset '%s' not found: this layer is skipped.", cur.c_str());
            else if(ImGui::Button("Open it")) pendingSelect = cur;
        }else{
            ImGui::TextDisabled("Grey: schema default.  Tick 'vary' to pick a value per image from a range.  A dot marks tabs this preset sets.");
            changed = drawParamTabs(*g, *editParams);
            // advanced: per-bar choices and linked picks
            int pbNow = editLinked ? editLinked->value("perBar", 0) : pr.perBar;
            json linkedNow = editLinked ? editLinked->value("linked", json::array()) : pr.linked;
            bool advSet = pbNow > 0 || (linkedNow.is_array() && !linkedNow.empty());
            if(ImGui::CollapsingHeader(advSet ? "Advanced: per-bar choices, linked values  ·###adv" : "Advanced: per-bar choices, linked values###adv")){
            {
                int pb = editLinked ? editLinked->value("perBar", 0) : pr.perBar;
                ImGui::SetNextItemWidth(90 * zz);
                if(ImGui::InputInt("redraw every N bars (0: once per image)", &pb)){
                    pb = ofClamp(pb, 0, 64);
                    if(editLinked){ if(pb > 0) (*editLinked)["perBar"] = pb; else editLinked->erase("perBar"); }
                    else pr.perBar = pb;
                    changed = true;
                }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Per-bar choices: ranges, options and linked picks are drawn again for every N bars,\nand each draw only paints its own bars (renders take N-bar-many passes).");
            }
            json linked = editLinked ? editLinked->value("linked", json::array()) : pr.linked;
            if(!linked.is_array()) linked = json::array();
            if(ImGui::TreeNode(linked.empty() ? "Linked values###lk" : "Linked values ·###lk")){
                if(drawLinkedEditor(linked, ref + "#" + (editLinked ? ofToString(stackLayer) : std::string("p")))){
                    changed = true;
                    if(editLinked){ if(linked.empty()) editLinked->erase("linked"); else (*editLinked)["linked"] = linked; }
                    else pr.linked = linked;
                }
                ImGui::TreePop();
            }
            }
        }
        if(ImGui::CollapsingHeader("Notes")){
            char nb[512];
            snprintf(nb, sizeof(nb), "%s", pr.notes.c_str());
            if(ImGui::InputTextMultiline("##notes", nb, sizeof(nb), ImVec2(-1, 60 * zz))){ pr.notes = nb; changed = true; }
        }
        if(changed) dirtyPresets.insert(ref);
        ImGui::EndChild();

        ImGui::TableNextColumn();
        drawPreviewPane(ref);
        ImGui::EndTable();
    }
    ImGui::EndChild();

    // deferred: these invalidate pr
    if(pendingRevert){
        try { engine->presets[ref] = Engine::readPresetFile(engine->presetPath(pr)); } catch(std::exception& e){ engine->note("revert " + ref + ": " + e.what()); }
        dirtyPresets.erase(ref);
    }
    if(!pendingRename.empty()){
        std::string oldThumb = (thumbs.dir / (ref + ".png")).string();
        std::string newRef = pr.gen + "/" + pendingRename;
        if(engine->renamePreset(ref, pendingRename)){
            if(fs::exists(oldThumb)){ std::error_code ec; fs::rename(oldThumb, thumbs.dir / (newRef + ".png"), ec); }
            thumbs.forget(ref);
            if(dirtyPresets.erase(ref)) dirtyPresets.insert(newRef);
            selPreset = newRef;
        }else{
            engine->note("rename: '" + pendingRename + "' is taken or invalid");
            renameFor.clear();
        }
    }
    if(pendingDelete){
        engine->deletePreset(ref);
        dirtyPresets.erase(ref);
        std::error_code ec; fs::remove(thumbs.dir / (ref + ".png"), ec);
        thumbs.forget(ref);
        selPreset.clear();
    }
    if(!pendingSelect.empty()) selPreset = pendingSelect;
}

// The parameters in tabs, one per group ("pattern", "pitch", "cell"...). The process groups share
// one "processes" tab with a list of the processes on the left. A dot marks the tabs (and
// processes) where the preset sets something.
// A menu of presets by generator (inside an open combo or popup); returns the picked ref
inline std::string Editor::presetMenu(const char* label, const std::string& exclude){
    std::string picked;
    if(ImGui::BeginMenu(label)){
        std::map<std::string, std::vector<std::string>> byGen;
        for(auto& [r, p] : engine->presets) if(r != exclude) byGen[p.gen].push_back(r);
        for(auto& [gen, refs] : byGen){
            if(ImGui::BeginMenu(gen.c_str())){
                for(auto& r : refs) if(ImGui::MenuItem(r.substr(gen.size() + 1).c_str())) picked = r;
                ImGui::EndMenu();
            }
        }
        ImGui::EndMenu();
    }
    return picked;
}

inline bool Editor::drawLinkedEditor(json& linked, const std::string& key){
    ImGui::TextWrapped("Parameter sets chosen together: one set per image (optional \"weight\"), laid over the values above. A list of such lists makes several independent choices.");
    auto& txt = linkedText[key];
    if(linkedFor != key){ txt = linked.empty() ? "" : linked.dump(1); linkedFor = key; linkedError.erase(key); }
    txt.reserve(4096);
    static char buf[4096];
    snprintf(buf, sizeof(buf), "%s", txt.c_str());
    bool changed = false;
    if(ImGui::InputTextMultiline("##linked", buf, sizeof(buf), ImVec2(-1, 110 * ImGui::GetIO().FontGlobalScale * ImGui::GetFontSize() / 13.0f))) txt = buf;
    if(ImGui::IsItemDeactivatedAfterEdit()){
        std::string t = ofTrim(txt);
        try {
            json j = t.empty() ? json::array() : json::parse(t);
            if(!j.is_array()) throw std::runtime_error("expected a list [ ... ]");
            linked = j; changed = true; linkedError.erase(key);
        } catch(std::exception& e){ linkedError[key] = e.what(); }
    }
    if(linkedError.count(key)) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "Not saved: %s", linkedError[key].c_str());
    return changed;
}

inline bool Editor::drawCoverParams(json& params){
    ImGui::TextWrapped("Duck other layers using this instrument's shapes. Eraser reveals what is underneath; black makes the covered area opaque black. Below follows the visual stack.");
    bool changed=false;
    auto schema=genDefFromJson({{"params",formscover::schema()}});
    if(beginParamTable()){
        for(auto& p:schema.params) changed|=paramRow(p,params);
        ImGui::EndTable();
    }
    return changed;
}

inline bool Editor::drawParamTabs(const GenDef& g, json& params){
    float zz = z();
    bool changed = false;
    std::vector<std::string> groups, procs;
    auto isProc = [](const std::string& s){ return s.rfind("process", 0) == 0; };
    for(auto& p : g.params){
        auto& list = isProc(p.group) ? procs : groups;
        std::string name = p.group.empty() ? "other" : p.group;
        if(std::find(list.begin(), list.end(), name) == list.end()) list.push_back(name);
    }
    auto setIn = [&](const std::string& grp, bool proc){
        for(auto& p : g.params){
            std::string name = p.group.empty() ? "other" : p.group;
            if(proc ? isProc(p.group) && (grp.empty() || name == grp) : name == grp)
                if(params.contains(p.key)) return true;
        }
        return false;
    };
    if(ImGui::BeginTabBar("##paramTabs", ImGuiTabBarFlags_FittingPolicyScroll)){
        for(auto& grp : groups){
            std::string label = grp + (setIn(grp, false) ? " ·" : "") + "###" + grp;
            if(ImGui::BeginTabItem(label.c_str())){
                if(grp=="cover") ImGui::TextWrapped("Duck other layers using only this instrument's shapes. Below follows the visual stack order.");
                ImGui::BeginChild("##tabbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
                if(beginParamTable()){
                    for(auto& p : g.params) if((p.group.empty() ? "other" : p.group) == grp) changed |= paramRow(p, params);
                    ImGui::EndTable();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        if(!procs.empty()){
            std::string label = std::string("fx") + (setIn("", true) ? " ·" : "") + "###processes";
            if(ImGui::BeginTabItem(label.c_str())){
                if(std::find(procs.begin(), procs.end(), procTab) == procs.end()) procTab = procs[0];
                ImGui::BeginChild("##proclist", ImVec2(150 * zz, -ImGui::GetFrameHeightWithSpacing() * 1.5f), ImGuiChildFlags_Borders);
                for(auto& pg : procs){
                    std::string short_ = pg.rfind("process: ", 0) == 0 ? pg.substr(9) : pg;
                    std::string item = short_ + (setIn(pg, true) ? " ·" : "") + "##" + pg;
                    if(ImGui::Selectable(item.c_str(), procTab == pg)) procTab = pg;
                }
                ImGui::EndChild();
                ImGui::SameLine();
                ImGui::BeginChild("##procbody", ImVec2(0, -ImGui::GetFrameHeightWithSpacing() * 1.5f));
                if(beginParamTable()){
                    for(auto& p : g.params) if(p.group == procTab) changed |= paramRow(p, params);
                    ImGui::EndTable();
                }
                ImGui::EndChild();
                ImGui::EndTabItem();
            }
        }
        ImGui::EndTabBar();
    }
    return changed;
}

// The last preview: picture and the values the image was made with
inline void Editor::drawPreviewPane(const std::string& ref){
    float avail = ImGui::GetContentRegionAvail().x;
    float h = avail * 9 / 16;
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + avail, p.y + h), IM_COL32(8, 8, 10, 255), 3 * z());
    if(previewImg.isAllocated()) ImGui::Image(texId(previewImg.getTexture()), ImVec2(avail, h));
    else {
        ImGui::Dummy(ImVec2(avail, h));
        dl->AddText(ImVec2(p.x + 10 * z(), p.y + h / 2 - 7 * z()), IM_COL32(120, 120, 120, 255), "Preview renders one image with this preset alone");
    }
    if(previewImg.isAllocated()){
        if(previewShownRef != ref) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "showing %s", previewShownRef.c_str());
        else ImGui::TextDisabled("%s  |  also its thumbnail", previewShownRef.c_str());

        if(ImGui::CollapsingHeader("Values used for this image")){
            if(ImGui::BeginTable("##vals", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)){
                for(auto& [k, val] : previewValues.items()){
                    ImGui::TableNextColumn(); ImGui::TextDisabled("%s", k.c_str());
                    ImGui::TableNextColumn(); ImGui::Text("%s", val.dump().c_str());
                }
                ImGui::EndTable();
            }
        }
    }
}

} // namespace forms
