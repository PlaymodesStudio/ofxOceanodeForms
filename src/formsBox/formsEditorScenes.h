//
//  formsEditorScenes.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Scenes tab: scene settings, the layer stack, performance and cell cards.
//  Methods of forms::Editor (declared in formsEditor.h, which includes this file).
//

#pragma once

namespace forms {

inline bool Editor::drawLayerEffectPreset(Scene& scene,int layer){
    if(layer<0 || layer>=(int)scene.layers.size())return false;
    auto& ref=scene.layers[layer].effectPreset;bool changed=false;
    ImGui::PushID(("layerEffect"+scene.name+std::to_string(layer)).c_str());
    if(ImGui::BeginCombo("Effect preset",ref.empty()?"None":ref.c_str())){
        if(ImGui::Selectable("None",ref.empty())){ref.clear();changed=true;}
        for(auto& kv:engine->presets)if(engine->onlyEffects(kv.second)){
            if(ImGui::Selectable(kv.first.c_str(),ref==kv.first)){ref=kv.first;changed=true;}
        }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered())ImGui::SetTooltip("Applied after all this layer's instruments. Saved with this scene.\nEnable Immediate to redraw existing pages when changing it.");
    if(!ref.empty()){
        ImGui::SameLine();if(ImGui::SmallButton("Edit effect")){selPreset=ref;showTab="Instruments";}
    }
    ImGui::PopID();if(changed)dirty.insert(scene.name);return changed;
}

// The scenes, by bank: New / Duplicate / Delete, the bank new scenes go to, the bank shown; click a
// scene to edit it, double-click to play it.
inline void Editor::drawSceneList(){
    float zz = z();
    if(ImGui::Button("New")) newScene();
    ImGui::SameLine();
    if(ImGui::Button("Duplicate") && !selected.empty()){
        std::string n = engine->duplicateScene(selected);
        if(!n.empty()){ selected = n; reload(); }
    }
    ImGui::SameLine();
    if(ImGui::Button("Delete") && !selected.empty()) ImGui::OpenPopup("Delete scene?");
    if(ImGui::BeginPopupModal("Delete scene?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
        ImGui::Text("Delete \"%s\"? Its file is removed.", selected.c_str());
        bool inSetlist = false;
        for(auto& e : engine->setlist) if(e.scene == selected) inSetlist = true;
        if(inSetlist) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "It is in the setlist: those entries will be skipped.");
        if(ImGui::Button("Delete")){ engine->deleteScene(selected); dirty.erase(selected); selected.clear(); reload(); ImGui::CloseCurrentPopup(); }
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
    // banks: where new scenes go, and which ones the list shows
    auto banks = engine->banks();
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##newBank", ("New in: " + Engine::bankLabel(engine->currentBank)).c_str())){
        for(auto& b : banks) if(ImGui::Selectable((Engine::bankLabel(b) + "##nb" + b).c_str(), b == engine->currentBank)) engine->currentBank = b;
        ImGui::Separator();
        ImGui::SetNextItemWidth(150 * zz);
        if(ImGui::InputTextWithHint("##bankName", "new bank, Enter", newBankBuf, sizeof(newBankBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
            std::string b = engine->createBank(newBankBuf);
            if(!b.empty()){ engine->currentBank = b; newBankBuf[0] = 0; ImGui::CloseCurrentPopup(); }
        }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("The bank (a folder under scenes/) that New, Duplicate and the Gallery's Freeze / Promote save into.\nLegacy: the scenes imported from 1920 (scenes/ itself).");
    ImGui::SetNextItemWidth(-1);
    if(ImGui::BeginCombo("##showBank", ("Show: " + (bankFilter == "*" ? std::string("all banks") : Engine::bankLabel(bankFilter))).c_str())){
        if(ImGui::Selectable("all banks", bankFilter == "*")) bankFilter = "*";
        for(auto& b : banks) if(ImGui::Selectable((Engine::bankLabel(b) + "##sb" + b).c_str(), b == bankFilter)) bankFilter = b;
        ImGui::EndCombo();
    }
    ImGui::Separator();
    for(auto& bank : banks){
    if(bankFilter != "*" && bank != bankFilter) continue;
    int inBank = 0; for(auto& [n, sc] : engine->scenes) if(sc.bank == bank) inBank++;
    if(bankFilter == "*") ImGui::SeparatorText((Engine::bankLabel(bank) + "  (" + ofToString(inBank) + ")").c_str());
    for(auto& [name, sc] : engine->scenes){
        if(sc.bank != bank) continue;
        ImVec2 p = ImGui::GetCursorScreenPos();
        const PaletteDef* pd = engine->findPalette(sc.palette);
        std::string label = "      " + name + (dirty.count(name) ? " *" : "");
        if(ImGui::Selectable(label.c_str(), selected == name, ImGuiSelectableFlags_AllowDoubleClick)){
            selected = name;
            if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && playScene) playScene(name);   // double-click: play it
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click to edit, double-click to play it");
        if(pd){
            auto* dl = ImGui::GetWindowDrawList();
            for(int i = 0; i < 3 && i < (int)pd->colors.size(); i++)
                dl->AddRectFilled(ImVec2(p.x + i * 6 * zz, p.y + 3 * zz), ImVec2(p.x + i * 6 * zz + 5 * zz, p.y + 13 * zz), hexColor(pd->colors[i]));
        }
    }
    }
}

// The edited scene's own controls, on the "Editing" line: Save / Revert, and a "scene" menu with
// rename, bank, play, a preview render. False when the scene was renamed (drawn again next frame).
inline bool Editor::drawSceneHeader(Scene& s){
    float zz = z();
    bool isDirty = dirty.count(s.name) > 0;
    ImGui::SameLine(0, 14 * zz);
    if(isDirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1));
    if(ImGui::Button(isDirty ? "Save *" : "Save")){ engine->saveScene(s); dirty.erase(s.name); }
    if(isDirty) ImGui::PopStyleColor();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Keep this scene's edits in its file");
    ImGui::SameLine(0, 4 * zz);
    bool savedAs = false;
    if(ImGui::Button("Save as...")){
        std::string n = s.name + " copy";
        for(int i = 2; engine->scenes.count(n); i++) n = s.name + " copy " + std::to_string(i);
        strncpy(saveAsBuf, n.c_str(), sizeof(saveAsBuf)); saveAsBuf[sizeof(saveAsBuf) - 1] = 0;
        saveAsError.clear();
        ImGui::OpenPopup("##sceneSaveAs");
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Save this scene, with its edits, under a new name.\nThe original keeps its last saved version.");
    if(ImGui::BeginPopup("##sceneSaveAs")){
        ImGui::TextUnformatted("New scene name (Enter to save)");
        if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
        ImGui::SetNextItemWidth(260 * zz);
        bool go = ImGui::InputText("##saveAsName", saveAsBuf, sizeof(saveAsBuf), ImGuiInputTextFlags_EnterReturnsTrue);
        go |= ImGui::Button("Save");
        ImGui::SameLine();
        if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
        if(go){
            std::string old = s.name, nn = saveAsBuf;
            saveAsError = engine->saveSceneAs(old, nn);
            if(saveAsError.empty()){ dirty.erase(old); selected = nn; savedAs = true; ImGui::CloseCurrentPopup(); }
        }
        if(!saveAsError.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "%s", saveAsError.c_str());
        ImGui::EndPopup();
    }
    if(savedAs) return false;   // the selection moved to the new scene: drawn again next frame
    ImGui::SameLine(0, 4 * zz);
    ImGui::BeginDisabled(!isDirty);
    if(ImGui::Button("Revert")){ engine->reloadScene(s.name); dirty.erase(s.name); }
    ImGui::EndDisabled();
    ImGui::SameLine(0, 4 * zz);
    if(ImGui::Button("scene...")) ImGui::OpenPopup("##sceneMenu");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rename, bank, play this scene, render a preview, hide the output view");
    bool renamed = false;
    if(ImGui::BeginPopup("##sceneMenu")){
        char nameBuf[128];
        strncpy(nameBuf, s.name.c_str(), sizeof(nameBuf)); nameBuf[sizeof(nameBuf) - 1] = 0;
        ImGui::SetNextItemWidth(220 * zz);
        if(ImGui::InputText("name", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
            std::string old = s.name, nn = nameBuf;
            if(engine->renameScene(old, nn)){ dirty.erase(old); selected = nn; renamed = true; ImGui::CloseCurrentPopup(); }
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rename: type and press Enter");
        if(!renamed){
            ImGui::SetNextItemWidth(220 * zz);
            if(ImGui::BeginCombo("bank", Engine::bankLabel(s.bank).c_str())){
                for(auto& b : engine->banks()) if(ImGui::Selectable((Engine::bankLabel(b) + "##mv" + b).c_str(), b == s.bank)){
                    if(dirty.count(s.name)){ engine->saveScene(s); dirty.erase(s.name); }
                    engine->moveSceneToBank(s.name, b);
                }
                ImGui::EndCombo();
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("The bank this scene's file is in: pick another to move it there");
            ImGui::Separator();
            if(playScene && ImGui::MenuItem("Play this scene")) playScene(s.name);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Turn the setlist off and play this scene from the next image");
            if(ImGui::MenuItem("Render a preview image")) renderScene(s.name);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Render one image of this scene now (unsaved edits included); it scrolls in next. The setlist doesn't move.");
            ImGui::Separator();
            if(ImGui::MenuItem(stageShowOutput ? "Hide the output view" : "Show the output view")) stageShowOutput = !stageShowOutput;
            if(s.snapshot >= 0) ImGui::TextDisabled("from 1920 Director slot %d", s.snapshot);
        }
        ImGui::EndPopup();
    }
    return !renamed;
}

// The selected layer's cells as cards, with the Director's groups and rows (the advanced view of the
// same instruments the Layers tab lists)
inline bool Editor::drawLayerCells(Scene& s){
    float zz = z();
    bool changed = false;
    sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    LayerDef& l = s.layers[sceneLayer];
        int active = 0;
        for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(!l.cells[i].item.empty() && l.cells[i].prob > 0) active++;
        ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.55f, 0.70f, 1));
        ImGui::SeparatorText(("LAYER " + ofToString(sceneLayer + 1) + " INSTRUMENTS  (" + ofToString(active) + " active, " + barsText(l.bars) + " bars per image)").c_str());
        ImGui::PopStyleColor();
        changed |= drawLayerEffectPreset(s,sceneLayer);
        if(ImGui::Button("+ Add instrument")){
            onPick = [this, name = s.name, layerIdx = sceneLayer](const std::string& m){
                auto it2 = engine->scenes.find(name);
                if(it2 == engine->scenes.end()) return;
                if(layerIdx >= (int)it2->second.layers.size()) return;
                engine->addItemToScene(it2->second, layerIdx, m);
                dirty.insert(name);
            };
            openPicker = true;
        }
        ImGui::SameLine();
        ImGui::SetNextItemWidth(140 * zz);
        changed |= ImGui::SliderFloat("image chance", &l.globalProb, 0, 100, "%.0f%%");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The chance that this layer draws anything at all in an image (the 1920 Global %%)");
        ImGui::SameLine();
        ImGui::TextDisabled("| click a thumbnail to edit the instrument, right-click to replace or move it, hover for details");
        if(l.fixed.is_object()){
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1, 1), "This layer is frozen: it always draws the image %s (from the Gallery).", l.fixed.value("from", std::string("?")).c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton("unfreeze")){ l.fixed = json(); changed = true; }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Back to a normal layer (its instruments are then the ones below; add some)");
        }
        if(ImGui::TreeNode("Groups and rows (advanced)")){
            ImGui::Text("Group chance");
            if(l.groupProb.size() < 7) l.groupProb.resize(7, 1.0f);
            for(int g = 0; g < 7; g++){
                if(g) ImGui::SameLine();
                ImGui::PushID(g);
                changed |= ImGui::VSliderFloat("##gp", ImVec2(22 * zz, 60 * zz), &l.groupProb[g], 0, 1, "");
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Group G%d: %.0f%% (%d instruments)", g, l.groupProb[g] * 100, countGroup(l, g));
                ImGui::PopID();
            }
            ImGui::SetNextItemWidth(120 * zz); changed |= ImGui::InputInt("Rows", &l.rows);
            l.rows = ofClamp(l.rows, 1, 512);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Only the first Rows instruments take part (the 1920 Director's Rows).");
            ImGui::TreePop();
        }
        changed |= drawCellCards(s, l);
    return changed;
}

// (the scene editor lives in Stage: drawSceneWorkspace, formsEditorStage.h)
inline void Editor::drawScenesTab(){ drawSceneWorkspace(std::string()); }

// The Live tab: the scene playing now (at the header), its live settings at hand. Layer opacity,
// blend, order, BPM, the root note (sound) and the performance settings (beat repeat, gate,
// tremolo, header jumps, speed modulation, sound) act at once; palette, scale, note grid and
// harmony are rendered into the images, so they show from the next images asked for (Render next
// now asks one at once). Edits are the scene's: Save keeps them.
inline void Editor::drawLiveTab(){
    float zz = z();
    std::string name;
    if(const Scene* hs = engine->sceneAtHeader()) name = hs->name;
    else if(engine->schedule->layers[0].header) name = engine->schedule->layers[0].header->scene;
    auto it = engine->scenes.find(name);
    if(it == engine->scenes.end()){
        ImGui::TextDisabled("Nothing playing yet: the scene under the header shows here once its first image arrives.");
        return;
    }
    Scene& s = it->second;
    bool changed = false;
    // ---- what is playing
    int bar = (int)std::floor(engine->schedule->headerBeat / 4.0);
    ImGui::SetWindowFontScale(1.3f);
    ImGui::TextColored(ImVec4(1, 0.85f, 0.5f, 1), "%s", s.name.c_str());
    ImGui::SetWindowFontScale(1.0f);
    ImGui::SameLine();
    ImGui::TextDisabled("  the scene under the header  |  bank %s  |  bar %d  |  %g BPM", Engine::bankLabel(s.bank).c_str(), bar, s.bpm);
    bool isDirty = dirty.count(s.name) > 0;
    if(isDirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1));
    if(ImGui::Button(isDirty ? "Save *" : "Save")){ engine->saveScene(s); dirty.erase(s.name); }
    if(isDirty) ImGui::PopStyleColor();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Keep these edits in the scene's file (else they last until the scene is reloaded)");
    ImGui::SameLine();
    ImGui::BeginDisabled(!isDirty);
    if(ImGui::Button("Revert")){ engine->reloadScene(s.name); dirty.erase(s.name); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Edit in Scenes tab")){ selected = s.name; showTab = "Scenes"; }
    ImGui::SameLine();
    ImGui::TextDisabled("(Render next now, at the top, shows palette / scale / harmony edits sooner)");
    ImGui::Separator();

    if(s.layers.empty()) return;
    if(sceneLayerFor != s.name){ sceneLayerFor = s.name; sceneLayer = 0; }
    sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    if(ImGui::BeginTable("##live", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
        ImGui::TableSetupColumn("layers", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("look", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("perf", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        // layers: opacity, blend, order, kick (at once)
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "LAYERS");
        ImGui::SameLine(); ImGui::TextDisabled("(act at once)");
        changed |= drawLayerStack(s);
        // the selected layer's instruments: mute / solo at once
        if(instMS){
            ImGui::Spacing();
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "INSTRUMENTS, LAYER %d", sceneLayer + 1);
            ImGui::SameLine(); ImGui::TextDisabled("(mute / solo: pages drawn again)");
            int n = 0;
            for(auto& u : sceneUses(s.name, true)){
                if(u.layer != sceneLayer) continue;
                auto pi = engine->presets.find(u.ref);
                if(pi == engine->presets.end()) continue;
                ImGui::PushID(n++);
                instMSButtons(u.ref);
                ImGui::SameLine();
                bool off = !u.depth && u.prob <= 0;
                if(off) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
                std::string lab = (u.depth ? std::string((size_t)u.depth * 2, ' ') + "- " : std::string()) + pi->second.name;
                ImGui::TextColored(genColour(pi->second.gen), "%s", lab.c_str());
                instrumentMenu("##instMenu", u.ref, u.depth ? std::string() : s.name, u.layer, u.depth ? -1 : u.cell);
                if(ImGui::IsItemHovered()) methodTooltip(u.ref);
                if(!u.depth){   // chance per image and volume, at the right of the row
                    float dragW = 50 * zz, gap = ImGui::GetStyle().ItemSpacing.x;
                    ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - 2 * dragW - gap));
                    if(cellChanceDrag(s.name, u, dragW)) changed = true;
                    ImGui::SameLine();
                    if(cellGainDrag(s.name, u, dragW)) changed = true;
                }
                if(off) ImGui::PopStyleVar();
                ImGui::PopID();
            }
            if(!n) ImGui::TextDisabled("no instruments on this layer");
        }
        // look and harmony (next images)
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "COLOUR AND HARMONY");
        ImGui::SameLine(); ImGui::TextDisabled("(next images)");
        changed |= paletteCombo("Palette", s.palette);
        if(const PaletteDef* pd = engine->findPalette(s.palette)){
            ImVec2 p = ImGui::GetCursorScreenPos(); auto* dl = ImGui::GetWindowDrawList();
            for(int i = 0; i < (int)pd->colors.size(); i++)
                dl->AddRectFilled(ImVec2(p.x + i * 22 * zz, p.y), ImVec2(p.x + i * 22 * zz + 20 * zz, p.y + 14 * zz), hexColor(pd->colors[i]));
            ImGui::Dummy(ImVec2(std::max<size_t>(1, pd->colors.size()) * 22 * zz, 16 * zz));
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Edit the palette's colours in the Library tab (every scene using it changes)");
        }
        changed |= scaleCombo("Scale", s.scale);
        if(engine->pitchFollow.scaleOn){
            static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            std::string nt; for(int n : engine->pitchFollow.notes) nt += (nt.empty() ? "" : " ") + ofToString(n);
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "Global Pitch: %s on %s%s (the scene scale is overridden; untick Forms in the Global Pitch inspector to keep it)", nt.c_str(), nn[((engine->pitchFollow.rootPc % 12) + 12) % 12], engine->pitchFollow.shift ? (", shift " + ofToString(engine->pitchFollow.shift)).c_str() : "");
        }
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Notegrid", &s.notegrid); s.notegrid = ofClamp(s.notegrid, 12, 400);
        ImGui::Spacing();
        ImGui::TextDisabled("at once:");
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputDouble("BPM", &s.bpm, 1, 10, "%g"); s.bpm = std::clamp(std::isfinite(s.bpm) ? s.bpm : 120.0, 1.0, 999.0);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once: the clock eases to it (the layers stay on the beat)");
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Root note (sound)", &s.root); s.root = ofClamp(s.root, 0, 127);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once on the sound (the partials, Root Out, Scale Out)");
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Bass degree", &s.bassDegree); s.bassDegree = ofClamp(s.bassDegree, 0, 11);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scale degree the bass instruments play, added to the bar's degree (0 = the root; past the scale's last degree it goes up an octave).\nAn instrument is a bass when its 'bass instrument' pitch option is on (next images).");
        if(ImGui::TreeNode("Harmony (degrees, transposes)")){
            const ScaleDef es = engine->effectiveScale(s);
            const ScaleDef* sd = es.name.empty() ? nullptr : &es;
            changed |= weightBars("Degrees", s.degreeWeights, sd ? (int)sd->notes.size() : 12, true, sd);
            changed |= weightBars("Transpose", s.transposeWeights, 12, false, nullptr);
            ImGui::TreePop();
        }
        // performance (at once)
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "PERFORMANCE");
        ImGui::SameLine(); ImGui::TextDisabled("(act at once)");
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        changed |= drawPerformance(s);
        ImGui::EndTable();
    }
    if(changed) dirty.insert(s.name);
}

