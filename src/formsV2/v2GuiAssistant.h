//
//  v2GuiAssistant.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The assistant panel (mockup assistant.html, ROADMAP 5.11): modes as chips, the request at the
//  bottom; the answer shows the validation result first, then a readable diff in plain language;
//  every change is auditioned with Keep / Discard / A-B (Keep is one undo step). Rejected answers
//  are shown, not hidden.
//

#pragma once

#include "v2GuiArrangement.h"

namespace forms2 {
namespace gui {

struct AssistantPanel {
    char request[2048] = "";
    std::string lastBefore, lastAfterText;
    std::vector<std::string> diff;

    void draw(Ctx& cx, bool compact){
        AssistantHost& a = cx.host->ai;
        Text(compact ? L : XL, col::text, "Assistant");
        ImGui::SameLine(); ImGui::AlignTextToFramePadding();
        Text(XS, col::muted, cx.sel.instance.empty() ? "Scene context attached" : "Scene context + " + cx.sel.instance + " attached");
        static const char* modes[] = {"Create instrument", "Create kit", "Create scene", "Modify", "Fix", "Explain"};
        for(int i = 0; i < 6; i++){
            if(i) ImGui::SameLine(0, px(4));
            if(compact && i == 3) ImGui::NewLine();
            ImGui::PushID(i);
            if(Chip(modes[i], a.mode == i)) a.mode = i;
            ImGui::PopID();
        }
        if(!a.hasKey) TextWrap(S_, col::warn, "No API key: set it in the Oceanode assistant settings.");
        ImGui::Spacing();
        float H = ImGui::GetContentRegionAvail().y;
        ImGui::BeginChild("##answer", ImVec2(0, H - px(130)), ImGuiChildFlags_None);
        // the answer
        if(a.busy){ Text(M, col::muted, a.status.empty() ? "Waiting for the answer…" : a.status); }
        if(a.auditioning || !a.problems.empty() || !a.explanation.empty()){
            BeginPanel("##res", ImVec2(0, 0));
            if(a.auditioning){
                ImVec2 p = ImGui::GetCursorScreenPos();
                DrawIcon(ImGui::GetWindowDrawList(), Icon::Check, ImVec2(p.x + px(8), p.y + px(9)), px(6), col::rgb(col::ok));
                ImGui::Dummy(ImVec2(px(18), px(18))); ImGui::SameLine();
                Text(M, col::ok, "Checked against the format · valid · plays in the engine");
            } else if(!a.problems.empty()){
                Text(M, col::warn, "Not applied: " + std::to_string(a.problems.size()) + " problems");
                for(auto& p : a.problems) TextWrap(S_, col::warn, "· " + p);
            }
            if(a.auditioning){
                // readable diff of the candidate against the scene before it
                if(a.auditionBefore != lastBefore || cx.docText != lastAfterText){
                    lastBefore = a.auditionBefore; lastAfterText = cx.docText;
                    diff.clear();
                    try { diff = describeChanges(json::parse(a.auditionBefore), cx.doc); } catch(...){}
                }
                std::set<std::string> touched;
                for(auto& d : diff){ size_t c = d.find(':'); if(c != std::string::npos && d[0] != '+' && d[0] != '-') touched.insert(d.substr(0, c)); }
                std::string who; for(auto& t : touched){ if(!who.empty()) who += ", "; who += t; }
                if(!who.empty()) Text(S_, col::text, "Changes to " + who + ".");
                ImGui::Spacing();
                for(auto& d : diff){
                    bool plus = d.find(": +") != std::string::npos || d[0] == '+', minus = d.find(": -") != std::string::npos || d[0] == '-';
                    TextWrap(S_, plus ? col::ok : minus ? col::warn : col::text, d);
                }
                if(diff.empty()) Text(S_, col::muted, "No visible change.");
                ImGui::Spacing(); Hairline(); ImGui::Spacing();
                Text(S_, col::mod, "Auditioning from the next phrase");
                if(Button("Keep", Btn::Primary) && a.keep) a.keep();
                ImGui::SameLine();
                if(Button("Discard") && a.discard) a.discard();
                ImGui::SameLine();
                if(Button(a.abShowingOld ? "A / B  (hearing before)" : "A / B", a.abShowingOld ? Btn::On : Btn::Normal) && a.ab) a.ab();
                Text(XS, col::muted, "Keep adds one undo step (Undo reverts). Discard restores the scene exactly.");
            }
            if(!a.explanation.empty()){ ImGui::Spacing(); TextWrap(S_, col::text, a.explanation); }
            EndPanel();
        }
        if(!a.status.empty() && !a.busy){ ImGui::Spacing(); TextWrap(XS, col::muted, a.status); }
        for(auto& r : a.rejected){
            ImGui::Spacing();
            ImVec2 p = ImGui::GetCursorScreenPos();
            DrawIcon(ImGui::GetWindowDrawList(), Icon::Warn, ImVec2(p.x + px(7), p.y + px(8)), px(6), col::rgb(col::mod));
            ImGui::Dummy(ImVec2(px(16), px(16))); ImGui::SameLine();
            TextWrap(XS, col::mod, r);
        }
        ImGui::EndChild();
        // the request
        ImGui::SetNextItemWidth(-1);
        { FontSize f(M); ImGui::InputTextMultiline("##req", request, sizeof request, ImVec2(-1, px(72))); }
        if(ImGui::IsItemHovered() && request[0] == 0) ImGui::SetTooltip("Ask for a change, a new instrument, or an explanation…");
        bool canSend = a.send && !a.busy && a.hasKey;
        ImGui::BeginDisabled(!canSend);
        if(Button("Send", Btn::Primary)){ a.send(a.mode, request); }
        ImGui::EndDisabled();
        ImGui::SameLine();
        ImGui::BeginDisabled(!cx.history.canUndo());
        if(Button("Undo last change", Btn::Ghost)) cx.undo();
        ImGui::EndDisabled();
    }
};

} // namespace gui
} // namespace forms2
