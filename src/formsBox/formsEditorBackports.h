//
//  formsEditorBackports.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backports from v2: GUI only)
//
//  Methods of forms::Editor (declared in formsEditor.h, which includes this file):
//    undo / redo and Compare with saved (top bar, Cmd-Z / Cmd-Shift-Z)
//    the Perform tab: scene pads, macro faders, queued scene change, snapshots
//    the Assistant tab (v1 presets and scenes, auditioned, Keep / Discard)
//    the typed harmony editor (Scenes tab, Harmony)
//    frame-time stats (System tab)
//  Nothing here changes how a page is chosen or drawn: pads use the same calls as the Next menu,
//  the setlist and Hard Cut; faders are 1 (no change) until moved.
//

#pragma once

namespace forms {

// ---------------------------------------------------------------- per frame (from the node's update)

inline void Editor::tick(){
    // frame time, always (one number); drawn only when its section is open
    frameMs[frameAt % frameMs.size()] = (float)(ofGetLastFrameTime() * 1000.0);
    frameAt++;
    // the queued scene: done when it reaches the header; a temporary Hard Cut ends once asked for
    auto& L = engine->schedule->layers[0];
    if(!perform.queued.empty() && L.header && L.header->scene == perform.queued) perform.queued.clear();
    if(!perform.cutFor.empty()){
        bool asked = !engine->layer1Plan.empty() && engine->layer1Plan.back().scene == perform.cutFor;
        if(asked || ofGetElapsedTimef() - perform.queuedAt > 60){
            if(setHardCut) setHardCut(perform.cutWas);
            perform.cutFor.clear();
        }
    }
    if(aiSetup){
        ai.update();
        pollAssistant();
        if(aiAuditioning && aiIsPreset && engine->presets.count(aiTempRef)) selPreset = aiTempRef;   // the audition follows the selected preset
    }
}

// ---------------------------------------------------------------- undo / redo, compare

inline void Editor::trackUndo(){
    undo.engine = engine;
    undo.dirtyScenes = &dirty; undo.dirtyPresets = &dirtyPresets;
    undo.setlistDirty = &setlistDirty; undo.libraryDirty = &libraryDirty;
    bool active = ImGui::IsAnyItemActive();
    // look for a change when an edit may have ended (cheap otherwise): a control let go, a click,
    // Enter, or now and then
    bool here = ImGui::IsWindowHovered(ImGuiHoveredFlags_RootAndChildWindows | ImGuiHoveredFlags_AllowWhenBlockedByActiveItem | ImGuiHoveredFlags_AllowWhenBlockedByPopup);
    bool click = here && (ImGui::IsMouseReleased(ImGuiMouseButton_Left) || ImGui::IsMouseReleased(ImGuiMouseButton_Right));
    bool maybe = (undoWasActive && !active) || click || ImGui::IsKeyPressed(ImGuiKey_Enter) || ++undoIdle > 600;
    undoWasActive = active;
    if(!maybe) return;
    undoIdle = 0;
    undo.track(active || aiAuditioning);
}

inline void Editor::doUndo(bool redo){
    if(aiAuditioning) return;   // Keep or Discard first
    if(redo) undo.redo(); else undo.undo();
}

inline void Editor::drawUndoButtons(){
    ImGui::BeginDisabled(!undo.canUndo() || aiAuditioning);
    if(ImGui::Button("Undo")) doUndo(false);
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo the last edit (Cmd-Z): scenes, presets, setlist, library.\nIt comes back unsaved (Save to keep it).");
    ImGui::SameLine();
    ImGui::BeginDisabled(!undo.canRedo() || aiAuditioning);
    if(ImGui::Button("Redo")) doUndo(true);
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo (Cmd-Shift-Z)");
    ImGui::SameLine();
    int unsaved = (int)dirty.size() + (int)dirtyPresets.size() + (setlistDirty ? 1 : 0) + (libraryDirty ? 1 : 0);
    ImGui::BeginDisabled(unsaved == 0);
    if(ImGui::Button(unsaved ? ("Compare with saved (" + ofToString(unsaved) + ")").c_str() : "Compare with saved")) compareOpen = true;
    ImGui::EndDisabled();
    if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("What the unsaved edits change against the files, item by item (and Revert)");
}

inline void Editor::drawCompareWindow(){
    if(!compareOpen) return;
    float zz = z();
    ImGui::SetNextWindowSize(ImVec2(720 * zz, 480 * zz), ImGuiCond_FirstUseEver);
    if(!ImGui::Begin("Compare with saved##forms", &compareOpen)){ ImGui::End(); return; }
    auto show = [&](const std::string& title, const json& saved, const json& now, const std::function<void()>& revert){
        ImGui::PushID(title.c_str());
        bool open = ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen);
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 70 * zz);
        if(ImGui::SmallButton("Revert")) revert();
        if(open){
            if(saved.is_null()) ImGui::TextDisabled("  not saved yet (no file)");
            else {
                auto lines = diffLines(saved, now);
                if(lines.empty()) ImGui::TextDisabled("  same as the file");
                for(auto& l : lines) ImGui::TextUnformatted(("  " + l).c_str());
            }
        }
        ImGui::PopID();
    };
    std::vector<std::string> sc(dirty.begin(), dirty.end()), pr(dirtyPresets.begin(), dirtyPresets.end());
    for(auto& n : sc){
        auto it = engine->scenes.find(n);
        if(it == engine->scenes.end()) continue;
        show("Scene " + n, savedScene(*engine, it->second), json(it->second), [this, n](){ engine->reloadScene(n); dirty.erase(n); });
    }
    for(auto& r : pr){
        auto it = engine->presets.find(r);
        if(it == engine->presets.end()) continue;
        show("Preset " + r, savedPreset(*engine, it->second), json(it->second), [this, r](){
            auto it2 = engine->presets.find(r);
            if(it2 == engine->presets.end()) return;
            try { engine->presets[r] = Engine::readPresetFile(engine->presetPath(it2->second)); dirtyPresets.erase(r); } catch(...) {}
        });
    }
    if(setlistDirty) show("Setlist", savedSetlist(*engine), json(engine->setlist), [this](){
        try { engine->setlist = savedSetlist(*engine).get<std::vector<SetlistEntry>>(); setlistDirty = false; } catch(...) {}
    });
    if(libraryDirty) ImGui::TextDisabled("Library: edited (scales and palettes are compared as a whole; Reload in System puts the files back).");
    if(dirty.empty() && dirtyPresets.empty() && !setlistDirty && !libraryDirty) ImGui::TextDisabled("Nothing unsaved.");
    ImGui::End();
}

