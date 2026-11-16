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

inline bool Editor::drawPerformanceSparse(Scene& s, const std::vector<std::string>& keys, const char* id){
    bool changed = false;
    json full = completePerformance(s.performance);
    if(full != s.performance) s.performance = full;
    auto same = [](const json& a, const json& b){
        if(a.is_number() && b.is_number()) return std::fabs(a.get<double>() - b.get<double>()) <= 1e-4 * std::max(1.0, std::fabs(b.get<double>()));
        if(a.is_array() && b.is_array() && a.size() == b.size()){
            for(size_t i = 0; i < a.size(); i++)
                if(!(a[i].is_number() && b[i].is_number() && std::fabs(a[i].get<double>() - b[i].get<double>()) <= 1e-4 * std::max(1.0, std::fabs(b[i].get<double>())))) return false;
            return true;
        }
        return a == b;
    };
    float w = -(118 * z());
    auto& shown = perfShown[s.name];
    std::vector<const PerfParam*> hidden;
    int drawn = 0;
    for(auto& k : keys){
        const PerfParam* p = PerformState::param(k);
        if(!p) continue;
        if(shown.count(k) || !same(s.performance[k], p->def)){ changed |= drawPerfParam(s, *p, w); drawn++; }
        else hidden.push_back(p);
    }
    if(!drawn) ImGui::TextDisabled("all at their defaults");
    if(!hidden.empty()){
        ImGui::SetNextItemWidth(190 * z());
        std::string label = "+ add setting  (" + ofToString(hidden.size()) + " at default)";
        if(ImGui::BeginCombo((std::string("##add") + id).c_str(), label.c_str(), ImGuiComboFlags_HeightLarge)){
            for(auto* p : hidden) if(ImGui::Selectable((std::string(p->label) + "##" + p->key).c_str())) shown.insert(p->key);
            ImGui::Separator();
            if(ImGui::Selectable("show all")) for(auto* p : hidden) shown.insert(p->key);
            ImGui::EndCombo();
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Settings this scene leaves at their default are hidden: pick one to change it here");
    }
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
    changed |= drawPerformanceSparse(s, {"kickThreshold", "kickStrip", "kickMinGap", "kickTune", "duckScale"}, "kickDetect");
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
            changed |= drawPerformanceSparse(s, all(levels, levelsMore), "levels");
            ImGui::TableNextColumn(); ImGui::TextColored(ImVec4(0.8f, 0.6f, 1.0f, 1), "MOVEMENT"); ImGui::SameLine(); ImGui::TextDisabled("speed, gate, tremolo, header, repeat"); ImGui::Separator();
            changed |= drawPerformanceSparse(s, all(move, moveMore), "move");
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
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Rename the palette, add or remove colours (Library > Scales & palettes). The colours themselves can be changed on the swatches below.");
    // the palette's colours, editable here (click a swatch): the palette is the library's, so every
    // scene using it changes; with Live edits on the pages on stage are drawn again at once
    for(auto& d : engine->paletteLib) if(d.name == s.palette){
        int users = 0;
        for(auto& [n, sc] : engine->scenes) if(sc.palette == d.name) users++;
        ImGui::PushID("stagePalette");
        for(int i = 0; i < (int)d.colors.size(); i++){
            ImGui::PushID(i);
            if(i) ImGui::SameLine(0, 3 * zz);
            unsigned v = 0;
            try { v = std::stoul(d.colors[i], nullptr, 16); } catch(...) {}
            float c[3] = {((v >> 16) & 0xFF) / 255.f, ((v >> 8) & 0xFF) / 255.f, (v & 0xFF) / 255.f};
            if(ImGui::ColorEdit3("##c", c, ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_NoTooltip)){
                char buf[8];
                snprintf(buf, sizeof(buf), "%02X%02X%02X", (int)(c[0] * 255 + .5f), (int)(c[1] * 255 + .5f), (int)(c[2] * 255 + .5f));
                d.colors[i] = buf;
                libraryDirty = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click to change this colour of the palette \"%s\" (used by %d %s).\n"
                                                         "Live edits on: the pages on stage change at once; off: from the next images.\n"
                                                         "Kept with Save all (top bar). Add or remove colours with \"edit\".", d.name.c_str(), users, users == 1 ? "scene" : "scenes");
            ImGui::PopID();
        }
        if(users > 1){ ImGui::SameLine(0, 8 * zz); ImGui::TextDisabled("shared by %d scenes", users); }
        ImGui::PopID();
        break;
    }
    changed |= scaleCombo("Scale", s.scale);
    sceneFieldMenu(s, "scale", s.scale, engine->scaleLib.empty() ? json("") : json(engine->scaleLib[0].name), [](Scene& x, const json& v){ if(v.is_string()) x.scale = v.get<std::string>(); }, changed);
    ImGui::SameLine();
    if(ImGui::SmallButton("edit##sc")){ libSelScale = s.scale; composeMode = 2; showTab = "Compose"; }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Edit this scale's notes (Library > Scales & palettes; every scene using it changes)");
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
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputDouble("BPM", &s.bpm, 1, 10, "%g"); s.bpm = std::clamp(std::isfinite(s.bpm) ? s.bpm : 120.0, 1.0, 999.0);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once: the clock eases to it (the layers stay on the beat)");
    sceneFieldMenu(s, "bpm", s.bpm, 120, [](Scene& x, const json& v){ if(v.is_number()) x.bpm = std::clamp(v.get<double>(), 1.0, 999.0); }, changed);
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Root note (sound)", &s.root); s.root = ofClamp(s.root, 0, 127);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Acts at once on the sound (the partials, Root Out, Scale Out)");
    sceneFieldMenu(s, "root", s.root, 24, [](Scene& x, const json& v){ if(v.is_number()) x.root = ofClamp(v.get<int>(), 0, 127); }, changed);
    ImGui::SetNextItemWidth(110 * zz); changed |= ImGui::InputInt("Bass degree", &s.bassDegree); s.bassDegree = ofClamp(s.bassDegree, 0, 11);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Scale degree the bass instruments play, added to the bar's degree (0 = the root; past the scale's last degree it goes up an octave).\nAn instrument is a bass when its 'bass instrument' pitch option is on.\nRight-click: default, copy / paste, every scene.");
    sceneFieldMenu(s, "bassDegree", s.bassDegree, 0, [](Scene& x, const json& v){ if(v.is_number()) x.bassDegree = ofClamp(v.get<int>(), 0, 11); }, changed);
    ImGui::SetNextItemWidth(160 * zz); changed |= ImGui::SliderFloat("Line weight", &s.lineWeight, 0.25f, 8.0f, "x %.2f", ImGuiSliderFlags_Logarithmic); s.lineWeight = ofClamp(s.lineWeight, 0.1f, 16.0f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Thins or thickens every line instrument of this scene: notes, harmonics, flow lines, shape outlines (x 1 = as each instrument says).\nBlocks, hits, masses' fills and textures don't change. Thicker lines also sound fuller (more partials lit).\nA line is never thinner than 1 pixel: at 1080 lines, values below 1 only thin instruments thicker than 1.\nCtrl-click to type. Right-click: default, copy / paste, every scene.");
    sceneFieldMenu(s, "lineWeight", s.lineWeight, 1.0f, [](Scene& x, const json& v){ if(v.is_number()) x.lineWeight = ofClamp(v.get<float>(), 0.1f, 16.0f); }, changed);
    // random deviation of every colour picked from the palette (hue, saturation, lightness)
    ImGui::TextDisabled("Colour deviation (each coloured element, at random)");
    ImGui::SetNextItemWidth(160 * zz); changed |= ImGui::SliderFloat("Hue deviation", &s.hueDeviation, 0.0f, 180.0f, "+/- %.0f deg"); s.hueDeviation = ofClamp(s.hueDeviation, 0.0f, 180.0f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each coloured element takes its palette colour turned by a random hue within +/- this many degrees\n"
                                                 "(0 = the palette as it is; 180 = any hue). White and greys have no hue: they do not change.\n"
                                                 "The shapes of an image stay the same. Live edits on: the pages on stage change at once.");
    sceneFieldMenu(s, "hueDeviation", s.hueDeviation, 0.0f, [](Scene& x, const json& v){ if(v.is_number()) x.hueDeviation = ofClamp(v.get<float>(), 0.0f, 180.0f); }, changed);
    ImGui::SetNextItemWidth(160 * zz); changed |= ImGui::SliderFloat("Saturation deviation", &s.satDeviation, 0.0f, 100.0f, "+/- %.0f%%"); s.satDeviation = ofClamp(s.satDeviation, 0.0f, 100.0f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each coloured element is more or less saturated at random, within +/- this (%% of the full range).\n"
                                                 "White and greys stay grey.");
    sceneFieldMenu(s, "satDeviation", s.satDeviation, 0.0f, [](Scene& x, const json& v){ if(v.is_number()) x.satDeviation = ofClamp(v.get<float>(), 0.0f, 100.0f); }, changed);
    ImGui::SetNextItemWidth(160 * zz); changed |= ImGui::SliderFloat("Lightness deviation", &s.lightDeviation, 0.0f, 100.0f, "+/- %.0f%%"); s.lightDeviation = ofClamp(s.lightDeviation, 0.0f, 100.0f);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each coloured element is lighter or darker at random, within +/- this (%% of the full range).\n"
                                                 "White can only get darker. The image's brightness is what sounds, so this also varies each element's loudness.");
    sceneFieldMenu(s, "lightDeviation", s.lightDeviation, 0.0f, [](Scene& x, const json& v){ if(v.is_number()) x.lightDeviation = ofClamp(v.get<float>(), 0.0f, 100.0f); }, changed);
    if(cols) ImGui::TableNextColumn();
    ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "HARMONY");
    ImGui::SameLine(); ImGui::TextDisabled("per bar: the chance of each scale degree and of each transpose (semitones); every bar draws from these");
    ImGui::Separator();
    {
        const ScaleDef es = engine->effectiveScale(s);
        const ScaleDef* sd = es.name.empty() ? nullptr : &es;
        // the two chance multisliders are always here (a scene that stays on the root with no
        // transposition just has one bar up in each)
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
            if(ImGui::BeginPopupContextItem(("##slPadMenu" + ofToString(i)).c_str())){
                ImGui::TextDisabled("%d  %s", i + 1, e.scene.c_str());
                ImGui::Separator();
                if(ImGui::MenuItem("Edit this scene")){ selected = e.scene; stageFollow = false; }
                if(ImGui::MenuItem("Go to this entry")) queueSetlistEntry(i);
                if(ImGui::MenuItem("Open the setlist")) showTab = "Setlist";
                ImGui::EndPopup();
            }
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
        if(ImGui::BeginPopupContextItem(("##scPadMenu" + name).c_str())){
            ImGui::TextDisabled("%s", name.c_str());
            ImGui::Separator();
            if(ImGui::MenuItem("Edit this scene")){ selected = name; stageFollow = false; }
            if(ImGui::MenuItem("Play it")) queueScene(name);
            ImGui::EndPopup();
        }
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
    float leftW = std::max(260 * zz, std::min(avail.x * 0.27f, 420 * zz));   // (the transport fold is two columns wide)

    // ---- the scene under the header
    auto& L = engine->schedule->layers[0];
    std::string name;
    if(const Scene* hs = engine->sceneAtHeader()) name = hs->name;
    else if(L.header) name = L.header->scene;

    // ---- left: the show's settings (the output is on the stage itself, and small in the top bar)
    if(stageShowOutput){
        ImGui::BeginChild("##stageLeft", ImVec2(leftW, gridH));
        // the left column is the show's (saved with the patch); the right one is the scene's
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if(ImGui::CollapsingHeader("Every scene (bars, density, image size)")) drawEveryScene();
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        if(ImGui::CollapsingHeader("Transport, header, scroller")){ if(drawPlayControls) drawPlayControls(); }
        ImGui::EndChild();
        ImGui::SameLine(0, gap);
    }

    // ---- right: the scene editor (the playing scene, or any other)
    ImGui::BeginChild("##stageRight", ImVec2(0, gridH));
    drawSceneWorkspace(name);
    ImGui::EndChild();
}