// The layers of the scene, one card per layer (top of the stack first) and nothing else: the card holds
// every layer setting. Line 1: select (click; drag to reorder; right-click menu), volume, order
// arrows, mute / solo, remove. Line 2: bars, repeat, offset, phrase, blend, kick. Folded: Cover, and
// Effects (blur, trail, mask: the layer's effects on every scene, saved with the patch).
// The lines wrap to the card's width. compact: no folds and no "+ layer".
inline bool Editor::drawLayerStack(Scene& s, bool compact){
    float zz = z();
    bool changed = false;
    if(s.layers.empty()) return false;
    sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    const ImGuiStyle& st = ImGui::GetStyle();
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "LAYERS");
    ImGui::SameLine();
    ImGui::TextDisabled("top of the stack first; layer 1 sets the clock; click a card: its instruments");
    std::vector<int> order = stackOrder(s.layers);           // bottom .. top
    int moveUp = -1, moveDown = -1, dragFrom = -1, dragTo = -1, removeAt = -1;
    const bool noKick = std::none_of(s.layers.begin(), s.layers.end(), [](const LayerDef& x){ return x.kick; });
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8 * zz, 6 * zz));
    for(int r = (int)order.size() - 1; r >= 0; r--){
        int i = order[r];
        LayerDef& L = s.layers[i];
        ImGui::PushID(i);
        bool sel = sceneLayer == i;
        ImGui::PushStyleColor(ImGuiCol_ChildBg, sel ? ImVec4(0.10f, 0.22f, 0.30f, 1) : ImVec4(0.08f, 0.13f, 0.17f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, sel ? ImVec4(0.30f, 0.70f, 0.90f, 1) : ImVec4(0.20f, 0.36f, 0.46f, 1));
        ImGui::BeginChild("##layerCard", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        // the controls flow: the next one stays on the line while it fits in the card
        const float right = ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
        auto flow = [&](float w){ if(ImGui::GetItemRectMax().x + st.ItemSpacing.x + w <= right) ImGui::SameLine(); };
        auto labelled = [&](const char* label, float field){ return field + st.ItemInnerSpacing.x + ImGui::CalcTextSize(label, nullptr, true).x; };
        const float frameH = ImGui::GetFrameHeight();

        // ---- line 1: select, volume, order, mute / solo, remove
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.20f, 0.55f, 0.75f, 1) : ImVec4(0.14f, 0.20f, 0.25f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? ImVec4(0.25f, 0.62f, 0.82f, 1) : ImVec4(0.20f, 0.30f, 0.38f, 1));
        std::string lab = "Layer " + ofToString(i + 1) + (L.kick || (i == 0 && noKick) ? "  [kick]" : "");
        if(ImGui::Button(lab.c_str())) sceneLayer = i;
        ImGui::PopStyleColor(2);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click: this layer's instruments at the right. Drag: its place in the stack. Right-click: menu.");
        // the same gestures as every list: drag to reorder, right-click for the menu
        if(ImGui::BeginDragDropSource()){
            ImGui::SetDragDropPayload("FORMS_LAYER_ORDER", &r, sizeof r);
            ImGui::Text("Move layer %d in the stack", i + 1);
            ImGui::EndDragDropSource();
        }
        if(ImGui::BeginDragDropTarget()){
            if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FORMS_LAYER_ORDER")){ dragFrom = *(const int*)pl->Data; dragTo = r; }
            ImGui::EndDragDropTarget();
        }
        if(ImGui::BeginPopupContextItem("##layerMenu")){
            ImGui::TextDisabled("Layer %d", i + 1);
            ImGui::Separator();
            if(layerMS && toggleLayerMS){
                int ms = layerMS(i);
                if(ImGui::MenuItem("Mute", nullptr, (ms & 1) != 0)) toggleLayerMS(i, 0);
                if(ImGui::MenuItem("Solo", nullptr, (ms & 2) != 0)) toggleLayerMS(i, 1);
                ImGui::Separator();
            }
            if(ImGui::MenuItem("Move up in the stack", nullptr, false, r < (int)order.size() - 1)) moveUp = r;
            if(ImGui::MenuItem("Move down in the stack", nullptr, false, r > 0)) moveDown = r;
            if(ImGui::MenuItem("Kick detection reads this layer", nullptr, L.kick)){
                bool on = !L.kick;
                for(auto& x : s.layers) x.kick = false;
                L.kick = on;
                changed = true;
            }
            ImGui::EndPopup();
        }
        flow(120 * zz);
        ImGui::SetNextItemWidth(120 * zz);
        if(ImGui::SliderFloat("##op", &L.gain, 0, 2, "volume %.2f")) changed = true;
        flow(2 * frameH + 2);
        ImGui::BeginDisabled(r == (int)order.size() - 1);
        if(ImGui::ArrowButton("##up", ImGuiDir_Up)) moveUp = r;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move up in the stack (drawn over the layers below)");
        ImGui::SameLine(0, 2);
        ImGui::BeginDisabled(r == 0);
        if(ImGui::ArrowButton("##down", ImGuiDir_Down)) moveDown = r;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move down in the stack");
        if(layerMS && toggleLayerMS){
            // mute / solo: performance state (not the scene's, not saved), seen and heard at once
            int ms = layerMS(i);
            flow(ImGui::CalcTextSize("MS").x + 4 * st.FramePadding.x + 2);
            if(msButton("M", ms & 1, false, "Mute this layer: hidden and silent (it keeps scrolling); not saved")) toggleLayerMS(i, 0);
            ImGui::SameLine(0, 2);
            if(msButton("S", ms & 2, true, "Solo: only the soloed layers are seen and heard; not saved")) toggleLayerMS(i, 1);
        }
        {   // blend and kick: on this line, right after mute / solo
            const char* blends[] = {"lighten", "add", "normal"};
            int bi = L.blend == "add" ? 1 : L.blend == "normal" ? 2 : 0;
            ImGui::SameLine();
            ImGui::SetNextItemWidth(72 * zz);
            if(ImGui::Combo("##blend", &bi, blends, 3)){ L.blend = blends[bi]; changed = true; }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blend: how it combines with the layers below it:\nlighten = the brighter of both, add = the sum, normal = over them (by its alpha)");
            ImGui::SameLine();
            if(ImGui::Checkbox("kick", &L.kick)){
                if(L.kick) for(int k = 0; k < (int)s.layers.size(); k++) if(k != i) s.layers[k].kick = false;
                changed = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Kick detection (the Kick output) reads this layer. None ticked: layer 1.");
        }
        {   // remove, at the right end of the line when it fits (layer 1 stays: it sets the clock)
            float rw = ImGui::CalcTextSize("remove").x + 2 * st.FramePadding.x;
            if(ImGui::GetItemRectMax().x + st.ItemSpacing.x + rw <= right){
                ImGui::SameLine();
                ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - rw));
            }
            ImGui::BeginDisabled(i == 0);
            if(ImGui::SmallButton("remove")) removeAt = i;
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(i == 0 ? "Layer 1 sets the clock: it cannot be removed" : "Remove this layer from the scene");
        }

        // ---- line 2: bars, repeat, offset, phrase, blend, kick
        ImGui::SetNextItemWidth(44 * zz);
        {
            double b = L.bars;
            // the node's Bar Multiplier: the field keeps the length as written, the tooltip says what plays
            std::string multNote = (engine->barMult == 1 && engine->barAdd == 0) ? std::string() : "\nBar Multiplier x" + barsText(engine->barMult) + ", Bar Add " + std::to_string(engine->barAdd) + ": plays as " + barsText(engine->layerBars(L)) + " bars.";
            // +/-: whole bars from 1 up, halves below 1 (1, 0.5, 0.25 ...); any multiple of 1/16 can be typed
            if(ImGui::InputDouble("bars", &b, 0, 0, "%g")){ L.bars = std::min(64.0, quantBars(b)); changed = true; }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Bars per image: more bars scroll more slowly; a fraction is shorter than a bar\n(0.5 = two beats, 0.25 = one beat; steps of 1/16 bar). %.2f s per image at speed 1.\n"
                                                         "Short pages need many renders: with Repeat they keep up.%s", engine->layerBars(L) * 4 * 60.0f / std::max(1.0, s.bpm), multNote.c_str());
            ImGui::SameLine(0, 2);
            if(ImGui::SmallButton("-##bars")){ L.bars = L.bars > 1 ? std::max(1.0, std::ceil(L.bars) - 1) : std::max(1.0 / kBarGrid, quantBars(L.bars / 2)); changed = true; }
            ImGui::SameLine(0, 2);
            if(ImGui::SmallButton("+##bars")){ L.bars = L.bars < 1 ? std::min(1.0, quantBars(L.bars * 2)) : std::min(64.0, std::floor(L.bars) + 1); changed = true; }
        }
        flow(labelled("repeat", 78 * zz));
        ImGui::SetNextItemWidth(78 * zz);
        if(ImGui::InputInt("repeat", &L.repeat)){ L.repeat = ofClamp(L.repeat, 1, 16); changed = true; }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each image (page) plays this many times in a row, then a new one comes (rendered while it repeats).\n"
                                                     "1: a new image every time. On layer 1 the harmony repeats with it, so the other layers follow its chords.\n"
                                                     "Takes effect from the next image asked for. %.1f s per new image at speed 1.", engine->layerBars(L) * L.repeat * 4 * 60.0f / std::max(1.0, s.bpm));
        {   // beat offset: this layer against the others, in beats (fractions allowed), live
            bool first = i == 0;
            flow(labelled("offset", 50 * zz));
            ImGui::BeginDisabled(first);
            ImGui::SetNextItemWidth(50 * zz);
            float off = first ? 0.0f : L.beatOffset;
            if(ImGui::DragFloat("offset", &off, 0.01f, -64.0f, 64.0f, "%.2f") && !first){ L.beatOffset = off; changed = true; }
            if(ImGui::IsItemClicked(ImGuiMouseButton_Right) && !first && L.beatOffset != 0){ L.beatOffset = 0; changed = true; }
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip(first ? "Beat offset. Layer 1 is the time reference (the clock follows it): offset the other layers against it."
                                        : "Beat offset: shifts this layer in time, in beats: positive plays it later, negative earlier (0.25 = a sixteenth, 1 = a beat, 4 = a bar).\nActs at once; drag, Ctrl-click to type, right-click for 0. Saved with the scene.\nIts kick detection and sound shift with it; its chords are still the ones of the bars it was drawn for.");
        }
        {   // blocking: the black background of this layer's blocking instruments (solo snares, bass gates...)
            flow(labelled("blocking", ImGui::GetFrameHeight()));
            if(ImGui::Checkbox("blocking", &L.blocking)) changed = true;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blocking instruments (blocking snares, coin snares, trance bass gates...) draw a black background\n"
                                                         "under their figures: it blanks the rest of their image and covers the layers below, like a solo.\n"
                                                         "Off: no instrument of this layer does that; their figures are drawn over everything else.\n"
                                                         "On: each instrument follows its own \"blocking\" option. Takes effect from the next image.");
        }
        flow(labelled("phrase", 44 * zz));
        ImGui::SetNextItemWidth(44 * zz);
        {
            double ph = L.phrase;
            if(ImGui::InputDouble("phrase", &ph, 0, 0, ph > 0 ? "%g" : "off")){ L.phrase = ph <= 0 ? 0 : std::min(64.0, quantBars(ph)); changed = true; }
            int n = phraseSlices(L);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Phrase (bars; 0 = off): one render draws the whole phrase and the layer shows it page by page,\n"
                                                         "each page the next part of it, so patterns and long notes run on across short pages\n"
                                                         "(e.g. bars 0.5, phrase 4: one 4-bar drawing shown in 8 pages). Rounded up to whole pages.\n"
                                                         "Now: %s. One render every %.1f s at speed 1 (Repeat repeats the whole phrase).\n"
                                                         "Drawn up to 16384 px wide; beyond that the pages are a little softer. Export and Gallery replay show the phrase whole.",
                                                         n > 1 ? (std::to_string(n) + " pages of " + barsText(L.bars) + " bars = " + barsText(phraseBars(L)) + " bars").c_str() : "off (the phrase is not longer than the page)",
                                                         engine->layerSpan(L) * L.repeat * 4 * 60.0f / std::max(1.0, s.bpm));
        }
        if(!compact){
            // ---- folded: cover
            auto cover = formscover::read(L.cover);
            if(ImGui::CollapsingHeader(cover.enabled ? "Cover  (on)###cover" : "Cover###cover")){
                bool edit = ImGui::Checkbox("Cover enabled", &cover.enabled);
                ImGui::TextWrapped("Use this layer's visible shapes to duck other layers. Below follows the visual stack order.");
                ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Grow right (px)", &cover.growRight, 0, 32);
                ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Grow height (px)", &cover.growHeight, 0, 32);
                ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Feather (px)", &cover.feather, 0, 32);
                ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Release (beats)", &cover.release, 0, 8);
                ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Cover amount", &cover.amount, 0, 1);
                int mode = cover.mode == "black" ? 1 : 0;
                ImGui::SetNextItemWidth(140 * zz);
                if(ImGui::Combo("Cover mode", &mode, "Eraser (transparent)\0Black\0")){ cover.mode = mode ? "black" : "eraser"; edit = true; }
                int target = cover.target == "all" ? 2 : cover.target == "layerBelow" ? 1 : 0;
                ImGui::SetNextItemWidth(140 * zz);
                if(ImGui::Combo("Cover acts on", &target, "All layers below\0Layer immediately below\0All other layers\0")){
                    cover.target = target == 2 ? "all" : target == 1 ? "layerBelow" : "allBelow"; edit = true;
                }
                if(edit){ L.cover = formscover::write(cover); changed = true; }
            }
            // ---- folded: the layer's effects before the sound (blur, trail, Perlin mask): this scene's,
            // saved with it (the Post-processing tab shows the ones of the scene on stage)
            if(ImGui::CollapsingHeader(LayerFxValues::from(L.fx, i).any() ? "Effects (blur, trail, mask)  (on)###fx" : "Effects (blur, trail, mask)###fx")){
                ImGui::TextDisabled("This scene's layer %d: saved with the scene.", i + 1);
                ImGui::PushItemWidth(-(110 * zz));
                changed |= drawLayerFxScene(L, i);
                ImGui::PopItemWidth();
            }
        }
        // a click anywhere on the card selects its layer (its instruments are listed at the right)
        if(ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem) && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) sceneLayer = i;
        ImGui::EndChild();
        ImGui::PopStyleColor(2);
        ImGui::PopID();
    }
    ImGui::PopStyleVar();
    if(dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo && dragFrom < (int)order.size() && dragTo < (int)order.size()){
        int moved = order[dragFrom];
        order.erase(order.begin() + dragFrom);
        order.insert(order.begin() + dragTo, moved);
        for(int k = 0; k < (int)order.size(); k++) s.layers[order[k]].z = k;
        changed = true;
    }
    // reorder: swap stacking positions, then number them 0..n-1
    if(moveUp >= 0 || moveDown >= 0){
        int r = moveUp >= 0 ? moveUp : moveDown, o = moveUp >= 0 ? r + 1 : r - 1;
        std::swap(order[r], order[o]);
        for(int k = 0; k < (int)order.size(); k++) s.layers[order[k]].z = k;
        changed = true;
    }
    // remove: after the cards (they hold references into the layers)
    if(removeAt > 0 && removeAt < (int)s.layers.size()){
        s.layers.erase(s.layers.begin() + removeAt);
        std::vector<int> o2 = stackOrder(s.layers);
        bool anyZ = false; for(auto& x : s.layers) if(x.z >= 0) anyZ = true;
        if(anyZ) for(int k = 0; k < (int)o2.size(); k++) s.layers[o2[k]].z = k;
        if(sceneLayer > removeAt) sceneLayer--;
        else if(sceneLayer == removeAt) sceneLayer = std::max(0, removeAt - 1);
        changed = true;
    }
    if(compact) return changed;
    if(ImGui::Button("+ layer")){
        LayerDef nl; nl.bars = 8; nl.blend = "lighten"; nl.gain = 1; nl.rows = 96;
        nl.groupProb = s.layers[0].groupProb;
        int top = 0; for(auto& x : s.layers) top = std::max(top, x.z >= 0 ? x.z : (int)(&x - &s.layers[0]));
        nl.z = top + 1;
        s.layers.push_back(nl);
        sceneLayer = (int)s.layers.size() - 1;
        changed = true;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("A transparent layer with its own bars per image (it scrolls at its own speed), instruments, blend and opacity");
    return changed;
}

