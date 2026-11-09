//
//  formsTheme.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport of the v2 design system)
//
//  The approved mockups' design system (notes/v2_mockups/index.html) for the Forms Box window:
//  near-black stage colours, one type scale (11/12/14/18/24/36), a 4 px grid, one look for every
//  control, and the family colours (Okabe-Ito) each paired with a shape. Pushed around the
//  window only (the other Oceanode windows keep their style). "Classic look" turns it off.
//  GUI only: nothing here reaches a request or an image.
//

#pragma once

#include "imgui.h"
#include <string>

namespace forms {
namespace theme {

inline bool& enabled(){ static bool on = true; return on; }

inline ImVec4 rgb(int hex, float a = 1){ return ImVec4(((hex >> 16) & 255) / 255.f, ((hex >> 8) & 255) / 255.f, (hex & 255) / 255.f, a); }
inline ImU32 u32(int hex, int a = 255){ return IM_COL32((hex >> 16) & 255, (hex >> 8) & 255, hex & 255, a); }

// tokens (index.html :root)
enum : int {
    BG = 0x0b0d10, PANEL = 0x14171c, PANEL2 = 0x1b1f26, LINE = 0x2a2f38, LINE2 = 0x3a404b,
    TEXT = 0xeef1f5, MUTED = 0x9aa3b0, FAINT = 0x6b7480,
    PITCHED = 0x56B4E9, PERC = 0xE69F00, TEXTURE = 0x2fbf8f, FX = 0xCC79A7, MOD = 0xF0E442,
    PLAY = 0xffffff, WARN = 0xD55E00
};
// type scale, relative to the 14 px body (window font scale)
inline float fsXS(){ return 11.f / 14.f; }
inline float fsS(){ return 12.f / 14.f; }
inline float fsL(){ return 18.f / 14.f; }
inline float fsXL(){ return 24.f / 14.f; }
inline float fsXXL(){ return 36.f / 14.f; }

// Families: v1's generators and legacy method families onto the five roles
enum Family { Pitched, Percussive, Texture, Effects, Modulation, None };
inline Family familyOfGen(const std::string& g){
    if(g == "legacy/g") return Texture;   // geometry: the geometric textures
    if(g == "masses" || g == "sweep" || g == "legacy/h") return Pitched;
    if(g == "pulse" || g == "hits" || g == "legacy/r") return Percussive;
    if(g == "shapes" || g == "legacy/t") return Texture;
    if(g == "effects" || g == "legacy/fx") return Effects;
    if(g == "stack") return Modulation;
    return None;
}
inline int familyHex(Family f){
    switch(f){ case Pitched: return PITCHED; case Percussive: return PERC; case Texture: return TEXTURE; case Effects: return FX; case Modulation: return MOD; default: return MUTED; }
}
inline const char* familyName(Family f){
    switch(f){ case Pitched: return "pitched"; case Percussive: return "percussive"; case Texture: return "texture"; case Effects: return "effects"; case Modulation: return "layered"; default: return ""; }
}
// The family's shape (square, circle, diamond, triangle; modulation a bar), so colour is never the only cue
inline void drawShape(ImDrawList* dl, ImVec2 c, float r, Family f, ImU32 col){
    switch(f){
        case Pitched: dl->AddRectFilled(ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r), col); break;
        case Percussive: dl->AddCircleFilled(c, r, col); break;
        case Texture: dl->AddQuadFilled(ImVec2(c.x, c.y - r * 1.2f), ImVec2(c.x + r * 1.2f, c.y), ImVec2(c.x, c.y + r * 1.2f), ImVec2(c.x - r * 1.2f, c.y), col); break;
        case Effects: dl->AddTriangleFilled(ImVec2(c.x, c.y - r * 1.2f), ImVec2(c.x + r * 1.15f, c.y + r), ImVec2(c.x - r * 1.15f, c.y + r), col); break;
        case Modulation: dl->AddRectFilled(ImVec2(c.x - r * 1.2f, c.y - r * 0.45f), ImVec2(c.x + r * 1.2f, c.y + r * 0.45f), col); break;
        default: dl->AddCircle(c, r, col); break;
    }
}
// A family badge inline with text (theme on); nothing in the classic look
inline void badge(const std::string& gen){
    if(!enabled()) return;
    Family f = familyOfGen(gen);
    float h = ImGui::GetTextLineHeight();
    ImVec2 p = ImGui::GetCursorScreenPos();
    drawShape(ImGui::GetWindowDrawList(), ImVec2(p.x + h * 0.4f, p.y + h * 0.5f), h * 0.26f, f, u32(familyHex(f)));
    ImGui::Dummy(ImVec2(h * 0.8f, h));
    if(ImGui::IsItemHovered() && f != None) ImGui::SetTooltip("%s", familyName(f));
    ImGui::SameLine(0, 4);
}

// Section heading in the type scale (muted, small caps feel)
inline void heading(const char* t){
    if(!enabled()){ ImGui::SeparatorText(t); return; }
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, rgb(MUTED));
    ImGui::SetWindowFontScale(fsS());
    ImGui::TextUnformatted(t);
    ImGui::SetWindowFontScale(1);
    ImGui::PopStyleColor();
    ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(ImVec2(p.x, p.y), ImVec2(p.x + ImGui::GetContentRegionAvail().x, p.y), u32(LINE));
    ImGui::Dummy(ImVec2(0, 4));
}

