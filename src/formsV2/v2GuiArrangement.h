//
//  v2GuiArrangement.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The main Edit screen (mockup arrangement.html): collection on the left, instruments as lanes in
//  the centre (solid = played / playing phrases, dashed = planned upcoming variations, M / S / L / ↻
//  per lane), the inspector on the right, sequencer + upcoming variations + modulation at the bottom.
//

#pragma once

#include "v2Gui.h"

namespace forms2 {
namespace gui {

// ---------------------------------------------------------------- small shared pieces
inline std::string typeLabel(const std::string& type){ auto t = findType(type); return t ? t->label : type; }

// a drawn icon button (glyphs like ↻ ▶ are not in every ImGui font)
enum class Icon { Play, Pause, Rewind, Reroll, Plus, Check, Cross, Warn };
inline void DrawIcon(ImDrawList* dl, Icon ic, ImVec2 c, float r, ImU32 k){
    switch(ic){
    case Icon::Play: dl->AddTriangleFilled(ImVec2(c.x - r * 0.6f, c.y - r), ImVec2(c.x + r, c.y), ImVec2(c.x - r * 0.6f, c.y + r), k); break;
    case Icon::Pause: dl->AddRectFilled(ImVec2(c.x - r * 0.8f, c.y - r), ImVec2(c.x - r * 0.2f, c.y + r), k); dl->AddRectFilled(ImVec2(c.x + r * 0.2f, c.y - r), ImVec2(c.x + r * 0.8f, c.y + r), k); break;
    case Icon::Rewind:
        dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x - r, c.y), ImVec2(c.x, c.y + r), k);
        dl->AddTriangleFilled(ImVec2(c.x + r, c.y - r), ImVec2(c.x, c.y), ImVec2(c.x + r, c.y + r), k); break;
    case Icon::Reroll:
        dl->PathArcTo(c, r * 0.8f, 0.4f, 5.4f, 16); dl->PathStroke(k, 0, std::max(1.f, r * 0.25f));
        dl->AddTriangleFilled(ImVec2(c.x + r * 0.95f, c.y - r * 0.15f), ImVec2(c.x + r * 0.35f, c.y - r * 0.15f), ImVec2(c.x + r * 0.7f, c.y + r * 0.45f), k); break;
    case Icon::Plus: dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x + r, c.y), k, 2); dl->AddLine(ImVec2(c.x, c.y - r), ImVec2(c.x, c.y + r), k, 2); break;
    case Icon::Check: dl->AddLine(ImVec2(c.x - r, c.y), ImVec2(c.x - r * 0.3f, c.y + r * 0.7f), k, 2); dl->AddLine(ImVec2(c.x - r * 0.3f, c.y + r * 0.7f), ImVec2(c.x + r, c.y - r * 0.7f), k, 2); break;
    case Icon::Cross: dl->AddLine(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), k, 2); dl->AddLine(ImVec2(c.x - r, c.y + r), ImVec2(c.x + r, c.y - r), k, 2); break;
    case Icon::Warn: dl->AddTriangleFilled(ImVec2(c.x, c.y - r), ImVec2(c.x + r, c.y + r * 0.8f), ImVec2(c.x - r, c.y + r * 0.8f), k);
        dl->AddLine(ImVec2(c.x, c.y - r * 0.4f), ImVec2(c.x, c.y + r * 0.3f), col::rgb(col::bg), 2); break;
    }
}
inline bool IconButton(const char* id, Icon ic, bool on = false, float size = 28, int onHex = col::text, const char* tip = nullptr){
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos(); float s = px(size);
    bool r = ImGui::InvisibleButton("##ib", ImVec2(s, s));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), col::rgb(on ? onHex : hov ? col::line : col::panel2), px(6));
    dl->AddRect(p, ImVec2(p.x + s, p.y + s), col::rgb(on ? onHex : col::line2), px(6));
    DrawIcon(dl, ic, ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.25f, col::rgb(on ? col::bg : col::text));
    if(tip && hov) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    return r;
}
// lane toggle: a small square with a letter
inline bool LetterToggle(const char* id, const char* letter, bool on, int onHex, const char* tip, float size = 22){
    ImGui::PushID(id);
    ImVec2 p = ImGui::GetCursorScreenPos(); float s = px(size);
    bool r = ImGui::InvisibleButton("##lt", ImVec2(s, s));
    ImDrawList* dl = ImGui::GetWindowDrawList();
    bool hov = ImGui::IsItemHovered();
    dl->AddRectFilled(p, ImVec2(p.x + s, p.y + s), col::rgb(on ? onHex : hov ? col::line : col::panel2), px(4));
    dl->AddRect(p, ImVec2(p.x + s, p.y + s), col::rgb(on ? onHex : col::line2), px(4));
    ImVec2 ts = TextSize(XS, letter);
    DrawText(dl, XS, ImVec2(p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f), col::rgb(on ? col::bg : col::muted), letter);
    if(hov && tip) ImGui::SetTooltip("%s", tip);
    ImGui::PopID();
    return r;
}