// The instruments of the selected layer, as a list: mute / solo, order (arrows), name (double-click or
// right-click > Edit: its own window), chance, volume, remove. Stack instruments show what they play,
// indented (mute / solo only).
inline bool Editor::drawLayerInstruments(Scene& s){
    float zz = z();
    bool changed = false;
    if(s.layers.empty()) return false;
    sceneLayer = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "INSTRUMENTS OF LAYER %d", sceneLayer + 1);
    ImGui::SameLine();
    if(ImGui::SmallButton("+ Add instrument")){
        onPick = [this, name = s.name, layerIdx = sceneLayer](const std::string& m){
            auto it2 = engine->scenes.find(name);
            if(it2 == engine->scenes.end() || layerIdx >= (int)it2->second.layers.size()) return;
            engine->addItemToScene(it2->second, layerIdx, m);
            dirty.insert(name);
        };
        openPicker = true;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Pick an instrument from the library for this layer");
    ImGui::TextDisabled("drag or arrows: order | double-click: edit | right-click: menu | chance, volume | x: remove");
    ImGui::Separator();
    ImGui::BeginChild("##layerInstList");
    const std::vector<SceneUse> uses = sceneUses(s.name, true);
    std::vector<Cell> cells = s.layers[sceneLayer].cells;   // a copy: the rows may edit the layer
    const int inRows = std::min<int>(s.layers[sceneLayer].rows, (int)cells.size());
    const int rows = (int)cells.size();   // every cell is listed; those beyond the layer's Rows do not play
    const float dragW = 38 * zz, gap = 3 * zz, xW = ImGui::CalcTextSize("x").x + ImGui::GetStyle().FramePadding.x * 2;
    int moveFrom = -1, moveTo = -1, n = 0;
    std::set<std::string> listed;
    for(int ci = 0; ci < rows; ci++){
        const Cell& c = cells[ci];
        if(c.item.empty() || c.item == "none") continue;
        auto pi = engine->presets.find(c.item);
        bool isPreset = pi != engine->presets.end();
        ImGui::PushID(n++);
        instMSButtons(c.item);
        ImGui::SameLine(0, 3 * zz);
        int prev = -1, next = -1;
        for(int k = ci - 1; k >= 0; k--) if(!cells[k].item.empty() && cells[k].item != "none"){ prev = k; break; }
        for(int k = ci + 1; k < rows; k++) if(!cells[k].item.empty() && cells[k].item != "none"){ next = k; break; }
        ImGui::BeginDisabled(prev < 0);
        if(ImGui::SmallButton("^")){ moveFrom = ci; moveTo = prev; }
        ImGui::EndDisabled(); ImGui::SameLine(0, 1 * zz);
        ImGui::BeginDisabled(next < 0);
        if(ImGui::SmallButton("v")){ moveFrom = ci; moveTo = next; }
        ImGui::EndDisabled(); ImGui::SameLine(0, 5 * zz);
        bool beyond = ci >= inRows;
        bool off = c.prob <= 0 || beyond;
        if(off) ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.45f);
        const bool ownVersion = isPreset && s.instruments.is_object() && s.instruments.contains(c.item);   // this scene plays its own version
        std::string lab = (isPreset ? pi->second.name : shortName(c.item)) + (ownVersion ? "  (scene version)" : "") + (dirtyPresets.count(c.item) ? "  *" : "")
                        + (c.group > 0 ? "  G" + ofToString(c.group) : std::string()) + (beyond ? "  (beyond Rows)" : "");
        float selW = std::max(20.0f, ImGui::GetContentRegionAvail().x - 2 * dragW - xW - 3 * gap);
        ImGui::PushStyleColor(ImGuiCol_Text, isPreset ? genColour(pi->second.gen) : ImVec4(0.75f, 0.75f, 0.75f, 1));
        if(ImGui::Selectable((lab + "##row").c_str(), isPreset && sceneInstrumentEditRef == c.item, ImGuiSelectableFlags_AllowDoubleClick | ImGuiSelectableFlags_AllowOverlap, ImVec2(selW, 0)))
            if(isPreset && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) openInstrument(c.item, s.name);
        ImGui::PopStyleColor();
        if(ImGui::BeginDragDropSource()){
            ImGui::SetDragDropPayload("FORMS_CELL_ORDER", &ci, sizeof ci);
            ImGui::Text("Move %s", lab.c_str());
            ImGui::EndDragDropSource();
        }
        if(ImGui::BeginDragDropTarget()){
            if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("FORMS_CELL_ORDER")){ moveFrom = *(const int*)pl->Data; moveTo = ci; }
            ImGui::EndDragDropTarget();
        }
        instrumentMenu("##instMenu", c.item, s.name, sceneLayer, ci);
        if(ImGui::IsItemHovered() && !ImGui::IsPopupOpen("##instMenu")) methodTooltip(c.item);
        SceneUse u{c.item, sceneLayer, c.prob, 0, ci};
        ImGui::SameLine(0, gap);
        if(cellChanceDrag(s.name, u, dragW)) changed = true;
        ImGui::SameLine(0, gap);
        if(cellGainDrag(s.name, u, dragW)) changed = true;
        ImGui::SameLine(0, gap);
        if(ImGui::SmallButton("x")) instAction = {3, s.name, c.item, sceneLayer, ci, -1};
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Remove it from this layer");
        if(off) ImGui::PopStyleVar();
        // what a stack plays, once per instrument: mute / solo
        if(isPreset && pi->second.isStack() && listed.insert(c.item).second){
            bool inside = false;
            int k = 0;
            for(auto& x : uses){
                if(x.layer != sceneLayer) continue;
                if(x.depth == 0){ inside = x.cell == ci; continue; }
                if(!inside) continue;
                auto xi = engine->presets.find(x.ref);
                if(xi == engine->presets.end()) continue;
                ImGui::PushID(1000 + k++);
                ImGui::Dummy(ImVec2(x.depth * 14 * zz, 0)); ImGui::SameLine();
                instMSButtons(x.ref);
                ImGui::SameLine();
                ImGui::TextColored(genColour(xi->second.gen), "%s", xi->second.name.c_str());
                instrumentMenu("##instMenu", x.ref);
                if(ImGui::IsItemHovered()) methodTooltip(x.ref);
                ImGui::PopID();
            }
        }
        ImGui::PopID();
    }
    if(!n) ImGui::TextDisabled("No instruments on this layer yet: + Add instrument.");
    // ---- the layer's own options (the Director's groups and rows, its image chance, its effect)
    ImGui::Spacing();
    if(ImGui::CollapsingHeader("Layer options (image chance, effect, groups, rows)")){
        LayerDef& l = s.layers[sceneLayer];
        ImGui::SetNextItemWidth(150 * zz);
        changed |= ImGui::SliderFloat("image chance", &l.globalProb, 0, 100, "%.0f%%");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The chance that this layer draws anything at all in an image (the 1920 Global %%)");
        changed |= drawLayerEffectPreset(s, sceneLayer);
        if(l.fixed.is_object()){
            ImGui::TextColored(ImVec4(0.6f, 0.85f, 1, 1), "Frozen: always draws image %s.", l.fixed.value("from", std::string("?")).c_str());
            ImGui::SameLine();
            if(ImGui::SmallButton("unfreeze")){ l.fixed = json(); changed = true; }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Back to a normal layer (its instruments are then the ones listed above)");
        }
        ImGui::TextDisabled("Group chance (right-click an instrument to set its group)");
        if(l.groupProb.size() < 7) l.groupProb.resize(7, 1.0f);
        for(int g = 0; g < 7; g++){
            if(g) ImGui::SameLine();
            ImGui::PushID(g);
            changed |= ImGui::VSliderFloat("##gp", ImVec2(22 * zz, 60 * zz), &l.groupProb[g], 0, 1, "");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Group G%d: %.0f%% (%d instruments)", g, l.groupProb[g] * 100, countGroup(l, g));
            ImGui::PopID();
        }
        ImGui::SetNextItemWidth(110 * zz);
        changed |= ImGui::InputInt("Rows", &l.rows);
        l.rows = ofClamp(l.rows, 1, 512);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Only the first Rows instruments take part (the 1920 Director's Rows).");
    }
    ImGui::EndChild();
    if(moveFrom >= 0 && moveSceneCell(s.name, sceneLayer, moveFrom, moveTo)) changed = true;
    return changed;
}