// Push / pop around the window (before Begin, after End)
struct Scope {
    int colours = 0, vars = 0;
    void push(float z = 1){
        if(!enabled()) return;
        auto C = [&](ImGuiCol c, ImVec4 v){ ImGui::PushStyleColor(c, v); colours++; };
        auto V = [&](ImGuiStyleVar s, float f){ ImGui::PushStyleVar(s, f); vars++; };
        auto V2 = [&](ImGuiStyleVar s, ImVec2 v){ ImGui::PushStyleVar(s, v); vars++; };
        C(ImGuiCol_WindowBg, rgb(BG));
        C(ImGuiCol_ChildBg, rgb(BG, 0));
        C(ImGuiCol_PopupBg, rgb(PANEL));
        C(ImGuiCol_Border, rgb(LINE));
        C(ImGuiCol_Text, rgb(TEXT));
        C(ImGuiCol_TextDisabled, rgb(MUTED));
        C(ImGuiCol_FrameBg, rgb(PANEL2));
        C(ImGuiCol_FrameBgHovered, rgb(LINE));
        C(ImGuiCol_FrameBgActive, rgb(LINE2));
        C(ImGuiCol_TitleBg, rgb(PANEL));
        C(ImGuiCol_TitleBgActive, rgb(PANEL2));
        C(ImGuiCol_Button, rgb(PANEL2));
        C(ImGuiCol_ButtonHovered, rgb(LINE));
        C(ImGuiCol_ButtonActive, rgb(LINE2));
        C(ImGuiCol_Header, rgb(PANEL2));
        C(ImGuiCol_HeaderHovered, rgb(LINE));
        C(ImGuiCol_HeaderActive, rgb(LINE2));
        C(ImGuiCol_Tab, rgb(PANEL));
        C(ImGuiCol_TabHovered, rgb(LINE));
        C(ImGuiCol_TabSelected, rgb(PANEL2));
        C(ImGuiCol_TabSelectedOverline, rgb(PLAY));
        C(ImGuiCol_TabDimmed, rgb(PANEL));
        C(ImGuiCol_TabDimmedSelected, rgb(PANEL2));
        C(ImGuiCol_SliderGrab, rgb(MUTED));
        C(ImGuiCol_SliderGrabActive, rgb(PLAY));
        C(ImGuiCol_CheckMark, rgb(PLAY));
        C(ImGuiCol_Separator, rgb(LINE));
        C(ImGuiCol_TableHeaderBg, rgb(PANEL));
        C(ImGuiCol_TableBorderStrong, rgb(LINE2));
        C(ImGuiCol_TableBorderLight, rgb(LINE));
        C(ImGuiCol_TableRowBg, rgb(BG, 0));
        C(ImGuiCol_TableRowBgAlt, rgb(PANEL, 0.6f));
        C(ImGuiCol_PlotHistogram, rgb(PITCHED));
        C(ImGuiCol_ScrollbarBg, rgb(BG));
        C(ImGuiCol_ScrollbarGrab, rgb(LINE));
        // 4 px grid, one radius (6 px) for every control
        V(ImGuiStyleVar_FrameRounding, 6 * z);
        V(ImGuiStyleVar_ChildRounding, 6 * z);
        V(ImGuiStyleVar_PopupRounding, 6 * z);
        V(ImGuiStyleVar_GrabRounding, 6 * z);
        V(ImGuiStyleVar_TabRounding, 6 * z);
        V(ImGuiStyleVar_WindowRounding, 6 * z);
        V2(ImGuiStyleVar_WindowPadding, ImVec2(16 * z, 16 * z));
        V2(ImGuiStyleVar_FramePadding, ImVec2(8 * z, 4 * z));
        V2(ImGuiStyleVar_ItemSpacing, ImVec2(8 * z, 8 * z));
        V2(ImGuiStyleVar_ItemInnerSpacing, ImVec2(4 * z, 4 * z));
        V(ImGuiStyleVar_FrameBorderSize, 1);
    }
    void pop(){
        if(vars) ImGui::PopStyleVar(vars);
        if(colours) ImGui::PopStyleColor(colours);
        vars = colours = 0;
    }
};

} // namespace theme
} // namespace forms
