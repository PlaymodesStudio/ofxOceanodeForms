//
//  formsEditorGallery.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Gallery tab.
//  Methods of forms::Editor (declared in formsEditor.h, which includes this file).
//

#pragma once

namespace forms {

inline void Editor::drawGalleryTab(){
    float zz = z();
    gallery.root = engine->rendererFolder / "sessions";
    if(!galleryScanned){ gallery.scanSessions(); galleryScanned = true; }
    gallery.thumbs.update();

    // sessions
    ImGui::BeginChild("##gsess", ImVec2(230 * zz, 0), ImGuiChildFlags_Borders);
    ImGui::TextDisabled("SESSIONS");
    ImGui::SameLine();
    if(ImGui::SmallButton("Refresh")){ gallery.scanSessions(); if(!gallery.selectedSession.empty()) gallery.openSession(gallery.selectedSession); }
    for(auto& se : gallery.sessions){
        std::string lab = se.name + "  (" + ofToString(se.images) + ")" + (se.name == engine->session ? "  now" : "");
        if(ImGui::Selectable(lab.c_str(), gallery.selectedSession == se.name)){ gallery.openSession(se.name); galleryPick = -1; }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%d images%s", se.images, se.hasTimeline ? ", with its timeline (all layers replay)" : " (layer 1 replays)");
    }
    ImGui::EndChild();
    ImGui::SameLine();

    ImGui::BeginChild("##gmain", ImVec2(0, 0));
    if(gallery.selectedSession.empty()){
        ImGui::TextDisabled("Pick a session on the left: every image Forms Box ever showed is archived there, with its recipe.");
        ImGui::EndChild();
        return;
    }
    // header: replay, filters
    ImGui::Text("%s", gallery.selectedSession.c_str());
    ImGui::SameLine();
    bool thisReplay = engine->replaying && engine->replayName == gallery.selectedSession;
    if(thisReplay){
        if(ImGui::Button("Stop replay")) engine->stopReplay();
    }else{
        if(ImGui::Button("Replay this session")){
            auto plan = gallery.replayPlan(gallery.selectedSession);
            engine->startReplay(gallery.selectedSession, plan.layer1, plan.extra);
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Plays this session again from its archive, in its order (no rendering),\nlooping until Stop replay. Every archived layer too if the session has a timeline.");
    }
    if(engine->replaying && !thisReplay){ ImGui::SameLine(); ImGui::TextColored(ImVec4(1, 0.7f, 0.3f, 1), "replaying %s", engine->replayName.c_str()); ImGui::SameLine(); if(ImGui::SmallButton("stop")) engine->stopReplay(); }
    ImGui::SameLine();
    ImGui::TextDisabled("|");
    ImGui::SameLine();
    int layerCount=1;
    for(auto& image:gallery.images) layerCount=std::max(layerCount,image.layer);
    if(galleryLayer>layerCount) galleryLayer=0;
    ImGui::SetNextItemWidth(110 * zz);
    std::string layerLabel=galleryLayer ? "layer "+std::to_string(galleryLayer) : "all layers";
    if(ImGui::BeginCombo("##glayer",layerLabel.c_str())){
        if(ImGui::Selectable("all layers",galleryLayer==0)) galleryLayer=0;
        for(int i=1;i<=layerCount;++i) if(ImGui::Selectable(("layer "+std::to_string(i)).c_str(),galleryLayer==i)) galleryLayer=i;
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160 * zz);
    ImGui::InputTextWithHint("##gfilter", "scene contains...", galleryFilter, sizeof(galleryFilter));

    // grid
    float gridH = ImGui::GetContentRegionAvail().y * (galleryPick >= 0 ? 0.45f : 1.0f);
    ImGui::BeginChild("##ggrid", ImVec2(0, gridH), ImGuiChildFlags_Borders);
    float cardW = 192 * zz, imgH = cardW * 9 / 16;
    int perRow = std::max(1, (int)(ImGui::GetContentRegionAvail().x / (cardW + 8 * zz)));
    int shown = 0;
    std::string filt = ofToLower(galleryFilter);
    for(int i = 0; i < (int)gallery.images.size(); i++){
        auto& im = gallery.images[i];
        if(galleryLayer > 0 && im.layer != galleryLayer) continue;
        if(!filt.empty() && ofToLower(im.scene).find(filt) == std::string::npos) continue;
        if(shown % perRow) ImGui::SameLine();
        shown++;
        ImGui::PushID(i);
        ImGui::BeginGroup();
        // only cards in view ask for a thumbnail
        bool visible = ImGui::IsRectVisible(ImVec2(cardW, imgH));
        const ofTexture* t = visible ? gallery.thumbs.get(im.png, gallery.cacheDir()) : nullptr;
        bool sel = galleryPick == i;
        if(sel) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.55f, 0.75f, 1));
        bool clicked = t ? ImGui::ImageButton("##g", texId(*t), ImVec2(cardW, imgH)) : ImGui::Button("##g", ImVec2(cardW, imgH));
        if(sel) ImGui::PopStyleColor();
        if(clicked) galleryPick = i;
        ImGui::TextDisabled("L%d %s  %s", im.layer, im.id.c_str(), im.scene.c_str());
        ImGui::EndGroup();
        ImGui::PopID();
    }
    if(!shown) ImGui::TextDisabled("No images match.");
    ImGui::EndChild();

