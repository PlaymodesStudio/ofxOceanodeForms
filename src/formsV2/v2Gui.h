//
//  v2Gui.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  The v2 GUI design system (ROADMAP 5.6, approved mockups notes/v2_mockups): stage theme,
//  family colours with shapes, level badges, type scale 11 / 12 / 14 / 18 / 24 / 36 px, 4 px grid,
//  and the one control set (knob, slider, range, toggle, menu, segmented control, step grid, fader,
//  pad). Also GuiHost, the small interface the node fills in, and Ctx, the state the screens share.
//
//  Only ImGui and forms2 headers: no openFrameworks, so it can be syntax-checked on its own.
//  Every colour / style change is pushed and popped inside the node's window (one shared context).
//

#pragma once

#include "imgui.h"
#include "imgui_internal.h"
#include "v2Engine.h"
#include "v2Perform.h"
#include "v2GuiModel.h"
#include <array>
#include <cmath>
#include <functional>
#include <string>
#include <vector>

namespace forms2 {
namespace gui {

// ---------------------------------------------------------------- the interface to the node
// Filled by the node every frame (pointers and callbacks); the GUI never owns the engine.
struct AssistantHost {
    int mode = 3;                                       // AssistMode
    bool hasKey = false, busy = false, auditioning = false, abShowingOld = false;
    std::string status, explanation, kind;
    std::vector<std::string> problems;                  // of the last answer
    std::vector<std::string> rejected;                  // answers rejected (shown, not hidden)
    std::string auditionBefore;                         // scene text before the candidate (for the diff)
    double auditionFrom = -1;                           // beat the audition started (local)
    std::function<void(int mode, const std::string& text)> send;
    std::function<void()> keep, discard, ab;
};

struct GuiHost {
    float z = 1;                                        // UI scale (Oceanode zoom)
    std::function<Engine*()> engine;                    // current scene's engine, null if none loaded
    Performer* perf = nullptr;
    std::function<std::string()> sceneText;             // the scene JSON being played
    // apply an edit: an instance changed (live, from the next phrase) or the whole scene (reload).
    // Return problems (empty = applied). The node marks the scene as edited.
    std::function<std::vector<std::string>(const Instance&, const std::string& fullText)> applyInstance;
    std::function<std::vector<std::string>(const std::string& fullText)> applyScene;

    // transport
    double nowBeat = 0;                                 // local beat of the current scene
    bool playing = false, usingTransport = false;
    float bpm = 120, fps = 60;
    std::function<void()> togglePlay, restart;
    std::function<void(float)> setBpm;

    // scene file
    std::string sceneName, sceneFile, fileStatus;
    bool dirty = false;
    std::vector<std::string> problems, warnings;
    std::function<void()> reload;
    std::function<void(const std::string& name)> saveAs;
    // Setup → Open scene: the folders of scene files (label, files) and opening one (it replaces the
    // scene playing; Perform's pads then list that file's folder)
    std::function<std::vector<std::pair<std::string, std::vector<std::string>>>()> sceneFolders;
    std::function<void(const std::string& path)> openScene;

    // live state
    std::function<float(int)> macro;                    // 0..7
    std::function<void(int, float)> setMacro;
    std::function<bool(const std::string&)> isMuted, isSoloed;
    std::function<void(const std::string&, bool)> mute, solo, lock;
    std::function<void(const std::string&)> reroll;
    bool held = false; int jumpDegree = -1000;
    std::function<void(bool)> hold;
    std::function<void(int)> jump;
    std::function<void()> clearHarmony;

    // perform: scene pads (the scene folder), snapshots
    std::vector<std::string> padNames;
    int currentPad = -1, queuedPad = -1;
    std::function<void(int)> queuePad;
    std::function<bool(int)> hasSnapshot;
    std::function<void(int)> storeSnapshot, recallSnapshot;

    AssistantHost ai;

    // sections of the classic window embedded unchanged ("Sound", "Stats", "3D", "Setlist", ...)
    std::vector<std::string> classicSections;
    std::function<void(const std::string&)> drawClassic;
    // seed files for the collection (example + template scenes)
    std::vector<std::string> seedScenes;
};

// ---------------------------------------------------------------- theme
namespace col {
inline ImU32 rgb(int hex, float a = 1){ return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, (int)(a * 255)); }
inline ImVec4 v4(int hex, float a = 1){ return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); }
constexpr int bg = 0x0b0d10, panel = 0x14171c, panel2 = 0x1b1f26, line = 0x2a2f38, line2 = 0x3a404b;
constexpr int text = 0xeef1f5, muted = 0x9aa3b0, faint = 0x6b7480;
constexpr int pitched = 0x56B4E9, perc = 0xE69F00, texture = 0x2fbf8f, fx = 0xCC79A7, mod = 0xF0E442, warn = 0xD55E00, ok = 0x2fbf8f;
inline int family(const std::string& f){
    if(f == "pitched") return pitched;
    if(f == "percussive") return perc;
    if(f == "texture") return texture;
    if(f == "effects") return fx;
    if(f == "mod") return mod;
    return text;
}
}
// type scale (px)
enum Fs { XS = 11, S_ = 12, M = 14, L = 18, XL = 24, XXL = 36 };