// ---------------------------------------------------------------- typed harmony (Scenes tab)

inline bool Editor::drawHarmonyText(Scene& s){
    float zz = z();
    bool changed = false;
    if(harmonyFor != s.name){ harmonyFor = s.name; harmonyBuf[0] = 0; }
    if(s.layers.empty()) return false;
    int li = ofClamp(sceneLayer, 0, (int)s.layers.size() - 1);
    LayerDef& l = s.layers[li];
    bool frozen = l.fixed.is_object() && l.fixed.contains("items");
    ImGui::Spacing();
    ImGui::TextDisabled("Typed harmony");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(240 * zz);
    ImGui::InputTextWithHint("##harmText", "I vi IV V   or   I:2 V:2", harmonyBuf, sizeof(harmonyBuf));
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Degrees I..VII (or 1..7), optional length in bars (I:2). Transpose 0.\n"
                                                 "A frozen layer plays it bar by bar (its roots); other layers draw their degrees\n"
                                                 "by chance, so for them it sets the degree chances in the progression's proportions.");
    if(frozen && l.fixed.contains("roots")) ImGui::TextDisabled("Layer %d (frozen) plays: %s", li + 1, harmonyToText(l.fixed.value("roots", std::vector<int>())).c_str());
    if(!harmonyBuf[0]) return false;
    HarmonyText h = parseHarmonyText(harmonyBuf);
    if(!h.error.empty()){ ImGui::TextColored(theme::rgb(theme::WARN), "%s", h.error.c_str()); return false; }
    auto roots = h.rootsFor(barCount(l.bars));
    ImGui::TextDisabled("Layer %d, %s bars: %s", li + 1, barsText(l.bars).c_str(), harmonyToText(roots).c_str());
    if(frozen){
        if(ImGui::Button("Write the roots into this frozen layer")){
            l.fixed["roots"] = roots;
            l.fixed["trans"] = std::vector<int>(barCount(l.bars), 0);
            changed = true;
        }
        ImGui::SameLine();
    }
    if(ImGui::Button("Set the degree chances from it")){
        std::vector<float> w(12, 0.0f);
        for(int r : h.rootsFor(h.totalBars())) if(r >= 0 && r < 12) w[r] += 1;
        float mx = *std::max_element(w.begin(), w.end());
        for(auto& v : w) v = mx > 0 ? v / mx : 0;
        s.degreeWeights = w;
        s.transposeWeights.assign(12, 0.0f);   // all zero: transpose 0, as in the 1920 scenes
        changed = true;
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Degree chances in proportion to the bars of each degree, transpose 0 only\n(the order is not kept: unfrozen layers draw a degree per bar)");
    return changed;
}

