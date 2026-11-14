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

// The kick of the scene: level, detection, the sample (or its number), the source and, for the synth, its shape
inline bool Editor::drawKickSettings(Scene& s){
    bool changed = drawPerformanceCard(s, {"kickVol", "kDetectOn", "kickSample"}, {});
    std::string sample = s.performance.value("kickSample", std::string());
    if(sample.empty() && !s.kickSample) changed |= drawPerformanceCard(s, {"kickSelect"}, {});
    changed |= drawPerformanceCard(s, {"kickSynth"}, {});
    if(s.performance.value("kickSynth", 0) > 0) changed |= drawPerformanceCard(s, {"kickPitchStart", "kickPitchEnd", "kickDecay"}, {"kickClick", "kickNoise", "kickDrive"});
    // ---- detection: the image analysis that fires the kick
    ImGui::Spacing();
    ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "DETECTION"); ImGui::SameLine(); ImGui::TextDisabled("from the image");
    ImGui::Separator();
    if(!s.layers.empty()){   // the layer it reads (this scene)
        int kl = kickLayer(s.layers);
        ImGui::SetNextItemWidth(-(118 * z()));
        if(ImGui::BeginCombo("Reads layer", ("Layer " + ofToString(kl + 1)).c_str())){
            for(int i = 0; i < (int)s.layers.size(); i++)
                if(ImGui::Selectable(("Layer " + ofToString(i + 1)).c_str(), i == kl)){
                    for(int k = 0; k < (int)s.layers.size(); k++) s.layers[k].kick = (k == i);
                    changed = true;
                }
            ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The layer whose bottom strip is measured at the header (this scene)");
    }
    if(drawKickMeter){       // the level now against this scene's threshold
        ImGui::PushItemWidth(-(118 * z()));
        drawKickMeter(s.performance.contains("kickThreshold") && s.performance["kickThreshold"].is_number() ? s.performance["kickThreshold"].get<float>() : 0.5f);
        ImGui::PopItemWidth();
    }
    // this scene's own detection and tuning (saved with the scene)
    changed |= drawPerformanceCard(s, {"kickThreshold", "kickStrip", "kickMinGap", "kickTune"}, {});
    return changed;
}

// The scene's sound settings in three folds (Compose): levels & stereo, movement, kick
inline bool Editor::drawSceneSound(Scene& s, bool columns){
    bool changed = false;
    static const std::vector<std::string> levels{"SpectrumGain", "sideChain", "reverbAmt", "Compressor", "scaleQuant", "LRDryWet", "LRTh", "LRHardness", "LRNoiseAmt"}, levelsMore{"LRStepBars", "LRprobSteps[]", "LRSmotthMs"};
    static const std::vector<std::string> move{"phasorSpeedMod", "phasorModChance", "PhasorMinSpeed", "PhasorMaxSpeed", "gateChance", "tremoloChance", "HeaderModChance", "repeatChance"},
                                          moveMore{"gateMults", "tremoloMaxSpeed", "tremoloFloor", "headerMaxDiv", "repeatMults", "repeatNum", "repeatPW", "repeatBarStart"};
    if(columns){   // three columns with everything in view
        if(ImGui::BeginTable("##sceneSound", 3, ImGuiTableFlags_SizingStretchSame | ImGuiTableFlags_BordersInnerV)){
            auto all = [](std::vector<std::string> a, const std::vector<std::string>& b){ a.insert(a.end(), b.begin(), b.end()); return a; };
            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "LEVELS & STEREO"); ImGui::Separator();
            changed |= drawPerformanceCard(s, all(levels, levelsMore), {});
            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "MOVEMENT"); ImGui::SameLine(); ImGui::TextDisabled("speed, gate, tremolo, header, repeat"); ImGui::Separator();
            changed |= drawPerformanceCard(s, all(move, moveMore), {});
            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "KICK"); ImGui::Separator();
            changed |= drawKickSettings(s);
            ImGui::EndTable();
        }
        return changed;
    }
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if(ImGui::CollapsingHeader("Levels & stereo"))
        changed |= drawPerformanceCard(s, levels, levelsMore);
    if(ImGui::CollapsingHeader("Movement (speed, gate, tremolo, header, repeat)"))
        changed |= drawPerformanceCard(s, move, moveMore);
    if(ImGui::CollapsingHeader("Kick")) changed |= drawKickSettings(s);
    return changed;
}