struct Theme {
    int colours = 0, vars = 0;
    void push(float z){
        auto C = [&](ImGuiCol c, int hex, float a = 1){ ImGui::PushStyleColor(c, col::v4(hex, a)); colours++; };
        C(ImGuiCol_WindowBg, col::bg); C(ImGuiCol_ChildBg, col::bg); C(ImGuiCol_PopupBg, col::panel2);
        C(ImGuiCol_Text, col::text); C(ImGuiCol_TextDisabled, col::faint);
        C(ImGuiCol_Border, col::line); C(ImGuiCol_Separator, col::line);
        C(ImGuiCol_FrameBg, col::panel2); C(ImGuiCol_FrameBgHovered, col::line); C(ImGuiCol_FrameBgActive, col::line2);
        C(ImGuiCol_Button, col::panel2); C(ImGuiCol_ButtonHovered, col::line); C(ImGuiCol_ButtonActive, col::line2);
        C(ImGuiCol_Header, col::line); C(ImGuiCol_HeaderHovered, col::line2); C(ImGuiCol_HeaderActive, col::line2);
        C(ImGuiCol_SliderGrab, col::text); C(ImGuiCol_SliderGrabActive, col::text); C(ImGuiCol_CheckMark, col::text);
        C(ImGuiCol_ScrollbarBg, col::bg); C(ImGuiCol_ScrollbarGrab, col::line2);
        C(ImGuiCol_Tab, col::panel); C(ImGuiCol_TabHovered, col::line); C(ImGuiCol_TabSelected, col::panel2);
        C(ImGuiCol_TableHeaderBg, col::panel); C(ImGuiCol_TableBorderLight, col::line); C(ImGuiCol_TableBorderStrong, col::line2);
        auto V = [&](ImGuiStyleVar v, float f){ ImGui::PushStyleVar(v, f); vars++; };
        auto V2 = [&](ImGuiStyleVar v, ImVec2 f){ ImGui::PushStyleVar(v, f); vars++; };
        V(ImGuiStyleVar_FrameRounding, 6 * z); V(ImGuiStyleVar_ChildRounding, 6 * z); V(ImGuiStyleVar_PopupRounding, 6 * z);
        V(ImGuiStyleVar_FrameBorderSize, 1); V(ImGuiStyleVar_ChildBorderSize, 1);
        V2(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 8 * z)); V2(ImGuiStyleVar_FramePadding, ImVec2(12 * z, 6 * z));
        V2(ImGuiStyleVar_WindowPadding, ImVec2(16 * z, 12 * z));
    }
    void pop(){ ImGui::PopStyleVar(vars); ImGui::PopStyleColor(colours); vars = colours = 0; }
};

// ---------------------------------------------------------------- text
inline float& uiScale(){ static float s = 1; return s; }
inline float px(float v){ return v * uiScale(); }
struct FontSize { FontSize(float size){ ImGui::PushFont(nullptr, size * uiScale()); } ~FontSize(){ ImGui::PopFont(); } };
inline void Text(float size, int hex, const std::string& s){ FontSize f(size); ImGui::PushStyleColor(ImGuiCol_Text, col::v4(hex)); ImGui::TextUnformatted(s.c_str()); ImGui::PopStyleColor(); }
inline void TextWrap(float size, int hex, const std::string& s){ FontSize f(size); ImGui::PushStyleColor(ImGuiCol_Text, col::v4(hex)); ImGui::TextWrapped("%s", s.c_str()); ImGui::PopStyleColor(); }
// small uppercase section label
inline void Label(const std::string& s){
    std::string u = s; for(auto& c : u) c = (char)std::toupper((unsigned char)c);
    Text(XS, col::faint, u);
}
inline void DrawText(ImDrawList* dl, float size, ImVec2 p, ImU32 c, const std::string& s){
    dl->AddText(ImGui::GetFont(), size * uiScale(), p, c, s.c_str());
}
inline ImVec2 TextSize(float size, const std::string& s){
    return ImGui::GetFont()->CalcTextSizeA(size * uiScale(), FLT_MAX, 0, s.c_str());
}
inline void Hairline(){
    ImVec2 p = ImGui::GetCursorScreenPos();
    float w = ImGui::GetContentRegionAvail().x;
    ImGui::GetWindowDrawList()->AddLine(p, ImVec2(p.x + w, p.y), col::rgb(col::line));
    ImGui::Dummy(ImVec2(w, px(1)));
}