// the library as a compact list with drag sources (payload: index into cx.lib.items)
inline void CollectionList(Ctx& cx, float height){
    if(!cx.libScanned) cx.scanLibrary();
    Label("Collection");
    static char q[64] = "";
    ImGui::SetNextItemWidth(-1);
    { FontSize f(S_); ImGui::InputTextWithHint("##cq", "Search…", q, sizeof q); }
    LibraryFilter flt; flt.text = q;
    ImGui::BeginChild("##clist", ImVec2(0, height - px(80)), ImGuiChildFlags_None);
    for(size_t i = 0; i < cx.lib.items.size(); i++){
        const auto& it = cx.lib.items[i];
        if(!flt.match(it)) continue;
        ImGui::PushID((int)i);
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x, h = px(40);
        bool sel = cx.sel.preset == it.path;
        if(ImGui::InvisibleButton("##ci", ImVec2(w, h))) cx.sel.preset = it.path;
        bool hov = ImGui::IsItemHovered();
        if(ImGui::BeginDragDropSource()){
            int idx = (int)i;
            ImGui::SetDragDropPayload("V2LIB", &idx, sizeof idx);
            Text(S_, col::text, it.name);
            ImGui::EndDragDropSource();
        }
        if(hov && ImGui::IsMouseDoubleClicked(0)){
            std::vector<std::string> added;
            const LibraryItem item = it;
            if(cx.editScene("Add " + item.name, [&](json& s){ return dropItem(s, item, -1, &added); }) && !added.empty()) cx.sel.instance = added.front();
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h - px(4)), col::rgb(sel ? col::line2 : hov ? col::line : col::panel2), px(6));
        FamilyMark(dl, ImVec2(p.x + px(14), p.y + (h - px(4)) * 0.5f), px(5), it.family);
        DrawText(dl, S_, ImVec2(p.x + px(28), p.y + px(4)), col::rgb(col::text), it.name);
        DrawText(dl, XS, ImVec2(p.x + px(28), p.y + px(20)), col::rgb(col::muted), it.kit ? "Kit · " + std::to_string(it.count) + " instruments" : typeLabel(it.type));
        ImGui::PopID();
    }
    if(cx.lib.items.empty()) TextWrap(S_, col::muted, "No presets yet in " + cx.lib.dir);
    ImGui::EndChild();
    TextWrap(XS, col::faint, "Drag onto a lane or the empty row below (double-click adds a lane)");
}

// accepts a library item dropped on the last item; layer -1 = the emptiest layer
inline void AcceptLibraryDrop(Ctx& cx, int layer){
    if(ImGui::BeginDragDropTarget()){
        if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2LIB")){
            int idx = *(const int*)pl->Data;
            if(idx >= 0 && idx < (int)cx.lib.items.size()){
                const LibraryItem item = cx.lib.items[(size_t)idx];
                std::vector<std::string> added;
                if(cx.editScene("Add " + item.name, [&](json& s){ return dropItem(s, item, layer, &added); }) && !added.empty()) cx.sel.instance = added.front();
            }
        }
        ImGui::EndDragDropTarget();
    }
}