// The scene's look and harmony: palette, scale (and the Global Pitch override), note grid, BPM,
// root, bass degree, the per-bar degree / transpose weights
inline bool Editor::drawSceneLook(Scene& s){
    float zz = z();
    bool changed = false;
    // two columns: the look and the fields on the left, the harmony unfolded on the right
    bool cols = ImGui::BeginTable("##lookCols", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
    if(cols){
        ImGui::TableSetupColumn("look", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("harmony", ImGuiTableColumnFlags_WidthStretch, 1.3f);
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "LOOK");
        ImGui::Separator();
    }
    changed |= paletteCombo("Palette", s.palette);
    sceneFieldMenu(s, "palette", s.palette, engine->paletteLib.empty() ? json("") : json(engine->paletteLib[0].name), [](Scene& x, const json& v){ if(v.is_string()) x.palette = v.get<std::string>(); }, changed);
    ImGui::SameLine();
    if(ImGui::SmallButton("edit##pal")){ libSelPalette = s.palette; composeMode = 2; showTab = "Compose"; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Edit this palette's colours (Compose > Library; every scene using it changes)");
    if(const PaletteDef* pd = engine->findPalette(s.palette)){
        ImVec2 p = ImGui::GetCursorScreenPos(); auto* dl = ImGui::GetWindowDrawList();
        for(int i = 0; i < (int)pd->colors.size(); i++)
            dl->AddRectFilled(ImVec2(p.x + i * 22 * zz, p.y), ImVec2(p.x + i * 22 * zz + 20 * zz, p.y + 14 * zz), hexColor(pd->colors[i]));
        ImGui::Dummy(ImVec2(std::max<size_t>(1, pd->colors.size()) * 22 * zz, 16 * zz));
    }
    changed |= scaleCombo("Scale", s.scale);
    sceneFieldMenu(s, "scale", s.scale, engine->scaleLib.empty() ? json("") : json(engine->scaleLib[0].name), [](Scene& x, const json& v){ if(v.is_string()) x.scale = v.get<std::string>(); }, changed);
    ImGui::SameLine();
    if(ImGui::SmallButton("edit##sc")){ libSelScale = s.scale; composeMode = 2; showTab = "Compose"; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Edit this scale's notes (Compose > Library; every scene using it changes)");
    if(engine->pitchFollow.scaleOn){
        static const char* nn[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
        std::string nt; for(int n : engine->pitchFollow.notes) nt += (nt.empty() ? "" : " ") + ofToString(n);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "Global Pitch: %s on %s%s", nt.c_str(), nn[((engine->pitchFollow.rootPc % 12) + 12) % 12],
                           engine->pitchFollow.shift ? (", shift " + ofToString(engine->pitchFollow.shift)).c_str() : "");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The Global Pitch node overrides the scene scale (untick Forms in its inspector to keep the scene's)");
    }
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Notegrid", &s.notegrid); s.notegrid = ofClamp(s.notegrid, 12, 400);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Pixels per semitone in the images (100 in every 1920 scene)");
    sceneFieldMenu(s, "notegrid", s.notegrid, 100, [](Scene& x, const json& v){ if(v.is_number()) x.notegrid = ofClamp(v.get<int>(), 12, 400); }, changed);
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("BPM", &s.bpm); s.bpm = ofClamp(s.bpm, 1, 999);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once: the clock eases to it (the layers stay on the beat)");
    sceneFieldMenu(s, "bpm", s.bpm, 120, [](Scene& x, const json& v){ if(v.is_number()) x.bpm = ofClamp(v.get<int>(), 1, 999); }, changed);
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Root note (sound)", &s.root); s.root = ofClamp(s.root, 0, 127);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once on the sound (the partials, Root Out, Scale Out)");
    sceneFieldMenu(s, "root", s.root, 24, [](Scene& x, const json& v){ if(v.is_number()) x.root = ofClamp(v.get<int>(), 0, 127); }, changed);
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Bass degree", &s.bassDegree); s.bassDegree = ofClamp(s.bassDegree, 0, 11);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scale degree the bass instruments play, added to the bar's degree (0 = the root; past the scale's last degree it goes up an octave).\nAn instrument is a bass when its 'bass instrument' pitch option is on.\nRight-click: default, copy / paste, every scene.");
    sceneFieldMenu(s, "bassDegree", s.bassDegree, 0, [](Scene& x, const json& v){ if(v.is_number()) x.bassDegree = ofClamp(v.get<int>(), 0, 11); }, changed);
    if(cols) ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "HARMONY");
    ImGui::SameLine(); ImGui::TextDisabled("per bar: the chance of each scale degree and of each transpose (semitones); every bar draws from these");
    ImGui::Separator();
    {
        const ScaleDef es = engine->effectiveScale(s);
        const ScaleDef* sd = es.name.empty() ? nullptr : &es;
        changed |= weightBars("Degrees", s.degreeWeights, sd ? (int)sd->notes.size() : 12, true, sd);
        changed |= weightBars("Transpose", s.transposeWeights, 12, false, nullptr);
        changed |= drawHarmonyText(s);
    }
    if(cols) ImGui::EndTable();
    return changed;
}