// ---------------------------------------------------------------- shapes: family marks, level badges
// pitched square, percussive circle, texture diamond, effects triangle (colour never the only cue)
inline void FamilyMark(ImDrawList* dl, ImVec2 c, float r, const std::string& family, ImU32 override = 0){
    ImU32 k = override ? override : col::rgb(col::family(family));
    if(family == "percussive") dl->AddCircleFilled(c, r, k, 16);
    else if(family == "texture") dl->AddQuadFilled(ImVec2(c.x, c.y - r * 1.25f), ImVec2(c.x + r * 1.25f, c.y), ImVec2(c.x, c.y + r * 1.25f), ImVec2(c.x - r * 1.25f, c.y), k);
    else if(family == "effects") dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.2f), ImVec2(c.x + r * 1.2f, c.y + r), ImVec2(c.x - r * 1.2f, c.y + r), k);
    else if(family == "kit"){ dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x, c.y), col::rgb(col::pitched)); dl->AddCircleFilled(ImVec2(c.x + r * 0.5f, c.y + r * 0.5f), r * 0.5f, col::rgb(col::perc)); }
    else dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), k, r * 0.3f);
}
inline void FamilyMarkInline(const std::string& family, float size = 10){
    ImVec2 p = ImGui::GetCursorScreenPos();
    float s = px(size), h = ImGui::GetTextLineHeight();
    FamilyMark(ImGui::GetWindowDrawList(), ImVec2(p.x + s * 0.5f, p.y + h * 0.5f), s * 0.5f, family);
    ImGui::Dummy(ImVec2(s, h)); ImGui::SameLine();
}
// E white circle, P blue rounded, L yellow rounded, S purple square: letter + shape + colour
inline void LevelBadgeAt(ImDrawList* dl, ImVec2 p, float s, Level l, bool struck = false){
    int hex = l == LevelEvent ? 0xffffff : l == LevelPhrase ? col::pitched : l == LevelLayer ? col::mod : col::fx;
    const char* t = l == LevelEvent ? "E" : l == LevelPhrase ? "P" : l == LevelLayer ? "L" : "S";
    ImU32 c = col::rgb(hex, struck ? 0.25f : 1);
    ImVec2 q(p.x + s, p.y + s);
    if(l == LevelEvent) dl->AddCircleFilled(ImVec2(p.x + s * 0.5f, p.y + s * 0.5f), s * 0.5f, c, 16);
    else dl->AddRectFilled(p, q, c, l == LevelScene ? 0 : s * 0.22f);
    ImVec2 ts = ImGui::GetFont()->CalcTextSizeA(s * 0.62f, FLT_MAX, 0, t);
    dl->AddText(ImGui::GetFont(), s * 0.62f, ImVec2(p.x + (s - ts.x) * 0.5f, p.y + (s - ts.y) * 0.5f), col::rgb(col::bg), t);
    if(struck) dl->AddLine(ImVec2(p.x - 2, p.y + s * 0.5f), ImVec2(q.x + 2, p.y + s * 0.5f), col::rgb(col::text), 1.5f);
}
inline void LevelBadge(Level l, float size = 18, bool struck = false){
    ImVec2 p = ImGui::GetCursorScreenPos();
    LevelBadgeAt(ImGui::GetWindowDrawList(), p, px(size), l, struck);
    ImGui::Dummy(ImVec2(px(size), px(size)));
}

