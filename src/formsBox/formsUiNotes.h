//
//  formsUiNotes.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Small shared captions of Forms GL Box's window.
//

#pragma once

#include "imgui.h"

namespace forms {

// Settings that are not part of a scene, a preset or the library (no Save button): they are stored
// with the patch, so they keep only when the patch is saved
inline void savedWithPatchNote(){
    ImGui::TextColored(ImVec4(0.55f, 0.75f, 0.55f, 1), "saved with the patch");
    if(ImGui::IsItemHovered()) ImGui::SetTooltip("Not part of a scene: the same for every scene, kept when the patch is saved\n"
                                                 "(scenes, instruments, setlist and library have their own Save buttons).");
}

} // namespace forms
