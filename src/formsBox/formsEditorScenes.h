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

inline void Editor::drawScenesTab(){
    float zz = z();
    // list
    ImGui::BeginChild("##sceneList", ImVec2(210 * zz, 0), ImGuiChildFlags_Borders);
    {
        // the scene under the header, one click away
        std::string playing = engine->schedule && engine->schedule->layers[0].header ? engine->schedule->layers[0].header->scene : std::string();
        bool can = !playing.empty() && engine->scenes.count(playing);
        ImGui::BeginDisabled(!can || selected == playing);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.45f, 0.30f, 0.08f, 1));
        if(ImGui::Button(("Edit playing: " + (can ? playing : std::string("-"))).c_str(), ImVec2(-FLT_MIN, 0))) selected = playing;
        ImGui::PopStyleColor();
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip(can ? (selected == playing ? "Already editing the scene under the header" : "Select the scene under the header") : "Nothing playing yet");
    }
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
    ImGui::EndChild();
    ImGui::SameLine();

    // editor
    ImGui::BeginChild("##sceneEdit", ImVec2(0, 0), ImGuiChildFlags_Borders);
    auto it = engine->scenes.find(selected);
    if(it == engine->scenes.end()){
        ImGui::TextDisabled("No scene. Press New, or import the 1920 scenes in the System tab.");
        ImGui::EndChild();
        return;
    }
    Scene& s = it->second;
    bool changed = false;

    // name, save, preview
    char nameBuf[128];
    strncpy(nameBuf, s.name.c_str(), sizeof(nameBuf)); nameBuf[sizeof(nameBuf) - 1] = 0;
    ImGui::SetNextItemWidth(260 * zz);
    ImGui::SetWindowFontScale(1.25f);
    if(ImGui::InputText("##sceneName", nameBuf, sizeof(nameBuf), ImGuiInputTextFlags_EnterReturnsTrue)){
        std::string old = s.name, nn = nameBuf;
        if(engine->renameScene(old, nn)){ dirty.erase(old); selected = nn; ImGui::SetWindowFontScale(1.0f); ImGui::EndChild(); return; }
    }
    ImGui::SetWindowFontScale(1.0f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rename: type and press Enter");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150 * zz);
    if(ImGui::BeginCombo("Bank", Engine::bankLabel(s.bank).c_str())){
        for(auto& b : engine->banks()) if(ImGui::Selectable((Engine::bankLabel(b) + "##mv" + b).c_str(), b == s.bank)){
            if(dirty.count(s.name)){ engine->saveScene(s); dirty.erase(s.name); }
            engine->moveSceneToBank(s.name, b);
        }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("The bank this scene's file is in: pick another to move it there");
    ImGui::SameLine();
    bool isDirty = dirty.count(s.name) > 0;
    if(isDirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1));
    if(ImGui::Button(isDirty ? "Save *" : "Save")){ engine->saveScene(s); dirty.erase(s.name); }
    if(isDirty) ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::BeginDisabled(!isDirty);
    if(ImGui::Button("Revert")){ engine->reloadScene(s.name); dirty.erase(s.name); }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(playScene){
        if(ImGui::Button("Play this scene")) playScene(s.name);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Turn the setlist off and play this scene from the next image\n(the Scene parameter follows).");
        ImGui::SameLine();
    }
    if(ImGui::Button("Render preview")) renderScene(s.name);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Render one image of this scene now (unsaved edits included);\nit scrolls in next. The setlist doesn't move.");
    if(s.snapshot >= 0){ ImGui::SameLine(); ImGui::TextDisabled("from 1920 Director slot %d", s.snapshot); }
    {   // edits redraw the pages on stage (Immediate mode) only when this is the scene that is playing
        std::string playing = engine->schedule && engine->schedule->layers[0].header ? engine->schedule->layers[0].header->scene : std::string();
        if(!playing.empty() && playing != s.name){
            ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Not the scene under the header (%s is playing): edits here reach the stage only when this scene plays.", playing.c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton("Edit the playing scene")) selected = playing;
        }else if(immediateSwitch && !*immediateSwitch) ImGui::TextDisabled("Immediate mode is off: edits reach the next images (Instruments tab > Immediate to redraw the pages on stage).");
    }
    ImGui::Separator();

    if(s.layers.empty()) s.layers.push_back(LayerDef());
    if(sceneLayerFor != s.name){ sceneLayerFor = s.name; sceneLayer = 0; }
    sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    const ImVec4 layerBlue(0.55f, 0.85f, 1.0f, 1);

    // ---- LAYERS (left) | SCENE (right)
    if(ImGui::BeginTable("##sceneTop", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
        ImGui::TableSetupColumn("layers", ImGuiTableColumnFlags_WidthStretch, 1.1f);
        ImGui::TableSetupColumn("scene", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
        changed |= drawLayerStack(s);
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "SCENE");
        ImGui::SameLine();
        ImGui::TextDisabled("(all layers)");
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("BPM", &s.bpm); s.bpm = ofClamp(s.bpm, 1, 999);
        changed |= paletteCombo("Palette", s.palette);
        changed |= scaleCombo("Scale", s.scale);
        if(engine->pitchFollow.scaleOn){
            static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            std::string nt; for(int n : engine->pitchFollow.notes) nt += (nt.empty() ? "" : " ") + ofToString(n);
            ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "Global Pitch: %s on %s%s (the scene scale is overridden; untick Forms in the Global Pitch inspector to keep it)", nt.c_str(), nn[((engine->pitchFollow.rootPc % 12) + 12) % 12], engine->pitchFollow.shift ? (", shift " + ofToString(engine->pitchFollow.shift)).c_str() : "");
        }
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Notegrid", &s.notegrid); s.notegrid = ofClamp(s.notegrid, 12, 400);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels per semitone in the images (100 in every 1920 scene)");
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Root note (sound)", &s.root); s.root = ofClamp(s.root, 0, 127);
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Bass degree", &s.bassDegree); s.bassDegree = ofClamp(s.bassDegree, 0, 11);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scale degree the bass instruments play, added to the bar's degree (0 = the root; past the scale's last degree it goes up an octave).\nAn instrument is a bass when its 'bass instrument' pitch option is on (next images).");
        if(ImGui::TreeNode("Harmony (which degrees and transposes, per bar)")){
            const ScaleDef es = engine->effectiveScale(s);
            const ScaleDef* sd = es.name.empty() ? nullptr : &es;
            changed |= weightBars("Degrees", s.degreeWeights, sd ? (int)sd->notes.size() : 12, true, sd);
            changed |= weightBars("Transpose", s.transposeWeights, 12, false, nullptr);
            changed |= drawHarmonyText(s);
            ImGui::TreePop();
        }
        ImGui::EndTable();
    }
    changed |= drawPerformance(s);
    ImGui::Spacing();

    // ---- the selected layer's instruments
    LayerDef& l = s.layers[sceneLayer];
    int active = 0;
    for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(!l.cells[i].item.empty() && l.cells[i].prob > 0) active++;
    ImGui::PushStyleColor(ImGuiCol_Separator, ImVec4(0.25f, 0.55f, 0.70f, 1));
    ImGui::SeparatorText(("LAYER " + ofToString(sceneLayer + 1) + " INSTRUMENTS  (" + ofToString(active) + " active, " + ofToString(l.bars) + " bars per image)").c_str());
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
    ImGui::TextDisabled("| click a thumbnail to swap, right-click to edit or move, hover for details");
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

    if(changed) dirty.insert(s.name);
    ImGui::EndChild();
}

