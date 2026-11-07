//
//  formsPerformance.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The per-scene performance settings (what Macro_8 "SNAP-SETTINGS XTRA" held as 25 router
//  snapshots): names, ranges, defaults (its slot 0) and, for the values Forms Box outputs, the
//  per-frame smoothing of the Smoother that followed them in the patch. Transport and Header read
//  the others from the image under the header.
//

#pragma once

#include <string>
#include <vector>
#include <map>
#include <nlohmann/json.hpp>

namespace forms {

struct PerfParam {
    const char* key;       // Macro_8 pin name (kept, so the patch rewiring is a rename)
    const char* label;
    const char* group;     // editor section
    char type;             // 'f' float, 'i' int, 'v' vector, 's' string
    float min, max;
    nlohmann::json def;    // Macro_8 slot 0
    float smooth;          // per-frame smoothing of the output (0 = none)
    bool output;           // Forms Box output port (else read by Transport / Header)
};

inline const std::vector<PerfParam>& perfParams(){
    using J = nlohmann::json;
    static const std::vector<PerfParam> p = {
        // sound levels (to the sonification macro)
        {"SpectrumGain", "Spectrum gain", "Sound", 'f', 0, 4, 2, 0.9609f, true},
        {"kickVol", "Kick", "Sound", 'f', 0, 1, 0.16, 0, true},
        {"sideChain", "Side chain", "Sound", 'f', 0, 1, 1, 0, true},
        {"reverbAmt", "Reverb", "Sound", 'f', 0, 1, 0.1119, 0, true},
        {"Compressor", "Compressor", "Sound", 'i', 0, 1, 0, 0, true},
        {"kDetectOn", "Kick detect", "Sound", 'i', 0, 1, 1, 0, true},
        {"kickSelect", "Kick sample", "Sound", 'i', 0, 40, 11, 0, true},
        // the kick sample by file name (Sonification tab's kick folder); "" = by number (kickSelect)
        {"kickSample", "Kick sample file", "Sound", 's', 0, 0, "", 0, false},
        {"scaleQuant", "Scale quantize", "Sound", 'f', 0, 1, 0, 0, true},
        // stereo (Macro_11)
        {"LRDryWet", "L/R amount", "Stereo", 'f', 0, 1, 0, 0.9462f, true},
        {"LRTh", "L/R rate (per bar)", "Stereo", 'f', 0, 32, 0.25, 0, true},
        {"LRHardness", "L/R hardness", "Stereo", 'f', 0, 1, 1, 0.9761f, true},
        {"LRNoiseAmt", "L/R noise", "Stereo", 'f', 0, 1, 0, 0.9761f, true},
        {"LRStepBars", "L/R step bars", "Stereo", 'i', 0, 16, 8, 0, true},
        {"LRprobSteps[]", "L/R step chances", "Stereo", 'v', 0, 1, J{1, 1, 1, 1, 1, 1, 1, 1}, 0, true},
        {"LRSmotthMs", "L/R smoothing ms", "Stereo", 'f', 0, 32000, 4000, 0, true},
        // scroll speed modulation (Transport)
        {"phasorSpeedMod", "Speed modulation", "Speed", 'f', 0, 1, 0, 0, false},
        {"phasorModChance", "Modulation chance", "Speed", 'f', 0, 1, 0.6642, 0, false},
        {"PhasorMinSpeed", "Min speed", "Speed", 'f', -1, 1, -0.1135, 0, false},
        {"PhasorMaxSpeed", "Max speed", "Speed", 'f', 0, 1, 0.5075, 0, false},
        // image: strobe gate, tremolo, header jumps, beat repeat (Header)
        {"gateChance", "Gate chance", "Gate", 'f', 0, 1, 0, 0, false},
        {"gateMults", "Gate rates", "Gate", 'v', 0, 16, J{0.0625}, 0, false},
        {"tremoloChance", "Tremolo chance", "Tremolo", 'f', 0, 1, 0, 0, false},
        {"tremoloMaxSpeed", "Tremolo max speed", "Tremolo", 'f', 1, 16, 16, 0, false},
        {"tremoloFloor", "Tremolo floor", "Tremolo", 'f', 0, 1, 0.3134, 0, false},
        {"HeaderModChance", "Header jump chance", "Header", 'f', 0, 1, 0, 0, false},
        {"headerMaxDiv", "Header jump max beats", "Header", 'f', 1, 16, 8, 0, false},
        {"repeatChance", "Repeat chance", "Repeat", 'f', 0, 1, 0, 0, false},
        {"repeatMults", "Repeat lengths", "Repeat", 'v', 0, 16, J{0.5, 1, 0.66, 1.5, 2, 0.25}, 0, false},
        {"repeatNum", "Repeat count", "Repeat", 'v', 1, 16, J{4}, 0, false},
        {"repeatPW", "Repeat pulse width", "Repeat", 'f', 0, 1, 0.75, 0, false},
        {"repeatBarStart", "Repeat start (bar phase)", "Repeat", 'f', 0, 1, 0.75, 0, false},
    };
    return p;
}

// Full set: defaults overridden by the given values (unknown keys dropped)
inline nlohmann::json completePerformance(const nlohmann::json& given){
    nlohmann::json out = nlohmann::json::object();
    for(auto& p : perfParams()) out[p.key] = (given.is_object() && given.contains(p.key)) ? given[p.key] : p.def;
    return out;
}

} // namespace forms
