//
//  formsEditorStage.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Stage tab: everything about what is playing, on one screen and without scrolling. Left the
//  output (the built-in player's view with its grids, the transport folded under it); right the
//  scene under the header in six cards of the same size: Layers, Colour & harmony, Levels & stereo,
//  Live faders, Movement, Kick; along the bottom the scene pads (setlist, scenes, snapshots).
//  Everything here acts at once, except the look card, which follows Immediate mode (now) or
//  reaches the next images (off). Replaces the Play, Perform and Live tabs (their functions stay).
//

#pragma once

namespace forms {

// Performance settings by key, the rarely touched ones under "more"
inline bool Editor::drawPerformanceCard(Scene& s, const std::vector<std::string>& keys, const std::vector<std::string>& more){
    bool changed = false;
    json full = completePerformance(s.performance);
    if(full != s.performance) s.performance = full;
    float w = -(118 * z());
    auto draw = [&](const std::string& k){ if(const PerfParam* p = PerformState::param(k)) changed |= drawPerfParam(s, *p, w); };
    for(auto& k : keys) draw(k);
    if(!more.empty() && ImGui::TreeNode("more")){ for(auto& k : more) draw(k); ImGui::TreePop(); }
    return changed;
}

// The scene's look and harmony: palette, scale (and the Global Pitch override), note grid, BPM,
// root, bass degree, the per-bar degree / transpose weights
inline bool Editor::drawSceneLook(Scene& s){
    float zz = z();
    bool changed = false;
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
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "Global Pitch: %s on %s%s", nt.c_str(), nn[((engine->pitchFollow.rootPc % 12) + 12) % 12],
                           engine->pitchFollow.shift ? (", shift " + ofToString(engine->pitchFollow.shift)).c_str() : "");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The Global Pitch node overrides the scene scale (untick Forms in its inspector to keep the scene's)");
    }
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Notegrid", &s.notegrid); s.notegrid = ofClamp(s.notegrid, 12, 400);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels per semitone in the images (100 in every 1920 scene)");
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("BPM", &s.bpm); s.bpm = ofClamp(s.bpm, 1, 999);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once: the clock eases to it (the layers stay on the beat)");
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Root note (sound)", &s.root); s.root = ofClamp(s.root, 0, 127);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once on the sound (the partials, Root Out, Scale Out)");
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Bass degree", &s.bassDegree); s.bassDegree = ofClamp(s.bassDegree, 0, 11);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scale degree the bass instruments play, added to the bar's degree (0 = the root; past the scale's last degree it goes up an octave).\nAn instrument is a bass when its 'bass instrument' pitch option is on.");
    if(ImGui::TreeNode("Harmony (degrees, transposes per bar)")){
        const ScaleDef es = engine->effectiveScale(s);
        const ScaleDef* sd = es.name.empty() ? nullptr : &es;
        changed |= weightBars("Degrees", s.degreeWeights, sd ? (int)sd->notes.size() : 12, true, sd);
        changed |= weightBars("Transpose", s.transposeWeights, 12, false, nullptr);
        ImGui::TreePop();
    }
    return changed;
}

