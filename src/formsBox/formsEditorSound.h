//
//  formsEditorSound.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Sonification tab: per-layer banks (level, pan, spread), the partials' tuning and stereo,
//  the kick (detection, samples, ducking), the FX and the master. Controls are ofParameters in
//  the control registry (right-click: publish to node); apply() copies them into SoundSettings.
//

#pragma once
#include <deque>

#include "formsSoundModel.h"
#include "formsControls.h"
#include "formsUiNotes.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

namespace forms {

// What the tab shows besides the controls (filled by the node)
struct SoundView {
    bool engineRunning = false;          // the graph is built (Audio is connected to an SC Output)
    bool hasSC = false;                  // this build has SuperCollider
    bool waitingForDefs = false;         // the graph waits for scsynth to load the SynthDefs
    std::vector<float> layerLevel = std::vector<float>(SOUND_LAYERS); // mean brightness the banks read
    std::vector<bool> layerActive = std::vector<bool>(SOUND_LAYERS);
    float kickLevel = 0;                 // the kick strip's brightness now
    int kicksFired = 0;
    std::string kickScene;               // the scene at the header
    std::string kickChosen;              // the sample it plays
    std::string kickError;
    std::vector<std::string> kickNames;
    float duck = 0;                      // the ducking envelope (C++ estimate)
    float lgain = 1, rgain = 1;
    float compGr = 0;                    // the bus compressor's gain reduction now (dB)
    std::function<void(int)> previewKick;                     // play this sample now
    std::function<void(const std::string&)> setSceneKick;     // the scene at the header: this sample
    std::function<void()> browseKick;
    std::function<void()> editKickSynth;
};

struct SoundControls {
    ofParameter<bool> on, kickOn, lrBalance, limiter;
    ofParameter<float> volume;
    std::deque<ofParameter<bool>> layerOn = std::deque<ofParameter<bool>>(SOUND_LAYERS);
    std::deque<ofParameter<float>> layerLevel = std::deque<ofParameter<float>>(SOUND_LAYERS), layerPan = std::deque<ofParameter<float>>(SOUND_LAYERS), layerSpread = std::deque<ofParameter<float>>(SOUND_LAYERS), layerReverb = std::deque<ofParameter<float>>(SOUND_LAYERS);
    ofParameter<float> bankGain, detune, loudness, lag, spreadLow, spreadHigh;
    ofParameter<float> duckBelow, duckDepth, duckShape, duckBeats, duckCurve;
    ofParameter<bool> duckFullBand;
    ofParameter<float> kickThreshold, kickStrip, kickGap, kickTune, kickLevel;
    ofParameter<std::string> kickFolder;
    ofParameter<float> reverbMult, revTime, lowShelf, midLow, mid, midHigh, highShelf, lowpass;
    // the bus compressor (MixerPro's, on the whole mix before the limiter): MixerPro's names and ranges
    ofParameter<bool> compOn, compAuto;
    ofParameter<float> compThr, compRatio, compKnee, compAtk, compRel, compMakeup, compRms, compHpf, compMix;
    int eqDragBand = -1;