// ---------------------------------------------------------------- frame time (System tab)

inline void Editor::drawFrameStats(){
    if(!ImGui::CollapsingHeader("Frame time, render queue, upload")) return;
    float zz = z();
    int n = (int)std::min<size_t>(frameAt, frameMs.size());
    float mean = 0, mx = 0;
    for(int i = 0; i < n; i++){ mean += frameMs[i]; mx = std::max(mx, frameMs[i]); }
    if(n) mean /= n;
    ImGui::PlotLines("##frameMs", frameMs.data(), n, n < (int)frameMs.size() ? 0 : frameAt % (int)frameMs.size(), nullptr, 0, std::max(40.0f, mx), ImVec2(-1, 60 * zz));
    ImGui::Text("frame %.1f ms (mean %.1f, max %.1f over %d frames)  %.0f fps", n ? frameMs[(frameAt - 1) % frameMs.size()] : 0, mean, mx, n, ofGetFrameRate());
    auto& L = engine->schedule->layers[0];
    ImGui::Text("render queue: %d rendering, %d ready, max %d rendering, %d skipped", (int)engine->pending.size(), (int)L.ready.size(), engine->maxPending, engine->skipped);
    auto& ps = pipelineStats();
    auto row = [](const char* label, const StageTime& t){ if(t.n) ImGui::Text("%-9s last %7.1f ms   mean %7.1f ms   (%d)", label, t.last, t.mean, t.n); else ImGui::TextDisabled("%-9s -", label); };
    row("queue", ps.queue);
    row("render", ps.render);
    row("upload", ps.upload);
    row("archive", ps.archive);
    row("total", ps.total);
    if(ps.archiveBacklog) ImGui::Text("archive backlog %d", ps.archiveBacklog);
}

// ---------------------------------------------------------------- Perform tab

inline void Editor::queueScene(const std::string& name){
    if(!playScene || !engine->scenes.count(name)) return;
    if(perform.queueHardCut && setHardCut && getHardCut){
        if(perform.cutFor.empty()) perform.cutWas = getHardCut();
        setHardCut(true);
        perform.cutFor = name;
    }
    perform.queuedAt = ofGetElapsedTimef();
    playScene(name);
    perform.queued = name;
}

inline void Editor::queueSetlistEntry(int i){
    auto& sl = engine->setlist;
    if(sl.empty()) return;
    i = ((i % (int)sl.size()) + (int)sl.size()) % (int)sl.size();
    engine->setlistEntry = i;
    engine->imagesInEntry = 0;
    if(useSetlist) *useSetlist = true;
    perform.queued = sl[i].scene;
    perform.queuedAt = ofGetElapsedTimef();
    if(perform.switchNow && switchScene) switchScene();
}

inline void Editor::storeSnapshot(int k){
    auto& s = perform.snaps[k];
    auto& L = engine->schedule->layers[0];
    const Scene* cur = engine->currentScene();
    s.used = true;
    s.scene = L.header ? L.header->scene : cur ? cur->name : std::string();
    s.useSetlist = useSetlist && *useSetlist;
    s.setlistEntry = engine->setlistEntry;
    s.imagesInEntry = engine->imagesInEntry;
    for(int i = 0; i < 8; i++) s.gains[i] = perform.faders[i].gain;
    auto it = engine->scenes.find(s.scene);
    s.performance = it != engine->scenes.end() ? it->second.performance : json();
}