// The eight live faders: each scales a performance output (1 = as the scene says)
inline void Editor::drawLiveFaders(){
    float zz = z();
    float gap = 8 * zz;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float fw = std::max(28 * zz, (avail.x - 3 * gap) / 4 - 2 * zz);
    float labelH = ImGui::GetTextLineHeightWithSpacing() * 2;
    float fh = std::max(36 * zz, (avail.y - 2 * labelH - gap - ImGui::GetFrameHeightWithSpacing()) / 2);
    for(int i = 0; i < 8; i++){
        auto& f = perform.faders[i];
        const PerfParam* pp = PerformState::param(f.key);
        if(i % 4) ImGui::SameLine(0, gap);
        ImGui::BeginGroup();
        ImGui::PushID(i);
        ImGui::PushStyleColor(ImGuiCol_SliderGrab, f.gain == 1.0f ? theme::rgb(theme::MUTED) : theme::rgb(theme::PLAY));
        ImGui::VSliderFloat("##fader", ImVec2(fw, fh), &f.gain, 0, 2, "x%.2f");
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) f.gain = 1;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: x%.2f  (right-click: what it drives; double-click: back to 1)", pp ? pp->label : f.key.c_str(), f.gain);
        if(ImGui::BeginPopupContextItem("##faderKey")){
            for(auto& p : perfParams()) if(p.output && p.type == 'f' && ImGui::Selectable(p.label, f.key == p.key)) f.key = p.key;
            ImGui::EndPopup();
        }
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + fw);
        ImGui::TextDisabled("%s", pp ? pp->label : f.key.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopID();
        ImGui::EndGroup();
    }
    if(!perform.neutral()){ if(ImGui::SmallButton("all to 1")) for(auto& f : perform.faders) f.gain = 1; }
}

