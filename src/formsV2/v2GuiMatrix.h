//
//  v2GuiMatrix.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The modulation matrix (mockup matrix.html, ROADMAP 5.3): sources are rows, destinations columns
//  grouped by instrument / layer / scene. A cell shows its level badge (E circle, P, L, S square),
//  an amount bar (grey = negative) and ~ when animated. Click an empty cell to connect the row's
//  source; right-click a cell for the options popover (levels not allowed are struck through).
//

#pragma once

#include "v2GuiArrangement.h"

namespace forms2 {
namespace gui {

struct MatrixScreen {
    int filterIdx = 0;               // 0 = all instruments
    bool allDest = false;
    RoutingRef cur; bool hasCur = false;
    int curRow = -1, curCol = -1;

    // the options of one routing; returns false when it was removed
    void options(Ctx& cx, const MatrixView& v){
        Routing r;
        if(!hasCur || !getRouting(cx.doc, cur, r)){ hasCur = false; return; }
        const ParamDef* d = destDef(cx.doc, cur.owner, r.dest);
        std::string title = sourceLabel(r) + " \xe2\x86\x92 " + (cur.owner.empty() ? std::string("Scene") : cur.owner) + " · " + destLabel(r.dest, d);
        Text(M, col::text, title);
        unsigned allowed = allowedLevels(cx.doc, cur.owner, r.dest);
        RoutingRef ref = cur;
        auto apply = [&](const std::string& label, const Routing& nr){
            cx.editRouting(label, ref.owner, [&](json& s){ return setRouting(s, ref, nr); });
        };
        ImGui::Spacing(); Label("When it changes");
        static const Level lv[] = {LevelEvent, LevelPhrase, LevelLayer, LevelScene};
        static const char* ln[] = {"Each event", "Each phrase", "Continuous", "Scene"};
        for(int i = 0; i < 4; i++){
            bool ok = (allowed & lv[i]) != 0, on = r.level == lv[i];
            ImGui::PushID(i);
            LevelBadge(lv[i], 16, !ok); ImGui::SameLine(0, px(4));
            ImGui::BeginDisabled(!ok);
            if(Chip(ln[i], on) && !on){ Routing nr = r; nr.level = lv[i]; apply("Routing level", nr); }
            ImGui::EndDisabled();
            if(!ok && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Not allowed for %s", destLabel(r.dest, d).c_str());
            ImGui::PopID();
            if(i % 2 == 0) ImGui::SameLine(0, px(12));
        }
        float lw = px(90), cw = px(200);
        ImGui::Spacing();
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Amount"); ImGui::SameLine(lw);
        float a = (float)r.amount;
        if(Slider("amt", a, -1, 1, cw, col::mod)){ Routing nr = r; nr.amount = std::round(a * 100) / 100.0; apply("Routing amount", nr); }
        ImGui::SameLine(); Text(S_, col::text, fmtNum(a));
        if(d && (d->type == "float" || d->type == "int")){
            ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Range"); ImGui::SameLine(lw);
            bool hr = r.hasRange;
            if(Toggle("hr", hr, col::mod)){ Routing nr = r; nr.hasRange = hr; if(hr){ nr.min = d->min; nr.max = d->max; } apply("Routing range", nr); }
            if(r.hasRange){
                ImGui::SameLine();
                float lo = (float)r.min, hi = (float)r.max;
                if(Range("rg", lo, hi, (float)d->min, (float)d->max, cw - px(40), col::mod)){ Routing nr = r; nr.min = lo; nr.max = hi; apply("Routing range", nr); }
                ImGui::SameLine(); Text(S_, col::text, fmtNum(lo) + "–" + fmtNum(hi));
            }
        }
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Curve"); ImGui::SameLine(lw);
        int ci = r.curve > 1.5 ? 1 : r.curve < 0.75 ? 2 : 0;
        if(Menu("##curve", ci, {"Linear", "Exponential", "Logarithmic"}, cw)){ Routing nr = r; nr.curve = ci == 1 ? 2.0 : ci == 2 ? 0.5 : 1.0; apply("Routing curve", nr); }
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Show as"); ImGui::SameLine(lw);
        int rm = routingRender(r) == "animated" ? 1 : 0;
        bool canAnim = allowsAnimated(r.dest) && !startsWith(r.dest, "seq.");
        ImGui::BeginDisabled(!canAnim && rm == 0);
        if(Segmented("render", {"Drawn", "Animated"}, rm)){ Routing nr = r; setRoutingRender(nr, rm ? "animated" : "drawn"); apply("Routing render", nr); }
        ImGui::EndDisabled();
        if(!canAnim) { ImGui::SameLine(); Text(XS, col::faint, "drawn only"); }
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Invert"); ImGui::SameLine(lw);
        bool inv = r.invert;
        if(Toggle("inv", inv, col::mod)){ Routing nr = r; nr.invert = inv; apply("Routing invert", nr); }
        if(r.level == LevelLayer){
            ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Glide"); ImGui::SameLine(lw);
            float sm = (float)r.smooth;
            if(Slider("sm", sm, 0, 8, cw, col::mod)){ Routing nr = r; nr.smooth = std::round(sm * 10) / 10.0; apply("Routing glide", nr); }
            ImGui::SameLine(); Text(S_, col::text, fmtNum(sm, 1) + " beats");
        }
        // same source on other columns of the owner
        std::string also;
        for(size_t c = 0; c < v.cols.size(); c++)
            if(curRow >= 0 && (int)c != curCol && v.cells[(size_t)curRow][c].used && v.cols[c].owner == cur.owner){ if(!also.empty()) also += ", "; also += v.cols[c].label; }
        if(!also.empty()) TextWrap(XS, col::muted, "Same source as " + also + ": one value drives them together.");
        ImGui::Spacing();
        if(ImGui::BeginMenu("Copy to")){
            for(size_t c = 0; c < v.cols.size(); c++){
                if((int)c == curCol || v.cols[c].owner != cur.owner) continue;
                if(ImGui::MenuItem(v.cols[c].label.c_str())){
                    Routing nr = r; nr.dest = v.cols[c].dest;
                    if(v.cols[c].def && !(v.cols[c].def->levels & nr.level)) nr.level = v.cols[c].def->defaultLevel;
                    if(!allowsAnimated(nr.dest)) setRoutingRender(nr, "drawn");
                    if(nr.hasRange && v.cols[c].def){ nr.min = v.cols[c].def->min; nr.max = v.cols[c].def->max; }
                    cx.editRouting("Copy routing", ref.owner, [&](json& s){ return addRouting(s, ref.owner, nr); });
                }
            }
            ImGui::EndMenu();
        }
        if(Button("Remove", Btn::Ghost)){
            if(cx.editRouting("Remove routing", ref.owner, [&](json& s){ return removeRouting(s, ref); })) hasCur = false;
        }
    }

    void draw(Ctx& cx){
        if(!cx.docOk){ Text(M, col::muted, "No scene."); return; }
        std::vector<std::string> owners = {"All instruments"};
        for(auto& id : instanceIds(cx.doc)) owners.push_back(id);
        if(!cx.sel.instance.empty() && filterIdx == 0 && false) {}
        filterIdx = std::clamp(filterIdx, 0, (int)owners.size() - 1);
        std::string filter = filterIdx == 0 ? "" : owners[(size_t)filterIdx];
        MatrixView v = buildMatrix(cx.doc, filter, allDest);

        Text(XL, col::text, "Modulation");
        ImGui::SameLine(); ImGui::AlignTextToFramePadding();
        Text(S_, col::muted, std::to_string(v.routingCount) + " routings");
        ImGui::SameLine(0, px(16));
        if(Button("+ Source")) ImGui::OpenPopup("##addsrc");
        if(ImGui::BeginPopup("##addsrc")){
            std::string owner = filter.empty() ? cx.sel.instance : filter;
            if(owner.empty() && !instanceIds(cx.doc).empty()) owner = instanceIds(cx.doc).front();
            Text(XS, col::muted, "Adds the source to " + owner + " · Brightness (change it after)");
            static const char* kinds[] = {"lfo", "random", "phraseEnv", "noteEnv", "macro"};
            static const char* names[] = {"LFO", "Random", "Phrase envelope", "Note envelope", "Macro"};
            for(int i = 0; i < 5; i++) if(ImGui::MenuItem(names[i]) && !owner.empty()){
                int n = 1; for(auto& k : v.rowKeys) if(k.rfind(kinds[i], 0) == 0) n++;
                Routing r = newSource(kinds[i], n); r.dest = "brightness"; r.level = LevelEvent;
                if(std::string(kinds[i]) == "macro" || std::string(kinds[i]) == "lfo") r.level = LevelLayer;
                if(std::string(kinds[i]) == "phraseEnv") r.level = LevelPhrase;
                cx.editRouting("Add source", owner, [&](json& s){ return addRouting(s, owner, r); });
            }
            ImGui::EndPopup();
        }
        ImGui::SameLine(0, px(16)); ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Show:"); ImGui::SameLine();
        Menu("##show", filterIdx, owners, px(180));
        ImGui::SameLine(); Toggle("alld", allDest); ImGui::SameLine(); Text(S_, col::muted, "all destinations");

        // legend
        LevelBadge(LevelEvent, 14); ImGui::SameLine(); Text(XS, col::muted, "per event"); ImGui::SameLine(0, px(12));
        LevelBadge(LevelPhrase, 14); ImGui::SameLine(); Text(XS, col::muted, "per phrase"); ImGui::SameLine(0, px(12));
        LevelBadge(LevelLayer, 14); ImGui::SameLine(); Text(XS, col::muted, "continuous (layer)"); ImGui::SameLine(0, px(12));
        LevelBadge(LevelScene, 14); ImGui::SameLine(); Text(XS, col::muted, "scene"); ImGui::SameLine(0, px(12));
        Text(XS, col::muted, "~ animated (moves in real time); otherwise drawn into the shape · bar: amount (grey = negative) · hatched: no level allowed");
        ImGui::Spacing();

        float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
        float sideW = hasCur ? px(340) : 0;
        ImGui::BeginChild("##grid", ImVec2(W - sideW - (hasCur ? px(8) : 0), H), ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float rowH = px(36), colW = px(96), rowHeadW = px(220), headH = px(44);
        ImVec2 o = ImGui::GetCursorScreenPos();
        // header
        DrawText(dl, XS, ImVec2(o.x, o.y + px(24)), col::rgb(col::faint), "SOURCES \xe2\x86\x93 / DESTINATIONS \xe2\x86\x92");
        std::string lastGroup = "\x01";
        for(size_t c = 0; c < v.cols.size(); c++){
            float x = o.x + rowHeadW + c * colW;
            const auto& mc = v.cols[c];
            std::string g = mc.owner.empty() ? "Scene" : mc.owner;
            if(startsWith(mc.dest, "layer")) g += " · layer";
            if(g != lastGroup){
                lastGroup = g;
                dl->AddLine(ImVec2(x, o.y), ImVec2(x, o.y + headH + rowH * v.rowKeys.size()), col::rgb(col::line2));
                FamilyMark(dl, ImVec2(x + px(8), o.y + px(8)), px(4), mc.owner.empty() ? "mod" : cx.familyOfId(mc.owner));
                DrawText(dl, XS, ImVec2(x + px(16), o.y + px(1)), col::rgb(col::muted), g);
            }
            dl->PushClipRect(ImVec2(x, o.y), ImVec2(x + colW - px(4), o.y + headH), true);
            DrawText(dl, S_, ImVec2(x + px(4), o.y + px(20)), col::rgb(col::text), mc.label);
            dl->PopClipRect();
        }
        ImGui::Dummy(ImVec2(rowHeadW + colW * v.cols.size(), headH));
        for(size_t r = 0; r < v.rowKeys.size(); r++){
            ImVec2 rp = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(rp.x, rp.y), ImVec2(rp.x + rowHeadW + colW * v.cols.size(), rp.y), col::rgb(col::line));
            dl->PushClipRect(rp, ImVec2(rp.x + rowHeadW - px(8), rp.y + rowH), true);
            DrawText(dl, S_, ImVec2(rp.x, rp.y + px(10)), col::rgb(col::text), v.rowLabels[r]);
            dl->PopClipRect();
            for(size_t c = 0; c < v.cols.size(); c++){
                ImVec2 a(rp.x + rowHeadW + c * colW + px(2), rp.y + px(4)), b(a.x + colW - px(6), a.y + rowH - px(8));
                ImGui::SetCursorScreenPos(a);
                ImGui::PushID((int)(r * 1000 + c));
                bool clicked = ImGui::InvisibleButton("##cell", ImVec2(b.x - a.x, b.y - a.y));
                bool hov = ImGui::IsItemHovered();
                const MatrixCell& cell = v.cells[r][c];
                unsigned allowed = v.cols[c].def ? v.cols[c].def->levels : 0;
                bool isCur = hasCur && cell.used && cell.ref.owner == cur.owner && cell.ref.index == cur.index;
                dl->AddRectFilled(a, b, col::rgb(isCur ? col::line2 : hov ? col::line : col::panel), px(4));
                if(!allowed){ for(float x = a.x; x < b.x; x += px(6)) dl->AddLine(ImVec2(x, b.y), ImVec2(std::min(b.x, x + (b.y - a.y)), a.y), col::rgb(col::line2)); }
                if(cell.used){
                    LevelBadgeAt(dl, ImVec2(a.x + px(4), a.y + px(5)), px(16), cell.r.level, !cell.valid);
                    float bx0 = a.x + px(26), bx1 = b.x - px(16), by = a.y + px(13);
                    dl->AddRectFilled(ImVec2(bx0, by - px(2)), ImVec2(bx1, by + px(2)), col::rgb(col::line2), px(2));
                    float amt = (float)std::clamp(std::fabs(cell.r.amount), 0.0, 1.0);
                    dl->AddRectFilled(ImVec2(bx0, by - px(2)), ImVec2(bx0 + (bx1 - bx0) * amt, by + px(2)), col::rgb(cell.r.amount < 0 ? col::muted : col::mod), px(2));
                    if(routingRender(cell.r) == "animated") DrawText(dl, M, ImVec2(b.x - px(13), a.y + px(2)), col::rgb(col::text), "~");
                    if(!cell.valid) DrawText(dl, XS, ImVec2(bx0, by + px(3)), col::rgb(col::warn), "not allowed");
                    if(clicked){ cur = cell.ref; hasCur = true; curRow = (int)r; curCol = (int)c; }
                    if(ImGui::IsItemClicked(ImGuiMouseButton_Right)){ cur = cell.ref; hasCur = true; curRow = (int)r; curCol = (int)c; ImGui::OpenPopup("##opts"); }
                    if(hov) ImGui::SetTooltip("%s \xe2\x86\x92 %s\n%s · amount %s", v.rowLabels[r].c_str(), v.cols[c].label.c_str(), levelWords(cell.r.level).c_str(), fmtNum(cell.r.amount).c_str());
                } else if(allowed){
                    if(hov) DrawIcon(dl, Icon::Plus, ImVec2((a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f), px(5), col::rgb(col::muted));
                    if(clicked){
                        int rr = (int)r, cc = (int)c;
                        std::string owner = v.cols[c].owner;
                        cx.editRouting("Connect " + v.rowLabels[r] + " \xe2\x86\x92 " + v.cols[c].label, owner, [&](json& s){ return connectCell(s, v, rr, cc); });
                    }
                }
                if(ImGui::BeginPopup("##opts")){ options(cx, v); ImGui::EndPopup(); }
                ImGui::PopID();
            }
            ImGui::SetCursorScreenPos(ImVec2(rp.x, rp.y + rowH));
        }
        if(v.rowKeys.empty()) TextWrap(M, col::muted, "No routings yet. Use + Source, or the assistant.");
        ImGui::Dummy(ImVec2(1, px(8)));
        ImGui::EndChild();
        if(hasCur){
            ImGui::SameLine();
            BeginPanel("##mopts", ImVec2(0, H));
            options(cx, v);
            ImGui::Spacing();
            if(Button("Close", Btn::Ghost)) hasCur = false;
            EndPanel();
        }
    }
};

} // namespace gui
} // namespace forms2