inline void Editor::recallSnapshot(int k){
    auto& s = perform.snaps[k];
    if(!s.used) return;
    for(int i = 0; i < 8; i++) perform.faders[i].gain = s.gains[i];
    if(s.useSetlist && !engine->setlist.empty()){ queueSetlistEntry(s.setlistEntry); engine->imagesInEntry = s.imagesInEntry; }
    else queueScene(s.scene);
    auto it = engine->scenes.find(s.scene);
    if(it != engine->scenes.end() && s.performance.is_object() && it->second.performance != s.performance){
        it->second.performance = s.performance;   // an edit of the scene (unsaved, undoable)
        dirty.insert(s.scene);
    }
}

// A big pad: 0 idle, 1 playing, 2 queued (dashed outline)
inline bool Editor::pad(const std::string& label, const std::string& sub, int state, ImVec2 size, ImU32 accent){
    ImVec2 p = ImGui::GetCursorScreenPos();
    bool clicked = ImGui::InvisibleButton(label.c_str(), size);
    bool hov = ImGui::IsItemHovered();
    auto* dl = ImGui::GetWindowDrawList();
    float r = 6 * z();
    ImU32 fill = state == 1 ? theme::u32(theme::PANEL2) : hov ? theme::u32(theme::LINE) : theme::u32(theme::PANEL);
    dl->AddRectFilled(p, ImVec2(p.x + size.x, p.y + size.y), fill, r);
    if(state == 1) dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), theme::u32(theme::PLAY), r, 0, 2.5f * z());
    else if(state == 2){
        // dashed: queued
        float d = 8 * z();
        ImU32 c = theme::u32(theme::PLAY, 200);
        for(float x = p.x; x < p.x + size.x; x += d * 2){ float e = std::min(x + d, p.x + size.x); dl->AddLine(ImVec2(x, p.y), ImVec2(e, p.y), c, 2); dl->AddLine(ImVec2(x, p.y + size.y), ImVec2(e, p.y + size.y), c, 2); }
        for(float y = p.y; y < p.y + size.y; y += d * 2){ float e = std::min(y + d, p.y + size.y); dl->AddLine(ImVec2(p.x, y), ImVec2(p.x, e), c, 2); dl->AddLine(ImVec2(p.x + size.x, y), ImVec2(p.x + size.x, e), c, 2); }
    }else dl->AddRect(p, ImVec2(p.x + size.x, p.y + size.y), theme::u32(theme::LINE), r);
    dl->AddRectFilled(ImVec2(p.x + r, p.y + size.y - 5 * z()), ImVec2(p.x + size.x - r, p.y + size.y - 2 * z()), accent, 2);
    std::string name = label.substr(0, label.find("##"));
    ImFont* f = ImGui::GetFont();
    float fs = ImGui::GetFontSize();
    dl->PushClipRect(p, ImVec2(p.x + size.x - 4, p.y + size.y), true);
    dl->AddText(f, fs * theme::fsL(), ImVec2(p.x + 10 * z(), p.y + 10 * z()), theme::u32(theme::TEXT), name.c_str());
    if(!sub.empty()) dl->AddText(f, fs * theme::fsS(), ImVec2(p.x + 10 * z(), p.y + 14 * z() + fs * theme::fsL()), theme::u32(theme::MUTED), sub.c_str());
    dl->PopClipRect();
    return clicked;
}

// ---------------------------------------------------------------- Assistant tab

inline bool Editor::aiKeyPresent(){
    std::ifstream f(ofToDataPath("config/requesty.md"));
    std::string k = ofxOceanodeRequesty::parseKey(f);
    if(k.empty()){ const char* e = std::getenv("REQUESTY_API_KEY"); if(e) k = ofxOceanodeRequesty::trim(e); }
    return !k.empty();
}