// The pads: the setlist's entries, every scene, the eight snapshots; Hard cut and Switch now
inline void Editor::drawScenePads(){
    float zz = z();
    auto& L = engine->schedule->layers[0];
    std::string playing = L.header ? L.header->scene : "";
    bool sl = useSetlist && *useSetlist;
    ImGui::Checkbox("Hard cut", &perform.queueHardCut);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Off: layer 1 changes at its next image, each other layer at its own next image.\nOn: the change is a hard cut (every layer switches at the change).");
    ImGui::SameLine(0, 10 * zz);
    ImGui::Checkbox("Switch now", &perform.switchNow);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("On: a chosen scene replaces what plays at once, on every layer (queued images of the old scene are dropped).\nOff: the scene waits for the next image.");
    ImGui::SameLine(0, 14 * zz);
    ImGui::TextDisabled("setlist  |  scenes  |  snapshots      click: queue / recall (dashed until it reaches the header); snapshot right-click: store / clear");
    auto accentOf = [this](const std::string& scene){
        auto it = engine->scenes.find(scene);
        if(it != engine->scenes.end()) if(auto* pd = engine->findPalette(it->second.palette)) if(!pd->colors.empty()) return hexColor(pd->colors[0]);
        return theme::u32(theme::LINE2);
    };
    ImGui::BeginChild("##padsRow", ImVec2(0, 0), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
    ImVec2 padSize(138 * zz, 50 * zz);
    bool first = true;
    auto next = [&](){ if(!first) ImGui::SameLine(0, 6 * zz); first = false; };
    auto divider = [&](){ ImGui::SameLine(0, 10 * zz); ImGui::TextDisabled("|"); ImGui::SameLine(0, 10 * zz); first = true; };
    if(!engine->setlist.empty()){
        int cur = engine->setlistEntry % (int)engine->setlist.size();
        for(int i = 0; i < (int)engine->setlist.size(); i++){
            auto& e = engine->setlist[i];
            next();
            int state = (sl && i == cur && perform.queued.empty() && playing == e.scene) ? 1 : (perform.queued == e.scene && sl && i == cur) ? 2 : 0;
            std::string sub = ofToString(i + 1) + (e.hardCut ? "  cut" : "") + (sl && i == cur ? "  " + ofToString(engine->imagesInEntry) + "/" + ofToString(e.images) : "  " + ofToString(e.images) + " img");
            if(pad(e.scene + "##sl" + ofToString(i), sub, state, padSize, accentOf(e.scene))) queueSetlistEntry(i);
        }
        divider();
    }
    for(auto& [name, sc] : engine->scenes){
        if(name.size() > 12 && name.compare(name.size() - 12, 12, " (assistant)") == 0) continue;
        if(bankFilter != "*" && sc.bank != bankFilter) continue;
        next();
        int state = perform.queued == name ? 2 : (playing == name ? 1 : 0);
        std::string sub = ofToString(sc.bpm) + " BPM  " + ofToString(sc.layers.size()) + (sc.layers.size() == 1 ? " layer" : " layers");
        if(pad(name + "##sc", sub, state, padSize, accentOf(name))) queueScene(name);
    }
    divider();
    for(int i = 0; i < 8; i++){
        auto& s = perform.snaps[i];
        next();
        std::string sub = s.used ? (s.useSetlist ? "setlist " + ofToString(s.setlistEntry + 1) : std::string("scene")) : "empty";
        ImGui::PushID(i);
        if(pad((s.used ? s.scene : std::string("Snapshot")) + "##snap" + ofToString(i), ofToString(i + 1) + "  " + sub, 0, ImVec2(104 * zz, 50 * zz),
               s.used ? accentOf(s.scene) : theme::u32(theme::LINE))){
            if(s.used) recallSnapshot(i); else storeSnapshot(i);
        }
        if(ImGui::BeginPopupContextItem("##snapMenu")){
            if(ImGui::Selectable("Store the current state")) storeSnapshot(i);
            if(s.used && ImGui::Selectable("Clear")) s = PerformState::Snapshot();
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    ImGui::EndChild();
}

inline void Editor::drawStageTab(){
    float zz = z();
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float gap = 8 * zz, padsH = 112 * zz;
    float gridH = std::max(220 * zz, avail.y - padsH - gap);
    float leftW = std::max(300 * zz, std::min(avail.x * 0.34f, 520 * zz));

    // ---- the scene under the header
    auto& L = engine->schedule->layers[0];
    std::string name;
    if(const Scene* hs = engine->sceneAtHeader()) name = hs->name;
    else if(L.header) name = L.header->scene;
    auto it = engine->scenes.find(name);
    Scene* sp = it != engine->scenes.end() ? &it->second : nullptr;
    if(sp && sp->layers.empty()) sp = nullptr;
    bool changed = false;
    bool immediate = immediateSwitch && *immediateSwitch;

    // ---- left: the output and, folded, the transport
    ImGui::BeginChild("##stageLeft", ImVec2(leftW, gridH));
    drawOutputView(ImGui::GetContentRegionAvail().x, std::min(gridH * 0.55f, ImGui::GetContentRegionAvail().x * 9 / 16));
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Transport, header, scroller")){ if(drawPlayControls) drawPlayControls(); }
    ImGui::EndChild();
    ImGui::SameLine(0, gap);

    // ---- right: six cards
    ImGui::BeginChild("##stageRight", ImVec2(0, gridH));
    float cw = (ImGui::GetContentRegionAvail().x - 2 * gap) / 3, ch = (gridH - gap) / 2;
    const ImVec4 cLayers(0.55f, 0.85f, 1.0f, 1), cLook(0.9f, 0.8f, 0.5f, 1), cSound(0.8f, 0.6f, 1.0f, 1);
    auto beginCard = [&](const char* id, const char* title, const char* hint, ImVec4 col){
        ImGui::BeginChild(id, ImVec2(cw, ch), ImGuiChildFlags_Borders);
        ImGui::TextColored(col, "%s", title);
        if(hint && *hint){ ImGui::SameLine(); ImGui::TextDisabled("%s", hint); }
        ImGui::Separator();
    };
    auto endCard = [&](bool last){ ImGui::EndChild(); if(!last) ImGui::SameLine(0, gap); };
    auto nothing = [&](){ ImGui::TextDisabled("Nothing playing yet: the scene under the header shows here once its first image arrives."); };

    // row 1
    beginCard("##cLayers", "LAYERS", "at once", cLayers);
    if(sp){
        Scene& s = *sp;
        if(sceneLayerFor != s.name){ sceneLayerFor = s.name; sceneLayer = 0; }
        sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
        changed |= drawLayerStack(s, true);
        if(instMS){
            ImGui::Spacing();
            ImGui::TextDisabled("instruments of layer %d  (M / S at once; chance and volume: next images)", sceneLayer + 1);
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
                if(!u.depth){
                    float dragW = 46 * zz, g2 = ImGui::GetStyle().ItemSpacing.x;
                    ImGui::SameLine(); ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.f, ImGui::GetContentRegionAvail().x - 2 * dragW - g2));
                    if(cellChanceDrag(s.name, u, dragW)) changed = true;
                    ImGui::SameLine();
                    if(cellGainDrag(s.name, u, dragW)) changed = true;
                }
                if(off) ImGui::PopStyleVar();
                ImGui::PopID();
            }
            if(!n) ImGui::TextDisabled("no instruments on this layer");
        }
    }else nothing();
    endCard(false);

    beginCard("##cLook", "COLOUR & HARMONY", immediate ? "now (Immediate)" : "next images", cLook);
    if(sp){
        Scene& s = *sp;
        bool isDirty = dirty.count(s.name) > 0;
        if(isDirty) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1));
        if(ImGui::SmallButton(isDirty ? "Save *" : "Save")){ engine->saveScene(s); dirty.erase(s.name); }
        if(isDirty) ImGui::PopStyleColor();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Keep these edits in the scene's file (else they last until the scene is reloaded)");
        ImGui::SameLine();
        ImGui::BeginDisabled(!isDirty);
        if(ImGui::SmallButton("Revert")){ engine->reloadScene(s.name); dirty.erase(s.name); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if(ImGui::SmallButton("Edit in Scenes")){ selected = s.name; showTab = "Scenes"; }
        ImGui::SameLine();
        ImGui::TextDisabled("%s", s.name.c_str());
        changed |= drawSceneLook(s);
    }else nothing();
    endCard(false);

    beginCard("##cLevels", "LEVELS & STEREO", "at once", cSound);
    if(sp) changed |= drawPerformanceCard(*sp, {"SpectrumGain", "sideChain", "reverbAmt", "Compressor", "scaleQuant", "LRDryWet", "LRTh", "LRHardness", "LRNoiseAmt"},
                                              {"LRStepBars", "LRprobSteps[]", "LRSmotthMs"});
    else nothing();
    endCard(true);

    // row 2
    beginCard("##cFaders", "LIVE FADERS", "x the scene's values; saved with the patch", cSound);
    drawLiveFaders();
    endCard(false);

    beginCard("##cMove", "MOVEMENT", "speed, gate, tremolo, header, repeat", cSound);
    if(sp) changed |= drawPerformanceCard(*sp, {"phasorSpeedMod", "phasorModChance", "PhasorMinSpeed", "PhasorMaxSpeed", "gateChance", "tremoloChance", "HeaderModChance", "repeatChance"},
                                              {"gateMults", "tremoloMaxSpeed", "tremoloFloor", "headerMaxDiv", "repeatMults", "repeatNum", "repeatPW", "repeatBarStart"});
    else nothing();
    endCard(false);

    beginCard("##cKick", "KICK", "at once", cSound);
    if(sp){
        Scene& s = *sp;
        changed |= drawPerformanceCard(s, {"kickVol", "kDetectOn", "kickSample"}, {});
        std::string sample = s.performance.value("kickSample", std::string());
        if(sample.empty() && !s.kickSample) changed |= drawPerformanceCard(s, {"kickSelect"}, {});
        changed |= drawPerformanceCard(s, {"kickSynth"}, {});
        bool synth = s.performance.value("kickSynth", 0) > 0;
        if(synth) changed |= drawPerformanceCard(s, {"kickPitchStart", "kickPitchEnd", "kickDecay"}, {"kickClick", "kickNoise", "kickDrive"});
    }else nothing();
    endCard(true);
    ImGui::EndChild();

    // ---- the pads
    ImGui::BeginChild("##stagePads", ImVec2(0, padsH), ImGuiChildFlags_Borders);
    drawScenePads();
    ImGui::EndChild();

    if(sp && changed) dirty.insert(sp->name);
}

} // namespace forms
