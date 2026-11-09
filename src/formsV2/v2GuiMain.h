//
//  v2GuiMain.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The whole v2 GUI inside the node's window: top bar (mode switch, transport, screen, undo / redo,
//  assistant, frame rate, classic view), Edit mode (arrangement with collection / inspector /
//  sequencer + matrix; instrument editor, collection, matrix, setup screens; assistant panel) and
//  Perform mode. The node calls Gui::draw(host) between its ImGui::Begin / End.
//

#pragma once

#include "v2GuiArrangement.h"
#include "v2GuiInstrument.h"
#include "v2GuiCollection.h"
#include "v2GuiMatrix.h"
#include "v2GuiPerform.h"
#include "v2GuiAssistant.h"

namespace forms2 {
namespace gui {

class Gui {
public:
    Ctx cx;
    ArrangementView arrangement;
    InstrumentView instrument;
    CollectionView collection;
    MatrixScreen matrix;
    PerformScreen perform;
    AssistantPanel assistant;
    bool showAssistant = false;
    bool classic = false;            // the node shows the old window when true
    int bottomTab = 0;
    char saveName[128] = "my_scene";
    // Setup → Open scene
    std::vector<std::pair<std::string, std::vector<std::string>>> openFolders;
    bool openScanned = false;
    char openFilter[64] = "";
    std::string openPending;          // a file to open once the discard is confirmed

    // timeSec: seconds since start (animations, tap tempo)
    void draw(GuiHost& host, double timeSec){
        cx.host = &host;
        uiScale() = host.z;
        cx.refresh();
        cx.frame();
        Theme th; th.push(host.z);
        keys();
        topBar(timeSec);
        float H = ImGui::GetContentRegionAvail().y - px(24);
        if(cx.mode == Mode::Perform){
            ImGui::BeginChild("##perform", ImVec2(0, H), ImGuiChildFlags_None);
            perform.draw(cx, timeSec);
            ImGui::EndChild();
        } else {
            float aw = showAssistant && cx.screen != Screen::Assistant ? px(360) : 0;
            ImGui::BeginChild("##edit", ImVec2(ImGui::GetContentRegionAvail().x - aw - (aw > 0 ? px(8) : 0), H), ImGuiChildFlags_None);
            switch(cx.screen){
                case Screen::Arrangement: drawArrangement(); break;
                case Screen::Instrument: instrument.draw(cx); break;
                case Screen::Collection: collection.draw(cx, timeSec); break;
                case Screen::Matrix: matrix.draw(cx); break;
                case Screen::Assistant: assistant.draw(cx, false); break;
            }
            ImGui::EndChild();
            if(aw > 0){
                ImGui::SameLine();
                BeginPanel("##aipanel", ImVec2(0, H));
                assistant.draw(cx, true);
                EndPanel();
            }
        }
        statusBar();
        th.pop();
    }

    // the node's undo / redo / Keep go through here (one history for GUI edits and the assistant)
    bool undo(){ return cx.undo(); }
    bool redo(){ return cx.redo(); }
    void pushUndo(const std::string& before, const std::string& label){ cx.history.push(before, label); }

private:
    int setupSel = -1;               // -1: not on the setup screen
    void keys(){
        if(!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || ImGui::GetIO().WantTextInput) return;
        bool mod = ImGui::GetIO().KeySuper || ImGui::GetIO().KeyCtrl;
        if(mod && ImGui::IsKeyPressed(ImGuiKey_Z, false)){ if(ImGui::GetIO().KeyShift) cx.redo(); else cx.undo(); }
        if(cx.mode == Mode::Edit && ImGui::IsKeyPressed(ImGuiKey_Enter, false) && cx.screen == Screen::Collection){
            for(auto& it : cx.lib.items) if(it.path == cx.sel.preset){ collection.addLane(cx, it); break; }
        }
        if(ImGui::IsKeyPressed(ImGuiKey_Space, false) && cx.host->togglePlay) cx.host->togglePlay();
    }