inline void Editor::setupAssistant(){
    aiSetup = true;
    aiKey = aiKeyPresent();
    ofxOceanodeAIAssistant::Spec s;
    s.title = "Forms assistant (Requesty)";
    s.subtitle = [this](){ return std::string("Mode: ") + assistV1ModeName((AssistV1Mode)aiMode) + " (choose it in the Assistant tab)"; };
    s.generateHint = "Describe the instrument preset or scene, or the change you want.";
    s.generateButton = "Send";
    s.fixButton = "Send";
    s.pendingHint = "The answer is checked against v1's schema and auditioned: Keep or Discard it in the Assistant tab.";
    s.readyMessage = "Answer received: see the Assistant tab";
    s.systemPrompt = [this](){ return assistV1SystemPrompt(*engine); };
    s.buildPrompt = [this](const std::string& user, bool){ aiUser = user; aiRetried = false; aiProblems.clear(); return assistV1UserPrompt((AssistV1Mode)aiMode, user, assistCurrent()); };
    s.apply = [this](const std::string& answer){ handleAssistAnswer(answer); return std::string(); };
    ai.setup(s);
    if(aiSaved.is_object()) ai.load(aiSaved);
}

inline std::string Editor::assistCurrent() const {
    auto m = (AssistV1Mode)aiMode;
    if(m == AssistV1Mode::ModifyPreset){ auto it = engine->presets.find(selPreset); if(it != engine->presets.end()) return json(it->second).dump(2); }
    if(m == AssistV1Mode::ModifyScene){ auto it = engine->scenes.find(selected); if(it != engine->scenes.end()) return json(it->second).dump(2); }
    return "";
}

inline void Editor::sendAssist(const std::string& user, bool retry){
    if(!retry){ aiProblems.clear(); aiRetried = false; aiUser = user; }
    std::string u = assistV1UserPrompt((AssistV1Mode)aiMode, user, assistCurrent());
    if(retry) u += assistV1RetryPrompt(*engine, aiAnswer, aiProblems);
    try {
        aiFuture = ai.generateText(assistV1SystemPrompt(*engine), u, 8000);
        aiBusy = true;
        aiStatus = retry ? "The answer had problems; asking once more..." : "Waiting for the answer...";
    } catch(const std::exception& e){ aiBusy = false; aiStatus = e.what(); }
}