// The scene's performance settings (sound levels, stereo, speed modulation, gate, tremolo,
// header jumps, beat repeat): they take effect when its images reach the header
inline bool Editor::drawPerformance(Scene& s){
    bool changed = false;
    if(!ImGui::CollapsingHeader("Performance (sound, stereo, speed, gate, tremolo, repeat)")) return false;
    json full = completePerformance(s.performance);
    if(full != s.performance){ s.performance = full; }
    std::string group;
    bool open = false;
    for(auto& p : perfParams()){
        if(group != p.group){
            if(open) ImGui::TreePop();
            group = p.group;
            open = ImGui::TreeNodeEx(p.group, ImGuiTreeNodeFlags_DefaultOpen);
        }
        if(!open) continue;
        changed |= drawPerfParam(s, p);
    }
    if(open) ImGui::TreePop();
    return changed;
}


// One performance setting (a slider, a combo, a list or the kick sample picker); itemWidth 0 = the editor's default
inline bool Editor::drawPerfParam(Scene& s, const PerfParam& p, float itemWidth){
    bool changed = false;
    ImGui::PushID(p.key);
    ImGui::SetNextItemWidth(itemWidth != 0 ? itemWidth : 220 * z());
    json& v = s.performance[p.key];
    if(p.type == 'f'){
        float f = v.is_number() ? v.get<float>() : p.def.get<float>();
        if(ImGui::SliderFloat(p.label, &f, p.min, p.max)){ v = f; changed = true; }
    }else if(p.type == 'i'){
        int i = v.is_number() ? (int)std::lround(v.get<float>()) : p.def.get<int>();
        if(p.key==std::string("kickSynth")){
            if(ImGui::Combo(p.label,&i,"Sample\0Synth\0")){v=i;changed=true;}
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Both sources use the marked kick layer. Enable Kick detect here and Kick on in Sonification.");
        }else if(ImGui::SliderInt(p.label, &i, (int)p.min, (int)p.max)){ v = i; changed = true; }
    }else if(p.type == 's'){
        // Folder samples retain their legacy names; a browsed sample belongs to this scene.
        std::string cur = v.is_string() ? v.get<std::string>() : std::string();
        std::string label=s.kickSample?s.kickSample->name+" (embedded)":cur.empty()?"(by number)":cur;
        if(browseKickSample){   // leave room for the Browse button after the label, inside the column
            float reserve = ImGui::CalcTextSize("Browse...").x + ImGui::GetStyle().FramePadding.x * 2 + ImGui::GetStyle().ItemSpacing.x;
            float base = itemWidth != 0 ? itemWidth : 220 * z();
            float labelW = ImGui::CalcTextSize(p.label, nullptr, true).x + ImGui::GetStyle().ItemInnerSpacing.x;
            if(base > 0) base = std::min(base, ImGui::GetContentRegionAvail().x - labelW);
            else base = std::min(base, -labelW);   // the label gutter must hold the whole label
            ImGui::SetNextItemWidth(base < 0 ? base - reserve : std::max(60 * z(), base - reserve));
        }
        if(ImGui::BeginCombo(p.label,label.c_str())){
            if(ImGui::Selectable("(by number)",cur.empty()&&!s.kickSample)){v="";s.kickSample.reset();s.performance["kickSynth"]=0;changed=true;}
            for(auto& k:kickNames) if(ImGui::Selectable(k.c_str(),k==cur&&!s.kickSample)){
                if(selectKickSample) changed|=selectKickSample(s,k);
                else {v=k;s.kickSample.reset();changed=true;}
            }
            ImGui::EndCombo();
        }
        if(browseKickSample){
            ImGui::SameLine();
            if(ImGui::Button("Browse...")) changed|=browseKickSample(s);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Choose a WAV or AIFF from any folder. Save embeds the audio bytes in this scene.");
            if(!kickSampleError.empty()) ImGui::TextWrapped("%s",kickSampleError.c_str());
        }
    }else{
        std::string txt;
        if(v.is_array()) for(size_t k = 0; k < v.size(); k++) txt += (k ? ", " : "") + ofToString(v[k].get<float>(), 4);
        char buf[256];
        strncpy(buf, txt.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
        if(ImGui::InputText(p.label, buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)){
            json arr = json::array();
            for(auto& t : ofSplitString(buf, ",", true, true)) if(!t.empty()) arr.push_back(ofClamp(ofToFloat(t), p.min, p.max));
            if(!arr.empty()){ v = arr; changed = true; }
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Comma-separated values, Enter to apply");
    }
    perfParamMenu(s, p, v, changed);   // right-click on the control
    ImGui::PopID();
    return changed;
}

inline std::string Editor::familyOf(const std::string& m){
    for(auto& [n, f] : engine->methods) if(n == m) return f;
    return m.rfind("fx", 0) == 0 ? "legacy/fx" : "legacy/" + m.substr(0, 1);
}

inline bool Editor::drawCellCards(Scene& s, LayerDef& l){
    float zz = z();
    bool changed = false;
    if(!ImGui::BeginTable("##cellEditorLayout", 1, ImGuiTableFlags_SizingStretchProp)) return false;
    ImGui::TableSetupColumn("cells", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableNextColumn();
    float avail = ImGui::GetContentRegionAvail().x;
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float cardW = std::max(1.f, std::min(168 * zz, avail));
    const int perRow = std::max(1, (int)((avail + gap) / (cardW + gap)));
    int shown = 0, removeAt = -1;
    int moveCell = -1, moveTo = -2;      // right-click "Move to layer": done after the loop
    for(int i = 0; i < (int)l.cells.size(); i++){
        Cell& c = l.cells[i];
        if(c.item.empty()) continue;
        bool beyond = i >= l.rows;
        if(shown % perRow) ImGui::SameLine();
        shown++;
        ImGui::PushID(i);
        // The child owns the card border and clips its contents to a fixed width. ImageButton
        // padding and long instrument names must never grow a row into the editor column.
        ImGui::BeginChild("##cellCard", ImVec2(cardW, 0),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
        const float contentW = std::max(1.f, ImGui::GetContentRegionAvail().x);
        const float thumbW = contentW, imgH = thumbW * 9 / 16;
        ImVec2 p = ImGui::GetCursorScreenPos();
        auto* dl = ImGui::GetWindowDrawList();
        float alpha = (beyond || c.prob <= 0) ? 0.35f : 1.0f;
        dl->AddRectFilled(p, ImVec2(p.x + thumbW, p.y + imgH), IM_COL32(8, 8, 10, 255), 3 * zz);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        const ofTexture* t = thumbs.get(c.item);
        bool clicked;
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 0));
        if(t) clicked = ImGui::ImageButton("##thumb", texId(*t), ImVec2(thumbW, imgH), ImVec2(0, 0), ImVec2(1, 1));
        else { clicked = ImGui::Button("##thumb", ImVec2(thumbW, imgH)); dl->AddText(ImVec2(p.x + 6 * zz, p.y + imgH / 2 - 6 * zz), IM_COL32(110, 110, 110, 255), "no thumbnail"); }
        ImGui::PopStyleVar();
        // click: edit the instrument beside the cells (a legacy method has no editor: the picker instead)
        auto replaceHere = [this, name = s.name, i, layerIdx = sceneLayer](){
            onPick = [this, name, i, layerIdx](const std::string& m){
                auto it2 = engine->scenes.find(name);
                if(it2 != engine->scenes.end() && layerIdx < (int)it2->second.layers.size() && i < (int)it2->second.layers[layerIdx].cells.size()){
                    it2->second.layers[layerIdx].cells[i].item = m;
                    dirty.insert(name);
                }
            };
            openPicker = true;
        };
        if(clicked){
            if(engine->presets.count(c.item)) openInstrument(c.item, s.name);
            else replaceHere();
        }
        if(ImGui::IsItemHovered()) methodTooltip(c.item);
        if(ImGui::BeginPopupContextItem("##cellmenu")){
            if(ImGui::MenuItem("Replace instrument...")) replaceHere();
            
            ImGui::Separator();
            instrumentMenuItems(c.item, s.name, sceneLayer, i);
            ImGui::EndPopup();
        }
        // family stripe + solo badge
        std::string fam = familyOf(c.item);
        ImVec4 fc = familyColor(fam);
        dl->AddRectFilled(p, ImVec2(p.x + 4 * zz, p.y + imgH), ImGui::GetColorU32(fc));
        if(theme::enabled()){   // the family's shape too (colour is never the only cue)
            std::string g = fam.rfind("gen/", 0) == 0 ? fam.substr(4) : familyRoot(fam);
            auto f = theme::familyOfGen(g);
            float r = 5 * zz;
            dl->AddCircleFilled(ImVec2(p.x + thumbW - 2 * r - 2 * zz, p.y + 2 * r + 2 * zz), r + 2 * zz, IM_COL32(0, 0, 0, 160));
            theme::drawShape(dl, ImVec2(p.x + thumbW - 2 * r - 2 * zz, p.y + 2 * r + 2 * zz), r * 0.8f, f, theme::u32(theme::familyHex(f)));
        }
        if(beyond) dl->AddText(ImVec2(p.x + 8 * zz, p.y + 4 * zz), IM_COL32(255, 180, 80, 255), "beyond Rows");
        // Reserve exactly one text line, clipping long names without widening the card.
        ImVec2 labelPos = ImGui::GetCursorScreenPos();
        ImVec4 labelClip(labelPos.x, labelPos.y, labelPos.x + contentW, labelPos.y + ImGui::GetTextLineHeight());
        dl->AddText(nullptr, 0, labelPos, ImGui::GetColorU32(fc), shortName(c.item).c_str(), nullptr, 0, &labelClip);
        ImGui::Dummy(ImVec2(contentW, ImGui::GetTextLineHeight()));
        const float groupW = 26 * zz;
        const float removeW = ImGui::CalcTextSize("x").x + 2 * ImGui::GetStyle().FramePadding.x;
        ImGui::SetNextItemWidth(std::max(1.f, contentW - groupW - removeW - 2 * gap));
        changed |= ImGui::SliderFloat("##p", &c.prob, 0, 100, "%.0f%%");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(groupW);
        if(ImGui::BeginCombo("##g", ("G" + ofToString(c.group)).c_str(), ImGuiComboFlags_NoArrowButton)){
            for(int g = 0; g < 7; g++) if(ImGui::Selectable(("G" + ofToString(g)).c_str(), c.group == g)){ c.group = g; changed = true; }
            ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Group (its group chance gates this instrument)");
        ImGui::SameLine();
        if(ImGui::SmallButton("x")) removeAt = i;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Remove");
        ImGui::PopStyleVar();
        ImGui::EndChild();
        ImGui::PopID();
    }
    if(removeAt >= 0){
        l.cells[removeAt].item.clear();
        l.cells[removeAt].prob = 0;
        changed = true;
    }
    if(moveCell >= 0 && moveTo != -2){
        // take it out of this layer first (adding a layer may move the layers in memory)
        Cell c = l.cells[moveCell];
        std::vector<float> groupProb = l.groupProb;
        l.cells[moveCell].item.clear();
        l.cells[moveCell].prob = 0;
        if(moveTo == -1){
            LayerDef nl; nl.bars = 8; nl.blend = "lighten"; nl.gain = 1; nl.rows = 96; nl.groupProb = groupProb;
            s.layers.push_back(nl);
            moveTo = (int)s.layers.size() - 1;
        }
        LayerDef& tl = s.layers[moveTo];
        bool placed = false;
        for(int k = 0; k < std::min<int>(tl.rows, (int)tl.cells.size()); k++)
            if(tl.cells[k].item.empty()){ tl.cells[k] = c; placed = true; break; }
        if(!placed){
            tl.cells.insert(tl.cells.begin() + std::min<int>(tl.rows, (int)tl.cells.size()), c);
            if((int)tl.cells.size() > tl.rows) tl.rows = std::min<int>(tl.rows + 1, (int)tl.cells.size());
        }
        changed = true;
        ImGui::EndTable();
        return changed;   // `l` may no longer be valid
    }
    if(!shown) ImGui::TextDisabled("No instruments yet: + Add instrument.");
    ImGui::EndTable();
    return changed;
}

inline void Editor::drawSceneInstrumentEditor(std::string ref){
    float zz = z();
    if(ref.empty() || !engine->presets.count(ref)){
        ImGui::TextDisabled("Click an instrument thumbnail to edit it here.");
        return;
    }
    // This table follows the scene settings and may start near the viewport's bottom.
    // Give the editor its own height: drawParamTabs fills its parent's remaining space,
    // which otherwise collapses to a few pixels here. The scene scrolls to reveal this
    // panel, and long parameter groups scroll inside their tab as in Instruments.
    ImGui::BeginChild("##sceneInstrumentEditor", ImVec2(0, 0));   // fills its floating window
    // Opened from a scene: the window edits that scene's own version of the instrument. The edits
    // are kept in the scene (Scene::instruments: only what differs) and saved with it; the shared
    // instrument and the other scenes stay as they are.
    Scene* own = nullptr;
    if(!sceneInstrumentEditScene.empty()){
        auto si = engine->scenes.find(sceneInstrumentEditScene);
        if(si != engine->scenes.end()) own = &si->second; else sceneInstrumentEditScene.clear();
    }
    Preset work;
    if(own) work = engine->effectivePreset(*own, ref);
    Preset& p = own ? work : engine->presets[ref];
    bool ownEdited = false;
    auto touch = [&]{ if(own) ownEdited = true; else dirtyPresets.insert(ref); };
    auto storeOwn = [&]{   // the working copy back into the scene, as its difference from the shared instrument
        json d = presetOwnDiff(engine->presets[ref], work);
        if(d.empty()) own->instruments.erase(ref); else own->instruments[ref] = d;
        dirty.insert(own->name);
    };
    const GenDef* g = engine->findGen(p.gen);
    ImGui::TextColored(genColour(p.gen), "%s", p.name.c_str());
    ImGui::SameLine();
    if(ImGui::SmallButton("Close##sceneInstrumentEditor")){
        sceneInstrumentEditRef.clear();
        ImGui::EndChild();
        return;
    }
    ImGui::SameLine();
    if(ImGui::SmallButton("Show in Library##sceneInstrumentEditor")){ selPreset = ref; composeMode = 1; showTab = "Compose"; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("The full instrument page: preview, audition, stack layers, notes");
    ImGui::SameLine();
    if(own){
        const bool has = own->instruments.is_object() && own->instruments.contains(ref);
        ImGui::BeginDisabled(!has);
        if(ImGui::SmallButton("Save to instrument##sceneInstrumentEditor")){
            engine->savePreset(work);            // the scene's version becomes the shared instrument (its file)
            dirtyPresets.erase(ref);
            own->instruments.erase(ref);
            dirty.insert(own->name);
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Make this scene's version the shared instrument and save it to the instrument's file:\nevery scene that uses the instrument then plays it like this.");
        ImGui::SameLine();
        if(ImGui::SmallButton("Revert to instrument##sceneInstrumentEditor")){
            own->instruments.erase(ref);         // this scene plays the shared instrument again
            dirty.insert(own->name);
            work = engine->presets[ref];
        }
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Drop this scene's changes: the scene plays the shared instrument again.");
        ImGui::EndDisabled();
    }else{
        bool isDirty = dirtyPresets.count(ref) > 0;
        if(ImGui::SmallButton(isDirty ? "Save *##sceneInstrumentEditor" : "Save##sceneInstrumentEditor")){
            engine->savePreset(p);
            dirtyPresets.erase(ref);
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!isDirty);
        if(ImGui::SmallButton("Revert##sceneInstrumentEditor")){
            try { engine->presets[ref] = Engine::readPresetFile(engine->presetPath(p)); dirtyPresets.erase(ref); }
            catch(std::exception& e){ engine->note("revert " + ref + ": " + e.what()); }
        }
        ImGui::EndDisabled();
    }
    if(immediateSwitch){
        ImGui::SameLine();
        ImGui::Checkbox("Immediate##sceneInstrument",immediateSwitch);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Redraw playing and queued pages after instrument edits, including nested stack instruments. No Save needed to hear the edit.");
    }
    if(own){
        const bool has = own->instruments.is_object() && own->instruments.contains(ref);
        if(has) ImGui::TextColored(ImVec4(1.0f, 0.78f, 0.35f, 1), "Edited in scene %s: %d setting%s differ from the shared instrument.", own->name.c_str(),
                                   presetOwnCount(own->instruments[ref]), presetOwnCount(own->instruments[ref]) == 1 ? "" : "s");
        else ImGui::TextDisabled("Scene %s plays the shared instrument.", own->name.c_str());
        ImGui::TextDisabled("Edits here stay in this scene and are saved with the scene; the other scenes do not change.");
    }
    ImGui::Separator();
    if(p.isStack()){
        if(drawStackBlocking(p.params)) touch();
        if(ImGui::CollapsingHeader("Cover (whole instrument)"))
            if(drawCoverParams(p.params)) touch();
        if(sceneStackFor != ref){ sceneStackFor = ref; sceneStackLayer = 0; }
        for(int i = 0; i < (int)p.layers.size(); i++){
            if(i) ImGui::SameLine();
            std::string label = std::to_string(i + 1) + "##sceneStackLayer";
            if(ImGui::RadioButton(label.c_str(), sceneStackLayer == i)) sceneStackLayer = i;
        }
        if(p.layers.empty()) ImGui::TextDisabled("This Stack has no layers.");
        else {
            sceneStackLayer = ofClamp(sceneStackLayer, 0, (int)p.layers.size() - 1);
            json& layer = p.layers[sceneStackLayer];
            if(layer.contains("preset")){
                std::string nested = layer.value("preset", std::string());
                ImGui::TextWrapped("This layer plays %s.", nested.c_str());
                if(engine->presets.count(nested) && ImGui::Button("Edit nested instrument")) sceneInstrumentEditRef = nested;
            }else{
                std::string genName = layer.value("gen", std::string());
                const GenDef* layerGen = engine->findGen(genName);
                if(layerGen){
                    json params = layer.contains("params") && layer["params"].is_object() ? layer["params"] : json::object();
                    ImGui::TextDisabled("Layer %d · %s", sceneStackLayer + 1, layerGen->label.c_str());
                    if(drawParamTabs(*layerGen, params)){ layer["params"] = params; touch(); }
                }else ImGui::TextDisabled("Unknown layer generator: %s", genName.c_str());
            }
        }
    }else if(g){
        ImGui::TextDisabled("Editing %s", ref.c_str());
        if(drawParamTabs(*g, p.params)) touch();
        if(!own && ImGui::CollapsingHeader("Notes")){   // the notes describe the shared instrument
            char noteBuf[512];
            snprintf(noteBuf, sizeof(noteBuf), "%s", p.notes.c_str());
            if(ImGui::InputTextMultiline("##scenePresetNotes", noteBuf, sizeof(noteBuf), ImVec2(-1, 70 * zz))){ p.notes = noteBuf; dirtyPresets.insert(ref); }
        }
    }else{
        ImGui::TextDisabled("Unknown generator: %s", p.gen.c_str());
    }
    if(own && ownEdited) storeOwn();
    ImGui::EndChild();
}

// A scene layer's own effects (LayerDef::fx): the same controls as the Post-processing tab, on the
// scene's values. The scene on stage shows them at once (the node's controls follow it).
inline bool Editor::drawLayerFxScene(LayerDef& L, int layer){
    LayerFxValues v = LayerFxValues::from(L.fx, layer);
    const LayerFxValues before = v;
    if(layerFxOn){
        bool on = layerFxOn->get();
        if(ImGui::Checkbox("Layer effects on", &on)) *layerFxOn = on;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The master switch for every layer of every scene (saved with the patch; also in Post-processing)");
    }
    ImGui::SliderFloat("H blur", &v.hBlur, 0, 60, "%.1f px");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blurs this layer horizontally (Gaussian sigma, px); the edges repeat, nothing comes from outside");
    ImGui::SliderFloat("V blur", &v.vBlur, 0, 60, "%.1f px");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blurs this layer vertically (Gaussian sigma, px): the sound's partials smear into their neighbours.");
    ImGui::SliderFloat("Trail", &v.trail, 0, 16, "%.2f beats");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("A tail behind what moves (to the right of it), fading like a reverb: its length in beats\n(whatever the layer's speed). At the header it sounds like a reverb.");
    ImGui::SliderFloat("Trail level", &v.trailLevel, 0, 1, "%.2f");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("How bright the tail starts (1: as bright as what leaves it)");
    ImGui::SeparatorText("Perlin mask");
    ImGui::SliderFloat("Mask amount", &v.maskAmount, 0, 1, "%.2f");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Masks this whole layer, including its blur and trail, before compositing and sonification. 0: off.");
    ImGui::SliderFloat("Mask width", &v.maskWidth, 0.125f, 32, "%.3f beats");
    ImGui::SliderFloat("Mask height", &v.maskHeight, 0.01f, 2, "%.3f");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Noise cell height as a fraction of the layer height; independent of resolution.");
    ImGui::SliderFloat("Mask contrast", &v.maskContrast, 0.1f, 8, "%.2f");
    ImGui::SliderFloat("Mask threshold", &v.maskThreshold, 0, 1, "%.2f");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("0: soft Perlin mask. Above 0: cut the noise into open / closed regions.");
    ImGui::SliderFloat("Mask blur", &v.maskBlur, 0, 60, "%.1f px");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Gaussian blur after the threshold, applied only to the mask. 0: off.");
    ImGui::Checkbox("Invert mask", &v.maskInvert);
    ImGui::SliderInt("Mask seed", &v.maskSeed, 0, 65535);
    ImGui::Checkbox("Animated", &v.maskAnimate);
    if(v.maskAnimate){
        ImGui::SliderFloat("Evolution speed", &v.maskSpeed, 0, 4, "%.3f / beat");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Perlin evolution per musical beat. Follows playback speed and pauses with the transport.");
    }
    if(before.any() && ImGui::SmallButton("No effects on this layer")) v = LayerFxValues::from(json(), layer);
    if(v == before) return false;
    L.fx = v.toJson(layer);
    if(L.fx.empty()) L.fx = json();
    return true;
}

} // namespace forms
