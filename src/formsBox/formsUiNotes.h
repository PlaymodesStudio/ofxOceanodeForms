//
//  formsUiNotes.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Small shared captions of Forms GL Box's window.
//

#pragma once

#include "imgui.h"
#include <cmath>
#include <cstdio>

namespace forms {

// Settings that are not part of a scene, a preset or the library (no Save button): they are stored
// with the patch, so they keep only when the patch is saved
inline void savedWithPatchNote(){
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.55f, 1), "saved with the patch");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Not part of a scene: the same for every scene, kept when the patch is saved\n"
                                                 "(scenes, instruments, setlist and library have their own Save buttons).");
}

// The echo's time as a note value (a beat = a quarter note): sets `beats`; true when it changed.
// A time that is no listed note value shows in beats.
inline bool echoNoteCombo(const char* label, float& beats){
    static const struct { const char* name; float beats; } notes[] = {
        {"Off", 0}, {"1/32", 0.125f}, {"1/16 triplet", 1.0f / 6}, {"1/16", 0.25f}, {"1/8 triplet", 1.0f / 3}, {"1/16 dotted", 0.375f},
        {"1/8", 0.5f}, {"1/4 triplet", 2.0f / 3}, {"1/8 dotted", 0.75f}, {"1/4", 1}, {"1/4 dotted", 1.5f}, {"1/2", 2}, {"1/2 dotted", 3}, {"1 bar", 4}, {"2 bars", 8}};
    char free[32]; snprintf(free, sizeof(free), "%.3f beats", beats);
    const char* shown = free;
    for(auto& n : notes) if(std::fabs(n.beats - beats) < 0.002f) shown = n.name;
    bool changed = false;
    if(ImGui::BeginCombo(label, shown)){
        for(auto& n : notes) if(ImGui::Selectable(n.name, shown == n.name)){ beats = n.beats; changed = true; }
        ImGui::EndCombo();
    }
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Echo: repeats of this layer behind what moves (to its right), one every note value,\nwhatever the layer's speed: heard as a tempo-synced delay. Off: no echo.\nThe repeats come from what is still inside the picture: what has left through the left edge stops repeating.");
    return changed;
}

} // namespace forms
