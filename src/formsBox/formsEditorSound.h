//
//  formsEditorSound.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Sonification tab: per-layer banks (level, pan, spread), the partials' tuning and stereo,
//  the kick (detection, samples, ducking), the FX and the master. Controls are ofParameters in
//  the control registry (right-click: publish to node); apply() copies them into SoundSettings.
//

#pragma once

#include "formsSoundModel.h"
#include "formsControls.h"
#include "formsUiNotes.h"
#include <functional>

namespace forms {

// What the tab shows besides the controls (filled by the node)
struct SoundView {
    bool engineRunning = false;          // the graph is built (Audio is connected to an SC Output)
    bool hasSC = false;                  // this build has SuperCollider
    bool waitingForDefs = false;         // the graph waits for scsynth to load the SynthDefs
    float layerLevel[SOUND_LAYERS] = {}; // mean brightness the banks read
    bool layerActive[SOUND_LAYERS] = {};
    float kickLevel = 0;                 // the kick strip's brightness now
    int kicksFired = 0;
    std::string kickScene;               // the scene at the header
    std::string kickChosen;              // the sample it plays
    std::vector<std::string> kickNames;
    float duck = 0;                      // the ducking envelope (C++ estimate)
    float lgain = 1, rgain = 1;
    float compGr = 0;                    // the bus compressor's gain reduction now (dB)
    std::function<void(int)> previewKick;                     // play this sample now
    std::function<void(const std::string&)> setSceneKick;     // the scene at the header: this sample
};

struct SoundControls {
    ofParameter<bool> on, kickOn, lrBalance, limiter;
    ofParameter<float> volume;
    ofParameter<bool> layerOn[SOUND_LAYERS];
    ofParameter<float> layerLevel[SOUND_LAYERS], layerPan[SOUND_LAYERS], layerSpread[SOUND_LAYERS], layerReverb[SOUND_LAYERS];
    ofParameter<float> bankGain, detune, loudness, lag, spreadLow, spreadHigh;
    ofParameter<float> duckBelow, duckDepth, duckShape, duckBeats, duckCurve;
    ofParameter<float> kickThreshold, kickStrip, kickGap, kickTune, kickLevel;
    ofParameter<std::string> kickFolder;
    ofParameter<float> reverbMult, revTime, lowShelf, highShelf, lowpass;
    // the bus compressor (MixerPro's, on the whole mix before the limiter): MixerPro's names and ranges
    ofParameter<bool> compOn, compAuto;
    ofParameter<float> compThr, compRatio, compKnee, compAtk, compRel, compMakeup, compRms, compHpf, compMix;