// The layer stack: top of the stack first. Each row: select, name (bars, blend), opacity,
// up / down in the stack, kick. Below: the selected layer's bars, blend and kick, + layer,
// remove. Opacity, blend and order apply live to what is playing.
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
    ImGui::TextDisabled("  the scene under the header  |  bank %s  |  bar %d  |  %d BPM", Engine::bankLabel(s.bank).c_str(), bar, s.bpm);
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
        ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("BPM", &s.bpm); s.bpm = ofClamp(s.bpm, 1, 999);
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

inline bool Editor::drawLayerStack(Scene& s, bool compact){
    float zz = z();
    bool changed = false;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.10f, 0.22f, 0.30f, 1));
    ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.25f, 0.55f, 0.70f, 1));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * zz, 8 * zz));
    ImGui::BeginChild("##layerstack", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "LAYERS");
    ImGui::SameLine();
    ImGui::TextDisabled("top of the stack first; layer 1 sets the clock");
    std::vector<int> order = stackOrder(s.layers);           // bottom .. top
    int moveUp = -1, moveDown = -1;
    for(int r = (int)order.size() - 1; r >= 0; r--){
        int i = order[r];
        LayerDef& L = s.layers[i];
        ImGui::PushID(i);
        bool sel = sceneLayer == i;
        ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.20f, 0.55f, 0.75f, 1) : ImVec4(0.14f, 0.20f, 0.25f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, sel ? ImVec4(0.25f, 0.62f, 0.82f, 1) : ImVec4(0.20f, 0.30f, 0.38f, 1));
        std::string lab = "Layer " + ofToString(i + 1) + (L.kick || (i == 0 && std::none_of(s.layers.begin(), s.layers.end(), [](const LayerDef& x){ return x.kick; })) ? "  [kick]" : "") +
                          "\n" + ofToString(L.bars) + " bars" + (L.repeat > 1 ? " x" + ofToString(L.repeat) : std::string()) + ", " + L.blend;
        if(ImGui::Button(lab.c_str(), ImVec2(150 * zz, 38 * zz))) sceneLayer = i;
        ImGui::PopStyleColor(2);
        ImGui::SameLine();
        ImGui::BeginGroup();
        ImGui::SetNextItemWidth(130 * zz);
        if(ImGui::SliderFloat("##op", &L.gain, 0, 2, "volume %.2f")) changed = true;
        ImGui::BeginDisabled(r == (int)order.size() - 1);
        if(ImGui::ArrowButton("##up", ImGuiDir_Up)) moveUp = r;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move up in the stack (drawn over the layers below)");
        ImGui::SameLine();
        ImGui::BeginDisabled(r == 0);
        if(ImGui::ArrowButton("##down", ImGuiDir_Down)) moveDown = r;
        ImGui::EndDisabled();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Move down in the stack");
        if(layerMS && toggleLayerMS){
            // mute / solo: performance state (not the scene's, not saved), seen and heard at once
            int st = layerMS(i);
            ImGui::SameLine(0, 10 * zz);
            if(msButton("M", st & 1, false, "Mute this layer: hidden and silent (it keeps scrolling); not saved")) toggleLayerMS(i, 0);
            ImGui::SameLine(0, 2);
            if(msButton("S", st & 2, true, "Solo: only the soloed layers are seen and heard; not saved")) toggleLayerMS(i, 1);
        }
        ImGui::EndGroup();
        ImGui::PopID();
    }
    // reorder: swap stacking positions, then number them 0..n-1
    if(moveUp >= 0 || moveDown >= 0){
        int r = moveUp >= 0 ? moveUp : moveDown, o = moveUp >= 0 ? r + 1 : r - 1;
        std::swap(order[r], order[o]);
        for(int k = 0; k < (int)order.size(); k++) s.layers[order[k]].z = k;
        changed = true;
    }
    if(compact){ ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(2); return changed; }
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
    // the selected layer's own settings
    LayerDef& sl = s.layers[sceneLayer];
    ImGui::Separator();
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "Layer %d", sceneLayer + 1);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * zz);
    if(ImGui::InputInt("bars", &sl.bars)){ sl.bars = ofClamp(sl.bars, 1, 64); changed = true; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Bars per image: more bars scroll more slowly. %.1f s per image at speed 1.", sl.bars * 4 * 60.0f / std::max(1, s.bpm));
    ImGui::SameLine();
    ImGui::SetNextItemWidth(90 * zz);
    if(ImGui::InputInt("repeat", &sl.repeat)){ sl.repeat = ofClamp(sl.repeat, 1, 16); changed = true; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each image (page) plays this many times in a row, then a new one comes (rendered while it repeats).\n"
                                                 "1: a new image every time. On layer 1 the harmony repeats with it, so the other layers follow its chords.\n"
                                                 "Takes effect from the next image asked for. %.1f s per new image at speed 1.", sl.bars * sl.repeat * 4 * 60.0f / std::max(1, s.bpm));
    ImGui::SameLine();
    const char* blends[] = {"lighten", "add", "normal"};
    int bi = sl.blend == "add" ? 1 : sl.blend == "normal" ? 2 : 0;
    ImGui::SetNextItemWidth(90 * zz);
    if(ImGui::Combo("blend", &bi, blends, 3)){ sl.blend = blends[bi]; changed = true; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("How it combines with the layers below it:\nlighten = the brighter of both, add = the sum, normal = over them (by its alpha)");
    ImGui::SameLine();
    if(ImGui::Checkbox("kick", &sl.kick)){
        if(sl.kick) for(int k = 0; k < (int)s.layers.size(); k++) if(k != sceneLayer) s.layers[k].kick = false;
        changed = true;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Kick detection (the Kick output) reads this layer. None ticked: layer 1.");
    if(ImGui::CollapsingHeader("Cover (this layer)")){
        auto cover = formscover::read(sl.cover);
        bool edit = ImGui::Checkbox("Cover enabled", &cover.enabled);
        ImGui::TextWrapped("Use this layer's visible shapes to duck other layers. Below follows the visual stack order.");
        ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Grow right (px)", &cover.growRight, 0, 32);
        ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Grow height (px)", &cover.growHeight, 0, 32);
        ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Feather (px)", &cover.feather, 0, 32);
        ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Release (beats)", &cover.release, 0, 8);
        ImGui::SetNextItemWidth(140 * zz); edit |= ImGui::SliderFloat("Cover amount", &cover.amount, 0, 1);
        int mode = cover.mode == "black" ? 1 : 0;
        if(ImGui::Combo("Cover mode", &mode, "Eraser (transparent)\0Black\0")){ cover.mode = mode ? "black" : "eraser"; edit = true; }
        int target = cover.target == "all" ? 2 : cover.target == "layerBelow" ? 1 : 0;
        if(ImGui::Combo("Cover acts on", &target, "All layers below\0Layer immediately below\0All other layers\0")){
            cover.target = target == 2 ? "all" : target == 1 ? "layerBelow" : "allBelow"; edit = true;
        }
        if(edit){ sl.cover = formscover::write(cover); changed = true; }
    }
    if(sceneLayer > 0){
        ImGui::Spacing();
        if(ImGui::SmallButton("remove")){
            int at = sceneLayer;
            s.layers.erase(s.layers.begin() + at);
            std::vector<int> o2 = stackOrder(s.layers);
            bool anyZ = false; for(auto& x : s.layers) if(x.z >= 0) anyZ = true;
            if(anyZ) for(int k = 0; k < (int)o2.size(); k++) s.layers[o2[k]].z = k;
            sceneLayer = std::max(0, at - 1);
            changed = true;
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor(2);
    // the same layer's effects and sound: not the scene's (every scene, saved with the patch), so in
    // a box of their own, and never marking the scene changed
    if(drawLayerLook || drawLayerSound){
        ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.12f, 0.16f, 0.12f, 1));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(0.35f, 0.50f, 0.35f, 1));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(10 * zz, 8 * zz));
        ImGui::BeginChild("##layerEveryScene", ImVec2(0, 0), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
        ImGui::TextColored(ImVec4(0.65f, 0.85f, 0.6f, 1), "Layer %d on every scene", sceneLayer + 1);
        ImGui::SameLine();
        savedWithPatchNote();
        ImGui::PushID(sceneLayer);
        if(ImGui::BeginTable("##everyScene", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)){
            ImGui::TableNextColumn();
            ImGui::TextDisabled("effects (before the sound)");
            ImGui::PushItemWidth(-90 * zz);
            if(drawLayerLook) drawLayerLook(sceneLayer);
            ImGui::PopItemWidth();
            ImGui::TableNextColumn();
            ImGui::TextDisabled("sound");
            if(drawLayerSound) drawLayerSound(sceneLayer);
            ImGui::EndTable();
        }
        ImGui::PopID();
        ImGui::EndChild();
        ImGui::PopStyleVar();
        ImGui::PopStyleColor(2);
    }
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
    if(!ImGui::BeginTable("##cellEditorLayout", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)) return false;
    ImGui::TableSetupColumn("cells", ImGuiTableColumnFlags_WidthStretch, 1.55f);
    ImGui::TableSetupColumn("instrument editor", ImGuiTableColumnFlags_WidthStretch, 1.0f);
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
        if(clicked){
            onPick = [this, name = s.name, i, layerIdx = sceneLayer](const std::string& m){
                auto it2 = engine->scenes.find(name);
                if(it2 != engine->scenes.end() && layerIdx < (int)it2->second.layers.size() && i < (int)it2->second.layers[layerIdx].cells.size()){
                    it2->second.layers[layerIdx].cells[i].item = m;
                    dirty.insert(name);
                }
            };
            openPicker = true;
        }
        if(ImGui::IsItemHovered()) methodTooltip(c.item);
        if(ImGui::BeginPopupContextItem("##cellmenu")){
            ImGui::TextDisabled("%s", shortName(c.item).c_str());
            ImGui::Separator();
            if(engine->presets.count(c.item) && ImGui::MenuItem("Edit instrument")) sceneInstrumentEditRef = c.item;
            for(int k = 0; k < (int)s.layers.size(); k++){
                if(k == sceneLayer) continue;
                std::string lab = "Move to layer " + ofToString(k + 1) + " (" + ofToString(s.layers[k].bars) + " bars)";
                if(ImGui::MenuItem(lab.c_str())){ moveCell = i; moveTo = k; }
            }
            if(ImGui::MenuItem("Move to a new layer")){ moveCell = i; moveTo = -1; }
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
        ImGui::TableNextColumn();
        drawSceneInstrumentEditor(sceneInstrumentEditRef);
        ImGui::EndTable();
        return changed;   // `l` may no longer be valid
    }
    if(!shown) ImGui::TextDisabled("No instruments yet: + Add instrument.");
    ImGui::TableNextColumn();
    drawSceneInstrumentEditor(sceneInstrumentEditRef);
    ImGui::EndTable();
    return changed;
}

inline void Editor::drawSceneInstrumentEditor(std::string ref){
    float zz = z();
    if(ref.empty() || !engine->presets.count(ref)){
        ImGui::TextDisabled("Right-click an instrument thumbnail and choose Edit instrument.");
        return;
    }
    // This table follows the scene settings and may start near the viewport's bottom.
    // Give the editor its own height: drawParamTabs fills its parent's remaining space,
    // which otherwise collapses to a few pixels here. The scene scrolls to reveal this
    // panel, and long parameter groups scroll inside their tab as in Instruments.
    ImGui::BeginChild("##sceneInstrumentEditor", ImVec2(0, 520 * zz), ImGuiChildFlags_Borders);
    Preset& p = engine->presets[ref];
    const GenDef* g = engine->findGen(p.gen);
    ImGui::TextColored(genColour(p.gen), "%s", p.name.c_str());
    ImGui::SameLine();
    if(ImGui::SmallButton("Close##sceneInstrumentEditor")){
        sceneInstrumentEditRef.clear();
        ImGui::EndChild();
        return;
    }
    ImGui::SameLine();
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
    if(immediateSwitch){
        ImGui::SameLine();
        ImGui::Checkbox("Immediate##sceneInstrument",immediateSwitch);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Redraw playing and queued pages after instrument edits, including nested stack instruments. No Save needed to hear the edit.");
    }
    ImGui::Separator();
    if(p.isStack()){
        if(ImGui::CollapsingHeader("Cover (whole instrument)"))
            if(drawCoverParams(p.params)) dirtyPresets.insert(ref);
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
                    if(drawParamTabs(*layerGen, params)){ layer["params"] = params; dirtyPresets.insert(ref); }
                }else ImGui::TextDisabled("Unknown layer generator: %s", genName.c_str());
            }
        }
    }else if(g){
        ImGui::TextDisabled("Editing %s", ref.c_str());
        if(drawParamTabs(*g, p.params)) dirtyPresets.insert(ref);
        if(ImGui::CollapsingHeader("Notes")){
            char noteBuf[512];
            snprintf(noteBuf, sizeof(noteBuf), "%s", p.notes.c_str());
            if(ImGui::InputTextMultiline("##scenePresetNotes", noteBuf, sizeof(noteBuf), ImVec2(-1, 70 * zz))){ p.notes = noteBuf; dirtyPresets.insert(ref); }
        }
    }else{
        ImGui::TextDisabled("Unknown generator: %s", p.gen.c_str());
    }
    ImGui::EndChild();
}

} // namespace forms