    void topBar(double timeSec){
        GuiHost& h = *cx.host;
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos();
        float W = ImGui::GetContentRegionAvail().x, bh = px(48);
        dl->AddRectFilled(ImVec2(p.x - px(16), p.y - px(12)), ImVec2(p.x + W + px(16), p.y + bh - px(8)), col::rgb(col::panel));
        dl->AddLine(ImVec2(p.x - px(16), p.y + bh - px(8)), ImVec2(p.x + W + px(16), p.y + bh - px(8)), col::rgb(col::line));
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + px(2)));
        ImGui::AlignTextToFramePadding();
        Text(S_, col::text, "E S P E C T R E S");
        ImGui::SameLine(0, px(16));
        // the scene playing: click to open another (legacy v1 scenes included)
        if(h.openScene){
            std::string nm = (h.sceneName.empty() ? std::string("(no scene)") : h.sceneName) + (h.dirty ? " *" : "");
            if(Button((nm + "  v").c_str())){ openScanned = false; ImGui::OpenPopup("##openScene"); }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Open a scene (the converted v1 scenes are under Legacy)");
            ImGui::SetNextWindowSizeConstraints(ImVec2(px(380), 0), ImVec2(px(600), px(560)));
            if(ImGui::BeginPopup("##openScene")){
                Label("Open scene");
                openSceneList(px(420));
                ImGui::EndPopup();
            }
            ImGui::SameLine(0, px(16));
        }
        int m = (int)cx.mode;
        if(Segmented("mode", {"Edit", "Perform"}, m)) cx.mode = (Mode)m;
        ImGui::SameLine(0, px(16));
        // transport
        if(IconButton("rw", Icon::Rewind, false, 28, col::text, "Restart") && h.restart) h.restart();
        ImGui::SameLine(0, px(4));
        if(IconButton("pl", h.playing ? Icon::Pause : Icon::Play, h.playing, 28, col::text, h.usingTransport ? "Oceanode transport drives the clock" : "Play / pause (space)") && h.togglePlay) h.togglePlay();
        ImGui::SameLine(0, px(8));
        float b = h.bpm;
        ImGui::SetNextItemWidth(px(84));
        { FontSize f(S_); if(ImGui::DragFloat("##bpm", &b, 0.1f, 20, 300, "%.0f BPM") && h.setBpm) h.setBpm(b); }
        ImGui::SameLine(0, px(8));
        Engine* e = cx.engine();
        if(e && e->ready()){
            double now = h.nowBeat;
            const auto& sec = e->harmony().sectionDef(now);
            auto mtr = e->meterAt(now);
            int bar = e->harmony().barIndex(now);
            double beatIn = now - std::floor(now / mtr.barBeats()) * mtr.barBeats();
            auto ha = e->harmonyAt(now);
            ImGui::AlignTextToFramePadding();
            Text(S_, col::text, "Section " + (sec.name.empty() ? std::string("-") : sec.name) + " · " + std::to_string(mtr.num) + "/" + std::to_string(mtr.den) + " · bar " + std::to_string(bar + 1) + "." + std::to_string((int)beatIn + 1));
            ImGui::SameLine(0, px(12));
            Text(S_, col::muted, "Chord: degree " + std::to_string(ha.root + 1) + (h.held ? " (held)" : ""));
        }
        ImGui::SameLine(0, px(16));
        // screens (Edit mode)
        if(cx.mode == Mode::Edit){
            int s = (int)cx.screen;
            if(Segmented("screen", {"Arrangement", "Instrument", "Collection", "Modulation", "Assistant"}, s)) cx.screen = (Screen)s;
        }
        // right side
        float rightW = px(430);
        ImGui::SameLine(std::max(ImGui::GetCursorPosX(), ImGui::GetWindowContentRegionMax().x - rightW));
        ImGui::BeginDisabled(!cx.history.canUndo());
        if(Button("Undo", Btn::Ghost)) cx.undo();
        if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled) && cx.history.canUndo()) ImGui::SetTooltip("Undo %s (cmd Z)", cx.history.undoLabel().c_str());
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(4));
        ImGui::BeginDisabled(!cx.history.canRedo());
        if(Button("Redo", Btn::Ghost)) cx.redo();
        ImGui::EndDisabled();
        ImGui::SameLine(0, px(8));
        if(Button("Assistant", showAssistant ? Btn::On : Btn::Normal)) showAssistant = !showAssistant;
        ImGui::SameLine(0, px(8));
        if(Button("Setup", setupSel >= 0 ? Btn::On : Btn::Ghost)) setupSel = setupSel >= 0 ? -1 : 0;
        ImGui::SameLine(0, px(8));
        ImGui::AlignTextToFramePadding();
        Text(XS, h.fps >= 55 ? col::muted : col::warn, "Render " + fmtNum(h.fps, 0) + " fps");
        ImGui::SameLine(0, px(8));
        if(Chip("Classic view", classic)) classic = !classic;
        ImGui::SetCursorScreenPos(ImVec2(p.x, p.y + bh));
        if(setupSel >= 0) setupPanel();
        (void)timeSec;
    }

    // scene file and the classic sections (sound, stats, 3D, setlist...) embedded unchanged
    void setupPanel(){
        GuiHost& h = *cx.host;
        BeginPanel("##setup", ImVec2(0, px(560)));
        Label("Scene");
        Text(M, col::text, (h.sceneName.empty() ? std::string("(no scene)") : h.sceneName) + (h.dirty ? "  (edited, not saved)" : ""));
        Text(XS, col::faint, h.sceneFile);
        if(!h.fileStatus.empty()) Text(XS, col::muted, h.fileStatus);
        if(Button("Reload") && h.reload) h.reload();
        ImGui::SameLine();
        ImGui::SetNextItemWidth(px(180));
        ImGui::InputText("##saveas", saveName, sizeof saveName);
        ImGui::SameLine();
        if(Button("Save scene as…") && h.saveAs) h.saveAs(saveName);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Writes <name>.json next to the scene file (never over an existing file).");
        openScenePanel();
        for(auto& p : h.problems) TextWrap(XS, col::warn, "problem: " + p);
        for(auto& w : h.warnings) TextWrap(XS, col::mod, "warning: " + w);
        ImGui::Spacing();
        if(h.drawClassic){

            // each classic section draws its own title / header, unchanged
            for(auto& s : h.classicSections){ ImGui::PushID(s.c_str()); h.drawClassic(s); ImGui::PopID(); }
        }
        EndPanel();
    }

    // Open scene: every scene file in the known folders (legacy v1 scenes included), filterable
    void openScenePanel(){
        GuiHost& h = *cx.host;
        if(!h.sceneFolders || !h.openScene) return;
        ImGui::Spacing();
        Label("Open scene");
        openSceneList(px(170));
    }
    // the list itself (Setup, and the scene button's popup at the left of the top bar)
    void openSceneList(float listH){
        GuiHost& h = *cx.host;
        if(!h.sceneFolders || !h.openScene) return;
        if(!openScanned){ openFolders = h.sceneFolders(); openScanned = true; }
        ImGui::SetNextItemWidth(px(220));
        ImGui::InputTextWithHint("##openFilter", "filter (e.g. tomita)", openFilter, sizeof openFilter);
        ImGui::SameLine();
        if(Button("Rescan")) openScanned = false;
        std::string q = openFilter;
        for(auto& c : q) c = (char)std::tolower((unsigned char)c);
        ImGui::BeginChild("##openList", ImVec2(std::max(ImGui::GetContentRegionAvail().x, px(360)), listH), ImGuiChildFlags_Borders);
        for(auto& [label, files] : openFolders){
            std::vector<const std::string*> shown;
            for(auto& f : files){
                std::string n = std::filesystem::path(f).stem().string();
                std::string low = n; for(auto& c : low) c = (char)std::tolower((unsigned char)c);
                if(q.empty() || low.find(q) != std::string::npos) shown.push_back(&f);
            }
            if(shown.empty()) continue;
            if(!q.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);
            std::string head = label + "  (" + std::to_string(shown.size()) + ")###" + label;
            if(ImGui::TreeNodeEx(head.c_str(), ImGuiTreeNodeFlags_SpanAvailWidth)){
                for(auto* f : shown){
                    std::string n = std::filesystem::path(*f).stem().string();
                    bool cur = *f == h.sceneFile;
                    if(ImGui::Selectable((n + "##" + *f).c_str(), cur) && !cur){
                        if(h.dirty) openPending = *f;
                        else { h.openScene(*f); openScanned = false; ImGui::CloseCurrentPopup(); }   // the top bar's popup, if in it (a no-op in Setup)
                    }
                    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", f->c_str());
                }
                ImGui::TreePop();
            }
        }
        ImGui::EndChild();
        if(!openPending.empty()) ImGui::OpenPopup("Discard edits?");
        if(ImGui::BeginPopupModal("Discard edits?", nullptr, ImGuiWindowFlags_AlwaysAutoResize)){
            ImGui::TextUnformatted(("The scene has unsaved edits. Open " + std::filesystem::path(openPending).stem().string() + " and discard them?").c_str());
            if(ImGui::Button("Open")){ h.openScene(openPending); openPending.clear(); openScanned = false; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine();
            if(ImGui::Button("Cancel")){ openPending.clear(); ImGui::CloseCurrentPopup(); }
            ImGui::EndPopup();
        }
    }

    void drawArrangement(){
        float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
        float leftW = px(220), rightW = px(300), bottomH = std::min(px(260), H * 0.4f);
        float topH = H - bottomH - px(8);
        BeginPanel("##coll", ImVec2(leftW, topH));
        CollectionList(cx, topH - px(24));
        EndPanel();
        ImGui::SameLine();
        BeginPanel("##lanesP", ImVec2(W - leftW - rightW - px(16), topH), false);
        arrangement.drawLanes(cx, topH - px(24));
        EndPanel();
        ImGui::SameLine();
        BeginPanel("##insp", ImVec2(0, topH));
        arrangement.drawInspector(cx);
        EndPanel();
        // bottom: sequencer + upcoming, or the matrix of the selected instrument
        BeginPanel("##bottom", ImVec2(0, 0));
        Segmented("btab", {"Sequencer and upcoming", "Modulation matrix"}, bottomTab, S_);
        ImGui::Spacing();
        if(bottomTab == 0){
            float bw = ImGui::GetContentRegionAvail().x;
            ImGui::BeginChild("##seqb", ImVec2(bw * 0.6f, 0), ImGuiChildFlags_None);
            arrangement.drawSequencer(cx);
            ImGui::EndChild();
            ImGui::SameLine();
            ImGui::BeginChild("##upb", ImVec2(0, 0), ImGuiChildFlags_None);
            arrangement.drawUpcoming(cx);
            ImGui::EndChild();
        } else {
            // the matrix filtered to the selected instrument
            auto ids = cx.docOk ? instanceIds(cx.doc) : std::vector<std::string>{};
            int idx = 0; for(size_t i = 0; i < ids.size(); i++) if(ids[i] == cx.sel.instance) idx = (int)i + 1;
            matrix.filterIdx = idx;
            matrix.draw(cx);
        }
        EndPanel();
    }

    void statusBar(){
        GuiHost& h = *cx.host;
        ImGui::AlignTextToFramePadding();
        if(!cx.status.empty()) Text(XS, cx.statusBad ? col::warn : col::muted, cx.status);
        else Text(XS, col::faint, h.dirty ? "Scene edited, not saved (Setup > Save scene as)" : "");
        if(h.ai.auditioning){ ImGui::SameLine(0, px(16)); Text(XS, col::mod, "Auditioning an assistant answer: Keep or Discard"); }
    }
};

} // namespace gui
} // namespace forms2