// ---------------------------------------------------------------- controls
enum class Btn { Normal, Primary, Ghost, On, Danger };
inline bool Button(const std::string& label, Btn kind = Btn::Normal, ImVec2 size = ImVec2(0, 0)){
    int n = 0;
    auto C = [&](ImGuiCol c, int hex, float a = 1){ ImGui::PushStyleColor(c, col::v4(hex, a)); n++; };
    if(kind == Btn::Primary){ C(ImGuiCol_Button, col::text); C(ImGuiCol_ButtonHovered, 0xffffff); C(ImGuiCol_ButtonActive, col::muted); C(ImGuiCol_Text, col::bg); C(ImGuiCol_Border, col::text); }
    else if(kind == Btn::Ghost){ C(ImGuiCol_Button, col::bg, 0); C(ImGuiCol_Border, col::line2); }
    else if(kind == Btn::On){ C(ImGuiCol_Border, col::text); C(ImGuiCol_Button, col::line2); }
    else if(kind == Btn::Danger){ C(ImGuiCol_Button, col::warn); C(ImGuiCol_Text, col::bg); }
    else C(ImGuiCol_Border, col::line2);
    if(size.y == 0) size.y = px(28);
    FontSize f(S_);
    bool r = ImGui::Button(label.c_str(), size);
    ImGui::PopStyleColor(n);
    return r;
}
// a pill (filters, modes)
inline bool Chip(const std::string& label, bool on, int famHex = -1){
    FontSize f(XS);
    int n = 0;
    auto C = [&](ImGuiCol c, int hex, float a = 1){ ImGui::PushStyleColor(c, col::v4(hex, a)); n++; };
    if(on){ C(ImGuiCol_Button, famHex >= 0 ? famHex : col::text); C(ImGuiCol_Text, col::bg); C(ImGuiCol_ButtonHovered, famHex >= 0 ? famHex : col::text); }
    else { C(ImGuiCol_Button, col::bg, 0); C(ImGuiCol_Text, col::muted); C(ImGuiCol_Border, col::line2); }
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, px(999));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(px(8), px(2)));
    bool r = ImGui::Button(label.c_str());
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(n);
    return r;
}
// segmented control: one of n, joined boxes; returns true when changed
inline bool Segmented(const char* id, const std::vector<std::string>& items, int& cur, float size = S_, float minW = 0){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p0 = ImGui::GetCursorScreenPos();
    float h = px(size + 12), x = p0.x;
    bool changed = false;
    std::vector<float> ws;
    for(auto& s : items) ws.push_back(std::max(px(minW), TextSize(size, s).x + px(24)));
    float total = 0; for(float w : ws) total += w;
    dl->AddRect(p0, ImVec2(p0.x + total, p0.y + h), col::rgb(col::line2), px(6));
    for(size_t i = 0; i < items.size(); i++){
        ImGui::SetCursorScreenPos(ImVec2(x, p0.y));
        ImGui::PushID((int)i);
        if(ImGui::InvisibleButton("##s", ImVec2(ws[i], h)) && cur != (int)i){ cur = (int)i; changed = true; }
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        bool on = cur == (int)i;
        ImDrawFlags fl = i == 0 ? ImDrawFlags_RoundCornersLeft : i + 1 == items.size() ? ImDrawFlags_RoundCornersRight : ImDrawFlags_RoundCornersNone;
        if(on) dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + ws[i], p0.y + h), col::rgb(col::text), px(6), fl);
        else if(hov) dl->AddRectFilled(ImVec2(x, p0.y), ImVec2(x + ws[i], p0.y + h), col::rgb(col::line), px(6), fl);
        ImVec2 ts = TextSize(size, items[i]);
        DrawText(dl, size, ImVec2(x + (ws[i] - ts.x) * 0.5f, p0.y + (h - ts.y) * 0.5f), col::rgb(on ? col::bg : col::muted), items[i]);
        x += ws[i];
    }
    ImGui::SetCursorScreenPos(p0);
    ImGui::Dummy(ImVec2(total, h));
    ImGui::PopID();
    return changed;
}
// knob: 44 px dial with a 270 degree arc, name under, value above. Drag up / down; double-click resets.
inline bool Knob(const char* id, const std::string& name, float& v, float mn, float mx, const std::string& valueText, int famHex = col::text, float def = NAN, float width = 64){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = px(width), D = px(44);
    float vh = TextSize(S_, "0").y;
    ImGui::InvisibleButton("##k", ImVec2(W, vh + D + px(4) + TextSize(XS, "A").y * 2 + px(4)));
    bool changed = false;
    if(ImGui::IsItemActive() && ImGui::GetIO().MouseDelta.y != 0){
        float range = mx - mn;
        float k = ImGui::GetIO().KeyShift ? 0.001f : 0.005f;
        v = std::clamp(v - ImGui::GetIO().MouseDelta.y * k * range, mn, mx);
        changed = true;
    }
    if(ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0) && !std::isnan(def)){ v = def; changed = true; }
    float t = mx > mn ? (v - mn) / (mx - mn) : 0;
    ImVec2 ts = TextSize(S_, valueText);
    DrawText(dl, S_, ImVec2(p.x + (W - ts.x) * 0.5f, p.y), col::rgb(col::text), valueText);
    ImVec2 c(p.x + W * 0.5f, p.y + vh + px(4) + D * 0.5f);
    float a0 = (float)(M_PI * 0.75), a1 = (float)(M_PI * 2.25);
    float r = D * 0.5f - px(3);
    dl->PathArcTo(c, r, a0, a1, 32); dl->PathStroke(col::rgb(col::line), 0, px(5));
    if(t > 0.001f){ dl->PathArcTo(c, r, a0, a0 + (a1 - a0) * t, 32); dl->PathStroke(col::rgb(famHex), 0, px(5)); }
    dl->AddCircleFilled(c, r - px(4), col::rgb(col::panel2), 24);
    float a = a0 + (a1 - a0) * t;
    dl->AddLine(ImVec2(c.x + std::cos(a) * (r - px(12)), c.y + std::sin(a) * (r - px(12))), ImVec2(c.x + std::cos(a) * (r - px(4)), c.y + std::sin(a) * (r - px(4))), col::rgb(col::text), px(2));
    ImVec2 ns = TextSize(XS, name);
    float ny = c.y + D * 0.5f + px(4);
    dl->PushClipRect(ImVec2(p.x, ny), ImVec2(p.x + W, ny + ns.y * 2), true);
    DrawText(dl, XS, ImVec2(p.x + std::max(0.f, (W - ns.x) * 0.5f), ny), col::rgb(col::muted), name);
    dl->PopClipRect();
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s: %s  (drag up / down, shift = fine%s)", name.c_str(), valueText.c_str(), std::isnan(def) ? "" : ", double-click = default");
    ImGui::PopID();
    return changed;
}
// horizontal slider: 6 px track, filled part in the family colour, 16 px white grab
inline bool Slider(const char* id, float& v, float mn, float mx, float width, int famHex = col::text){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = width > 0 ? width : ImGui::GetContentRegionAvail().x, H = px(20);
    ImGui::InvisibleButton("##sl", ImVec2(W, H));
    bool changed = false;
    if(ImGui::IsItemActive()){
        float t = std::clamp((ImGui::GetIO().MousePos.x - p.x - px(8)) / std::max(1.f, W - px(16)), 0.f, 1.f);
        float nv = mn + t * (mx - mn);
        if(nv != v){ v = nv; changed = true; }
    }
    float t = mx > mn ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0;
    float y = p.y + H * 0.5f, x0 = p.x + px(8), x1 = p.x + W - px(8), gx = x0 + (x1 - x0) * t;
    dl->AddRectFilled(ImVec2(x0, y - px(3)), ImVec2(x1, y + px(3)), col::rgb(col::line), px(3));
    dl->AddRectFilled(ImVec2(x0, y - px(3)), ImVec2(gx, y + px(3)), col::rgb(famHex), px(3));
    dl->AddCircleFilled(ImVec2(gx, y), px(8), col::rgb(col::text), 16);
    ImGui::PopID();
    return changed;
}
// range: two grabs on one track
inline bool Range(const char* id, float& lo, float& hi, float mn, float mx, float width, int famHex = col::text){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = width > 0 ? width : ImGui::GetContentRegionAvail().x, H = px(20);
    ImGui::InvisibleButton("##rg", ImVec2(W, H));
    float x0 = p.x + px(8), x1 = p.x + W - px(8);
    auto tx = [&](float v){ return x0 + (x1 - x0) * (mx > mn ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0); };
    static ImGuiID dragId = 0; static int which = 0;
    bool changed = false;
    if(ImGui::IsItemActivated()){
        float mxp = ImGui::GetIO().MousePos.x;
        which = std::fabs(mxp - tx(lo)) <= std::fabs(mxp - tx(hi)) ? 0 : 1;
        dragId = ImGui::GetItemID();
    }
    if(ImGui::IsItemActive() && dragId == ImGui::GetItemID()){
        float t = std::clamp((ImGui::GetIO().MousePos.x - x0) / std::max(1.f, x1 - x0), 0.f, 1.f);
        float nv = mn + t * (mx - mn);
        if(which == 0){ nv = std::min(nv, hi); if(nv != lo){ lo = nv; changed = true; } }
        else { nv = std::max(nv, lo); if(nv != hi){ hi = nv; changed = true; } }
    }
    float y = p.y + H * 0.5f;
    dl->AddRectFilled(ImVec2(x0, y - px(3)), ImVec2(x1, y + px(3)), col::rgb(col::line), px(3));
    dl->AddRectFilled(ImVec2(tx(lo), y - px(3)), ImVec2(tx(hi), y + px(3)), col::rgb(famHex), px(3));
    dl->AddCircleFilled(ImVec2(tx(lo), y), px(8), col::rgb(col::text), 16);
    dl->AddCircleFilled(ImVec2(tx(hi), y), px(8), col::rgb(col::text), 16);
    ImGui::PopID();
    return changed;
}
// toggle: 30 x 16 switch
inline bool Toggle(const char* id, bool& on, int famHex = col::text){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float W = px(30), H = px(16);
    float lh = ImGui::GetFrameHeight();
    p.y += (lh - H) * 0.5f;
    ImGui::InvisibleButton("##tg", ImVec2(W, lh));
    bool changed = false;
    if(ImGui::IsItemClicked()){ on = !on; changed = true; }
    dl->AddRectFilled(p, ImVec2(p.x + W, p.y + H), col::rgb(on ? famHex : col::line2), H * 0.5f);
    float kx = on ? p.x + W - H * 0.5f : p.x + H * 0.5f;
    dl->AddCircleFilled(ImVec2(kx, p.y + H * 0.5f), H * 0.5f - px(2), col::rgb(col::bg), 16);
    ImGui::PopID();
    return changed;
}
// menu: a combo with plain-language items
inline bool Menu(const char* id, int& cur, const std::vector<std::string>& items, float width = 0){
    if(items.empty()) return false;
    cur = std::clamp(cur, 0, (int)items.size() - 1);
    FontSize f(S_);
    if(width > 0) ImGui::SetNextItemWidth(width);
    bool changed = false;
    std::string prev = items[(size_t)cur] + "  \xe2\x96\xbe";
    if(ImGui::BeginCombo(id, prev.c_str(), ImGuiComboFlags_NoArrowButton)){
        for(int i = 0; i < (int)items.size(); i++)
            if(ImGui::Selectable(items[(size_t)i].c_str(), i == cur)){ cur = i; changed = true; }
        ImGui::EndCombo();
    }
    return changed;
}
// step grid: one cell per step (0 rest, 1 hit, 2 accent, 3 maybe), a readout under each, the playing step outlined
inline int StepGrid(const char* id, const std::vector<int>& steps, const std::vector<std::string>& under, int playStep, int famHex, float cell = 28){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float avail = ImGui::GetContentRegionAvail().x;
    float gap = px(4), c = std::min(px(cell), steps.empty() ? px(cell) : (avail - gap * (steps.size() - 1)) / steps.size());
    int clicked = -1;
    float uh = under.empty() ? 0 : TextSize(XS, "0").y + px(4);
    for(size_t i = 0; i < steps.size(); i++){
        ImVec2 a(p.x + i * (c + gap), p.y), b(a.x + c, a.y + c);
        ImGui::SetCursorScreenPos(a);
        ImGui::PushID((int)i);
        if(ImGui::InvisibleButton("##st", ImVec2(c, c))) clicked = (int)i;
        bool hov = ImGui::IsItemHovered();
        ImGui::PopID();
        int s = steps[i];
        bool beat = i % 4 == 0;
        dl->AddRectFilled(a, b, col::rgb(beat ? col::line : col::panel2), px(4));
        if(s == 1) dl->AddRectFilled(ImVec2(a.x + px(3), a.y + px(3)), ImVec2(b.x - px(3), b.y - px(3)), col::rgb(famHex, 0.75f), px(3));
        if(s == 2) dl->AddRectFilled(ImVec2(a.x + px(2), a.y + px(2)), ImVec2(b.x - px(2), b.y - px(2)), col::rgb(famHex), px(3));
        if(s == 3) dl->AddRect(ImVec2(a.x + px(4), a.y + px(4)), ImVec2(b.x - px(4), b.y - px(4)), col::rgb(famHex), px(3), 0, px(1.5f));
        if(hov) dl->AddRect(a, b, col::rgb(col::muted), px(4));
        if((int)i == playStep) dl->AddRect(ImVec2(a.x - px(1), a.y - px(1)), ImVec2(b.x + px(1), b.y + px(1)), col::rgb(0xffffff), px(4), 0, px(2));
        if(i < under.size() && !under[i].empty()){
            ImVec2 ts = TextSize(XS, under[i]);
            DrawText(dl, XS, ImVec2(a.x + (c - ts.x) * 0.5f, b.y + px(3)), col::rgb(col::muted), under[i]);
        }
    }
    ImGui::SetCursorScreenPos(p);
    ImGui::Dummy(ImVec2(steps.size() * (c + gap), c + uh));
    ImGui::PopID();
    return clicked;
}
// big vertical fader (perform mode)
inline bool Fader(const char* id, const std::string& name, float& v, ImVec2 size, int famHex = col::mod){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos();
    float th = TextSize(M, "0").y;
    ImVec2 tr0(p.x + size.x * 0.5f - px(14), p.y + th + px(8)), tr1(p.x + size.x * 0.5f + px(14), p.y + size.y - th - px(8));
    ImGui::InvisibleButton("##fd", size);
    bool changed = false;
    if(ImGui::IsItemActive()){
        float t = std::clamp(1 - (ImGui::GetIO().MousePos.y - tr0.y) / std::max(1.f, tr1.y - tr0.y), 0.f, 1.f);
        if(t != v){ v = t; changed = true; }
    }
    dl->AddRectFilled(tr0, tr1, col::rgb(col::panel2), px(6));
    float y = tr1.y - (tr1.y - tr0.y) * std::clamp(v, 0.f, 1.f);
    dl->AddRectFilled(ImVec2(tr0.x, y), tr1, col::rgb(famHex), px(6));
    dl->AddRectFilled(ImVec2(tr0.x - px(6), y - px(4)), ImVec2(tr1.x + px(6), y + px(4)), col::rgb(0xffffff), px(2));
    std::string val = fmtNum(v, 2);
    ImVec2 vs = TextSize(M, val), ns = TextSize(M, name);
    DrawText(dl, M, ImVec2(p.x + (size.x - vs.x) * 0.5f, p.y), col::rgb(col::text), val);
    DrawText(dl, M, ImVec2(p.x + (size.x - ns.x) * 0.5f, p.y + size.y - th), col::rgb(col::muted), name);
    ImGui::PopID();
    return changed;
}
// big pad (perform mode): state 0 idle, 1 on, 2 queued (dashed)
inline bool Pad(const char* id, const std::string& label, int state, ImVec2 size, int famHex = col::text, float fs = L){
    ImGui::PushID(id);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    ImVec2 p = ImGui::GetCursorScreenPos(), q(p.x + size.x, p.y + size.y);
    bool r = ImGui::InvisibleButton("##pad", size);
    bool hov = ImGui::IsItemHovered(), act = ImGui::IsItemActive();
    dl->AddRectFilled(p, q, col::rgb(state == 1 ? famHex : act ? col::line2 : hov ? col::line : col::panel2), px(8));
    if(state == 2){
        // dashed outline
        float d = px(8);
        auto dash = [&](ImVec2 a, ImVec2 b){
            float L = std::hypot(b.x - a.x, b.y - a.y); ImVec2 u((b.x - a.x) / L, (b.y - a.y) / L);
            for(float s = 0; s < L; s += d * 2) dl->AddLine(ImVec2(a.x + u.x * s, a.y + u.y * s), ImVec2(a.x + u.x * std::min(L, s + d), a.y + u.y * std::min(L, s + d)), col::rgb(famHex), px(2));
        };
        dash(p, ImVec2(q.x, p.y)); dash(ImVec2(q.x, p.y), q); dash(q, ImVec2(p.x, q.y)); dash(ImVec2(p.x, q.y), p);
    } else dl->AddRect(p, q, col::rgb(col::line2), px(8));
    ImVec2 ts = TextSize(fs, label);
    dl->PushClipRect(p, q, true);
    DrawText(dl, fs, ImVec2(p.x + std::max(px(8), (size.x - ts.x) * 0.5f), p.y + (size.y - ts.y) * 0.5f), col::rgb(state == 1 ? col::bg : col::text), label);
    dl->PopClipRect();
    ImGui::PopID();
    return r;
}
// a dashed rectangle (planned phrases, drop targets)
inline void DashedRect(ImDrawList* dl, ImVec2 p, ImVec2 q, ImU32 c, float dash = 6, float th = 1){
    auto line = [&](ImVec2 a, ImVec2 b){
        float L = std::hypot(b.x - a.x, b.y - a.y); if(L < 1) return;
        ImVec2 u((b.x - a.x) / L, (b.y - a.y) / L);
        for(float s = 0; s < L; s += dash * 2) dl->AddLine(ImVec2(a.x + u.x * s, a.y + u.y * s), ImVec2(a.x + u.x * std::min(L, s + dash), a.y + u.y * std::min(L, s + dash)), c, th);
    };
    line(p, ImVec2(q.x, p.y)); line(ImVec2(q.x, p.y), q); line(q, ImVec2(p.x, q.y)); line(ImVec2(p.x, q.y), p);
}
// a panel: child window with the panel colour and a 1 px line
inline bool BeginPanel(const char* id, ImVec2 size, bool scroll = true){
    ImGui::PushStyleColor(ImGuiCol_ChildBg, col::v4(col::panel));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(px(12), px(12)));
    bool r = ImGui::BeginChild(id, size, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding, scroll ? 0 : ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
    return r;
}
inline void EndPanel(){ ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor(); }