    void setup(ControlRegistry& reg, const std::string& defaultKickFolder){
        on.set("Sound", true);
        volume.set("Volume", 1, 0, 8);
        for(int l = 0; l < SOUND_LAYERS; l++){
            std::string n = "L" + ofToString(l + 1);
            layerOn[l].set(n + " On", true);
            layerLevel[l].set(n + " Level", 1, 0, 2);
            layerPan[l].set(n + " Pan", 0, -1, 1);
            layerSpread[l].set(n + " Spread", 1, 0, 1);
            layerReverb[l].set(n + " Reverb", 1, 0, 2);
        }
        bankGain.set("Bank Gain", 0.0216f, 0, 0.1f);
        detune.set("Detune", 0.0746f, 0, 1);
        loudness.set("Loudness", 0.2f, 0, 1);
        lag.set("Amp Lag", 1.0f / 30, 0, 0.5f);
        spreadLow.set("Spread Low", 40, 0, 127);
        spreadHigh.set("Spread High", 100, 0, 127);
        duckBelow.set("Duck Below", 64, 20, 127);
        duckDepth.set("Duck Depth", 0.89f, 0, 1);
        duckShape.set("Duck Shape", 0.6f, 0.1f, 4);
        duckBeats.set("Duck Beats", 2, 0.25f, 8);
        duckCurve.set("Duck Curve", -4, -10, 10);
        kickOn.set("Kick On", true);
        kickThreshold.set("Kick Threshold", 0.5f, 0, 1);
        kickStrip.set("Kick Strip", 100.0f / 3840, 0.005f, 0.25f);
        kickGap.set("Kick Gap", 0.08f, 0, 1);
        kickTune.set("Kick Tune", -2, -24, 24);
        kickLevel.set("Kick Level", 1.5f, 0, 4);
        kickFolder.set("Kick Folder", defaultKickFolder);
        reverbMult.set("Reverb Mult", 1, 0, 3);
        revTime.set("Reverb Time", 4, 0.1f, 20);
        lowShelf.set("Low Shelf", 10, -12, 18);
        highShelf.set("High Shelf", -6.72f, -24, 12);
        lowpass.set("Lowpass", 120.36f, 60, 135);
        lrBalance.set("LR Balance", false);
        limiter.set("Limiter", true);
        compOn.set("Comp On", false);
        compThr.set("Comp Threshold", -18, -60, 0);
        compRatio.set("Comp Ratio", 3, 1, 20);
        compKnee.set("Comp Knee", 6, 0, 48);
        compAtk.set("Comp Attack", 10, 0.1f, 200);
        compRel.set("Comp Release", 150, 5, 2000);
        compMakeup.set("Comp Makeup", 0, -24, 24);
        compAuto.set("Comp Auto Makeup", false);
        compRms.set("Comp RMS", 0, 0, 1);
        compHpf.set("Comp SC HPF", 20, 20, 1000);
        compMix.set("Comp Mix", 1, 0, 1);
        for(auto* p : {&on, &kickOn, &lrBalance, &limiter}) reg.reg(*p);
        reg.reg(volume);
        for(int l = 0; l < SOUND_LAYERS; l++){ reg.reg(layerOn[l]); reg.reg(layerLevel[l]); reg.reg(layerPan[l]); reg.reg(layerSpread[l]); reg.reg(layerReverb[l]); }
        for(auto* p : {&bankGain, &detune, &loudness, &lag, &spreadLow, &spreadHigh, &duckBelow, &duckDepth, &duckShape,
                       &duckBeats, &duckCurve, &kickThreshold, &kickStrip, &kickGap, &kickTune, &kickLevel,
                       &reverbMult, &revTime, &lowShelf, &highShelf, &lowpass}) reg.reg(*p);
        reg.reg(kickFolder);
        reg.reg(compOn); reg.reg(compAuto);
        for(auto* p : {&compThr, &compRatio, &compKnee, &compAtk, &compRel, &compMakeup, &compRms, &compHpf, &compMix}) reg.reg(*p);
    }

    void apply(SoundSettings& s) const {
        s.on = on;
        for(int l = 0; l < SOUND_LAYERS; l++){
            s.layers[l].on = layerOn[l]; s.layers[l].level = layerLevel[l];
            s.layers[l].pan = layerPan[l]; s.layers[l].spread = layerSpread[l];
            s.layers[l].reverb = layerReverb[l];
        }
        s.bankGain = bankGain; s.detune = detune; s.loudnessBlend = loudness; s.lag = lag;
        s.spreadLow = spreadLow; s.spreadHigh = spreadHigh;
        s.duckBelow = duckBelow; s.duckDepth = duckDepth; s.duckShape = duckShape; s.duckBeats = duckBeats; s.duckCurve = duckCurve;
        s.kickOn = kickOn; s.kickThreshold = kickThreshold; s.kickStrip = kickStrip; s.kickMinGap = kickGap;
        s.kickTune = kickTune; s.kickGain = kickLevel; s.kickFolder = kickFolder;
        s.volume = volume; s.limit = limiter ? 1 : 0; s.lrBalance = lrBalance;
    }

    // one layer's sound (also drawn in Scenes / Live, under the layer stack)
    void drawLayer(ControlRegistry& reg, const SoundView& v, int l){
        float z = ofxOceanodeShared::getZoomLevel();
        l = std::max(0, std::min(SOUND_LAYERS - 1, l));
        std::string t = "Layer " + ofToString(l + 1);
        reg.check(layerOn[l], t.c_str());
        ImGui::SameLine();
        char buf[32]; snprintf(buf, sizeof(buf), v.layerActive[l] ? "%.2f" : "no image", v.layerLevel[l]);
        ImGui::ProgressBar(v.layerActive[l] ? std::min(1.0f, v.layerLevel[l] * 4) : 0, ImVec2(-1, 0), buf);
        ImGui::PushItemWidth(-80 * z);
        reg.slider(layerLevel[l], "%.2f", "Level");
        reg.slider(layerPan[l], "%.2f", "Pan");
        reg.slider(layerSpread[l], "%.2f", "Spread");
        reg.slider(layerReverb[l], "x%.2f", "Reverb");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("How much of this layer goes to the reverb: x the scene's reverb (and Reverb x scene).\n0: dry, 1: as the scene says, 2: double (the send tops out at all-wet).");
        ImGui::PopItemWidth();
    }