    // the selected image and its recipe
    if(galleryPick >= 0 && galleryPick < (int)gallery.images.size()){
        auto& im = gallery.images[galleryPick];
        if(galleryBigFor != im.png){
            galleryBigFor = im.png;
            ofPixels px;
            if(ofLoadImage(px, im.png) && px.getNumChannels() == 4){
                ofPixels rgb; rgb.allocate(px.getWidth(), px.getHeight(), OF_PIXELS_RGB);
                const unsigned char* s = px.getData(); unsigned char* d = rgb.getData();
                for(size_t k = 0; k < px.getWidth() * px.getHeight(); k++){ int a = s[k * 4 + 3]; d[k * 3] = s[k * 4] * a / 255; d[k * 3 + 1] = s[k * 4 + 1] * a / 255; d[k * 3 + 2] = s[k * 4 + 2] * a / 255; }
                galleryBig.setFromPixels(rgb);
            }else galleryBig.clear();
            try { galleryRecipe = readJsonFile(im.json); } catch(...) { galleryRecipe = json::object(); }
        }
        if(ImGui::BeginTable("##gdetail", 2, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_BordersInnerV)){
            ImGui::TableSetupColumn("img", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableSetupColumn("recipe", ImGuiTableColumnFlags_WidthStretch, 1.0f);
            ImGui::TableNextColumn();
            float w = ImGui::GetContentRegionAvail().x;
            if(galleryBig.isAllocated()) ImGui::Image(texId(galleryBig.getTexture()), ImVec2(w, w * 9 / 16));
            ImGui::TableNextColumn();
            ImGui::BeginChild("##grecipe", ImVec2(0, 0));
            const json& r = galleryRecipe;
            ImGui::Text("%s", r.value("scene", std::string("?")).c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("layer %d  |  image %s  |  seed %u", r.value("layer", 1), r.value("id", std::string()).c_str(), r.value("seed", 0u));
            ImGui::TextDisabled("%g bars  |  %d BPM  |  %s  |  %s", r.value("bars", 0.0), r.value("bpm", 0), r.value("scaleName", std::string()).c_str(), r.value("paletteName", std::string()).c_str());
            std::string harm;
            auto roots = r.value("roots", std::vector<int>()), trans = r.value("trans", std::vector<int>());
            for(size_t b = 0; b < roots.size(); b++) harm += (b ? "  " : "") + ofToString(roots[b]) + (b < trans.size() && trans[b] ? (trans[b] > 0 ? "+" : "") + ofToString(trans[b]) : "");
            ImGui::TextDisabled("harmony per bar (degree, transpose): %s", harm.c_str());
            if(ImGui::SmallButton("Copy recipe")) ImGui::SetClipboardText(r.dump(1).c_str());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("The whole sidecar (request and results) as JSON, to the clipboard");
            // use it
            ImGui::SameLine();
            if(ImGui::SmallButton("Freeze as scene")){
                std::string n = engine->sceneFromImage(r, true);
                selected = n;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("A new scene that always shows exactly this image (same recipe, same seed).\nIt opens in the Scenes tab; add it to the setlist or play it.");
            ImGui::SameLine();
            if(ImGui::SmallButton("Promote to scene")){
                std::string n = engine->sceneFromImage(r, false);
                selected = n;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("A new scene with this image's scene settings and its instruments (each at 100%%),\nto vary from. Every image of it is new.");
            ImGui::Text("Re-render larger:");
            for(int k : {2, 4}){
                ImGui::SameLine();
                std::string lab = "x" + ofToString(k);
                if(ImGui::SmallButton(lab.c_str()) && sendRequest){ std::string pth = engine->makePrint(r, k); if(!pth.empty()) sendRequest(pth); }
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("The same recipe at %d x the size, for print (into sessions/prints/; large renders take a while)", k);
            }
            for(auto& [tag, png] : printsDone){ ImGui::TextDisabled("%s ready: %s", tag.c_str(), png.c_str()); }
            ImGui::SeparatorText("Instruments (in drawing order)");
            const json items = r.value("items", json::array());
            const json res = r.contains("result") ? r["result"].value("items", json::array()) : json::array();
            for(size_t k = 0; k < items.size(); k++){
                const json& it = items[k];
                std::string name = it.value("preset", it.value("method", it.value("gen", std::string("?"))));
                int ms = k < res.size() ? res[k].value("ms", 0) : 0;
                std::string err = k < res.size() ? res[k].value("error", std::string()) : std::string();
                std::string clip = it.contains("clipBars") ? "  bars " + ofToString(it["clipBars"][0].get<int>() + 1) + "-" + ofToString(it["clipBars"][1].get<int>()) : "";
                ImGui::PushID((int)k);
                bool open = ImGui::TreeNode("##it", "%s%s  (%d ms)%s", name.c_str(), clip.c_str(), ms, err.empty() ? "" : "  ERROR");
                if(open){
                    if(!err.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.4f, 1), "%s", err.c_str());
                    const GenDef* g = it.contains("gen") ? engine->findGen(it.value("gen", std::string())) : nullptr;
                    const json params = it.value("params", json::object());
                    int nset = 0;
                    for(auto& [key, v] : params.items()){
                        bool isDefault = false;
                        if(g) for(auto& pd : g->params) if(pd.key == key){ isDefault = (v == pd.def) || (v.is_number() && pd.def.is_number() && std::fabs(v.get<double>() - pd.def.get<double>()) < 1e-6); break; }
                        if(isDefault) continue;
                        nset++;
                        std::string vs = v.dump();
                        if(vs.size() > 60) vs = vs.substr(0, 57) + "...";
                        ImGui::TextDisabled("%s", key.c_str()); ImGui::SameLine(200 * zz); ImGui::TextUnformatted(vs.c_str());
                    }
                    if(!nset) ImGui::TextDisabled(g ? "all values at their defaults" : "(a legacy method: no values)");
                    ImGui::TreePop();
                }
                ImGui::PopID();
            }
            ImGui::EndChild();
            ImGui::EndTable();
        }
    }
    ImGui::EndChild();
}

} // namespace forms
