//
//  v2GuiPerform.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Perform mode (mockup perform.html): only big, touch- and MIDI-friendly controls. Scene pads (the
//  queued one dashed, switching at the next phrase), 8 macro faders, per-instrument mute / lock /
//  reroll, Drop, Hold chord, snapshots, tap tempo. No editing, no small text.
//

#pragma once

#include "v2GuiArrangement.h"

namespace forms2 {
namespace gui {

struct PerformScreen {
    std::vector<double> taps;        // tap tempo times (s)
    bool dropArmed = false;

    void draw(Ctx& cx, double timeSec){
        GuiHost& h = *cx.host;
        float W = ImGui::GetContentRegionAvail().x;
        float gap = px(12);
        // ---- scenes
        int n = (int)h.padNames.size();
        if(n){
            int per = std::min(n, 6);
            float pw = (W - gap * (per - 1)) / per;
            for(int i = 0; i < n; i++){
                if(i % per) ImGui::SameLine(0, gap);
                int state = i == h.queuedPad ? 2 : i == h.currentPad ? 1 : 0;
                std::string lab = std::to_string(i + 1) + "  " + h.padNames[(size_t)i];
                if(Pad(("sc" + std::to_string(i)).c_str(), lab, state, ImVec2(pw, px(72)), col::text, L) && h.queuePad) h.queuePad(i);
            }
            Text(M, col::muted, h.queuedPad >= 0 ? "Queued: switches at the next phrase" : "Scenes switch at the next phrase");
        } else Text(M, col::muted, "No scenes in the scene folder.");
        ImGui::Spacing();

        // ---- macros
        Text(L, col::text, "Macros");
        float fw = (W - gap * 7) / 8, fh = px(220);
        for(int i = 0; i < 8; i++){
            if(i) ImGui::SameLine(0, gap);
            float v = h.macro ? h.macro(i) : 0;
            std::string name = macroName(cx, i);
            if(Fader(("mf" + std::to_string(i)).c_str(), name, v, ImVec2(fw, fh), col::mod) && h.setMacro) h.setMacro(i, v);
        }
        ImGui::Spacing();

        // ---- instruments
        Text(L, col::text, "Instruments");
        ImGui::SameLine(); Text(M, col::muted, "M mute · L lock · reroll next phrase");
        auto ids = cx.docOk ? instanceIds(cx.doc) : std::vector<std::string>{};
        Engine* e = cx.engine();
        int per = std::max(1, std::min((int)ids.size(), 8));
        float tw = (W - gap * (per - 1)) / per;
        for(size_t i = 0; i < ids.size(); i++){
            if(i % per) ImGui::SameLine(0, gap);
            const std::string& id = ids[i];
            ImGui::PushID(id.c_str());
            ImGui::BeginGroup();
            std::string fam = cx.familyOfId(id);
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + px(120)), col::rgb(col::panel), px(8));
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + px(4)), col::rgb(col::family(fam)), px(2));
            FamilyMark(dl, ImVec2(p.x + px(16), p.y + px(22)), px(6), fam);
            dl->PushClipRect(p, ImVec2(p.x + tw, p.y + px(40)), true);
            DrawText(dl, L, ImVec2(p.x + px(30), p.y + px(10)), col::rgb(col::text), id);
            dl->PopClipRect();
            bool m = h.isMuted && h.isMuted(id), l = e && e->ready() && e->locked(id);
            float bw = (tw - px(24)) / 3;
            ImGui::SetCursorScreenPos(ImVec2(p.x + px(6), p.y + px(48)));
            if(Pad("m", "M", m ? 1 : 0, ImVec2(bw, px(64)), col::warn, XL) && h.mute) h.mute(id, !m);
            ImGui::SameLine(0, px(6));
            if(Pad("l", "L", l ? 1 : 0, ImVec2(bw, px(64)), col::pitched, XL) && h.lock) h.lock(id, !l);
            ImGui::SameLine(0, px(6));
            ImVec2 rp = ImGui::GetCursorScreenPos();
            if(Pad("r", "", 0, ImVec2(bw, px(64))) && h.reroll) h.reroll(id);
            DrawIcon(dl, Icon::Reroll, ImVec2(rp.x + bw * 0.5f, rp.y + px(32)), px(12), col::rgb(col::text));
            // the tile's full size as an item: ImGui asserts if the cursor alone extends the group
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy(ImVec2(tw, px(124)));
            ImGui::EndGroup();
            ImGui::PopID();
        }
        ImGui::Spacing();

        // ---- drop, hold, snapshots, tap
        float bh = px(72);
        if(Pad("drop", "DROP", dropArmed ? 2 : 0, ImVec2(px(180), bh), col::warn, XL)) drop(cx);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Unmutes everything and rerolls every instrument at its next phrase.");
        ImGui::SameLine(0, gap);
        if(Pad("hold", "Hold chord", h.held ? 1 : 0, ImVec2(px(180), bh), col::pitched) && h.hold) h.hold(!h.held);
        for(int i = 0; i < 8; i++){
            bool has = h.hasSnapshot && h.hasSnapshot(i);
            if(!has && i > 1) continue;
            ImGui::SameLine(0, gap);
            std::string lab = std::string("Snapshot ") + (char)('A' + i);
            if(Pad(("snap" + std::to_string(i)).c_str(), lab, has ? 0 : 0, ImVec2(px(150), bh), col::text, M)){
                if(has && h.recallSnapshot) h.recallSnapshot(i);
                else if(h.storeSnapshot) h.storeSnapshot(i);
            }
            if(ImGui::IsItemClicked(ImGuiMouseButton_Right) && h.storeSnapshot) h.storeSnapshot(i);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip(has ? "Click: recall · right-click: store again" : "Click: store the current state");
        }
        ImGui::SameLine(0, gap);
        int free = -1; for(int i = 0; i < 8; i++) if(!(h.hasSnapshot && h.hasSnapshot(i))){ free = i; break; }
        if(free >= 0 && Pad("savesnap", "+ Save snapshot", 0, ImVec2(px(170), bh), col::text, M) && h.storeSnapshot) h.storeSnapshot(free);
        ImGui::SameLine(0, gap);
        if(Pad("tap", "Tap tempo  " + fmtNum(h.bpm, 1), 0, ImVec2(px(200), bh), col::text, M)) tap(cx, timeSec);
    }

    static std::string macroName(Ctx& cx, int i){
        // a macro is named by a scene routing "macroN" with sourceParams.label, else "Macro N"
        if(cx.docOk && cx.doc.contains("macroNames") && cx.doc["macroNames"].is_array() && i < (int)cx.doc["macroNames"].size() && cx.doc["macroNames"][(size_t)i].is_string())
            return cx.doc["macroNames"][(size_t)i].get<std::string>();
        return "Macro " + std::to_string(i + 1);
    }
    void drop(Ctx& cx){
        GuiHost& h = *cx.host;
        for(auto& id : cx.docOk ? instanceIds(cx.doc) : std::vector<std::string>{}){
            if(h.isMuted && h.isMuted(id) && h.mute) h.mute(id, false);
            if(h.reroll) h.reroll(id);
        }
        cx.say("Drop: everything plays, new variations from the next phrase");
    }
    void tap(Ctx& cx, double t){
        if(!taps.empty() && t - taps.back() > 2.0) taps.clear();
        taps.push_back(t);
        if(taps.size() > 6) taps.erase(taps.begin());
        if(taps.size() >= 3){
            double d = (taps.back() - taps.front()) / (taps.size() - 1);
            if(d > 0.1 && cx.host->setBpm) cx.host->setBpm((float)std::clamp(60.0 / d, 20.0, 300.0));
        }
    }
};

} // namespace gui
} // namespace forms2