    void setup(ControlRegistry& reg, const std::string& defaultKickFolder){
        on.set("Sound", true);
        volume.set("Volume", 1, 0, 8);
        for(int l = 0; l < (int)layerOn.size(); l++){
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
        duckFullBand.set("Duck Full Image",true);
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
        midLow.set("EQ Mid Low", 3.69f, -18, 18);
        mid.set("EQ Mid", 2.69f, -18, 18);
        midHigh.set("EQ Mid High", 1.34f, -18, 18);
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
        for(int l = 0; l < (int)layerOn.size(); l++){ reg.reg(layerOn[l]); reg.reg(layerLevel[l]); reg.reg(layerPan[l]); reg.reg(layerSpread[l]); reg.reg(layerReverb[l]); }
        for(auto* p : {&bankGain, &detune, &loudness, &lag, &spreadLow, &spreadHigh, &duckBelow, &duckDepth, &duckShape,
                       &duckBeats, &duckCurve, &kickThreshold, &kickStrip, &kickGap, &kickTune, &kickLevel,
                       &reverbMult, &revTime, &lowShelf, &midLow, &mid, &midHigh, &highShelf, &lowpass}) reg.reg(*p);
        reg.reg(kickFolder);
        reg.reg(duckFullBand);
        reg.reg(compOn); reg.reg(compAuto);
        for(auto* p : {&compThr, &compRatio, &compKnee, &compAtk, &compRel, &compMakeup, &compRms, &compHpf, &compMix}) reg.reg(*p);
    }

    void ensureLayers(ControlRegistry& reg, size_t count){
        size_t old=layerOn.size(); if(count<=old) return;
        layerOn.resize(count); layerLevel.resize(count); layerPan.resize(count); layerSpread.resize(count); layerReverb.resize(count);
        for(size_t l=old;l<count;++l){
            std::string n="L"+ofToString(l+1);
            layerOn[l].set(n+" On",true); layerLevel[l].set(n+" Level",1,0,2);
            layerPan[l].set(n+" Pan",0,-1,1); layerSpread[l].set(n+" Spread",1,0,1); layerReverb[l].set(n+" Reverb",1,0,2);
            reg.reg(layerOn[l]); reg.reg(layerLevel[l]); reg.reg(layerPan[l]); reg.reg(layerSpread[l]); reg.reg(layerReverb[l]);
        }
    }

    void apply(SoundSettings& s) const {
        s.on = on;
        s.layers.resize(layerOn.size());
        for(int l = 0; l < (int)layerOn.size(); l++){
            s.layers[l].on = layerOn[l]; s.layers[l].level = layerLevel[l];
            s.layers[l].pan = layerPan[l]; s.layers[l].spread = layerSpread[l];
            s.layers[l].reverb = layerReverb[l];
        }
        s.bankGain = bankGain; s.detune = detune; s.loudnessBlend = loudness; s.lag = lag;
        s.spreadLow = spreadLow; s.spreadHigh = spreadHigh;
        s.duckBelow = duckBelow; s.duckDepth = duckDepth; s.duckShape = duckShape; s.duckBeats = duckBeats; s.duckCurve = duckCurve;
        s.duckFullBand=duckFullBand;
        s.kickOn = kickOn; s.kickThreshold = kickThreshold; s.kickStrip = kickStrip; s.kickMinGap = kickGap;
        s.kickTune = kickTune; s.kickGain = kickLevel; s.kickFolder = kickFolder;
        s.volume = volume; s.limit = limiter ? 1 : 0; s.lrBalance = lrBalance;
    }

    // one layer's sound (also drawn in Scenes / Live, under the layer stack)
    void drawLayer(ControlRegistry& reg, const SoundView& v, int l){
        float z = ofxOceanodeShared::getZoomLevel();
        l = std::max(0, std::min((int)layerOn.size() - 1, l));
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

    void drawEqGraph(float zoom){
        const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x);
        const float height = 168.0f * zoom;
        const float left = 34.0f * zoom, right = 8.0f * zoom;
        const float top = 8.0f * zoom, bottom = 25.0f * zoom;
        const float dbMin = -24.0f, dbMax = 24.0f;
        const float chartW = std::max(10.0f, width - left - right);
        const float chartH = std::max(10.0f, height - top - bottom);
        const float freq[] = {80, 270, 800, 2000, 3328};
        const char* freqLabel[] = {"80", "270", "800", "2k", "3.3k"};
        const ImU32 colors[] = {IM_COL32(100, 180, 255, 255), IM_COL32(100, 220, 140, 255), IM_COL32(240, 210, 90, 255), IM_COL32(255, 145, 90, 255), IM_COL32(205, 130, 255, 255)};
        ofParameter<float>* bands[] = {&lowShelf, &midLow, &mid, &midHigh, &highShelf};
        const float logMin = std::log10(20.0f), logMax = std::log10(20000.0f);
        auto xFor = [&](float hz){ return left + (std::log10(hz) - logMin) / (logMax - logMin) * chartW; };
        auto yFor = [&](float db){ return top + (dbMax - ofClamp(db, dbMin, dbMax)) / (dbMax - dbMin) * chartH; };

        ImVec2 origin = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##soundEqGraph", ImVec2(width, height));
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 a(origin.x, origin.y), b(origin.x + width, origin.y + height);
        dl->AddRectFilled(a, b, IM_COL32(12, 14, 20, 255));
        dl->AddRect(a, b, IM_COL32(70, 70, 90, 255));
        for(int db = -20; db <= 20; db += 10){
            float y = origin.y + yFor((float)db);
            dl->AddLine(ImVec2(origin.x + left, y), ImVec2(origin.x + left + chartW, y), db == 0 ? IM_COL32(190, 195, 205, 190) : IM_COL32(48, 52, 65, 180));
            char label[16]; snprintf(label, sizeof(label), "%+d", db);
            dl->AddText(ImVec2(origin.x + 2 * zoom, y - 6 * zoom), IM_COL32(145, 150, 165, 220), label);
        }
        const float ticks[] = {20, 100, 1000, 10000, 20000};
        const char* tickLabels[] = {"20", "100", "1k", "10k", "20k"};
        for(int i = 0; i < 5; i++){
            float x = origin.x + xFor(ticks[i]);
            dl->AddLine(ImVec2(x, origin.y + top), ImVec2(x, origin.y + top + chartH), IM_COL32(48, 52, 65, 180));
            float labelX = i == 0 ? x : (i == 4 ? x - 24 * zoom : x - 8 * zoom);
            dl->AddText(ImVec2(labelX, origin.y + top + chartH + 3 * zoom), IM_COL32(145, 150, 165, 220), tickLabels[i]);
        }
        // A smooth log-frequency response estimate for the fixed shelf and peak bands.
        ImVec2 previous;
        for(int i = 0; i <= 160; i++){
            float t = i / 160.0f;
            float hz = std::pow(10.0f, logMin + t * (logMax - logMin));
            float octLow = std::log2(hz / freq[0]);
            float octHigh = std::log2(hz / freq[4]);
            float db = bands[0]->get() / (1.0f + std::exp(6.0f * octLow));
            for(int band = 1; band <= 3; band++){
                float oct = std::log2(hz / freq[band]);
                float weight = std::exp(-0.5f * std::pow(oct / 0.72f, 2.0f));
                db += bands[band]->get() * weight;
            }
            db += bands[4]->get() / (1.0f + std::exp(-6.0f * octHigh));
            ImVec2 point(origin.x + xFor(hz), origin.y + yFor(db));
            if(i) dl->AddLine(previous, point, IM_COL32(80, 210, 255, 255), 2.0f * zoom);
            previous = point;
        }
        for(int band = 0; band < 5; band++){
            ImVec2 point(origin.x + xFor(freq[band]), origin.y + yFor(bands[band]->get()));
            const ImU32 markerColors[] = {IM_COL32(100, 180, 255, 100), IM_COL32(100, 220, 140, 100), IM_COL32(240, 210, 90, 100), IM_COL32(255, 145, 90, 100), IM_COL32(205, 130, 255, 100)};
            dl->AddLine(ImVec2(point.x, origin.y + top), ImVec2(point.x, origin.y + top + chartH), markerColors[band]);
            float radius = (eqDragBand == band ? 7.0f : 5.5f) * zoom;
            dl->AddCircleFilled(point, radius, colors[band], 16);
            dl->AddCircle(point, radius, IM_COL32(245, 245, 250, 230), 16, 1.2f * zoom);
            dl->AddText(ImVec2(point.x + 5 * zoom, point.y - 15 * zoom), colors[band], freqLabel[band]);
        }

        bool hovered = ImGui::IsItemHovered();
        if(ImGui::IsItemActivated() && hovered){
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            float best = 14.0f * zoom;
            eqDragBand = -1;
            for(int band = 0; band < 5; band++){
                ImVec2 point(origin.x + xFor(freq[band]), origin.y + yFor(bands[band]->get()));
                float dist = std::hypot(mouse.x - point.x, mouse.y - point.y);
                if(dist < best){ best = dist; eqDragBand = band; }
            }
        }
        if(!ImGui::IsMouseDown(ImGuiMouseButton_Left)) eqDragBand = -1;
        if(eqDragBand >= 0 && hovered && ImGui::IsMouseDragging(ImGuiMouseButton_Left)){
            const float limitsMin[] = {-12, -18, -18, -18, -24};
            const float limitsMax[] = {18, 18, 18, 18, 12};
            float db = dbMax - ((ImGui::GetIO().MousePos.y - origin.y - top) / chartH) * (dbMax - dbMin);
            bands[eqDragBand]->set(ofClamp(db, limitsMin[eqDragBand], limitsMax[eqDragBand]));
        }
        if(hovered){
            ImGui::SetTooltip("Drag a band point vertically to change its gain.\nFrequency bands: 80 Hz, 270 Hz, 800 Hz, 2 kHz, 3.3 kHz.");
        }
        ImGui::SetCursorScreenPos(ImVec2(origin.x, origin.y + height + 4 * zoom));
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
        for(int l = 0; l < (int)layerOn.size(); l++){
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
        ImGui::SeparatorText("EQ");
        drawEqGraph(z);
        if(ImGui::TreeNode("EQ band values")){
            reg.slider(lowShelf, "%.1f dB", "Low shelf 80 Hz");
            reg.slider(midLow, "%.1f dB", "Low mid 270 Hz");
            reg.slider(mid, "%.1f dB", "Mid 800 Hz");
            reg.slider(midHigh, "%.1f dB", "High mid 2 kHz");
            reg.slider(highShelf, "%.1f dB", "High shelf 3.3 kHz");
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Filter");
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
        if(v.editKickSynth && ImGui::Button("Sample / Synth settings...")) v.editKickSynth();
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
        reg.check(duckFullBand,"Duck whole image");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Lower the full image-sonification mix, including reverb, on each kick. The kick itself is unaffected. Off: duck only the selected lower frequencies.");
        ImGui::BeginDisabled(duckFullBand);
        reg.slider(duckBelow, "%.0f", "Duck below (MIDI)");
        reg.slider(duckShape, "%.2f", "Shape");
        ImGui::EndDisabled();
        reg.slider(duckDepth, "%.2f", "Depth");
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
        if(v.browseKick && ImGui::Button("Browse sample...")) v.browseKick();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Choose a WAV or AIFF from any folder and embed it in this scene.");
        if(!v.kickError.empty()) ImGui::TextWrapped("%s",v.kickError.c_str());
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