// Effect presets that are not the same on every layer when applied bar by bar (they work across bar
// lines or on the whole image). Measured headless 2026-11-11: each effect rendered per bar on images
// of 3, 4 and 8 bars and compared at the same timeline bars (80 of 121 were identical).
inline const std::set<std::string>& notBarExactEffects(){
    static const std::set<std::string> s = {
        "effects/8th band slashes", "effects/aacc phrase", "effects/anacrusis silences", "effects/cell shuffle",
        "effects/curved band pass", "effects/deep low pass", "effects/dotted 8th smear", "effects/dotted echo",
        "effects/dotted eighth echo", "effects/dotted half echo", "effects/dotted quarter echo", "effects/echo full",
        "effects/echo reverb", "effects/edge vignette", "effects/eye tears", "effects/faint dotted echo",
        "effects/first bar loop", "effects/frame vignette", "effects/frozen staircase", "effects/high pass sweep",
        "effects/last bar stutter", "effects/last half band pass", "effects/last half band steps", "effects/low pass sweep",
        "effects/noise band filter", "effects/noise filter", "effects/pitch cell shuffle", "effects/quarter drops",
        "effects/random beat 16ths", "effects/rhythm gate", "effects/sixteenth loop", "effects/smear reverb",
        "effects/soft high pass", "effects/steep low pass sweep", "effects/stretch pitch cells", "effects/tear windows",
        "effects/wandering band pass", "effects/wide vignette", "stack/dotted repeat", "stack/freeze cut stutter",
        "stack/triplet half loops"};
    return s;
}

