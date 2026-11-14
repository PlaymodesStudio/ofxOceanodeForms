//
//  formsEditorTabs.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Play, Setlist, Library and System tabs.
//  Methods of forms::Editor (declared in formsEditor.h, which includes this file).
//

#pragma once

namespace forms {

// The built-in player's picture with the header line (drag it to move the header) and its
// controls (drawn by Forms Box)
inline void Editor::drawPlayTab(){
    float zz = z();
    // one row: the view, then the Transport / Header / Scroller columns (drawn by Forms Box)
    bool row = ImGui::BeginTable("##playRow", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
    if(row){
        ImGui::TableSetupColumn("view", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("controls", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableNextColumn();
    }
    drawOutputView(ImGui::GetContentRegionAvail().x, std::max(120.0f * zz, ImGui::GetContentRegionAvail().y * 0.9f));
    if(row) ImGui::TableNextColumn();
    if(drawPlayControls) drawPlayControls();
    if(row) ImGui::EndTable();
}

// The built-in player's output, at most maxW x maxH, with the pitch / beat grids and the header line
inline void Editor::drawOutputView(float maxW, float maxH){
    float zz = z();
    Schedule* s = engine->schedule.get();
    bool on = playerOn && playerOn();
    const ofTexture* tex = (on && playTexture) ? playTexture() : nullptr;
    float avail = maxW;
    float aspect = (tex && tex->getWidth() > 0) ? tex->getHeight() / tex->getWidth() : 9.0f / 16.0f;
    float vh = std::min(avail * aspect, maxH);
    float vw = std::min(avail, vh / aspect);
    ImVec2 p = ImGui::GetCursorScreenPos();
    auto* dl = ImGui::GetWindowDrawList();
    dl->AddRectFilled(p, ImVec2(p.x + vw, p.y + vh), IM_COL32(6, 6, 8, 255));
    ImGui::InvisibleButton("##view", ImVec2(vw, vh));
    bool active = ImGui::IsItemActive(), hovered = ImGui::IsItemHovered();
    if(tex && tex->isAllocated()){
        ImVec2 uv0 = flipView ? ImVec2(0, 1) : ImVec2(0, 0), uv1 = flipView ? ImVec2(1, 0) : ImVec2(1, 1);
        dl->AddImage(texId(*tex), p, ImVec2(p.x + vw, p.y + vh), uv0, uv1);
    }else{
        dl->AddText(ImVec2(p.x + 10 * zz, p.y + vh / 2 - 7 * zz), IM_COL32(130, 130, 130, 255),
                    on ? "waiting for the first image" : "Built-in Player is off");
    }
    // the grids: pitch (rows where the renderer draws each semitone above the scene root) and beats
    if(on && gridInfo && (showPitchGrid || showBeatGrid)){
        GridInfo g = gridInfo();
        if(g.valid){
            float sx = vw / std::max(1.0f, g.width);
            dl->PushClipRect(p, ImVec2(p.x + vw, p.y + vh), true);
            if(showBeatGrid){
                const ImU32 layerCol[4] = {IM_COL32(255, 255, 255, 120), IM_COL32(110, 200, 255, 150), IM_COL32(255, 170, 90, 150), IM_COL32(170, 255, 140, 150)};
                for(auto& sp : g.spans){
                    double len = std::max(1.0 / kBarGrid, sp.bars) * 4;   // beats (a short page: less than a bar)
                    int beats = (int)std::ceil(len - 1e-9);
                    for(int k = 0; k <= beats; k++){
                        bool barLine = k % 4 == 0;
                        if(sp.layer > 0 && !barLine) continue;   // the other layers: their bar lines
                        float x = p.x + (sp.x + sp.w * (float)(k / len)) * sx;
                        if(x < p.x - 1 || x > p.x + vw + 1) continue;
                        ImU32 c = sp.layer == 0 ? (barLine ? IM_COL32(255, 255, 255, 110) : IM_COL32(255, 255, 255, 35)) : layerCol[std::min(3, sp.layer)];
                        if(sp.layer == 0) dl->AddLine(ImVec2(x, p.y), ImVec2(x, p.y + vh), c, barLine ? 1.5f * zz : 1.0f);
                        else for(float y = p.y; y < p.y + vh; y += 8 * zz) dl->AddLine(ImVec2(x, y), ImVec2(x, std::min(p.y + vh, y + 4 * zz)), c, 1.5f * zz);   // dashed
                    }
                }
            }
            if(showPitchGrid){
                auto pcs = scaleGrid(g.root, g.trans, g.scale);
                int ng = std::max(1, g.notegrid);
                float safe = (float)(g.imageH / ng) / std::max(1, g.imageH);   // the renderer's safey, as a fraction
                float rowPx = vh / ng;
                static const char* names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
                for(int pp = -1; pp <= ng; pp++){
                    float yn = 1.0f - (float)pp / ng - safe;   // from the top
                    if(yn < 0 || yn > 1) continue;
                    float y = flipView ? p.y + vh * (1 - yn) : p.y + vh * yn;
                    int midi = g.root + pp, pc = ((midi % 12) + 12) % 12;
                    bool inScale = std::find(pcs.begin(), pcs.end(), pc) != pcs.end();
                    bool chord = ((midi - g.chordRoot) % 12 + 12) % 12 == 0;
                    if(!inScale && rowPx < 3) continue;   // too dense: the scale's notes only
                    ImU32 c = chord ? IM_COL32(255, 90, 70, 170) : inScale ? IM_COL32(120, 220, 255, 90) : IM_COL32(255, 255, 255, 22);
                    dl->AddLine(ImVec2(p.x, y), ImVec2(p.x + vw, y), c, chord ? 1.5f * zz : 1.0f);
                    if(chord && rowPx >= 2.5f) dl->AddText(ImVec2(p.x + 3 * zz, y - 13 * zz), IM_COL32(255, 120, 100, 220), (std::string(names[pc]) + ofToString(midi / 12 - 1)).c_str());
                }
            }
            dl->PopClipRect();
        }
    }
    if(on && headerNormalized){
        if(active && setHeaderNormalized) setHeaderNormalized(ofClamp((ImGui::GetIO().MousePos.x - p.x) / vw, 0, 1));
        float hx = p.x + ofClamp(headerNormalized(), 0, 1) * vw;
        dl->AddLine(ImVec2(hx, p.y), ImVec2(hx, p.y + vh), IM_COL32(255, 80, 60, 230), 2 * zz);
        if(hovered) ImGui::SetTooltip("Drag to move the header");
    }
    dl->AddRect(p, ImVec2(p.x + vw, p.y + vh), IM_COL32(70, 70, 80, 255));
    ImGui::Checkbox("Flip view", &flipView);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Turn the view upside down if the low notes are at the top");
    ImGui::SameLine();
    ImGui::Checkbox("Pitch grid", &showPitchGrid);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Where the renderer draws each semitone above the scene root: the scale's notes brighter,\nthe bar's chord root (Root Out) in red with its name");
    ImGui::SameLine();
    ImGui::Checkbox("Beat grid", &showBeatGrid);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Layer 1's bars and beats as they scroll; the other layers' bar lines dashed, in their colour");
    (void)s;
}

inline void Editor::drawSetlistTab(){
    float zz = z();
    auto& sl = engine->setlist;
    if(ImGui::Button(setlistDirty ? "Save *" : "Save")){ engine->saveSetlist(); setlistDirty = false; }
    ImGui::SameLine();
    if(ImGui::Button("+ Add entry")){ sl.push_back({selected.empty() ? std::string() : selected, 4}); setlistDirty = true; }
    ImGui::SameLine();
    if(ImGui::Button("Restart setlist")) engine->resetSetlist();
    int total = 0;
    float seconds = 0;
    for(auto& e : sl){
        total += e.images;
        auto it = engine->scenes.find(e.scene);
        if(it != engine->scenes.end() && !it->second.layers.empty())
            seconds += e.images * (float)engine->layerBars(it->second.layers[0]) * 4 * 60.0f / std::max(1.0, it->second.bpm);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d entries, %d images, about %d:%02d per cycle at speed 1", (int)sl.size(), total, (int)seconds / 60, (int)seconds % 60);

    if(ImGui::BeginTable("##setlist", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_ScrollY)){
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 30 * zz);
        ImGui::TableSetupColumn("scene");
        ImGui::TableSetupColumn("images", ImGuiTableColumnFlags_WidthFixed, 110 * zz);
        ImGui::TableSetupColumn("length", ImGuiTableColumnFlags_WidthFixed, 70 * zz);
        ImGui::TableSetupColumn("cut", ImGuiTableColumnFlags_WidthFixed, 40 * zz);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 80 * zz);
        ImGui::TableHeadersRow();
        int cur = sl.empty() ? -1 : engine->setlistEntry % (int)sl.size();
        int moveFrom = -1, moveTo = -1, removeAt = -1, dragFrom = -1, dragTo = -1;
        for(int i = 0; i < (int)sl.size(); i++){
            auto& e = sl[i];
            ImGui::PushID(i);
            ImGui::TableNextRow();
            if(i == cur) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg0, IM_COL32(60, 90, 60, 160));
            ImGui::TableNextColumn();
            if(ImGui::Selectable((ofToString(i + 1) + "##row").c_str(), false, ImGuiSelectableFlags_AllowDoubleClick) && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) editScene(e.scene);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Drag to reorder, double-click to edit this scene, right-click for the menu");
            if(ImGui::BeginDragDropSource()){
                ImGui::SetDragDropPayload("FORMS_SETLIST_ORDER", &i, sizeof i);
                ImGui::Text("Move entry %d (%s)", i + 1, e.scene.c_str());
                ImGui::EndDragDropSource();
            }
            if(ImGui::BeginDragDropTarget()){
                if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FORMS_SETLIST_ORDER")){ dragFrom = *(const int*)pl->Data; dragTo = i; }
                ImGui::EndDragDropTarget();
            }
            if(ImGui::BeginPopupContextItem("##entryMenu")){
                ImGui::TextDisabled("%d  %s", i + 1, e.scene.c_str());
                ImGui::Separator();
                if(ImGui::MenuItem("Edit this scene")) editScene(e.scene);
                if(ImGui::MenuItem("Move up", nullptr, false, i > 0)){ moveFrom = i; moveTo = i - 1; }
                if(ImGui::MenuItem("Move down", nullptr, false, i + 1 < (int)sl.size())){ moveFrom = i; moveTo = i + 1; }
                if(ImGui::MenuItem("Remove from the setlist")) removeAt = i;
                ImGui::EndPopup();
            }
            ImGui::TableNextColumn();
            bool missing = !engine->scenes.count(e.scene);
            ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("##scene", (e.scene + (missing ? "  (missing)" : "")).c_str())){
                for(auto& [n, s] : engine->scenes) if(ImGui::Selectable(n.c_str(), n == e.scene)){ e.scene = n; setlistDirty = true; }
                ImGui::EndCombo();
            }
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-1);
            if(ImGui::InputInt("##n", &e.images)){ e.images = std::max(1, e.images); setlistDirty = true; }
            if(i == cur){ ImGui::SameLine(); }
            ImGui::TableNextColumn();
            auto it = engine->scenes.find(e.scene);
            if(it != engine->scenes.end() && !it->second.layers.empty()){
                float sec = e.images * (float)engine->layerBars(it->second.layers[0]) * 4 * 60.0f / std::max(1.0, it->second.bpm);
                ImGui::TextDisabled("%d:%02d", (int)sec / 60, (int)sec % 60);
            }
            if(i == cur){ ImGui::SameLine(); ImGui::TextColored(ImVec4(0.5f, 1, 0.6f, 1), "%d/%d", engine->imagesInEntry, e.images); }
            ImGui::TableNextColumn();
            if(ImGui::Checkbox("##cut", &e.hardCut)) setlistDirty = true;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Hard cut: when this scene arrives, every layer switches at its first bar\n(off: each extra layer switches at its own next image).");
            ImGui::TableNextColumn();
            if(ImGui::SmallButton("^") && i > 0){ moveFrom = i; moveTo = i - 1; }
            ImGui::SameLine();
            if(ImGui::SmallButton("v") && i + 1 < (int)sl.size()){ moveFrom = i; moveTo = i + 1; }
            ImGui::SameLine();
            if(ImGui::SmallButton("x")) removeAt = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
        if(moveFrom >= 0){ std::swap(sl[moveFrom], sl[moveTo]); setlistDirty = true; }
        else if(dragFrom >= 0 && dragTo >= 0 && dragFrom != dragTo && dragFrom < (int)sl.size() && dragTo < (int)sl.size()){
            auto moved = sl[dragFrom];
            sl.erase(sl.begin() + dragFrom);
            sl.insert(sl.begin() + dragTo, moved);
            setlistDirty = true;
        }
        if(removeAt >= 0){ sl.erase(sl.begin() + removeAt); setlistDirty = true; }
    }
}

inline void Editor::drawLibraryTab(){
    float zz = z();
    if(ImGui::Button(libraryDirty ? "Save library *" : "Save library")){ engine->saveLibrary(); libraryDirty = false; }
    ImGui::SameLine();
    ImGui::TextDisabled("Scales and palettes are sent with every request; the renderer has no tables of its own.");
    if(ImGui::BeginTable("##lib", 2, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)){
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Scales");
        libraryList(engine->scaleLib, libSelScale, true);
        for(auto& d : engine->scaleLib) if(d.name == libSelScale){
            ImGui::PushID("scale");
            bool ro = !d.source.empty();
            if(ro) ImGui::TextColored(ImVec4(0.55f, 0.8f, 1, 1), "From the Pitchclass catalog (%s): read-only here. Duplicate it to make a local copy, or edit the catalog file.", d.source.c_str());
            ImGui::BeginDisabled(ro);
            if(nameEditor(d.name, true)) {}
            ImGui::Text("Notes (semitones from the root):");
            static const char* nn[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
            for(int o = 0; o < 2; o++){
                for(int k = 0; k < 12; k++){
                    int semi = o * 12 + k;
                    if(k) ImGui::SameLine(0, 2 * zz);
                    bool on = std::find(d.notes.begin(), d.notes.end(), semi) != d.notes.end();
                    bool black = std::string(nn[k]).size() > 1;
                    ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.95f, 0.75f, 0.25f, 1) : (black ? ImVec4(0.12f, 0.12f, 0.14f, 1) : ImVec4(0.3f, 0.3f, 0.32f, 1)));
                    ImGui::PushID(semi);
                    if(ImGui::Button(ofToString(semi).c_str(), ImVec2(26 * zz, 34 * zz))){
                        if(on) d.notes.erase(std::remove(d.notes.begin(), d.notes.end(), semi), d.notes.end());
                        else { d.notes.push_back(semi); std::sort(d.notes.begin(), d.notes.end()); }
                        if(d.notes.empty()) d.notes = {0};
                        libraryDirty = true;
                    }
                    ImGui::PopID();
                    ImGui::PopStyleColor();
                }
            }
            ImGui::EndDisabled();
            ImGui::TextDisabled("%s%s", notesText(d.notes).c_str(), d.legacyIndex >= 0 ? ("   (1920 scale #" + ofToString(d.legacyIndex) + ")").c_str() : "");
            ImGui::PopID();
        }
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Palettes");
        libraryList(engine->paletteLib, libSelPalette, false);
        for(auto& d : engine->paletteLib) if(d.name == libSelPalette){
            ImGui::PushID("palette");
            nameEditor(d.name, false);
            ImGui::Text("Colours (the renderer uses 5 slots and cycles fewer):");
            int removeAt = -1;
            for(int i = 0; i < (int)d.colors.size(); i++){
                ImGui::PushID(i);
                unsigned v = 0;
                try { v = std::stoul(d.colors[i], nullptr, 16); } catch(...) {}
                float c[3] = {((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f};
                if(ImGui::ColorEdit3("##c", c, ImGuiColorEditFlags_NoInputs)){
                    char buf[8];
                    snprintf(buf, sizeof(buf), "%02X%02X%02X", (int)(c[0] * 255 + .5f), (int)(c[1] * 255 + .5f), (int)(c[2] * 255 + .5f));
                    d.colors[i] = buf;
                    libraryDirty = true;
                }
                ImGui::SameLine();
                ImGui::TextDisabled("%s", d.colors[i].c_str());
                ImGui::SameLine();
                if(d.colors.size() > 1 && ImGui::SmallButton("x")) removeAt = i;
                ImGui::PopID();
            }
            if(removeAt >= 0){ d.colors.erase(d.colors.begin() + removeAt); libraryDirty = true; }
            if(d.colors.size() < 5 && ImGui::Button("+ Colour")){ d.colors.push_back("FFFFFF"); libraryDirty = true; }
            if(d.legacyIndex >= 0) ImGui::TextDisabled("(1920 palette #%d)", d.legacyIndex);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

inline void Editor::drawSystemTab(){
    float zz = z();
    auto& L = engine->schedule->layers[0];
    // three columns: the data and session | the renderer and the node | the log
    bool cols = ImGui::BeginTable("##systemCols", 3, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
    if(cols){
        ImGui::TableSetupColumn("data", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("renderer", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableSetupColumn("log", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableNextColumn();
    }
    ImGui::SeparatorText("Data folder");
    ImGui::TextWrapped("%s", engine->rendererFolder.string().c_str());
    ImGui::SeparatorText("Image size");
    ImGui::Text("%d x %d", imageW, imageH); ImGui::SameLine(); ImGui::TextDisabled("set in Stage > Look & harmony");
    ImGui::SeparatorText("Session");
    ImGui::Text("%s   requested %d   rendering %d   ready %d   shown %d   repeats %d", engine->session.c_str(), engine->counter,
                (int)engine->pending.size(), (int)L.ready.size(), L.shown, L.repeats);
    ImGui::Text("skipped %d (renderer busy, max %d rendering)   lost %d   errors %d", engine->skipped, engine->maxPending, engine->lost, engine->errors);
    ImGui::Text("render time %.1f s   renderer load %.0f%%", engine->renderSeconds, engine->renderLoad() * 100);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("New images the scene at the header needs per second, over what the renderer makes.\nAbove 100%%: Repeat on some layers.");
    if(ImGui::Button("New session")) newSession();
    ImGui::SameLine();
    if(ImGui::Button("Reload scenes and library")) reload();
    ImGui::SeparatorText("Look");
    {
        bool classic = !theme::enabled();
        if(ImGui::Checkbox("Classic look", &classic)) theme::enabled() = !classic;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The window as it was before the design system (colours, spacing, family colours).");
    }
    if(cols) ImGui::TableNextColumn();
    // the renderer (the node's panel: counts, timings, archive, GPU) and the thumbnails it draws
    ImGui::SeparatorText("Renderer");
    if(drawRendererPanel) drawRendererPanel();
    if(ImGui::Button("Make thumbnails")){ makeThumbnails(); thumbs.clear(); }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", thumbnailsTip.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("(the instrument pictures in Scenes and the picker)");
    // what the node publishes (inputs / outputs)
    if(drawNodeTab && ImGui::CollapsingHeader("Node: published settings and outputs")){
        savedWithPatchNote();
        drawNodeTab();
    }
    if(ImGui::CollapsingHeader("Import from 1920")){
        char buf[1024];
        strncpy(buf, importPath.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
        ImGui::SetNextItemWidth(-120 * zz);
        if(ImGui::InputText("##importPath", buf, sizeof(buf))) importPath = buf;
        ImGui::SameLine();
        if(ImGui::Button("Import 1920")){
            try { engine->importPreset(importPath); reload(); }
            catch(std::exception& e){ engine->note(std::string("import failed: ") + e.what()); }
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Overwrites scenes with the same names, and the setlist.");
    }
    const Scene* curS = engine->currentScene();
    if(curS && curS->layers.size() > 1 && ImGui::CollapsingHeader("Layers: timing and panorama")){
        ImGui::SetNextItemWidth(100 * zz);
        ImGui::InputInt("start bars ahead", &engine->renderAheadBars);
        engine->renderAheadBars = ofClamp(engine->renderAheadBars, 1, 16);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("A layer that starts (or restarts after falling behind) begins this many bars after the header,\nso its first image has time to render. Raise it at high BPM.");
        ImGui::Text("Header beat %.2f (bar %d)", engine->schedule->headerBeat, (int)std::floor(engine->schedule->headerBeat / 4.0));
        for(int i = 1; i < (int)curS->layers.size(); i++){
            auto& L = engine->schedule->layer(i);
            ImGui::Text("Layer %d: in image %.3f | %d shown, %d repeated, %d late (moved on), %d too late, %d waiting", i + 1, L.phase, L.shown, L.repeats, L.retimed, L.dropped, (int)L.ready.size());
        }
        if(kickLevel) ImGui::Text("Kick probe %.2f (fires above 0.50)", *kickLevel);
        if(exportPanorama && ImGui::Button("Export panorama")) panoramaResult = exportPanorama();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The last whole cycle of all layers (their lengths' least common multiple) as one long PNG\nin the session folder, layer 1's scale, composited as live, with each layer's blur / trail\n(the effects before the sound; not the post-processing).");
        if(!panoramaResult.empty()){ ImGui::SameLine(); ImGui::TextWrapped("%s", panoramaResult.c_str()); }
        if(ImGui::CollapsingHeader("Sync at each bar line")){
            ImGui::BeginChild("##sync", ImVec2(0, 120 * zz), ImGuiChildFlags_Borders);
            for(auto it = engine->syncLog.rbegin(); it != engine->syncLog.rend(); ++it) ImGui::TextUnformatted(it->c_str());
            ImGui::EndChild();
        }
    }
    drawFrameStats();
    if(cols) ImGui::TableNextColumn();
    ImGui::SeparatorText("Log");
    ImGui::BeginChild("##log", ImVec2(0, 0), ImGuiChildFlags_Borders);
    for(auto it = engine->log.rbegin(); it != engine->log.rend(); ++it) ImGui::TextUnformatted(it->c_str());
    ImGui::EndChild();
    if(cols) ImGui::EndTable();
}

inline void Editor::drawSizeControl(){
    float zz = z();
    static const std::vector<std::pair<const char*, glm::ivec2>> presets = {
        {"1280 x 720", {1280, 720}}, {"1920 x 1080", {1920, 1080}}, {"2560 x 1440", {2560, 1440}},
        {"3840 x 2160 (4K)", {3840, 2160}}, {"7680 x 4320 (8K)", {7680, 4320}},
        {"15360 x 8640 (16K)", {15360, 8640}}};
    std::string current = ofToString(imageW) + " x " + ofToString(imageH);
    ImGui::SetNextItemWidth(170 * zz);
    if(ImGui::BeginCombo("##sizePreset", current.c_str())){
        for(auto& [label, wh] : presets) if(ImGui::Selectable(label, imageW == wh.x && imageH == wh.y)) setImageSize(wh.x, wh.y);
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    // typed sizes apply when the field is left (Enter, Tab or click elsewhere); InputInt
    // doesn't support EnterReturnsTrue (it asserts)
    if(!sizeEditing){ editW = imageW; editH = imageH; }
    ImGui::SetNextItemWidth(80 * zz);
    ImGui::InputInt("##w", &editW, 0, 0);
    bool activeW = ImGui::IsItemActive();
    if(ImGui::IsItemDeactivatedAfterEdit()) setImageSize(ofClamp(editW, 16, 16384), imageH);
    ImGui::SameLine(); ImGui::Text("x"); ImGui::SameLine();
    ImGui::SetNextItemWidth(80 * zz);
    ImGui::InputInt("##h", &editH, 0, 0);
    bool activeH = ImGui::IsItemActive();
    if(ImGui::IsItemDeactivatedAfterEdit()) setImageSize(imageW, ofClamp(editH, 16, 16384));
    sizeEditing = activeW || activeH;
    if(imageW * imageH > 1920 * 1080 * 2)
        ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "Large images render several times slower (4K about 4.7x): fine for prints, may not keep up live.");
    ImGui::TextDisabled("The size every scene's images are drawn at (from the next image on) and the size of the output texture (at once).\nNot saved in the scene. The node's Width/Height can set another output size afterwards; exports pick their own size.");
}

} // namespace forms