inline void Editor::pollAssistant(){
    if(!aiBusy || !aiFuture.valid()) return;
    if(aiFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto res = aiFuture.get();
    aiBusy = false;
    if(!res.error.empty()){ aiStatus = "Error: " + res.error; return; }
    handleAssistAnswer(res.text);
}

inline void Editor::handleAssistAnswer(const std::string& text){
    aiAnswer = text;
    aiNotes.clear();
    auto m = (AssistV1Mode)aiMode;
    json j;
    std::vector<std::string> bad;
    if(!extractJsonObject(text, j)) bad.push_back("no JSON object in the answer");
    else {
        // the obvious slips are fixed first (a bare number for a range, a label as key...) and listed as ADJUSTED
        if(!assistV1IsScene(m)) normalizePresetV1(*engine, j, aiNotes);
        if(m == AssistV1Mode::ModifyPreset){
            auto it = engine->presets.find(selPreset);
            if(it == engine->presets.end()) bad.push_back("no instrument selected (Library > Instruments)");
            else {
                j["name"] = it->second.name;
                // what the answer leaves out stays as it was
                if(!it->second.legacy.empty() && !j.contains("legacy")) j["legacy"] = it->second.legacy;
                if(!it->second.notes.empty() && !j.contains("notes")) j["notes"] = it->second.notes;
                if(!it->second.category.empty() && !j.contains("category")) j["category"] = it->second.category;
                if(it->second.perBar > 0 && !j.contains("perBar")) j["perBar"] = it->second.perBar;
                if(j.value("gen", std::string()) != it->second.gen) bad.push_back("the generator changed (" + it->second.gen + " -> " + j.value("gen", std::string()) + ")");
            }
        }
        if(m == AssistV1Mode::ModifyScene){
            if(!engine->scenes.count(selected)) bad.push_back("no scene selected (Stage)");
            else {
                j["name"] = selected;
                const Scene& cur = engine->scenes[selected];
                if(!j.contains("performance") || !j["performance"].is_object() || j["performance"].empty()) j["performance"] = cur.performance;
                if(!j.contains("compat") || !j["compat"].is_object() || j["compat"].empty()) j["compat"] = cur.compat;
                if(!j.contains("snapshot")) j["snapshot"] = cur.snapshot;
            }
        }
        auto v = assistV1IsScene(m) ? validateSceneV1(*engine, j) : validatePresetV1(*engine, j);
        bad.insert(bad.end(), v.begin(), v.end());
    }
    if(!bad.empty()){
        aiProblems = bad;
        if(!aiRetried){ aiRetried = true; sendAssist(aiUser, true); return; }
        aiStatus = "Rejected: the answer does not fit v1's schema (nothing was changed).";
        return;
    }
    aiProblems.clear();
    startAssistAudition(j);
}

inline void Editor::startAssistAudition(const json& j){
    auto m = (AssistV1Mode)aiMode;
    aiIsPreset = !assistV1IsScene(m);
    aiProposal = j;
    if(aiIsPreset){
        Preset p = j.get<Preset>();
        aiTargetRef = m == AssistV1Mode::ModifyPreset ? selPreset : std::string();
        aiBefore = m == AssistV1Mode::ModifyPreset ? json(engine->presets[selPreset]) : json();
        std::string base = p.name + " (assistant)";
        p.name = base;
        for(int i = 2; engine->presets.count(p.ref()); i++) p.name = base + " " + ofToString(i);
        aiTempRef = p.ref();
        engine->presets[aiTempRef] = p;
        engine->refreshPresetMethods();
        aiSelBefore = selPreset;
        selPreset = aiTempRef;
        if(auditionSwitch){ aiAuditionWas = *auditionSwitch; *auditionSwitch = true; }
        else if(sendPreviews){ auto items = engine->presetItems(p); sendPreviews(previewSceneName(), {{aiTempRef, items}}); }
        aiStatus = auditionSwitch ? "Auditioning (the proposal alone on stage, with the preview scene). Keep or Discard."
                                  : "Previewing (Library > Instruments preview). Keep or Discard.";
    }else{
        Scene s = j.get<Scene>();
        aiTargetRef = m == AssistV1Mode::ModifyScene ? selected : std::string();
        aiBefore = m == AssistV1Mode::ModifyScene ? json(engine->scenes[selected]) : json();
        std::string base = s.name + " (assistant)", n = base;
        for(int i = 2; engine->scenes.count(n); i++) n = base + " " + ofToString(i);
        s.name = n;
        s.bank = m == AssistV1Mode::ModifyScene ? engine->scenes[selected].bank : engine->currentBank;
        aiTempRef = n;
        engine->scenes[n] = s;
        aiSelBefore = selected;
        selected = n;
        if(renderScene) renderScene(n);
        aiStatus = "Previewing the scene (a preview render; the stage is unchanged). Keep or Discard.";
    }
    aiAuditioning = true;
}

inline void Editor::endAssistAudition(bool keep){
    if(!aiAuditioning) return;
    if(aiIsPreset){
        auto it = engine->presets.find(aiTempRef);
        Preset p = it != engine->presets.end() ? it->second : aiProposal.get<Preset>();
        if(it != engine->presets.end()) engine->presets.erase(it);
        if(auditionSwitch) *auditionSwitch = aiAuditionWas;
        if(keep){
            if(!aiTargetRef.empty()){
                p.name = engine->presets[aiTargetRef].name;
                engine->presets[aiTargetRef] = p;
                dirtyPresets.insert(aiTargetRef);
                selPreset = aiTargetRef;
            }else{
                std::string base = aiProposal.value("name", std::string("assistant")), n = base;
                p.name = n;
                for(int i = 2; engine->presets.count(p.ref()); i++) p.name = base + " " + ofToString(i);
                engine->savePreset(p);   // new presets are files at once, as New does
                selPreset = p.ref();
            }
        }else selPreset = aiSelBefore;
        engine->refreshPresetMethods();
    }else{
        auto it = engine->scenes.find(aiTempRef);
        Scene s = it != engine->scenes.end() ? it->second : aiProposal.get<Scene>();
        if(it != engine->scenes.end()) engine->scenes.erase(it);
        if(keep){
            if(!aiTargetRef.empty() && engine->scenes.count(aiTargetRef)){
                s.name = aiTargetRef;
                s.bank = engine->scenes[aiTargetRef].bank;
                s.snapshot = engine->scenes[aiTargetRef].snapshot;
                engine->scenes[aiTargetRef] = s;
                dirty.insert(aiTargetRef);
                selected = aiTargetRef;
            }else{
                std::string base = aiProposal.value("name", std::string("assistant")), n = base;
                for(int i = 2; engine->scenes.count(n); i++) n = base + " " + ofToString(i);
                s.name = n;
                s.snapshot = -1;
                engine->scenes[n] = s;
                engine->saveScene(s);
                selected = n;
            }
        }else selected = aiSelBefore;
    }
    aiAuditioning = false;
    aiStatus = keep ? "Kept (one undo step)." : "Discarded.";
    if(keep) undo.commit("assistant");
}

inline void Editor::drawAssistantTab(){
    float zz = z();
    if(!aiSetup) setupAssistant();
    if(!aiKey){
        ImGui::TextDisabled("The assistant is inactive: no Requesty API key (config/requesty.md in the data folder, or REQUESTY_API_KEY).");
        if(ImGui::Button("Check again")) aiKey = aiKeyPresent();
        return;
    }
    theme::heading("MODE");
    for(int i = 0; i < 4; i++){
        if(i) ImGui::SameLine();
        ImGui::BeginDisabled(aiAuditioning || aiBusy);
        if(ImGui::RadioButton(assistV1ModeName((AssistV1Mode)i), aiMode == i)) aiMode = i;
        ImGui::EndDisabled();
    }
    auto m = (AssistV1Mode)aiMode;
    if(m == AssistV1Mode::ModifyPreset) ImGui::TextDisabled("Instrument: %s (select it in Library > Instruments)", selPreset.empty() ? "-" : selPreset.c_str());
    if(m == AssistV1Mode::ModifyScene) ImGui::TextDisabled("Scene: %s (the one being edited in Stage)", selected.empty() ? "-" : selected.c_str());
    theme::heading("REQUEST");
    ImGui::InputTextMultiline("##aiPrompt", aiPrompt, sizeof(aiPrompt), ImVec2(-1, 90 * zz));
    ImGui::BeginDisabled(aiBusy || aiAuditioning || !aiPrompt[0]);
    if(ImGui::Button("Send")) sendAssist(aiPrompt, false);
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Model...")) ai.openPopup();
    ai.drawPopup();
    ImGui::SameLine();
    ImGui::TextDisabled("model %s", ai.getModel().c_str());
    if(!aiStatus.empty()) ImGui::TextWrapped("%s", aiStatus.c_str());
    if(!aiProblems.empty()){
        theme::heading("REJECTED");
        for(auto& p : aiProblems) ImGui::TextColored(theme::rgb(theme::WARN), "- %s", p.c_str());
    }
    if(!aiNotes.empty()){
        theme::heading("ADJUSTED");
        ImGui::TextDisabled("Slips in the answer, read as v1 means them:");
        for(auto& p : aiNotes) ImGui::TextDisabled("- %s", p.c_str());
    }
    if(aiAuditioning){
        theme::heading(aiIsPreset ? "PROPOSED PRESET (auditioning)" : "PROPOSED SCENE (previewing)");
        ImGui::TextColored(theme::rgb(theme::TEXTURE), "Valid for v1's schema.");
        json now = aiIsPreset ? (engine->presets.count(aiTempRef) ? json(engine->presets[aiTempRef]) : aiProposal) : (engine->scenes.count(aiTempRef) ? json(engine->scenes[aiTempRef]) : aiProposal);
        if(aiBefore.is_null()) ImGui::TextDisabled("New: %s", aiProposal.value("name", std::string()).c_str());
        else {
            json a = aiBefore, b = now;
            a.erase("name"); b.erase("name");
            auto lines = diffLines(a, b);
            if(lines.empty()) ImGui::TextDisabled("No change.");
            for(auto& l : lines) ImGui::TextUnformatted(("  " + l).c_str());
        }
        if(ImGui::Button("Keep")) endAssistAudition(true);
        ImGui::SameLine();
        if(ImGui::Button("Discard")) endAssistAudition(false);
        ImGui::SameLine();
        if(!aiIsPreset && renderScene && ImGui::Button("Preview again")) renderScene(aiTempRef);
    }
    if(!aiAnswer.empty() && ImGui::CollapsingHeader("Last answer (raw)")){
        ImGui::BeginChild("##aiRaw", ImVec2(0, 160 * zz), ImGuiChildFlags_Borders);
        ImGui::TextUnformatted(aiAnswer.c_str());
        ImGui::EndChild();
    }
}

} // namespace forms