// the context menu of an instance (lane header, perform tile)
inline void InstanceMenu(Ctx& cx, const std::string& id){
    if(ImGui::BeginPopupContextItem(("##im" + id).c_str())){
        Text(S_, col::muted, id);
        if(ImGui::MenuItem("Open in the instrument editor")){ cx.sel.instance = id; cx.screen = Screen::Instrument; }
        if(ImGui::MenuItem("Duplicate")){ std::string nid; if(cx.editScene("Duplicate " + id, [&](json& s){ return duplicateInstance(s, id, &nid); })) cx.sel.instance = nid; }
        if(ImGui::BeginMenu("Layer")){
            const json* in = findInstance(cx.doc, id);
            int cur = in ? in->value("layer", 0) : 0;
            for(int l = 0; l < 4; l++)
                if(ImGui::MenuItem(("Layer " + std::to_string(l + 1)).c_str(), nullptr, cur == l))
                    cx.editInstance("Move " + id + " to layer " + std::to_string(l + 1), id, [&](json& s){ return setField(s, id, "layer", l); });
            ImGui::EndMenu();
        }
        if(ImGui::MenuItem("Save as new preset")){
            if(const json* in = findInstance(cx.doc, id)){
                Instance inst = instanceFromJson(*in);
                InstanceOrigin o; bool hasO = in->contains("origin"); if(hasO) o = originFromJson((*in)["origin"]);
                std::string base = hasO && !o.preset.empty() ? o.preset : id;
                std::string path = cx.lib.savePreset(presetFromInstance(inst, hasO ? &o : nullptr, base));
                cx.say(path.empty() ? "could not save the preset" : "Saved preset " + std::filesystem::path(path).filename().string(), path.empty());
                cx.lib.scan();
            }
        }
        ImGui::Separator();
        if(ImGui::MenuItem("Remove")){ if(cx.editScene("Remove " + id, [&](json& s){ return removeInstance(s, id); }) && cx.sel.instance == id) cx.sel.instance.clear(); }
        ImGui::EndPopup();
    }
}