// The eight live faders: each scales a performance output (1 = as the scene says)
inline void Editor::drawLiveFaders(){
    float zz = z();
    float gap = 8 * zz;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    int perRow = avail.x > 8 * (72 * zz + gap) ? 8 : 4;
    float fw = std::min(64 * zz, std::max(28 * zz, (avail.x - (perRow - 1) * gap) / perRow - 2 * zz));
    float labelH = ImGui::GetTextLineHeightWithSpacing() * 2;
    float rows = perRow == 8 ? 1 : 2;
    float fh = std::min(200 * zz, std::max(36 * zz, (avail.y - rows * labelH - gap - ImGui::GetFrameHeightWithSpacing()) / rows));
    for(int i = 0; i < 8; i++){
        auto& f = perform.faders[i];
        const PerfParam* pp = PerformState::param(f.key);
        if(i % perRow) ImGui::SameLine(0, gap);
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

// The scene pads, as a grid: the setlist's entries, every scene, the eight snapshots; Hard cut and Switch now
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
    ImGui::TextDisabled("a pad queues its scene for the next image (dashed until it reaches the header); a snapshot recalls (empty: stores); right-click a snapshot: store / clear");
    auto accentOf = [this](const std::string& scene){
        auto it = engine->scenes.find(scene);
        if(it != engine->scenes.end()) if(auto* pd = engine->findPalette(it->second.palette)) if(!pd->colors.empty()) return hexColor(pd->colors[0]);
        return theme::u32(theme::LINE2);
    };
    ImGui::BeginChild("##padsGrid", ImVec2(0, 0));
    float avail = ImGui::GetContentRegionAvail().x;
    ImVec2 padSize(170 * zz, 64 * zz);
    int perRow = std::max(1, (int)((avail + 8 * zz) / (padSize.x + 8 * zz)));
    if(!engine->setlist.empty()){
        theme::heading(sl ? "SETLIST" : "SETLIST  (off: a pad turns it on from that entry)");
        int cur = engine->setlistEntry % (int)engine->setlist.size();
        for(int i = 0; i < (int)engine->setlist.size(); i++){
            auto& e = engine->setlist[i];
            if(i % perRow) ImGui::SameLine(0, 8 * zz);
            int state = (sl && i == cur && perform.queued.empty() && playing == e.scene) ? 1 : (perform.queued == e.scene && sl && i == cur) ? 2 : 0;
            std::string sub = ofToString(i + 1) + "  |  " + ofToString(e.images) + " images" + (e.hardCut ? "  |  cut" : "") + (sl && i == cur ? "  |  " + ofToString(engine->imagesInEntry) + "/" + ofToString(e.images) : "");
            if(pad(e.scene + "##sl" + ofToString(i), sub, state, padSize, accentOf(e.scene))) queueSetlistEntry(i);
        }
        ImGui::Spacing();
    }
    theme::heading(sl ? "SCENES  (a pad turns the setlist off)" : "SCENES");
    int k = 0;
    for(auto& [name, sc] : engine->scenes){
        if(name.size() > 12 && name.compare(name.size() - 12, 12, " (assistant)") == 0) continue;
        if(bankFilter != "*" && sc.bank != bankFilter) continue;
        if(k++ % perRow) ImGui::SameLine(0, 8 * zz);
        int state = perform.queued == name ? 2 : (playing == name ? 1 : 0);
        std::string sub = ofToString(sc.bpm) + " BPM  |  " + ofToString(sc.layers.size()) + (sc.layers.size() == 1 ? " layer" : " layers");
        if(pad(name + "##sc", sub, state, padSize, accentOf(name))) queueScene(name);
    }
    ImGui::Spacing();
    theme::heading("SNAPSHOTS  (scene, setlist position, faders and sound settings; saved with the patch)");
    for(int i = 0; i < 8; i++){
        auto& s = perform.snaps[i];
        if(i % perRow) ImGui::SameLine(0, 8 * zz);
        std::string sub = s.used ? (s.useSetlist ? "setlist " + ofToString(s.setlistEntry + 1) : std::string("scene")) : "empty: click to store";
        ImGui::PushID(i);
        if(pad((s.used ? s.scene : std::string("Snapshot")) + "##snap" + ofToString(i), ofToString(i + 1) + "  |  " + sub, 0, padSize,
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
    float gap = 8 * zz;
    float gridH = std::max(220 * zz, avail.y);
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
    ImGui::SetNextItemOpen(true, ImGuiCond_Once);
    if(ImGui::CollapsingHeader("Transport, header, scroller")){ if(drawPlayControls) drawPlayControls(); }
    ImGui::EndChild();
    ImGui::SameLine(0, gap);

    // ---- right: the scene under the header in sub-tabs (each with the whole panel)
    ImGui::BeginChild("##stageRight", ImVec2(0, gridH));
    if(sp) crumb(2, sp->name + " (playing)");
    pushTabLevel(1);
    bool stageTabs = ImGui::BeginTabBar("##stageTabs");   // the colours stay on for the tab items
    if(!stageTabs) popTabLevel();
    if(stageTabs){
        if(ImGui::BeginTabItem("Scenes")){
            crumb(1, "Scenes");
            ImGui::BeginChild("##stScenes");
            drawScenePads();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        auto nothing = [](){ ImGui::TextDisabled("Nothing playing yet: the scene under the header shows here once its first image arrives."); };
        if(sp){ if(sceneLayerFor != sp->name){ sceneLayerFor = sp->name; sceneLayer = 0; } sceneLayer = ofClamp(sceneLayer, 0, (int)sp->layers.size() - 1); }
        if(ImGui::BeginTabItem("Layers")){
            crumb(1, "Layers");
            ImGui::BeginChild("##stLayers");
            if(!sp) nothing(); else {
            Scene& s = *sp;
            ImGui::TextDisabled("%s  |  opacity, order, mute / solo act at once", s.name.c_str());
            bool cols = ImGui::BeginTable("##stageLayersCols", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV);
            if(cols){ ImGui::TableSetupColumn("stack", ImGuiTableColumnFlags_WidthStretch, 1.0f); ImGui::TableSetupColumn("instruments", ImGuiTableColumnFlags_WidthStretch, 1.2f); ImGui::TableNextColumn(); }
            changed |= drawLayerStack(s, true);
            if(cols) ImGui::TableNextColumn();
            if(instMS){
                ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "INSTRUMENTS OF LAYER %d", sceneLayer + 1);
                ImGui::SameLine(); ImGui::TextDisabled("M / S at once; chance and volume: next images");
                ImGui::Separator();
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
                    if(!u.depth){
                        float dragW = 50 * zz, g2 = ImGui::GetStyle().ItemSpacing.x;
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
            if(cols) ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        std::string lookTab = std::string("Look & harmony  (") + (immediate ? "now" : "next images") + ")###stLook";
        if(ImGui::BeginTabItem(lookTab.c_str())){
            crumb(1, "Look & harmony");
            ImGui::BeginChild("##stLook");
            if(!sp) nothing(); else {
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
            if(ImGui::SmallButton("Edit in Compose")){ selected = s.name; composeMode = 0; showTab = "Compose"; }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", s.name.c_str());
            changed |= drawSceneLook(s);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Sound")){
            crumb(1, "Sound");
            ImGui::BeginChild("##stSound");
            if(!sp) nothing(); else {
            ImGui::TextDisabled("%s's sound: acts at once, saved with the scene", sp->name.c_str());
            changed |= drawSceneSound(*sp, true);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Live faders")){
            crumb(1, "Live faders");
            ImGui::BeginChild("##stFaders");
            ImGui::TextDisabled("Each fader multiplies a sound setting of whatever scene plays (1 = as the scene says). Right-click: what it drives; double-click: back to 1. Saved with the patch.");
            drawLiveFaders();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        popTabLevel();
    }
    ImGui::EndChild();

    if(sp && changed) dirty.insert(sp->name);
}

} // namespace forms