// ---------------------------------------------------------------- shared state of the screens
struct Ctx {
    GuiHost* host = nullptr;
    json doc;                         // the scene being played, parsed
    std::string docText;
    bool docOk = false;
    History history;
    Selection sel;
    Library lib;
    Mode mode = Mode::Edit;
    Screen screen = Screen::Arrangement;
    bool libScanned = false;
    std::string status;               // last edit message
    bool statusBad = false;
    ImGuiID editItem = 0;             // the control being dragged (one undo step per drag)
    // An edit's new document is staged and becomes `doc` at the start of the next frame: the views
    // hold pointers into `doc` (selected(), findInstance) while they draw, and replacing it under them
    // mid-frame left them reading freed memory (a crash on the next control).
    json staged;
    bool hasStaged = false;

    Engine* engine() const { return host && host->engine ? host->engine() : nullptr; }

    void refresh(){
        if(hasStaged || !host || !host->sceneText) return;   // frame() takes the staged edit first
        std::string t = host->sceneText();
        if(t == docText) return;
        docText = t;
        try { doc = json::parse(t); docOk = doc.is_object(); } catch(...){ docOk = false; doc = json::object(); }
        if(!sel.instance.empty() && !findInstance(doc, sel.instance)) sel.instance.clear();
    }
    // once per frame: a released control ends its undo step
    // (and the staged edit becomes the document, before any view takes pointers into it)
    void frame(){
        if(hasStaged){
            doc = std::move(staged); staged = json(); hasStaged = false;
            if(!sel.instance.empty() && !findInstance(doc, sel.instance)) sel.instance.clear();
        }
        if(ImGui::GetActiveID() == 0) editItem = 0;
    }
    void say(const std::string& s, bool bad = false){ status = s; statusBad = bad; }

