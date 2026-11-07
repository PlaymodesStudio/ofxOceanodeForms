//
//  formsPanels.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  ImGui widgets bound to ofParameters, for the Forms Box window's Play tab: the widget writes
//  the parameter, so presets, connections and custom panels keep working.
//

#pragma once

#include "ofMain.h"
#include "imgui.h"
#include "formsEngine.h"
#include <algorithm>
#include <vector>

namespace forms {

// ImGui helpers bound to ofParameters (the value only changes when the widget is edited)
inline bool paramSlider(const char* label, ofParameter<float>& p, const char* fmt = "%.3g"){
    float v = p.get();
    if(ImGui::SliderFloat(label, &v, p.getMin(), p.getMax(), fmt)){ p = v; return true; }
    return false;
}
inline bool paramSlider(const char* label, ofParameter<int>& p){
    int v = p.get();
    if(ImGui::SliderInt(label, &v, p.getMin(), p.getMax())){ p = v; return true; }
    return false;
}
inline bool paramCheck(const char* label, ofParameter<bool>& p){
    bool v = p.get();
    if(ImGui::Checkbox(label, &v)){ p = v; return true; }
    return false;
}
inline bool paramCheck(const char* label, ofParameter<int>& p){   // int 0/1 toggles (Pause)
    bool v = p.get() != 0;
    if(ImGui::Checkbox(label, &v)){ p = v ? 1 : 0; return true; }
    return false;
}
// A read-only meter for an output
inline void meter(const char* label, float v, float lo, float hi, const char* fmt = "%.3g"){
    char buf[64];
    snprintf(buf, sizeof(buf), fmt, v);
    ImGui::ProgressBar(hi > lo ? (v - lo) / (hi - lo) : 0, ImVec2(ImGui::CalcItemWidth(), 0), buf);
    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::TextUnformatted(label);
}

} // namespace forms