// The scene's effects on every layer: effect-only instruments, each resolved once per bar of the
// timeline, so layers of different lengths get the same effect at the same moment.
inline bool Editor::drawGlobalEffects(Scene& s){
    float zz = z();
    bool changed = false;
    ImGui::PushID("globalEffects");
    ImGui::AlignTextToFramePadding();
    ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.95f, 1), "EFFECTS ON EVERY LAYER");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Effect instruments applied to all the layers of this scene, after their own instruments.\nEach bar of the timeline gets the effect once (its own random choices), cut to that bar,\nso layers with different numbers of bars get exactly the same effect at the same moment.\nEffects that work across bar lines (long echoes, last-bar or whole-image effects) are cut at each bar.\nFrom the next images; saved with the scene.");
    int removeAt = -1;
    for(int i = 0; i < (int)s.globalEffects.size(); i++){
        ImGui::SameLine();
        ImGui::PushID(i);
        const std::string& ref = s.globalEffects[i];
        bool known = engine->presets.count(ref) > 0;
        if(ImGui::SmallButton((shortName(ref) + (known ? "" : " (missing)") + (s.instruments.is_object() && s.instruments.contains(ref) ? " (scene version)" : "")).c_str()) && known) openInstrument(ref, s.name);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Click: edit this effect (its own window)");
        instrumentMenu("##instMenu", ref, s.name);
        ImGui::SameLine(0, 1 * zz);
        if(ImGui::SmallButton("x")) removeAt = i;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Take it off every layer");
        ImGui::PopID();
    }
    if(removeAt >= 0){ s.globalEffects.erase(s.globalEffects.begin() + removeAt); changed = true; }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(130 * zz);
    if(ImGui::BeginCombo("##addGlobalEffect", "+ add effect", ImGuiComboFlags_HeightLarge)){
        for(auto& kv : engine->presets){
            if(!engine->onlyEffects(kv.second)) continue;
            if(std::find(s.globalEffects.begin(), s.globalEffects.end(), kv.first) != s.globalEffects.end()) continue;
            bool imageWide = notBarExactEffects().count(kv.first) > 0;
            if(ImGui::Selectable((kv.first + (imageWide ? "   (not bar-exact)" : "")).c_str())){ s.globalEffects.push_back(kv.first); changed = true; }
            if(ImGui::IsItemHovered()){
                if(imageWide) ImGui::SetTooltip("This effect works across bar lines or on the whole image (an echo, a sweep, a vignette, a noise field...):\napplied bar by bar it is cut at each bar and can differ from layer to layer.");
                else methodTooltip(kv.first);
            }
        }
        ImGui::EndCombo();
    }
    if(s.globalEffects.empty()){ ImGui::SameLine(); ImGui::TextDisabled("none"); }
    ImGui::PopID();
    return changed;
}

