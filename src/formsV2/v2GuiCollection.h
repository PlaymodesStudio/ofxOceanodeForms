//
//  v2GuiCollection.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The collection browser (mockup collection.html): search, role filters (family colour + shape),
//  tags and source; tiles for presets and kits (a kit is a tile like any other, with its count);
//  the detail panel with audition, "Drop onto: New lane / Replace <selected>" and main parameters.
//  Thumbnails are a drawn family pattern, not a live render (decision: cost with many tiles).
//

#pragma once

#include "v2GuiArrangement.h"

namespace forms2 {
namespace gui {

struct CollectionView {
    char search[96] = "";
    LibraryFilter filter;
    std::string auditionId;          // instance added for audition ("" none)
    std::string auditionBefore;      // scene text before the audition

    // a looping drawn thumbnail: the family shape scattered and moving with time
    static void Thumb(ImDrawList* dl, ImVec2 p, ImVec2 q, const LibraryItem& it, float t){
        dl->AddRectFilled(p, q, col::rgb(col::bg), px(4));
        dl->PushClipRect(p, q, true);
        uint64_t h = std::hash<std::string>{}(it.name);
        int n = it.kit ? 10 : 7;
        for(int i = 0; i < n; i++){
            double rx = ((h >> (i * 5)) & 31) / 31.0, ry = ((h >> (i * 3 + 7)) & 31) / 31.0;
            float x = p.x + (float)std::fmod(rx + t * 0.08 * (1 + (i % 3)), 1.0) * (q.x - p.x);
            float y = p.y + (float)(0.15 + 0.7 * ry) * (q.y - p.y);
            std::string fam = it.kit ? (i % 2 ? "percussive" : "pitched") : it.family;
            if(fam == "pitched") dl->AddRectFilled(ImVec2(x - px(10), y - px(1.5f)), ImVec2(x + px(10), y + px(1.5f)), col::rgb(col::family(fam), 0.85f));
            else FamilyMark(dl, ImVec2(x, y), px(3), fam, col::rgb(col::family(fam), 0.85f));
        }
        dl->PopClipRect();
    }