    void draw(ControlRegistry& reg, const SoundView& v){
        float z = ofxOceanodeShared::getZoomLevel();
        if(!v.hasSC) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "This build has no SuperCollider: no sound.");
        else if(v.waitingForDefs) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Loading the SynthDefs into SuperCollider...");
        else if(!v.engineRunning) ImGui::TextColored(ImVec4(1, 0.75f, 0.3f, 1), "Silent: connect the Audio output to an SC Output (the graph builds then).");
        else ImGui::TextColored(ImVec4(0.4f, 0.9f, 0.5f, 1), "Playing: 512 partials per layer, kick, FX, master.");
        ImGui::SameLine();
        ImGui::TextDisabled("  Right-click a control to publish it to the node.");
        ImGui::SameLine();
        savedWithPatchNote();
        reg.check(on, "Sound on");
        ImGui::SameLine();
        ImGui::SetNextItemWidth(220 * z);
        reg.slider(volume, "%.2f");

        if(!ImGui::BeginTable("##snd", 3, ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchSame)) return;
        // ---- layers
        ImGui::TableNextColumn();
        ImGui::SeparatorText("Layers");
        ImGui::TextDisabled("Each layer's header column plays its own bank.");
        for(int l = 0; l < SOUND_LAYERS; l++){
            ImGui::PushID(l);
            drawLayer(reg, v, l);
            ImGui::PopID();
            ImGui::Spacing();
        }
        // ---- partials and FX
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-110 * z);
        ImGui::SeparatorText("Partials");
        reg.slider(bankGain, "%.4f");
        reg.slider(detune, "%.4f st", "Detune R");
        reg.slider(loudness, "%.2f", "Equal loudness");
        reg.slider(lag, "%.3f s", "Amp lag");
        reg.slider(spreadLow, "%.0f", "Spread from");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("MIDI pitch: below it the partials stay centred (mono bass)");
        reg.slider(spreadHigh, "%.0f", "Spread full at");
        ImGui::TextDisabled("Pitch: bottom row = scene root, top = root + note grid + 1;\nscale quantize per scene (on the real root).");
        ImGui::SeparatorText("FX");
        reg.slider(reverbMult, "%.2f", "Reverb x scene");
        reg.slider(revTime, "%.1f s", "Reverb time");
        reg.slider(lowShelf, "%.1f dB", "Low shelf 80 Hz");
        reg.slider(highShelf, "%.1f dB", "High shelf 3.3k");
        reg.slider(lowpass, "%.1f", "Lowpass (MIDI)");
        ImGui::SeparatorText("Compressor");
        reg.check(compOn, "Compressor on");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("MixerPro's compressor on the whole mix (partials, reverb and kick), before the limiter.\n"
                                                     "The scenes' own Compressor (Performance) stays in the FX, as before.");
        ImGui::SameLine();
        {
            char gb[32]; snprintf(gb, sizeof(gb), "GR %.1f dB", v.compGr);
            ImGui::ProgressBar(compOn ? std::min(1.0f, v.compGr / 24.0f) : 0.0f, ImVec2(-1, 0), compOn ? gb : "off");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Gain reduction now (full bar: 24 dB)");
        }
        ImGui::BeginDisabled(!compOn);
        reg.slider(compThr, "%.1f dB", "Threshold");
        reg.slider(compRatio, "%.1f:1", "Ratio");
        reg.slider(compKnee, "%.1f dB", "Knee");
        reg.slider(compAtk, "%.1f ms", "Attack");
        reg.slider(compRel, "%.0f ms", "Release");
        reg.slider(compMakeup, "%.1f dB", "Makeup");
        reg.check(compAuto, "Auto makeup");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Adds half the gain reduction a 0 dB signal would get");
        reg.slider(compRms, "%.2f", "Peak / RMS");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Detector: 0 peak, 1 RMS");
        reg.slider(compHpf, "%.0f Hz", "Sidechain HPF");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The detector ignores what is below (the kick and bass pump the mix less)");
        reg.slider(compMix, "%.2f", "Mix");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Parallel compression: 0 dry, 1 fully compressed");
        ImGui::EndDisabled();
        ImGui::SeparatorText("Output");
        reg.check(limiter, "Limiter");
        reg.check(lrBalance, "Scenes' L/R auto-balance");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The scenes' L/R settings (Performance > Stereo) pan the whole mix bar by bar");
        if(lrBalance) ImGui::Text("L %.2f  R %.2f", v.lgain, v.rgain);
        ImGui::PopItemWidth();
        // ---- kick
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-110 * z);
        ImGui::SeparatorText("Kick");
        reg.check(kickOn, "Kick on");
        char kb[48]; snprintf(kb, sizeof(kb), "%.2f  (%d fired)", v.kickLevel, v.kicksFired);
        ImGui::ProgressBar(v.kickLevel, ImVec2(ImGui::CalcItemWidth(), 0), kb);
        // the threshold on the meter
        {
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            float x = a.x + (b.x - a.x) * kickThreshold;
            ImGui::GetWindowDrawList()->AddLine(ImVec2(x, a.y), ImVec2(x, b.y), IM_COL32(255, 120, 80, 255), 2);
        }
        reg.slider(kickThreshold, "%.2f", "Threshold");
        reg.slider(kickStrip, "%.3f", "Strip height");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The bottom part of the kick layer's header column that is measured (fraction of the height)");
        reg.slider(kickGap, "%.2f s", "Min gap");
        reg.slider(kickTune, "%.1f st", "Tune");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("On top of the scene's transpose (-2: the 1920 speed)");
        reg.slider(kickLevel, "%.2f", "Level");
        ImGui::SeparatorText("Ducking");
        reg.slider(duckBelow, "%.0f", "Duck below (MIDI)");
        reg.slider(duckDepth, "%.2f", "Depth");
        reg.slider(duckShape, "%.2f", "Shape");
        reg.slider(duckBeats, "%.2f", "Length (beats)");
        reg.slider(duckCurve, "%.1f", "Curve");
        ImGui::ProgressBar(v.duck, ImVec2(ImGui::CalcItemWidth(), 0), "duck");
        ImGui::SeparatorText("Samples");
        char folder[512];
        strncpy(folder, kickFolder.get().c_str(), sizeof(folder)); folder[sizeof(folder) - 1] = 0;
        if(ImGui::InputText("Folder", folder, sizeof(folder), ImGuiInputTextFlags_EnterReturnsTrue)) kickFolder = std::string(folder);
        reg.menu(kickFolder.getEscapedName());
        ImGui::PopItemWidth();
        ImGui::TextDisabled("Scene at the header: %s", v.kickScene.empty() ? "-" : v.kickScene.c_str());
        ImGui::TextDisabled("plays: %s", v.kickChosen.empty() ? "-" : v.kickChosen.c_str());
        ImGui::BeginChild("##kicks", ImVec2(0, 200 * z), true);
        for(int i = 0; i < (int)v.kickNames.size(); i++){
            ImGui::PushID(i);
            if(ImGui::SmallButton(">") && v.previewKick) v.previewKick(i);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip(v.engineRunning ? "Play it now" : "Play it now (needs Audio connected to an SC Output)");
            ImGui::SameLine();
            bool sel = v.kickNames[i] == v.kickChosen;
            if(ImGui::Selectable(v.kickNames[i].c_str(), sel, ImGuiSelectableFlags_AllowDoubleClick)){
                if(ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) && v.setSceneKick) v.setSceneKick(v.kickNames[i]);
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Double-click: the scene at the header plays this kick (saved in the scene)");
            ImGui::PopID();
        }
        if(v.kickNames.empty()) ImGui::TextDisabled("no .wav / .aif files in the folder");
        ImGui::EndChild();
        ImGui::EndTable();
    }
};

} // namespace forms