// The settings that act on every scene (saved with the patch, not with a scene): the bar multiplier and
// bar add, the density, the image size. In Stage's left column, with the transport: the right
// column is the scene's.
inline void Editor::drawEveryScene(){
    savedWithPatchNote();
    auto it = engine->scenes.find(selected);
    const Scene* sp = it != engine->scenes.end() ? &it->second : nullptr;
    if(barMult && barAdd){
        std::string plays;
        if(sp && (engine->barMult != 1 || engine->barAdd != 0)){
            for(auto& l : sp->layers) plays += (plays.empty() ? "" : ", ") + barsText(l.bars) + " > " + barsText(engine->layerBars(l));
            plays = "\nThe layers of " + sp->name + " now play as (bars): " + plays;
        }
        float bm = barMult->get();
        ImGui::SetNextItemWidth(92 * z());
        if(ImGui::InputFloat("bar multiplier", &bm, 1.0f, 1.0f, "x%g")) *barMult = ofClamp(bm, barMult->getMin(), barMult->getMax());
        if(nodeParamMenu) nodeParamMenu(barMult->getEscapedName());
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Every layer of every scene lasts its bars times this, so a wide projection keeps its picture proportions\n"
                                                     "(a view 3 screens wide: x3, and an 8-bar layer draws 24 bars across it). -/+ in steps of 1; a fraction can be typed.\n"
                                                     "The scenes keep their own lengths. From the next image asked for; saved with the patch, not the scene.%s", plays.c_str());
        int ba = barAdd->get();
        ImGui::SetNextItemWidth(92 * z());
        if(ImGui::InputInt("bar add", &ba, 1, 4)) *barAdd = ofClamp(ba, barAdd->getMin(), barAdd->getMax());
        if(nodeParamMenu) nodeParamMenu(barAdd->getEscapedName());
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Whole bars added to every layer of every scene, after the multiplier (bars x multiplier + this).\n"
                                                     "Negative takes bars away (a layer never goes below one bar that way). From the next image asked for.%s", plays.c_str());
        if(density){
            float de = density->get();
            ImGui::SetNextItemWidth(140 * z());
            if(ImGui::SliderFloat("density", &de, density->getMin(), density->getMax(), "%+.0f")) *density = ofClamp(de, density->getMin(), density->getMax());
            if(ImGui::IsItemClicked(ImGuiMouseButton_Right) && !nodeParamMenu) *density = 0;
            if(nodeParamMenu) nodeParamMenu(density->getEscapedName());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("How many instruments play, in every scene: bends each instrument's chance.\n"
                                                         "Below 0 the chances shrink (-100: nothing plays); above 0 they grow (+100: every instrument that is on plays every image).\n"
                                                         "0 = the chances as written (ctrl-click the slider to type a value). Instruments at 0%% stay off; frozen layers and previews are not touched.\n"
                                                         "From the next image asked for (with Live edits on: the pages on stage change at once); saved with the patch, not the scene.");
        }
    }
    ImGui::Spacing();
    ImGui::TextDisabled("Image size");
    drawSizeControl();
}