    void draw(Ctx& cx, double time){
        if(!cx.libScanned) cx.scanLibrary();
        float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;
        float filterW = px(220), detailW = px(320);
        // ---- filters
        BeginPanel("##cf", ImVec2(filterW, H));
        ImGui::SetNextItemWidth(-1);
        { FontSize f(S_); if(ImGui::InputTextWithHint("##q", "Search sounds, roles, tags…", search, sizeof search)) filter.text = search; }
        ImGui::Spacing(); Label("Roles");
        auto counts = familyCounts(cx.lib);
        static const char* fams[] = {"pitched", "percussive", "texture", "effects", "kit"};
        static const char* names[] = {"Pitched", "Percussive", "Texture", "Effects", "Kits"};
        for(int i = 0; i < 5; i++){
            bool on = filter.families.count(fams[i]) > 0;
            ImGui::PushID(i);
            FamilyMarkInline(fams[i]);
            if(ImGui::Selectable((std::string(names[i]) + "  " + std::to_string(counts[fams[i]])).c_str(), on)){
                if(on) filter.families.erase(fams[i]); else filter.families.insert(fams[i]);
            }
            ImGui::PopID();
        }
        ImGui::Spacing(); Label("Tags");
        int k = 0;
        for(auto& t : allTags(cx.lib)){
            bool on = filter.tags.count(t) > 0;
            if(k++ % 3) ImGui::SameLine();
            ImGui::PushID(t.c_str());
            if(Chip(t, on)){ if(on) filter.tags.erase(t); else filter.tags.insert(t); }
            ImGui::PopID();
        }
        ImGui::Spacing(); Label("Source");
        static const char* srcs[] = {"", "mine", "factory", "assistant"};
        static const char* srcNames[] = {"All", "My presets", "Factory", "From the assistant"};
        for(int i = 0; i < 4; i++) if(ImGui::Selectable(srcNames[i], filter.source == srcs[i])) filter.source = srcs[i];
        ImGui::Spacing();
        if(Button("Rescan", Btn::Ghost)) cx.scanLibrary();
        TextWrap(XS, col::faint, cx.lib.dir);
        for(auto& e : cx.lib.errors) TextWrap(XS, col::warn, e);
        EndPanel();

        // ---- tiles
        ImGui::SameLine();
        ImGui::BeginChild("##tiles", ImVec2(W - filterW - detailW - px(16), H), ImGuiChildFlags_None);
        float tw = px(150), th = px(130), gap = px(12);
        int perRow = std::max(1, (int)((ImGui::GetContentRegionAvail().x + gap) / (tw + gap)));
        int col_ = 0;
        for(size_t i = 0; i < cx.lib.items.size(); i++){
            const auto& it = cx.lib.items[i];
            if(!filter.match(it)) continue;
            if(col_ % perRow) ImGui::SameLine(0, gap);
            col_++;
            ImGui::PushID((int)i);
            ImVec2 p = ImGui::GetCursorScreenPos();
            bool sel = cx.sel.preset == it.path;
            if(ImGui::InvisibleButton("##tile", ImVec2(tw, th))) cx.sel.preset = it.path;
            bool hov = ImGui::IsItemHovered();
            if(hov && ImGui::IsMouseDoubleClicked(0)) addLane(cx, it);
            if(ImGui::BeginDragDropSource()){ int idx = (int)i; ImGui::SetDragDropPayload("V2LIB", &idx, sizeof idx); Text(S_, col::text, it.name); ImGui::EndDragDropSource(); }
            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), col::rgb(col::panel), px(6));
            dl->AddRect(p, ImVec2(p.x + tw, p.y + th), col::rgb(sel ? col::text : hov ? col::line2 : col::line), px(6), 0, sel ? px(2) : 1);
            Thumb(dl, ImVec2(p.x + px(6), p.y + px(6)), ImVec2(p.x + tw - px(6), p.y + px(66)), it, (float)time);
            FamilyMark(dl, ImVec2(p.x + px(14), p.y + px(80)), px(4), it.family);
            dl->PushClipRect(p, ImVec2(p.x + tw - px(4), p.y + th), true);
            DrawText(dl, S_, ImVec2(p.x + px(24), p.y + px(72)), col::rgb(col::text), it.name);
            DrawText(dl, XS, ImVec2(p.x + px(8), p.y + px(92)), col::rgb(col::muted), it.kit ? "Kit · " + std::to_string(it.count) + " instruments" : typeLabel(it.type));
            std::string tags; for(auto& t : it.tags){ if(!tags.empty()) tags += " "; tags += t; }
            DrawText(dl, XS, ImVec2(p.x + px(8), p.y + px(108)), col::rgb(col::faint), tags);
            dl->PopClipRect();
            ImGui::PopID();
        }
        if(col_ == 0) TextWrap(M, col::muted, "Nothing matches the filters.");
        ImGui::EndChild();

