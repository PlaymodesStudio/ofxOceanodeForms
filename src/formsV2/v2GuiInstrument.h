//
//  v2GuiInstrument.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The instrument editor (mockup instrument.html): Main page = 6 performance knobs + look summary,
//  Advanced one click away (segmented control), the sequencer as a tabbed card (Rhythm / Pitch /
//  Notes / Chance / Variation) with a step grid and degree readouts, the look preview with
//  Audition / Reroll / Lock, harmony follow as five big choices with a plain sentence, and phrase.
//

#pragma once

#include "v2GuiArrangement.h"

namespace forms2 {
namespace gui {

struct InstrumentView {
    int page = 0;              // 0 main, 1 advanced
    int seqTab = 0;
    char presetName[96] = "";
    bool showCompare = false;

    void draw(Ctx& cx){
        const json* in = cx.selected();
        if(!in){
            Text(XL, col::text, "Instrument editor");
            TextWrap(M, col::muted, "Select an instrument in the arrangement (click a lane) to edit it.");
            for(auto& id : cx.docOk ? instanceIds(cx.doc) : std::vector<std::string>{}){
                FamilyMarkInline(cx.familyOfId(id));
                if(ImGui::Selectable(id.c_str())) cx.sel.instance = id;
            }
            return;
        }
        std::string id = cx.sel.instance, type = in->value("type", std::string()), fam = familyOf(type);
        int famHex = col::family(fam);
        float W = ImGui::GetContentRegionAvail().x, H = ImGui::GetContentRegionAvail().y;

        // ---- header
        FamilyMarkInline(fam, 14);
        Text(XL, col::text, id);
        std::string origin = in->contains("origin") ? (*in)["origin"].value("preset", std::string()) + " v" + std::to_string((*in)["origin"].value("version", 1)) + " · " : std::string();
        int changes = changesFromPreset(*in);
        Text(S_, col::muted, origin + fam + " · layer " + std::to_string(in->value("layer", 0) + 1) + (in->contains("origin") ? " · " + std::to_string(changes) + " changes from preset" : ""));
        auto adv = instanceParams(type, true);
        if(Segmented("page", {"Main", "Advanced (" + std::to_string(adv.size()) + ")"}, page)){}
        ImGui::SameLine(0, px(16));
        if(in->contains("origin")){ if(Button("Compare with preset", showCompare ? Btn::On : Btn::Ghost)) showCompare = !showCompare; ImGui::SameLine(); }
        if(Button("Save as preset…", Btn::Ghost)){
            std::snprintf(presetName, sizeof presetName, "%s", (in->contains("origin") ? (*in)["origin"].value("preset", id) : id).c_str());
            ImGui::OpenPopup("##savepreset");
        }
        if(ImGui::BeginPopup("##savepreset")){
            Label("Save as a new preset");
            ImGui::SetNextItemWidth(px(240));
            ImGui::InputText("##pn", presetName, sizeof presetName);
            if(Button("Save", Btn::Primary)){
                Instance inst = instanceFromJson(*in);
                InstanceOrigin o; bool hasO = in->contains("origin"); if(hasO) o = originFromJson((*in)["origin"]);
                std::string path = cx.lib.savePreset(presetFromInstance(inst, hasO ? &o : nullptr, presetName));
                cx.say(path.empty() ? "could not save the preset" : "Saved " + std::filesystem::path(path).filename().string(), path.empty());
                cx.lib.scan();
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
        if(showCompare){
            auto ov = (*in)["origin"].value("overrides", std::vector<std::string>{});
            if(ov.empty()) Text(S_, col::muted, "Same as the preset.");
            for(auto& k : ov){
                size_t dot = k.find('.');
                std::string sec = dot == std::string::npos ? k : k.substr(0, dot), key = dot == std::string::npos ? "" : k.substr(dot + 1);
                const ParamDef* d = paramOf(type, sec, key);
                Text(S_, col::text, "changed: " + (d ? d->label : k) + " = " + (key.empty() ? std::string("(list)") : valueText(d, valueOf(*in, sec, key))));
            }
        }
        ImGui::Spacing();

        float leftW = W * 0.62f;
        ImGui::BeginChild("##il", ImVec2(leftW, H - ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y), ImGuiChildFlags_None);
        // ---- main / advanced
        BeginPanel("##knobs", ImVec2(0, page == 0 ? px(170) : px(300)));
        if(page == 0){
            Label("Performance knobs");
            auto knobs = performanceKnobs(type, 6);
            for(size_t i = 0; i < knobs.size(); i++){
                auto& r = knobs[i];
                json cur = valueOf(*in, r.section, r.key);
                float v = cur.is_number() ? cur.get<float>() : (float)r.def->min;
                if(i) ImGui::SameLine(0, px(12));
                if(Knob(r.key.c_str(), r.def->label, v, (float)r.def->min, (float)r.def->max, valueText(r.def, json(v)), famHex, r.def->def.is_number() ? r.def->def.get<float>() : NAN, 72)){
                    json nv = r.def->type == "int" ? json((int)std::lround(v)) : json(std::round(v * 1000) / 1000.0);
                    std::string key = r.key;
                    cx.editInstance(r.def->label, id, [&](json& s){ return setParam(s, id, "look", key, nv); });
                }
            }
            std::string summary;
            for(auto& r : instanceParams(type, false)){
                if(r.def->type != "enum" && r.def->type != "bool") continue;
                if(!summary.empty()) summary += " · ";
                summary += r.def->label + ": " + valueText(r.def, valueOf(*in, r.section, r.key));
            }
            Text(S_, col::muted, summary.empty() ? "" : summary);
            ImGui::SameLine();
            if(Chip("Show advanced \xe2\x96\xb8", false)) page = 1;
        } else {
            std::string group;
            for(auto& r : adv){
                if(r.def->group != group){ group = r.def->group; ImGui::Spacing(); Label(group.empty() ? "other" : group); }
                ParamRowUi(cx, id, r, famHex, px(150), ImGui::GetContentRegionAvail().x - px(160));
            }
        }
        EndPanel();
        ImGui::Spacing();

        // ---- sequencer card
        BeginPanel("##seq", ImVec2(0, 0));
        static const char* tabs[] = {"Rhythm", "Pitch", "Notes", "Chance", "Variation"};
        static const char* groups[] = {"rhythm", "pitch", "notes", "chance", "variation"};
        std::vector<std::string> tv(tabs, tabs + 5);
        Segmented("seqtab", tv, seqTab, M);
        ImGui::Spacing();
        if(seqTab == 0) drawRhythm(cx, *in, id, famHex);
        else {
            for(auto& r : seqParams(groups[seqTab])) ParamRowUi(cx, id, r, famHex, px(150), ImGui::GetContentRegionAvail().x - px(160));
            if(seqTab == 1 && fam == "pitched") for(auto& r : seqParams("mass")) ParamRowUi(cx, id, r, famHex, px(150), ImGui::GetContentRegionAvail().x - px(160));
        }
        EndPanel();
        ImGui::EndChild();

        // ---- right column
        ImGui::SameLine();
        ImGui::BeginChild("##ir", ImVec2(0, H - ImGui::GetCursorPosY() + ImGui::GetStyle().WindowPadding.y), ImGuiChildFlags_None);
        BeginPanel("##prev", ImVec2(0, px(210)), false);
        Label("Look preview · the planned phrases around the playhead");
        drawPreview(cx, id, famHex, ImVec2(ImGui::GetContentRegionAvail().x, px(110)));
        bool solo = cx.host->isSoloed && cx.host->isSoloed(id);
        Engine* e = cx.engine();
        bool locked = e && e->ready() && e->locked(id);
        if(Button(solo ? "Stop audition" : "Audition", solo ? Btn::On : Btn::Normal) && cx.host->solo) cx.host->solo(id, !solo);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Solo this instrument (the others go quiet).");
        ImGui::SameLine();
        if(Button("Reroll") && cx.host->reroll) cx.host->reroll(id);
        ImGui::SameLine();
        if(Button("Lock", locked ? Btn::On : Btn::Normal) && cx.host->lock) cx.host->lock(id, !locked);
        EndPanel();
        ImGui::Spacing();

        BeginPanel("##harm", ImVec2(0, px(170)), false);
        Label("Follows the harmony");
        std::string follow = in->value("follow", std::string("chord"));
        std::string psrc = valueOf(*in, "seq", "pitchSource").is_string() ? valueOf(*in, "seq", "pitchSource").get<std::string>() : "scale";
        int cur = follow == "scale" ? 1 : follow == "pedal" ? 3 : follow == "off" ? 4 : psrc == "root" ? 2 : 0;
        if(Segmented("follow", {"Chord", "Scale", "Root", "Pedal", "Off"}, cur, M, 56)){
            static const char* f[] = {"chord", "scale", "chord", "pedal", "off"};
            int c = cur;
            cx.editInstance("Follow", id, [&](json& s){
                std::string e1 = setField(s, id, "follow", f[c]);
                if(!e1.empty()) return e1;
                if(c == 2) return setParam(s, id, "seq", "pitchSource", "root");
                if(c == 0 && psrc == "root") return setParam(s, id, "seq", "pitchSource", "chord");
                return std::string();
            });
        }
        std::string sentence = cur == 2 ? followSentence("root") : followSentence(follow);
        if(e && e->ready() && cur == 0){
            auto ha = e->harmonyAt(cx.host->nowBeat);
            sentence = "Plays the tones of the current chord (now degree " + std::to_string(ha.root + 1) + ", " + colourName(ha.colour) + "). Changes when the chord changes.";
        }
        TextWrap(S_, col::muted, sentence);
        if(cur == 3){
            int pd = in->value("pedalDegree", 0);
            ImGui::SetNextItemWidth(px(120));
            if(ImGui::SliderInt("Pedal degree", &pd, -7, 14)) cx.editInstance("Pedal degree", id, [&](json& s){ return setField(s, id, "pedalDegree", pd); });
        }
        EndPanel();
        ImGui::Spacing();

        BeginPanel("##phrase", ImVec2(0, 0));
        Label("Phrase");
        drawPhrase(cx, *in, id, famHex);
        EndPanel();
        ImGui::EndChild();
    }

    void drawRhythm(Ctx& cx, const json& in, const std::string& id, int famHex){
        static const char* modes[] = {"euclidean", "probability", "pattern", "follow"};
        std::string mode = valueOf(in, "seq", "mode").is_string() ? valueOf(in, "seq", "mode").get<std::string>() : "euclidean";
        int m = 0; for(int i = 0; i < 4; i++) if(mode == modes[i]) m = i;
        bool other = mode == "external" || mode == "position";
        if(Segmented("rmode", {"Euclidean", "Probability", "Typed pattern", "Follow"}, m) )
            cx.editInstance("Rhythm mode", id, [&](json& s){ return setParam(s, id, "seq", "mode", modes[m]); });
        ImGui::SameLine(0, px(16));
        auto steps = stepPreview(in);
        int spb = (int)numberOf(in, "seq", "subdivision", 0);
        ImGui::AlignTextToFramePadding();
        Text(S_, col::muted, std::to_string(steps.size()) + " steps" + (spb > 0 ? " · " + std::to_string(spb) + " per beat" : "") + (other ? " · mode: " + mode : ""));
        // degrees under each step: from the events of the current phrase
        std::vector<std::string> under(steps.size());
        Engine* e = cx.engine();
        int play = -1;
        if(e && e->ready() && e->instance(id)){
            double now = cx.host->nowBeat;
            double barB = e->meterAt(now).barBeats();
            double bar0 = std::floor(now / barB) * barB;
            for(auto* s : e->eventsOf(id)){
                double t = s->e.start - bar0;
                if(t < 0 || t >= barB || !s->e.pitched) continue;
                int k = std::clamp((int)(t / barB * steps.size()), 0, (int)steps.size() - 1);
                if(under[(size_t)k].empty()) under[(size_t)k] = std::to_string(s->e.degree + 1);
            }
            play = std::clamp((int)((now - bar0) / barB * steps.size()), 0, (int)steps.size() - 1);
        }
        ImGui::Spacing();
        int c = StepGrid("ig", steps, under, play, famHex, 30);
        if(c >= 0) cx.editInstance("Step " + std::to_string(c + 1), id, [&](json& s){ return toggleStep(s, id, c); });
        ImGui::Spacing();
        float lw = px(150), cw = ImGui::GetContentRegionAvail().x - px(160);
        if(m == 0){ for(auto k : {"hits", "rotation", "steps"}) ParamRowUi(cx, id, {"seq", k, sequencerParam(k)}, famHex, lw, cw); }
        if(m == 1){ for(auto k : {"density", "densityRamp", "noRepeat"}) ParamRowUi(cx, id, {"seq", k, sequencerParam(k)}, famHex, lw, cw); }
        if(m == 2){ for(auto k : {"pattern", "patternLength"}) ParamRowUi(cx, id, {"seq", k, sequencerParam(k)}, famHex, lw, cw); }
        if(m == 3){
            // follow: pick another instance
            std::vector<std::string> ids; int fi = 0;
            std::string fid = valueOf(in, "seq", "followId").is_string() ? valueOf(in, "seq", "followId").get<std::string>() : "";
            for(auto& o : instanceIds(cx.doc)) if(o != id){ if(o == fid) fi = (int)ids.size(); ids.push_back(o); }
            ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Follows"); ImGui::SameLine(lw);
            if(!ids.empty() && Menu("##fid", fi, ids, px(200))) cx.editInstance("Follow", id, [&](json& s){ return setParam(s, id, "seq", "followId", ids[(size_t)fi]); });
            ParamRowUi(cx, id, {"seq", "followMode", sequencerParam("followMode")}, famHex, lw, cw);
        }
        for(auto k : {"chance", "swing", "rollChance", "rollShape"}) ParamRowUi(cx, id, {"seq", k, sequencerParam(k)}, famHex, lw, cw);
        if(ImGui::TreeNode("More rhythm settings")){
            for(auto& r : seqParams("rhythm")) ParamRowUi(cx, id, r, famHex, lw, cw);
            ImGui::TreePop();
        }
    }

    // phrase length (fixed or random), variations, restart at sections
    void drawPhrase(Ctx& cx, const json& in, const std::string& id, int famHex){
        json L = in.contains("phrase") && in["phrase"].is_object() && in["phrase"].contains("length") ? in["phrase"]["length"] : json(16);
        bool random = L.is_array() && L.size() == 2;
        float lw = px(130), cw = ImGui::GetContentRegionAvail().x - px(140);
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Length"); ImGui::SameLine(lw);
        if(Toggle("rnd", random, famHex)){
            json nv = random ? json::array({4, 16}) : json(16);
            cx.editInstance("Phrase length", id, [&](json& s){ return setParam(s, id, "phrase", "length", nv); });
        }
        ImGui::SameLine(); Text(XS, col::muted, random ? "random" : "fixed");
        ImGui::Dummy(ImVec2(lw - px(8), 1)); ImGui::SameLine(lw);
        if(random){
            float lo = L[0].get<float>(), hi = L[1].get<float>();
            if(Range("len", lo, hi, 1, 64, cw - px(80), famHex)){
                json nv = json::array({(int)std::lround(lo), (int)std::lround(hi)});
                cx.editInstance("Phrase length", id, [&](json& s){ return setParam(s, id, "phrase", "length", nv); });
            }
            ImGui::SameLine(); Text(S_, col::text, fmtNum(lo, 0) + "–" + fmtNum(hi, 0) + " beats");
        } else {
            float v = L.is_number() ? L.get<float>() : 16;
            if(Slider("len", v, 1, 64, cw - px(80), famHex)){
                json nv = (int)std::lround(v);
                cx.editInstance("Phrase length", id, [&](json& s){ return setParam(s, id, "phrase", "length", nv); });
            }
            ImGui::SameLine(); Text(S_, col::text, fmtNum(v, 0) + " beats");
        }
        for(auto k : {"varRhythm", "varPitch", "mutateAmount"}) ParamRowUi(cx, id, {"seq", k, sequencerParam(k)}, famHex, lw, cw);
        bool rs = in.contains("phrase") && in["phrase"].is_object() && in["phrase"].value("restartAtSections", false);
        ImGui::AlignTextToFramePadding(); Text(S_, col::muted, "Restart at sections"); ImGui::SameLine(lw);
        if(Toggle("rs", rs, famHex)) cx.editInstance("Restart at sections", id, [&](json& s){ return setParam(s, id, "phrase", "restartAtSections", rs); });
    }

    // a small piano roll of the instance around the playhead (current phrase solid, planned dimmed)
    void drawPreview(Ctx& cx, const std::string& id, int famHex, ImVec2 size){
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + size.x, p.y + size.y);
        dl->AddRectFilled(p, q, col::rgb(col::bg), px(4));
        ImGui::Dummy(size);
        Engine* e = cx.engine();
        if(!e || !e->ready() || !e->instance(id)) return;
        double now = cx.host->nowBeat, span = 16;
        double t0 = now - span * 0.25;
        auto X = [&](double b){ return (float)(p.x + (b - t0) / span * size.x); };
        dl->PushClipRect(p, q, true);
        auto evs = e->eventsOf(id);
        int lo = 1000, hi = -1000;
        for(auto* s : evs) if(s->e.pitched){ lo = std::min(lo, s->e.degree); hi = std::max(hi, s->e.degree); }
        if(lo > hi){ lo = 0; hi = 7; }
        hi = std::max(hi, lo + 7);
        for(auto* s : evs){
            const Event& ev = s->e;
            if(ev.start + ev.length < t0 || ev.start > t0 + span) continue;
            double h = ev.pitched ? (ev.degree - lo + 0.5) / (hi - lo + 1) : ev.height;
            float y = q.y - (float)h * size.y;
            float th = std::max(px(2), size.y / (hi - lo + 1) * 0.6f);
            bool future = ev.start > now;
            dl->AddRectFilled(ImVec2(X(ev.start), y - th * 0.5f), ImVec2(std::max(X(ev.start) + px(2), X(ev.start + ev.length)), y + th * 0.5f), col::rgb(famHex, (future ? 0.45f : 1.f) * (float)std::clamp(ev.level, 0.25, 1.0)), px(2));
        }
        dl->AddLine(ImVec2(X(now), p.y), ImVec2(X(now), q.y), col::rgb(0xffffff), px(2));
        dl->PopClipRect();
    }
};

} // namespace gui
} // namespace forms2