// The scene editor: which scene (the playing one while "follow what plays" is ticked), its own line
// (name, bank, save), and its tabs. The only place a scene is edited.
inline void Editor::drawSceneWorkspace(const std::string& playing){
    float zz = z();
    bool canFollow = !playing.empty() && engine->scenes.count(playing) > 0;
    // a scene selected elsewhere since the last frame (New, the Gallery, the Assistant): stay on it
    // (not the first time the editor is drawn: the scene it starts on was chosen by nobody, so it must
    // not switch following off; the editor then opens on the playing scene)
    if(!stageSelectedSeen.empty() && selected != stageSelectedSeen && !selected.empty() && selected != playing) stageFollow = false;
    if(stageFollow && canFollow) selected = playing;
    if(!selected.empty() && !engine->scenes.count(selected)) selected.clear();
    if(selected.empty() && !engine->scenes.empty()) selected = engine->scenes.begin()->first;
    bool immediate = immediateSwitch && *immediateSwitch;

    // ---- which scene
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Editing");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(250 * zz);
    if(ImGui::BeginCombo("##editScene", selected.empty() ? "(no scene)" : (selected + (dirty.count(selected) ? "  *" : "")).c_str(), ImGuiComboFlags_HeightLarge)){
        std::string pick;
        for(auto& [nm, sc] : engine->scenes)
            if(ImGui::Selectable((nm + (dirty.count(nm) ? "  *" : "") + (nm == playing ? "   (playing)" : "")).c_str(), nm == selected)) pick = nm;
        if(!pick.empty()){ selected = pick; if(pick != playing) stageFollow = false; }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("follow what plays", &stageFollow);
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ticked: this editor always shows the scene under the header.\nUnticked: it stays on the scene picked here (or in the Scenes tab's list), whatever plays.");
    bool isPlaying = canFollow && selected == playing;
    ImGui::SameLine(0, 14 * zz);
    if(isPlaying) ImGui::TextColored(ImVec4(0.5f, 0.9f, 0.6f, 1), "playing");
    else if(!selected.empty()) ImGui::TextColored(ImVec4(1, 0.7f, 0.2f, 1), "not playing");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip(isPlaying ? (immediate ? "This scene is under the header: edits act on the stage at once (Live edits is on)" : "This scene is under the header: edits act from the next images (Live edits is off)")
                                                           : "Another scene is playing: edits here reach the stage when this scene plays");

    auto it = engine->scenes.find(selected);
    Scene* sp = it != engine->scenes.end() ? &it->second : nullptr;
    if(sp){
        if(sp->layers.empty()) sp->layers.push_back(LayerDef());
        if(!drawSceneHeader(*sp)){ stageSelectedSeen = selected; return; }   // renamed: drawn again on the next frame
        if(sceneLayerFor != sp->name){ sceneLayerFor = sp->name; sceneLayer = 0; }
        sceneLayer = ofClamp(sceneLayer, 0, (int)sp->layers.size() - 1);
    }
    const std::string editName = sp ? sp->name : std::string();   // the Scenes tab may reload the scenes: no `sp` after the tabs
    bool changed = false;
    auto nothing = [](){ ImGui::TextDisabled("No scene yet: New, in the Scenes tab, or import the 1920 scenes in System."); };

    pushTabLevel(1);
    bool tabs = ImGui::BeginTabBar("##stageTabs");   // the colours stay on for the tab items
    if(!tabs) popTabLevel();
    if(tabs){
        if(ImGui::BeginTabItem("Scenes")){
            ImGui::BeginChild("##stScenes");
            if(ImGui::BeginTable("##scenesCols", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
                ImGui::TableSetupColumn("pads", ImGuiTableColumnFlags_WidthStretch, 2.2f);
                ImGui::TableSetupColumn("list", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                ImGui::TableNextColumn();
                drawScenePads();
                ImGui::TableNextColumn();
                ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "ALL SCENES");
                ImGui::SameLine(); ImGui::TextDisabled("click: edit, double-click: play");
                std::string before = selected;
                drawSceneList();
                if(selected != before) stageFollow = false;   // picked from the list: stay on it
                ImGui::EndTable();
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
            sp = nullptr;
        }
        if(ImGui::BeginTabItem("Layers")){
            ImGui::BeginChild("##stLayers");
            if(!sp) nothing(); else {
                Scene& s = *sp;
                changed |= drawGlobalEffects(s);
                ImGui::Separator();
                if(ImGui::BeginTable("##layersAndInstruments", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
                    ImGui::TableSetupColumn("layers", ImGuiTableColumnFlags_WidthStretch, 1.25f);
                    ImGui::TableSetupColumn("instruments", ImGuiTableColumnFlags_WidthStretch, 1.0f);
                    ImGui::TableNextColumn();
                    ImGui::BeginChild("##layersCol");          // the stack, and under it the selected layer's settings
                    layerSettingsBelow = true;
                    changed |= drawLayerStack(s);
                    layerSettingsBelow = false;
                    ImGui::EndChild();
                    ImGui::TableNextColumn();
                    changed |= drawLayerInstruments(s);
                    ImGui::EndTable();
                }
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        std::string lookTab = std::string("Look & harmony  (") + (immediate ? "now" : "next images") + ")###stLook";
        if(ImGui::BeginTabItem(lookTab.c_str())){
            ImGui::BeginChild("##stLook");
            if(!sp) nothing(); else changed |= drawSceneLook(*sp);
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Sound")){
            ImGui::BeginChild("##stSound");
            if(!sp) nothing(); else {
                ImGui::TextDisabled("This scene's sound: levels and stereo, movement, kick. Saved with the scene; acts at once while it plays.");
                changed |= drawSceneSound(*sp, true);
            }
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        if(ImGui::BeginTabItem("Live faders  (every scene)###stFadersTab")){
            ImGui::BeginChild("##stFaders");
            ImGui::TextDisabled("Each fader multiplies a sound setting of whatever scene plays (1 = as the scene says). Right-click: what it drives; double-click: back to 1. Saved with the patch.");
            drawLiveFaders();
            ImGui::EndChild();
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        popTabLevel();
    }
    if(changed && !editName.empty()) dirty.insert(editName);
    stageSelectedSeen = selected;
}

} // namespace forms