// ---------------------------------------------------------------- lanes
struct ArrangementView {
    float barsVisible = 8;
    std::string laneDragKey; float laneDragVal = 0;   // the lane control being dragged ("id|key"), committed on release
    // a lane header value as a % drag; one edit (one undo step) on release
    void laneValue(Ctx& cx, const std::string& id, const char* key, const std::string& section, const std::string& pkey,
                   float cur01, float scale, float width, const std::string& label, const std::string& tip){
        std::string dk = id + "|" + key;
        float v = laneDragKey == dk ? laneDragVal : cur01 * scale;
        ImGui::SetNextItemWidth(width);
        { FontSize f(S_); if(ImGui::DragFloat(key, &v, 0.5f, 0, 100, "%.0f%%", ImGuiSliderFlags_AlwaysClamp)){ laneDragKey = dk; laneDragVal = v; } }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip.c_str());
        if(ImGui::IsItemDeactivatedAfterEdit() && laneDragKey == dk){
            double nv = std::round(laneDragVal) / scale;
            laneDragKey.clear();
            if(std::fabs(nv - cur01) > 1e-9) cx.editInstance(label + " " + fmtNum(nv * scale, 0) + "%", id, [&](json& s){ return setParam(s, id, section, pkey, json(nv)); });
        } else if(laneDragKey == dk && !ImGui::IsItemActive()) laneDragKey.clear();
    }

    void drawLanes(Ctx& cx, float height){
        Engine* e = cx.engine();
        ImDrawList* dl = ImGui::GetWindowDrawList();
        float headW = px(320), laneH = px(52), rulerH = px(24);
        float valW = px(54), btnW = px(100), nameW = headW - btnW - 2 * valW - px(10);   // header: name | chance | volume | M S L ↻
        std::string moveId, moveTo;   // a lane dropped on another (order), done after the lanes
        ImVec2 p0 = ImGui::GetCursorScreenPos();
        float W = ImGui::GetContentRegionAvail().x;
        float tlX = p0.x + headW, tlW = std::max(px(100), W - headW);
        double now = cx.host->nowBeat;
        double barB = e && e->ready() ? e->meterAt(now).barBeats() : 4;
        double span = barsVisible * barB;
        double t0 = std::floor((now - span * 0.45) / barB) * barB;
        if(t0 < 0) t0 = 0;
        double t1 = t0 + span;
        auto X = [&](double b){ return (float)(tlX + (b - t0) / span * tlW); };

        // ruler: bar numbers
        Label("Instruments");
        ImGui::SameLine(headW);
        ImVec2 rp = ImGui::GetCursorScreenPos();
        int bar0 = e && e->ready() ? e->harmony().barIndex(t0) : (int)(t0 / barB);
        for(int b = 0; b < (int)barsVisible; b++){
            float x = X(t0 + b * barB);
            DrawText(dl, XS, ImVec2(x + px(4), rp.y), col::rgb(col::faint), std::to_string(bar0 + b + 1));
        }
        ImGui::Dummy(ImVec2(tlW, rulerH - px(12)));

        ImGui::BeginChild("##lanes", ImVec2(0, height - rulerH - px(56)), ImGuiChildFlags_None);
        dl = ImGui::GetWindowDrawList();
        ImVec2 lp = ImGui::GetCursorScreenPos();
        tlX = lp.x + headW;
        auto X2 = [&](double b){ return (float)(tlX + (b - t0) / span * tlW); };
        std::vector<std::string> ids = cx.docOk ? instanceIds(cx.doc) : std::vector<std::string>{};
        // order lanes by layer, then scene order
        std::stable_sort(ids.begin(), ids.end(), [&](const std::string& a, const std::string& b){
            return findInstance(cx.doc, a)->value("layer", 0) < findInstance(cx.doc, b)->value("layer", 0);
        });
        for(auto& id : ids){
            const json* in = findInstance(cx.doc, id);
            std::string type = in->value("type", std::string()), fam = familyOf(type);
            int famHex = col::family(fam);
            bool muted = cx.host->isMuted ? cx.host->isMuted(id) : false;
            bool solo = cx.host->isSoloed ? cx.host->isSoloed(id) : false;
            bool locked = e && e->ready() && e->locked(id);
            bool sel = cx.sel.instance == id;
            ImGui::PushID(id.c_str());
            ImVec2 a = ImGui::GetCursorScreenPos();
            // header
            dl->AddRectFilled(a, ImVec2(a.x + headW - px(8), a.y + laneH - px(4)), col::rgb(sel ? col::line : col::panel), px(6));
            dl->AddRectFilled(a, ImVec2(a.x + px(3), a.y + laneH - px(4)), col::rgb(famHex), px(2));
            FamilyMark(dl, ImVec2(a.x + px(16), a.y + px(14)), px(5), fam);
            ImGui::SetCursorScreenPos(a);
            if(ImGui::InvisibleButton("##hdr", ImVec2(nameW, laneH - px(4)))) cx.sel.instance = id;
            if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)){ cx.sel.instance = id; cx.screen = Screen::Instrument; }
            InstanceMenu(cx, id);
            AcceptLibraryDrop(cx, in->value("layer", 0));
            // drag a lane's name onto another lane of its layer: the order (what "earlier" effects reach)
            if(ImGui::BeginDragDropSource()){
                char buf[128]; std::snprintf(buf, sizeof buf, "%s", id.c_str());
                ImGui::SetDragDropPayload("V2LANE", buf, std::strlen(buf) + 1);
                Text(S_, col::text, "Move " + id);
                ImGui::EndDragDropSource();
            }
            if(ImGui::BeginDragDropTarget()){
                if(const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("V2LANE")){
                    std::string from((const char*)pl->Data);
                    const json* fi = findInstance(cx.doc, from);
                    if(fi && from != id && fi->value("layer", 0) == in->value("layer", 0)){ moveId = from; moveTo = id; }
                }
                ImGui::EndDragDropTarget();
            }
            dl->PushClipRect(a, ImVec2(a.x + nameW, a.y + laneH), true);
            DrawText(dl, M, ImVec2(a.x + px(28), a.y + px(5)), col::rgb(muted ? col::faint : col::text), id);
            std::string sub = typeLabel(type) + " · layer " + std::to_string(in->value("layer", 0) + 1);
            std::string fid = valueOf(*in, "seq", "followId").is_string() ? valueOf(*in, "seq", "followId").get<std::string>() : "";
            if(valueOf(*in, "seq", "mode") == "follow" && !fid.empty()) sub = typeLabel(type) + " · follows " + fid;
            DrawText(dl, XS, ImVec2(a.x + px(28), a.y + px(25)), col::rgb(col::muted), sub);
            dl->PopClipRect();
            // chance and volume: drags (double-click to type); one edit on release
            {
                json sc = valueOf(*in, "seq", "chanceScope");
                std::string scope = sc.is_string() ? sc.get<std::string>() : "note";
                std::string per = scope == "phrase" ? "per phrase (a v1 page)" : scope == "beats" ? "per " + fmtNum(numberOf(*in, "seq", "chanceBeats", 8)) + " beats" : scope == "scene" ? "once for the scene" : "per note";
                ImGui::SetCursorScreenPos(ImVec2(a.x + nameW + px(2), a.y + px(13)));
                laneValue(cx, id, "##chance", "seq", "chance", (float)numberOf(*in, "seq", "chance", 100) / 100.f, 100, valW, "Chance",
                          "Chance " + per + " that this instrument plays.\nDrag, or double-click to type. From its next phrase (Sequencer > Chance has the scope).");
                ImGui::SameLine(0, px(4));
                const ParamDef* ld = paramOf(type, "look", "level");
                bool hasLevel = ld != nullptr;
                float lv = hasLevel ? (float)numberOf(*in, "look", "level", ld->def.is_number() ? ld->def.get<double>() : 0.8) : 1.f;
                if(hasLevel) laneValue(cx, id, "##volume", "look", "level", lv, 100, valW, "Volume",
                                       type == "timefx" || type == "filterfx" ? "Effect amount (level).\nDrag, or double-click to type. From its next phrase."
                                                                               : "Volume: how bright this instrument is drawn, and so how loud it sounds (level).\nDrag, or double-click to type. From its next phrase.");
                else ImGui::Dummy(ImVec2(valW, 1));
            }
            // M S L ↻
            float bx = a.x + headW - btnW;
            ImGui::SetCursorScreenPos(ImVec2(bx, a.y + px(12)));
            if(LetterToggle("m", "M", muted, col::warn, "Mute (from the next phrase)") && cx.host->mute) cx.host->mute(id, !muted);
            ImGui::SameLine(0, px(2));
            if(LetterToggle("s", "S", solo, col::mod, "Solo") && cx.host->solo) cx.host->solo(id, !solo);
            ImGui::SameLine(0, px(2));
            if(LetterToggle("l", "L", locked, col::pitched, "Lock: the next phrases repeat this one") && cx.host->lock) cx.host->lock(id, !locked);
            ImGui::SameLine(0, px(2));
            if(IconButton("r", Icon::Reroll, false, 22, col::text, "Reroll: a new variation at the next phrase") && cx.host->reroll) cx.host->reroll(id);

            // timeline
            ImVec2 ta(tlX, a.y), tb(tlX + tlW, a.y + laneH - px(4));
            dl->AddRectFilled(ta, tb, col::rgb(col::panel), px(4));
            ImGui::SetCursorScreenPos(ta);
            if(ImGui::InvisibleButton("##tl", ImVec2(tlW, laneH - px(4)))) cx.sel.instance = id;
            AcceptLibraryDrop(cx, in->value("layer", 0));
            dl->PushClipRect(ta, tb, true);
            for(int b = 0; b <= (int)barsVisible; b++) dl->AddLine(ImVec2(X2(t0 + b * barB), ta.y), ImVec2(X2(t0 + b * barB), tb.y), col::rgb(col::line, 0.6f));
            if(e && e->ready() && e->instance(id)){
                int k = std::max(0, e->phraseIndexAt(id, t0));
                std::string var = variationText(*in);
                auto evs = e->eventsOf(id);
                for(int guard = 0; guard < 64; guard++, k++){
                    Phrase ph = e->phrase(id, k);
                    if(ph.start >= t1 || ph.length <= 0) break;
                    float x0 = X2(ph.start) + px(1), x1 = X2(ph.end()) - px(1);
                    bool upcoming = ph.start > now;
                    bool current = !upcoming && ph.end() > now;
                    ImVec2 q0(x0, ta.y + px(4)), q1(x1, tb.y - px(4));
                    ImU32 fc = col::rgb(famHex, muted ? 0.15f : upcoming ? 0.08f : current ? 0.35f : 0.22f);
                    dl->AddRectFilled(q0, q1, fc, px(4));
                    if(upcoming) DashedRect(dl, q0, q1, col::rgb(famHex, 0.8f), px(5), px(1));
                    else dl->AddRect(q0, q1, col::rgb(famHex, current ? 1.f : 0.5f), px(4));
                    // the notes of this phrase (dimmed when planned)
                    for(auto* s : evs){
                        if(s->phrase != ph.index) continue;
                        const Event& ev = s->e;
                        double h = ev.pitched ? std::clamp(0.5 + ev.degree / 28.0, 0.05, 0.95) : std::clamp(ev.height, 0.05, 0.95);
                        float ex0 = X2(ev.start), ex1 = std::max(ex0 + px(2), X2(ev.start + ev.length));
                        float ey = q1.y - (float)h * (q1.y - q0.y);
                        dl->AddRectFilled(ImVec2(ex0, ey - px(1.5f)), ImVec2(std::min(ex1, x1), ey + px(1.5f)), col::rgb(famHex, upcoming ? 0.35f : 0.95f));
                    }
                    std::string lab = fmtNum(ph.length) + " beats · " + (upcoming && (k == e->phraseIndexAt(id, now) + 1) ? "next: " + var : var);
                    if(locked && upcoming) lab = fmtNum(ph.length) + " beats · locked";
                    DrawText(dl, XS, ImVec2(x0 + px(4), q0.y + px(1)), col::rgb(upcoming ? col::muted : col::text), lab);
                }
            }
            // playhead
            float hx = X2(now);
            dl->AddLine(ImVec2(hx, ta.y), ImVec2(hx, tb.y), col::rgb(0xffffff), px(2));
            dl->PopClipRect();
            ImGui::SetCursorScreenPos(ImVec2(a.x, a.y + laneH));
            ImGui::PopID();
        }
        if(!moveId.empty()) cx.editScene("Move " + moveId, [&](json& s){ return moveInstance(s, moveId, moveTo); });
        // the empty row: drop target
        ImVec2 a = ImGui::GetCursorScreenPos();
        float w = ImGui::GetContentRegionAvail().x;
        ImGui::InvisibleButton("##droprow", ImVec2(w, laneH - px(4)));
        bool clickAdd = ImGui::IsItemClicked();
        AcceptLibraryDrop(cx, -1);
        DashedRect(dl, a, ImVec2(a.x + w, a.y + laneH - px(4)), col::rgb(col::line2), px(6), px(1));
        DrawIcon(dl, Icon::Plus, ImVec2(a.x + px(20), a.y + (laneH - px(4)) * 0.5f), px(5), col::rgb(col::muted));
        DrawText(dl, M, ImVec2(a.x + px(34), a.y + px(8)), col::rgb(col::text), "Add instrument");
        DrawText(dl, XS, ImVec2(a.x + px(34), a.y + px(28)), col::rgb(col::faint), "Drop instruments or kits here, or click to open the collection");
        if(clickAdd) cx.screen = Screen::Collection;
        ImGui::EndChild();
    }

    // ---------------------------------------------------------------- inspector
    void drawInspector(Ctx& cx){
        const json* in = cx.selected();
        Engine* e = cx.engine();
        if(!in){
            Label("Selected");
            TextWrap(S_, col::muted, "Click a lane to inspect an instrument. Right-click a lane for more (duplicate, layer, save as preset, remove).");
            ImGui::Spacing(); Hairline(); ImGui::Spacing();
            Label("Scene");
            Text(L, col::text, cx.host->sceneName.empty() ? "(no scene)" : cx.host->sceneName);
            Text(XS, col::muted, std::to_string(cx.docOk ? instanceIds(cx.doc).size() : 0) + " instruments · " + fmtNum(cx.host->bpm, 1) + " BPM");
            for(auto& p : cx.host->problems) TextWrap(XS, col::warn, p);
            for(auto& w : cx.host->warnings) TextWrap(XS, col::mod, w);
            return;
        }
        std::string id = sel(cx), type = in->value("type", std::string()), fam = familyOf(type);
        int famHex = col::family(fam);
        Label("Selected");
        FamilyMarkInline(fam, 12);
        Text(L, col::text, id);
        std::string based;
        if(in->contains("origin") && (*in)["origin"].is_object()) based = " · based on \"" + (*in)["origin"].value("preset", std::string()) + " v" + std::to_string((*in)["origin"].value("version", 1)) + "\"";
        TextWrap(XS, col::muted, typeLabel(type) + " · layer " + std::to_string(in->value("layer", 0) + 1) + based);
        ImGui::Spacing();
        // performance knobs (4 here, 6 in the editor)
        auto knobs = performanceKnobs(type, 4);
        for(size_t i = 0; i < knobs.size(); i++){
            auto& r = knobs[i];
            json cur = valueOf(*in, r.section, r.key);
            float v = cur.is_number() ? cur.get<float>() : (float)r.def->min;
            if(i) ImGui::SameLine(0, px(4));
            if(Knob(r.key.c_str(), r.def->label, v, (float)r.def->min, (float)r.def->max, valueText(r.def, json(v)), famHex, r.def->def.is_number() ? r.def->def.get<float>() : NAN)){
                json nv = r.def->type == "int" ? json((int)std::lround(v)) : json(std::round(v * 1000) / 1000.0);
                std::string key = r.key;
                cx.editInstance(r.def->label, id, [&](json& s){ return setParam(s, id, "look", key, nv); });
            }
        }
        if(Button("Open editor", Btn::Ghost)) cx.screen = Screen::Instrument;
        ImGui::Spacing(); Hairline(); ImGui::Spacing();
        Label("Phrase");
        Text(S_, col::text, "Length " + phraseLengthText(*in));
        Text(S_, col::text, "Variation " + variationText(*in));
        if(e && e->ready() && e->instance(id)){
            double now = cx.host->nowBeat;
            Phrase ph = e->phrase(id, e->phraseIndexAt(id, now));
            Text(S_, col::mod, "Next change in " + fmtNum(std::max(0.0, ph.end() - now), 1) + " beats");
        }
        ImGui::Spacing(); Hairline(); ImGui::Spacing();
        // effects on this layer: instances of the effects family on the same layer
        int layer = in->value("layer", 0);
        Label("Effects on layer " + std::to_string(layer + 1));
        int nfx = 0;
        for(auto& other : cx.doc["instances"]){
            if(familyOf(other.value("type", std::string())) != "effects" || other.value("layer", 0) != layer) continue;
            std::string oid = other.value("id", std::string());
            FamilyMarkInline("effects", 8);
            std::string eff = valueOf(other, "look", "effect").is_string() ? valueOf(other, "look", "effect").get<std::string>() : typeLabel(other.value("type", std::string()));
            if(ImGui::Selectable((oid + " · " + eff).c_str(), cx.sel.instance == oid)) cx.sel.instance = oid;
            nfx++;
        }
        if(!nfx) Text(XS, col::faint, "No effect slots on this layer.");
        if(Button("+ Add effect slot", Btn::Ghost)) cx.screen = Screen::Collection;
        ImGui::Spacing(); Hairline(); ImGui::Spacing();
        Label("Modulation");
        auto rs = routingsFromJson(in->value("routings", json::array()));
        for(size_t i = 0; i < rs.size(); i++){
            auto& r = rs[i];
            LevelBadge(r.level, 16); ImGui::SameLine();
            const ParamDef* d = destDef(cx.doc, id, r.dest);
            ImGui::AlignTextToFramePadding();
            Text(S_, col::text, destLabel(r.dest, d) + " \xe2\x86\x90 " + sourceLabel(r));
        }
        if(rs.empty()) Text(XS, col::faint, "No routings.");
        if(Button("Open matrix", Btn::Ghost)) cx.screen = Screen::Matrix;
    }
    static std::string sel(Ctx& cx){ return cx.sel.instance; }

    // ---------------------------------------------------------------- bottom: sequencer, upcoming, modulation
    void drawSequencer(Ctx& cx){
        const json* in = cx.selected();
        if(!in){ Label("Sequencer"); Text(S_, col::muted, "Select an instrument."); return; }
        std::string id = cx.sel.instance;
        int famHex = col::family(familyOf(in->value("type", std::string())));
        std::string mode = valueOf(*in, "seq", "mode").is_string() ? valueOf(*in, "seq", "mode").get<std::string>() : "euclidean";
        auto steps = stepPreview(*in);
        Label("Sequencer · " + id + " · " + mode + ", " + std::to_string(steps.size()) + " steps");
        int play = -1;
        if(Engine* e = cx.engine()){
            if(e->ready()){
                double now = cx.host->nowBeat;
                double barB = e->meterAt(now).barBeats();
                double ph = now / barB - std::floor(now / barB);   // steps are per bar
                play = std::clamp((int)(ph * steps.size()), 0, (int)steps.size() - 1);
            }
        }
        int c = StepGrid("grid", steps, {}, play, famHex, 24);
        if(c >= 0) cx.editInstance("Step " + std::to_string(c + 1), id, [&](json& s){ return toggleStep(s, id, c); });
        Text(XS, col::faint, "Click a step: hit, accent, rest (switches the rhythm to a typed pattern).");
        auto chip = [&](const std::string& t){ Chip(t, false); ImGui::SameLine(); };
        chip("Chance " + fmtNum(numberOf(*in, "seq", "chance", 100), 0) + "%");
        if(numberOf(*in, "seq", "rollChance", 0) > 0) chip("Roll " + fmtNum(numberOf(*in, "seq", "rollChance", 0), 0) + "% · " + valueOf(*in, "seq", "rollShape").get<std::string>());
        double ramp = numberOf(*in, "seq", "densityRamp", 0);
        if(ramp != 0) chip(ramp > 0 ? "Ramp thins start" : "Ramp thins end");
        double sw = in->value("swing", -1.0);
        chip(sw < 0 ? "Swing: section" : "Swing " + fmtNum(sw));
        ImGui::NewLine();
    }
    void drawUpcoming(Ctx& cx){
        Label("Upcoming variations");
        Engine* e = cx.engine();
        if(!e || !e->ready()){ Text(S_, col::muted, "No scene."); return; }
        double now = cx.host->nowBeat;
        struct U { double at; std::string text; int hex; };
        std::vector<U> us;
        for(auto& id : instanceIds(cx.doc)){
            const json* in = findInstance(cx.doc, id);
            if(!e->instance(id)) continue;
            Phrase cur = e->phrase(id, e->phraseIndexAt(id, now));
            Phrase nx = e->phrase(id, cur.index + 1);
            std::string what = e->locked(id) ? "locked (repeats)" : variationText(*in);
            int bar = e->harmony().barIndex(nx.start) + 1;
            double left = nx.start - now;
            std::string when = left < 4 ? "in " + fmtNum(left, 1) + " beats" : "at bar " + std::to_string(bar);
            us.push_back({nx.start, id + "  " + what + " " + when + " (length " + fmtNum(nx.length) + ")", col::family(familyOf(in->value("type", std::string())))});
        }
        std::sort(us.begin(), us.end(), [](const U& a, const U& b){ return a.at < b.at; });
        for(auto& u : us){
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(p.x, p.y + px(3)), ImVec2(p.x + px(3), p.y + px(15)), col::rgb(u.hex));
            ImGui::Dummy(ImVec2(px(6), px(1))); ImGui::SameLine();
            Text(S_, col::text, u.text);
        }
        // the next harmony section
        const auto& sec = e->harmony().sectionDef(now);
        auto m = e->meterAt(now);
        Text(XS, col::muted, "Section " + (sec.name.empty() ? std::string("-") : sec.name) + " · " + std::to_string(m.num) + "/" + std::to_string(m.den));
    }
};

} // namespace gui
} // namespace forms2