    // one undo step per control drag: a new step unless this item is still being dragged
    void pushUndo(const std::string& label){
        ImGuiID id = ImGui::GetActiveID();
        bool continuing = id != 0 && id == editItem;
        if(!continuing) history.push(docText, label);
        editItem = id;
    }
    // edit an instance; applied live through the engine (from its next phrase)
    bool editInstance(const std::string& label, const std::string& id, const std::function<std::string(json&)>& fn){
        if(!docOk) return false;
        json next = hasStaged ? staged : doc;
        std::string e = fn(next);
        if(!e.empty()){ say(e, true); return false; }
        const json* in = findInstance(next, id);
        if(!in){ say("no instance '" + id + "'", true); return false; }
        std::string text = next.dump(2);
        pushUndo(label);
        std::vector<std::string> probs = host->applyInstance ? host->applyInstance(instanceFromJson(*in), text) : std::vector<std::string>{"not wired"};
        if(!probs.empty()){ history.undo.pop_back(); say(probs.front(), true); return false; }
        staged = std::move(next); hasStaged = true; docText = text;
        say(label);
        return true;
    }
    // edit the scene (structure: instances added / removed, scene routings): the scene reloads
    bool editScene(const std::string& label, const std::function<std::string(json&)>& fn){
        if(!docOk) return false;
        json next = hasStaged ? staged : doc;
        std::string e = fn(next);
        if(!e.empty()){ say(e, true); return false; }
        std::string text = next.dump(2);
        pushUndo(label);
        std::vector<std::string> probs = host->applyScene ? host->applyScene(text) : std::vector<std::string>{"not wired"};
        if(!probs.empty()){
            if(!history.undo.empty()) history.undo.pop_back();
            if(host->applyScene) host->applyScene(docText);   // back to the scene before
            say(probs.front(), true); return false;
        }
        staged = std::move(next); hasStaged = true; docText = text;
        say(label);
        return true;
    }
    // a routing edit inside an instance is live; a scene routing reloads
    bool editRouting(const std::string& label, const std::string& owner, const std::function<std::string(json&)>& fn){
        return owner.empty() ? editScene(label, fn) : editInstance(label, owner, fn);
    }
    bool undo(){
        refresh();
        std::string t;
        if(!history.stepBack(docText, t)) return false;
        if(host->applyScene) host->applyScene(t);
        say("Undo");
        return true;
    }
    bool redo(){
        refresh();
        std::string t;
        if(!history.stepForward(docText, t)) return false;
        if(host->applyScene) host->applyScene(t);
        say("Redo");
        return true;
    }
    void scanLibrary(){
        if(lib.dir.empty()) lib.dir = Library::defaultDir();
        lib.ensureDir();
        if(host) lib.seedFromFiles(host->seedScenes);
        lib.scan();
        libScanned = true;
    }
    // the selected instance json (or null)
    const json* selected() const { return sel.instance.empty() || !docOk ? nullptr : findInstance(doc, sel.instance); }
    std::string familyOfId(const std::string& id) const {
        const json* in = docOk ? findInstance(doc, id) : nullptr;
        return in ? familyOf(in->value("type", std::string())) : "";
    }
};