        // ---- detail
        ImGui::SameLine();
        BeginPanel("##cd", ImVec2(0, H));
        const LibraryItem* it = nullptr;
        for(auto& x : cx.lib.items) if(x.path == cx.sel.preset) it = &x;
        if(!it){ TextWrap(M, col::muted, "Select a tile. Double-click or drag it onto the arrangement to add it."); EndPanel(); return; }
        FamilyMarkInline(it->family, 12);
        Text(L, col::text, it->name);
        Text(S_, col::muted, (it->kit ? "Kit · " + std::to_string(it->count) + " instruments" : typeLabel(it->type)) + " · v" + std::to_string(it->version) + (it->source.empty() ? "" : " · " + it->source));
        if(!it->notes.empty()) TextWrap(S_, col::text, it->notes);
        ImGui::Spacing();
        bool auditioning = !auditionId.empty();
        if(Button(auditioning ? "Stop audition" : "Audition with scene", auditioning ? Btn::On : Btn::Primary)){
            if(auditioning) stopAudition(cx); else startAudition(cx, *it, false);
        }
        ImGui::SameLine();
        if(!auditioning && !it->kit && Button("Solo audition")) startAudition(cx, *it, true);
        if(auditioning){
            if(Button("Keep it", Btn::Normal)){ cx.history.push(auditionBefore, "Add " + it->name); cx.sel.instance = auditionId; auditionId.clear(); auditionBefore.clear(); }
            ImGui::SameLine(); Text(XS, col::muted, "Plays from the next phrase.");
        }
        ImGui::Spacing(); Label("Drop onto");
        if(Button("New lane")) addLane(cx, *it);
        const json* selIn = cx.selected();
        if(selIn && !it->kit){
            ImGui::SameLine();
            if(Button("Replace " + cx.sel.instance)){
                std::string id = cx.sel.instance; const LibraryItem item = *it;
                cx.editInstance("Replace " + id + " with " + item.name, id, [&](json& s){ return replaceWith(s, id, item); });
            }
        }
        ImGui::Spacing(); Label("Main parameters");
        if(!it->kit){
            Preset p = presetFromJson(it->data);
            for(auto& r : instanceParams(p.type, false)){
                json v = p.look.contains(r.key) ? p.look[r.key] : r.def->def;
                Text(S_, col::muted, r.def->label); ImGui::SameLine(px(140)); Text(S_, col::text, valueText(r.def, v));
            }
            if(p.seq.contains("mode")){ Text(S_, col::muted, "Rhythm"); ImGui::SameLine(px(140)); Text(S_, col::text, p.seq["mode"].is_string() ? p.seq["mode"].get<std::string>() : ""); }
            Text(S_, col::muted, "Routings"); ImGui::SameLine(px(140)); Text(S_, col::text, std::to_string(p.modulation.size()));
        } else {
            for(auto& e : it->data["instances"]){ FamilyMarkInline(familyOf(e.value("type", std::string()))); Text(S_, col::text, e.value("id", std::string()) + " · " + typeLabel(e.value("type", std::string()))); }
        }
        ImGui::Spacing();
        TextWrap(XS, col::faint, "Drag the tile onto the arrangement, or double-click to add it as a new lane.");
        EndPanel();
    }

    void addLane(Ctx& cx, const LibraryItem& it){
        const LibraryItem item = it;
        std::vector<std::string> added;
        if(cx.editScene("Add " + item.name, [&](json& s){ return dropItem(s, item, -1, &added); }) && !added.empty()) cx.sel.instance = added.front();
    }
    // adds the item to the scene without an undo step; Keep makes it one, Stop removes it
    void startAudition(Ctx& cx, const LibraryItem& it, bool solo){
        if(!cx.docOk || !cx.host->applyScene) return;
        json next = cx.doc;
        std::vector<std::string> added;
        std::string e = dropItem(next, it, -1, &added);
        if(!e.empty() || added.empty()){ cx.say(e.empty() ? "nothing to audition" : e, true); return; }
        auditionBefore = cx.docText;
        auto probs = cx.host->applyScene(next.dump(2));
        if(!probs.empty()){ cx.say(probs.front(), true); auditionBefore.clear(); return; }
        auditionId = added.front();
        cx.refresh();
        if(solo && cx.host->solo) cx.host->solo(auditionId, true);
        cx.say("Auditioning " + it.name);
    }
    void stopAudition(Ctx& cx){
        if(auditionId.empty()) return;
        if(cx.host->solo && cx.host->isSoloed && cx.host->isSoloed(auditionId)) cx.host->solo(auditionId, false);
        if(cx.host->applyScene && !auditionBefore.empty()) cx.host->applyScene(auditionBefore);
        auditionId.clear(); auditionBefore.clear();
        cx.say("Audition stopped");
    }
};

} // namespace gui
} // namespace forms2
