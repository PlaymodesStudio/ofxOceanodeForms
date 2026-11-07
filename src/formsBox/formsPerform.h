//
//  formsPerform.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport: the Perform screen's state)
//
//  What the Perform tab keeps: 8 macro faders, each scaling one of Forms Box's performance outputs
//  (1 = as the scene says: the default, so nothing changes until a fader moves), the queued scene
//  change, and 8 snapshots of the performance state (scene or setlist position, the faders, the
//  scene's performance settings). Saved with the patch ("formsPerform").
//  Plain C++ and nlohmann::json (tests/test_v1_backports.cpp checks save / load and the identity).
//

#pragma once

#include "formsPerformance.h"
#include <array>
#include <algorithm>
#include <string>

namespace forms {

struct PerformState {
    struct Fader { std::string key; float gain = 1; };
    struct Snapshot {
        bool used = false;
        std::string scene;              // the scene under the header when taken
        bool useSetlist = false;
        int setlistEntry = 0, imagesInEntry = 0;
        std::array<float, 8> gains {{1, 1, 1, 1, 1, 1, 1, 1}};
        nlohmann::json performance;     // that scene's performance settings
    };
    std::array<Fader, 8> faders;
    std::array<Snapshot, 8> snaps;
    bool queueHardCut = false;          // pads: every layer switches at the change (else layer 1 first)
    bool switchNow = false;             // a chosen scene replaces what plays now, on every layer
    // the queued change (not saved)
    std::string queued;
    std::string cutFor;                 // the scene a temporary Hard Cut waits for
    bool cutWas = false;
    double queuedAt = 0;

    PerformState(){
        static const char* keys[8] = {"SpectrumGain", "kickVol", "sideChain", "reverbAmt", "LRDryWet", "LRHardness", "LRNoiseAmt", "scaleQuant"};
        for(int i = 0; i < 8; i++) faders[i].key = keys[i];
    }
    static const PerfParam* param(const std::string& key){
        for(auto& p : perfParams()) if(key == p.key) return &p;
        return nullptr;
    }
    // the factor on an output (1: untouched)
    float gain(const std::string& key) const {
        float g = 1;
        for(auto& f : faders) if(f.key == key) g *= f.gain;
        return g;
    }
    // an output value with the faders on it (exactly the value when the gain is 1)
    float apply(const PerfParam& p, float v) const {
        float g = gain(p.key);
        if(g == 1.0f) return v;
        return std::min(p.max, std::max(p.min, v * g));
    }
    bool neutral() const { for(auto& f : faders) if(f.gain != 1.0f) return false; return true; }

    nlohmann::json save() const {
        nlohmann::json j = nlohmann::json::object();
        nlohmann::json fs = nlohmann::json::array();
        for(auto& f : faders) fs.push_back({{"key", f.key}, {"gain", f.gain}});
        j["faders"] = fs;
        nlohmann::json ss = nlohmann::json::array();
        for(auto& s : snaps){
            if(!s.used){ ss.push_back(nullptr); continue; }
            ss.push_back({{"scene", s.scene}, {"useSetlist", s.useSetlist}, {"setlistEntry", s.setlistEntry},
                          {"imagesInEntry", s.imagesInEntry}, {"gains", s.gains}, {"performance", s.performance}});
        }
        j["snapshots"] = ss;
        j["hardCut"] = queueHardCut;
        j["switchNow"] = switchNow;
        return j;
    }
    void load(const nlohmann::json& j){
        if(!j.is_object()) return;
        try {
            auto fs = j.value("faders", nlohmann::json::array());
            for(size_t i = 0; i < fs.size() && i < 8; i++){
                faders[i].key = fs[i].value("key", faders[i].key);
                faders[i].gain = std::min(2.0f, std::max(0.0f, fs[i].value("gain", 1.0f)));
            }
            auto ss = j.value("snapshots", nlohmann::json::array());
            for(size_t i = 0; i < ss.size() && i < 8; i++){
                Snapshot s;
                if(ss[i].is_object()){
                    s.used = true;
                    s.scene = ss[i].value("scene", std::string());
                    s.useSetlist = ss[i].value("useSetlist", false);
                    s.setlistEntry = ss[i].value("setlistEntry", 0);
                    s.imagesInEntry = ss[i].value("imagesInEntry", 0);
                    auto g = ss[i].value("gains", std::vector<float>());
                    for(size_t k = 0; k < g.size() && k < 8; k++) s.gains[k] = g[k];
                    s.performance = ss[i].value("performance", nlohmann::json());
                }
                snaps[i] = s;
            }
            queueHardCut = j.value("hardCut", false);
            switchNow = j.value("switchNow", false);
        } catch(...) {}
    }
};

} // namespace forms