// value editing for any ParamDef (used by the inspector, the instrument editor, advanced lists)
// returns true and the new value in out when changed
inline bool ParamControl(const ParamDef& d, const json& cur, json& out, int famHex, float width){
    ImGui::PushID(d.key.c_str());
    bool ch = false;
    if(d.type == "bool"){
        bool b = cur.is_boolean() ? cur.get<bool>() : false;
        if(Toggle("##b", b, famHex)){ out = b; ch = true; }
    } else if(d.type == "enum"){
        int idx = 0;
        for(size_t i = 0; i < d.options.size(); i++) if(cur.is_string() && d.options[i] == cur.get<std::string>()) idx = (int)i;
        if(Menu("##e", idx, d.options, width)){ out = d.options[(size_t)idx]; ch = true; }
    } else if(d.type == "float" || d.type == "int"){
        float v = cur.is_number() ? cur.get<float>() : (d.def.is_number() ? d.def.get<float>() : (float)d.min);
        if(Slider("##f", v, (float)d.min, (float)d.max, width - px(56), famHex)){
            out = d.type == "int" ? json((int)std::lround(v)) : json(std::round(v * 1000) / 1000.0);
            ch = true;
        }
        ImGui::SameLine();
        Text(S_, col::text, valueText(&d, cur.is_number() ? cur : json(v)));
    } else if(d.type == "floatRange" || d.type == "intRange"){
        float lo = cur.is_array() && cur.size() == 2 ? cur[0].get<float>() : (float)d.min;
        float hi = cur.is_array() && cur.size() == 2 ? cur[1].get<float>() : (float)d.max;
        if(Range("##r", lo, hi, (float)d.min, (float)d.max, width - px(72), famHex)){
            if(d.type == "intRange") out = json::array({(int)std::lround(lo), (int)std::lround(hi)});
            else out = json::array({std::round(lo * 1000) / 1000.0, std::round(hi * 1000) / 1000.0});
            ch = true;
        }
        ImGui::SameLine();
        Text(S_, col::text, fmtNum(lo) + "–" + fmtNum(hi));
    } else if(d.type == "string"){
        char buf[256]; std::snprintf(buf, sizeof buf, "%s", cur.is_string() ? cur.get<std::string>().c_str() : "");
        ImGui::SetNextItemWidth(width);
        if(ImGui::InputText("##s", buf, sizeof buf, ImGuiInputTextFlags_EnterReturnsTrue)){ out = std::string(buf); ch = true; }
    } else {
        Text(S_, col::muted, cur.dump());
    }
    if(ImGui::IsItemHovered() && !d.help.empty()) ImGui::SetTooltip("%s", d.help.c_str());
    ImGui::PopID();
    return ch;
}
// a labelled row: name on the left (muted), control on the right
inline bool ParamRowUi(Ctx& cx, const std::string& id, const ParamRow& r, int famHex, float labelW, float ctrlW){
    const json* in = findInstance(cx.doc, id);
    if(!in || !r.def) return false;
    json cur = valueOf(*in, r.section, r.key), out;
    ImGui::AlignTextToFramePadding();
    Text(S_, col::muted, r.def->label);
    ImGui::SameLine(labelW);
    if(ParamControl(*r.def, cur, out, famHex, ctrlW)){
        std::string sec = r.section, key = r.key;
        return cx.editInstance(r.def->label, id, [&](json& s){ return setParam(s, id, sec, key, out); });
    }
    return false;
}

} // namespace gui
} // namespace forms2
