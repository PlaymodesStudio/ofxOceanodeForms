//
//  formsEditorPost.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Post-processing tab: its controls (ofParameters registered in the control registry, so
//  any of them can be published to the node) and how they map onto forms::Post.
//

#pragma once
#include <deque>

#include "formsPost.h"
#include "formsControls.h"
#include "formsUiNotes.h"

namespace forms {

struct PostControls {
    ofParameter<bool> enabled, stretch, useOpacity, line;
    ofParameter<float> hue, sat, val, levelMin, levelMax, gamma, low, high;
    ofParameter<float> lineWidth, lineSoft, lineSoftV, lineAlpha, lineEase, lineOffset, lineR, lineG, lineB;
    ofParameter<float> bgR, bgG, bgB;
    ofParameter<bool> paper, featherExport;   // paper view / export options
    ofParameter<int> lineCurve, stretchBlend;
    ofParameter<float> featherW[4], featherK[4];   // edge feathers: left, right, top, bottom
    // before the sound: each layer's buffer, each layer its own settings
    int FX_LAYERS = 4; // initial settings for legacy patches; grows with the scene
    ofParameter<bool> fxOn;
    std::deque<ofParameter<float>> hBlur = std::deque<ofParameter<float>>(4), vBlur = std::deque<ofParameter<float>>(4), trailBeats = std::deque<ofParameter<float>>(4), trailLevel = std::deque<ofParameter<float>>(4);
    std::deque<ofParameter<float>> echoBeats = std::deque<ofParameter<float>>(4), echoFeedback = std::deque<ofParameter<float>>(4), echoMix = std::deque<ofParameter<float>>(4), echoLow = std::deque<ofParameter<float>>(4), echoHigh = std::deque<ofParameter<float>>(4);
    void setupEchoLayer(ControlRegistry& reg, int l){
        std::string n = "L" + ofToString(l + 1) + " Echo";
        echoBeats[l].set(n, 0, 0, 8); echoFeedback[l].set(n + " Feedback", 0.5f, 0, 0.95f); echoMix[l].set(n + " Mix", 0.5f, 0, 1);
        echoLow[l].set(n + " Lowpass", 1, 0, 1); echoHigh[l].set(n + " Highpass", 0, 0, 1);
        for(auto* p : {&echoBeats[l], &echoFeedback[l], &echoMix[l], &echoLow[l], &echoHigh[l]}) reg.reg(*p);
    }
    std::deque<ofParameter<float>> maskAmount=std::deque<ofParameter<float>>(4), maskWidth=std::deque<ofParameter<float>>(4), maskHeight=std::deque<ofParameter<float>>(4), maskContrast=std::deque<ofParameter<float>>(4), maskThreshold=std::deque<ofParameter<float>>(4), maskSpeed=std::deque<ofParameter<float>>(4), maskBlur=std::deque<ofParameter<float>>(4);
    std::deque<ofParameter<bool>> maskInvert=std::deque<ofParameter<bool>>(4), maskAnimate=std::deque<ofParameter<bool>>(4);
    std::deque<ofParameter<int>> maskSeed=std::deque<ofParameter<int>>(4);
    void setupMaskLayer(ControlRegistry& reg,int l){
        std::string n="L"+ofToString(l+1)+" Perlin ";
        maskAmount[l].set(n+"Amount",0,0,1); maskWidth[l].set(n+"Width",4,.125f,32);
        maskHeight[l].set(n+"Height",.25f,.01f,2); maskContrast[l].set(n+"Contrast",1,.1f,8);
        maskBlur[l].set(n+"Blur",0,0,60); maskThreshold[l].set(n+"Threshold",0,0,1); maskSpeed[l].set(n+"Speed",.25f,0,4);
        maskSeed[l].set(n+"Seed",l+1,0,65535); maskInvert[l].set(n+"Invert",false); maskAnimate[l].set(n+"Animated",false);
        for(auto* p:{&maskAmount[l],&maskWidth[l],&maskHeight[l],&maskContrast[l],&maskThreshold[l],&maskBlur[l],&maskSpeed[l]})reg.reg(*p);
        reg.reg(maskSeed[l]);reg.reg(maskInvert[l]);reg.reg(maskAnimate[l]);
    }
    std::function<void(int)> drawEffectPreset;
    int fxLayer = 0;   // the layer the tab shows

    void setup(ControlRegistry& reg){
        enabled.set("Post", true);
        stretch.set("Stretch", true);
        stretchBlend.set("Stretch Blend", 0, 0, 5);
        useOpacity.set("Post Opacity", true);
        hue.set("Hue", 0, 0, 1);
        sat.set("Saturation", 1, 0, 2);
        val.set("Value", 1, 0, 2);
        levelMin.set("Levels Min", 0, 0, 1);
        levelMax.set("Levels", 0.617f, 0.001f, 1);
        gamma.set("Gamma", 1, 0.1f, 5);
        low.set("Levels Low", 0, 0, 1);
        high.set("Levels High", 1, 0, 1);
        line.set("Line", true);
        lineWidth.set("Line Width", 4.8f, 0, 64);
        lineSoft.set("Line Soft", 0, 0, 64);
        lineSoftV.set("Line Soft V", 0, 0, 540);
        lineAlpha.set("Line Alpha", 1, 0, 1);
        lineEase.set("Line Ease", 0, 0, 2);
        lineCurve.set("Line Curve", 0, 0, 2);
        lineOffset.set("Line Offset", 0, -256, 256);
        lineR.set("Line R", 1, 0, 1);
        lineG.set("Line G", 1, 0, 1);
        lineB.set("Line B", 1, 0, 1);
        bgR.set("Background R", 0, 0, 1);
        bgG.set("Background G", 0, 0, 1);
        bgB.set("Background B", 0, 0, 1);
        paper.set("Paper View", false);
        featherExport.set("Feather PNG Exports", false);
        static const char* edgeName[4] = {"Left", "Right", "Top", "Bottom"};
        for(int e = 0; e < 4; e++){
            featherW[e].set(std::string("Feather ") + edgeName[e], 0, 0, 0.5f);
            featherK[e].set(std::string("Feather ") + edgeName[e] + " Curve", 1.0f, 0.1f, 5.0f);
            reg.reg(featherW[e]); reg.reg(featherK[e]);
        }
        fxOn.set("Layer FX", true);
        reg.reg(fxOn);
        for(int l = 0; l < FX_LAYERS; l++){
            std::string n = "L" + ofToString(l + 1) + " ";
            hBlur[l].set(n + "H Blur", 0, 0, 60);
            vBlur[l].set(n + "V Blur", 0, 0, 60);
            trailBeats[l].set(n + "Trail", 0, 0, 16);
            trailLevel[l].set(n + "Trail Level", 0.8f, 0, 1);
            for(auto* p : {&hBlur[l], &vBlur[l], &trailBeats[l], &trailLevel[l]}) reg.reg(*p);
            setupEchoLayer(reg, l);
            setupMaskLayer(reg,l);
        }
        for(auto* p : {&enabled, &stretch, &useOpacity, &line, &paper, &featherExport}) reg.reg(*p);
        for(auto* p : {&hue, &sat, &val, &levelMin, &levelMax, &gamma, &low, &high, &lineWidth, &lineSoft, &lineSoftV, &lineAlpha,
                       &lineEase, &lineOffset, &lineR, &lineG, &lineB, &bgR, &bgG, &bgB}) reg.reg(*p);
        reg.reg(lineCurve);
        reg.reg(stretchBlend);
    }

    void ensureLayers(ControlRegistry& reg, size_t count){
        int old=FX_LAYERS; if(count<=(size_t)old) return;
        maskAmount.resize(count);maskWidth.resize(count);maskHeight.resize(count);maskContrast.resize(count);maskThreshold.resize(count);maskSpeed.resize(count);maskBlur.resize(count);maskSeed.resize(count);maskInvert.resize(count);maskAnimate.resize(count);
        hBlur.resize(count); vBlur.resize(count); trailBeats.resize(count); trailLevel.resize(count); FX_LAYERS=(int)count;
        echoBeats.resize(count); echoFeedback.resize(count); echoMix.resize(count); echoLow.resize(count); echoHigh.resize(count);
        for(int l=old;l<FX_LAYERS;++l){
            std::string n="L"+ofToString(l+1)+" ";
            hBlur[l].set(n+"H Blur",0,0,60); vBlur[l].set(n+"V Blur",0,0,60);
            trailBeats[l].set(n+"Trail",0,0,16); trailLevel[l].set(n+"Trail Level",.8f,0,1);
            for(auto* p:{&hBlur[l],&vBlur[l],&trailBeats[l],&trailLevel[l]}) reg.reg(*p);
            setupEchoLayer(reg, l);
            setupMaskLayer(reg,l);
        }
    }

    // A patch saved when the effects were one setting for every layer: every layer gets it
    void migrate(const ofJson& j){
        if(!j.contains("formsControls") || !j["formsControls"].is_object()) return;
        auto& v = j["formsControls"];
        if(v.contains("L1_H_Blur")) return;
        auto take = [&](const char* key, std::deque<ofParameter<float>>& ps){
            if(!v.contains(key) || !v[key].is_number()) return;
            for(int l = 0; l < FX_LAYERS; l++) ps[l] = ofClamp(v[key].get<float>(), ps[l].getMin(), ps[l].getMax());
        };
        take("H_Blur", hBlur); take("V_Blur", vBlur); take("Trail", trailBeats); take("Trail_Level", trailLevel);
    }
    // one layer's effects as numbers (the scene keeps them: LayerDef::fx)
    LayerFxValues fxValues(int l) const {
        LayerFxValues v;
        v.hBlur = hBlur[l]; v.vBlur = vBlur[l]; v.trail = trailBeats[l]; v.trailLevel = trailLevel[l];
        v.echo = echoBeats[l]; v.echoFeedback = echoFeedback[l]; v.echoMix = echoMix[l]; v.echoLow = echoLow[l]; v.echoHigh = echoHigh[l];
        v.maskAmount = maskAmount[l]; v.maskWidth = maskWidth[l]; v.maskHeight = maskHeight[l]; v.maskContrast = maskContrast[l];
        v.maskThreshold = maskThreshold[l]; v.maskBlur = maskBlur[l]; v.maskSpeed = maskSpeed[l];
        v.maskSeed = maskSeed[l]; v.maskInvert = maskInvert[l]; v.maskAnimate = maskAnimate[l];
        return v;
    }
    void setFxValues(int l, const LayerFxValues& v){
        auto setf = [](ofParameter<float>& p, float x){ if(p.get() != x) p = x; };
        setf(hBlur[l], v.hBlur); setf(vBlur[l], v.vBlur); setf(trailBeats[l], v.trail); setf(trailLevel[l], v.trailLevel);
        setf(echoBeats[l], v.echo); setf(echoFeedback[l], v.echoFeedback); setf(echoMix[l], v.echoMix); setf(echoLow[l], v.echoLow); setf(echoHigh[l], v.echoHigh);
        setf(maskAmount[l], v.maskAmount); setf(maskWidth[l], v.maskWidth); setf(maskHeight[l], v.maskHeight); setf(maskContrast[l], v.maskContrast);
        setf(maskThreshold[l], v.maskThreshold); setf(maskBlur[l], v.maskBlur); setf(maskSpeed[l], v.maskSpeed);
        if(maskSeed[l].get() != v.maskSeed) maskSeed[l] = v.maskSeed;
        if(maskInvert[l].get() != v.maskInvert) maskInvert[l] = v.maskInvert;
        if(maskAnimate[l].get() != v.maskAnimate) maskAnimate[l] = v.maskAnimate;
    }
    bool anyFx(int l) const { return fxOn && (hBlur[l] > 0.05f || vBlur[l] > 0.05f || trailBeats[l] > 0.01f || (echoBeats[l] > 0.01f && echoMix[l] > 0) || maskAmount[l] > 0); }

    void apply(Post& p) const {
        p.enabled = enabled; p.stretch = stretch; p.stretchBlend = stretchBlend; p.useOpacity = useOpacity;
        p.hue = hue; p.sat = sat; p.val = val;
        p.levelMin = levelMin; p.levelMax = levelMax; p.gamma = gamma; p.low = low; p.high = high;
        p.line = line; p.lineWidth = lineWidth; p.lineSoft = lineSoft; p.lineSoftV = lineSoftV; p.lineAlpha = lineAlpha;
        p.lineEase = lineEase; p.lineEaseCurve = lineCurve; p.lineOffset = lineOffset;
        p.lineColor = ofFloatColor(lineR, lineG, lineB);
        for(int e = 0; e < 4; e++){ p.edges.width[e] = featherW[e]; p.edges.curve[e] = featherK[e]; }
    }

    // one layer's effects before the sound (also drawn in Scenes / Live, under the layer stack)
    void drawLayerFx(ControlRegistry& reg, int L, bool showMaster, bool effectPreset=true){
        L = std::max(0, std::min(FX_LAYERS - 1, L));
        if(showMaster){
            reg.check(fxOn, "Layer effects on");
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("For every layer (also in Post-processing)");
        }
        if(effectPreset && drawEffectPreset)drawEffectPreset(L);
        reg.slider(hBlur[L], "%.1f px", "H blur");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blurs this layer horizontally (Gaussian sigma, px); the edges repeat, nothing comes from outside");
        reg.slider(vBlur[L], "%.1f px", "V blur");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blurs this layer vertically (Gaussian sigma, px): the sound's partials smear into their neighbours.\nNothing comes from or goes past the image's top and bottom.");
        reg.slider(trailBeats[L], "%.2f beats", "Trail");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("A tail behind what moves (to the right of it), fading like a reverb: its length in beats\n(whatever the layer's speed). At the header it sounds like a reverb.");
        reg.slider(trailLevel[L], "%.2f", "Trail level");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("How bright the tail starts (1: as bright as what leaves it)");
        ImGui::SeparatorText("Echo");
        { float b = echoBeats[L]; if(echoNoteCombo("Echo time", b)) echoBeats[L] = b; }
        reg.slider(echoBeats[L], "%.3f beats", "Echo beats");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The same time, free (in beats): for times between the note values, the node or a timeline. 0: off.");
        reg.slider(echoFeedback[L], "%.2f", "Echo feedback");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Each repeat's level over the one before. 0: one repeat.");
        reg.slider(echoMix[L], "%.2f", "Echo mix");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The first repeat's level (1: as bright as the original). 0: no echo.");
        reg.slider(echoLow[L], "%.2f", "Echo low-pass");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Fades the repeats' high pitches (the top of the picture) with a soft slope; each repeat is\nfiltered once more, so they get darker as they go. 1: open.");
        reg.slider(echoHigh[L], "%.2f", "Echo high-pass");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Fades the repeats' low pitches (the bottom of the picture) with a soft slope; each repeat is\nfiltered once more, so they get thinner as they go. 0: open.");
        ImGui::SeparatorText("Perlin mask");
        reg.slider(maskAmount[L],"%.2f","Mask amount");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Masks this whole layer, including its blur, trail and echo, before compositing and sonification. 0: off.");
        reg.slider(maskWidth[L],"%.3f beats","Mask width");
        reg.slider(maskHeight[L],"%.3f","Mask height");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Noise cell height as a fraction of the layer height; independent of resolution.");
        reg.slider(maskContrast[L],"%.2f","Mask contrast");
        reg.slider(maskThreshold[L],"%.2f","Mask threshold");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("0: soft Perlin mask. Above 0: cut the noise into open / closed regions.");
        reg.slider(maskBlur[L],"%.1f px","Mask blur");
        if(ImGui::IsItemHovered())ImGui::SetTooltip("Gaussian blur after the threshold, applied only to the mask. Softens its edges without smearing the layer's image. 0: off.");
        reg.check(maskInvert[L],"Invert mask");
        reg.slider(maskSeed[L],"Mask seed");
        reg.check(maskAnimate[L],"Animated");
        if(maskAnimate[L]){
            reg.slider(maskSpeed[L],"%.3f / beat","Evolution speed");
            if(ImGui::IsItemHovered())ImGui::SetTooltip("Perlin evolution per musical beat. Follows playback speed and pauses with the transport.");
        }

    }

    void draw(ControlRegistry& reg, const Post& post){
        float z = ofxOceanodeShared::getZoomLevel();
        savedWithPatchNote();
        ImGui::SameLine();
        ImGui::TextDisabled("(the layer effects on the left belong to the scene on stage and are saved with it; the rest with the patch)");
        ImGui::TextDisabled("Left of the line: effects on each layer before the sound reads it (heard and seen). Right: the final image only.\n"
                            "Right-click a control to set its value, publish it to the node or add it to a timeline.");
        if(!post.ok && post.tried) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "The shader did not compile: Output is the plain composite.");
        if(!ImGui::BeginTable("##post", 5, ImGuiTableFlags_SizingStretchSame)) return;
        ImGui::TableSetupColumn("pre", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("divider", ImGuiTableColumnFlags_WidthFixed, 34 * z);
        ImGui::TableSetupColumn("image", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("levels", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("line", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        // ---- before the sound
        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-110 * z);
        ImGui::TextColored(ImVec4(0.55f, 0.85f, 1.0f, 1), "BEFORE THE SOUND");
        ImGui::TextDisabled("each layer, heard and seen: the scene on stage's\n(saved with the scene; also under its layer stack in Stage)");
        reg.check(fxOn, "Layer effects on");
        // which layer's settings: a button per layer, lit when that layer has an effect on
        for(int l = 0; l < FX_LAYERS; l++){
            if(l) ImGui::SameLine();
            bool sel = fxLayer == l, has = anyFx(l);
            ImGui::PushStyleColor(ImGuiCol_Button, sel ? ImVec4(0.20f, 0.55f, 0.75f, 1) : has ? ImVec4(0.25f, 0.35f, 0.2f, 1) : ImVec4(0.14f, 0.20f, 0.25f, 1));
            if(ImGui::Button(("L" + ofToString(l + 1) + (has ? "*" : "") + "##fxl").c_str())) fxLayer = l;
            ImGui::PopStyleColor();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Layer %d's effects (* = it has one on)", l + 1);
        }
        int L = ofClamp(fxLayer, 0, FX_LAYERS - 1);
        drawLayerFx(reg, L, false);
        if(ImGui::SmallButton("Same for every layer")){
            for(int l = 0; l < FX_LAYERS; l++) if(l != L){ hBlur[l] = hBlur[L].get(); vBlur[l] = vBlur[L].get(); trailBeats[l] = trailBeats[L].get(); trailLevel[l] = trailLevel[L].get(); echoBeats[l] = echoBeats[L].get(); echoFeedback[l] = echoFeedback[L].get(); echoMix[l] = echoMix[L].get(); echoLow[l] = echoLow[L].get(); echoHigh[l] = echoHigh[L].get(); maskAmount[l]=maskAmount[L].get();maskWidth[l]=maskWidth[L].get();maskHeight[l]=maskHeight[L].get();maskContrast[l]=maskContrast[L].get();maskThreshold[l]=maskThreshold[L].get();maskBlur[l]=maskBlur[L].get();maskSpeed[l]=maskSpeed[L].get();maskSeed[l]=maskSeed[L].get();maskInvert[l]=maskInvert[L].get();maskAnimate[l]=maskAnimate[L].get(); }
        }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Copies layer %d's blur, trail, echo and Perlin settings to the other layers", L + 1);
        ImGui::Spacing();
        ImGui::TextDisabled("Value and Header opacity\n(right) scale the sound too.");
        ImGui::PopItemWidth();
        // ---- the line: where the sound reads the image
        ImGui::TableNextColumn();
        {
            ImVec2 p0 = ImGui::GetCursorScreenPos();
            float colW = ImGui::GetContentRegionAvail().x, hgt = 330 * z;
            auto* dl = ImGui::GetWindowDrawList();
            float x = p0.x + colW * 0.5f;
            dl->AddLine(ImVec2(x, p0.y), ImVec2(x, p0.y + hgt), IM_COL32(255, 170, 70, 220), 3 * z);
            const char* word = "SOUND READS HERE";
            float y = p0.y + 18 * z;
            for(const char* c = word; *c; c++){
                char b[2] = {*c, 0};
                if(*c != ' '){
                    ImVec2 ts = ImGui::CalcTextSize(b);
                    dl->AddRectFilled(ImVec2(x - ts.x * 0.5f - 2 * z, y - 1), ImVec2(x + ts.x * 0.5f + 2 * z, y + ts.y + 1), IM_COL32(30, 22, 12, 255));
                    dl->AddText(ImVec2(x - ts.x * 0.5f, y), IM_COL32(255, 190, 110, 255), b);
                }
                y += ImGui::GetTextLineHeight() * (*c == ' ' ? 0.6f : 1.0f);
            }
            ImGui::Dummy(ImVec2(colW, hgt));
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("The sonification reads each layer's column here: effects on the left are heard, those on the right only seen");
        }
        // ---- after the sound
        ImGui::TableNextColumn();
        ImGui::TextColored(ImVec4(0.9f, 0.8f, 0.5f, 1), "AFTER THE SOUND");
        ImGui::SameLine();
        reg.check(enabled, "on##postOn");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Post-processing of the final image (Output)");
        ImGui::PushItemWidth(-110 * z);
        ImGui::SeparatorText("Image");
        reg.check(stretch, "Stretch left of the header");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Everything left of the header shows the header column (PixelStretch)");
        ImGui::BeginDisabled(!stretch);
        reg.combo(stretchBlend, {"Replace", "Add", "Lighten", "Screen", "Multiply", "Darken"}, "Stretch blend");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Blend the stretched header column with the scrolling image beneath it.\nReplace preserves the original pixel-stretch appearance. Visual only: does not change the sound.");
        ImGui::EndDisabled();
        reg.slider(hue, "%.3f");
        reg.slider(sat, "%.2f");
        reg.slider(val, "%.2f");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Also scales the sound (the brightness the partials read)");
        reg.check(useOpacity, "Header opacity");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The header's beat-repeat fade and strobe gate darken the image");
        float bg[3] = {bgR, bgG, bgB};
        if(ImGui::ColorEdit3("Background", bg)){ bgR = bg[0]; bgG = bg[1]; bgB = bg[2]; }
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Shows where no layer draws (Output only: not heard). Black: as before.\nTo publish it: R / G / B below, right-click.");
        reg.check(paper, "Paper view");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Dark ink on paper: hue and saturation kept, lightness inverted, over the background\n(white while the background is black). Display only: not heard.");
        if(ImGui::TreeNode("Background R G B")){
            reg.slider(bgR, "%.3f", "R##bg"); reg.slider(bgG, "%.3f", "G##bg"); reg.slider(bgB, "%.3f", "B##bg");
            ImGui::TreePop();
        }
        ImGui::SeparatorText("Edge feathers");
        static const char* edgeLabel[4] = {"Left", "Right", "Top", "Bottom"};
        for(int e = 0; e < 4; e++){
            reg.slider(featherW[e], "%.3f", (std::string(edgeLabel[e]) + "##fw").c_str());
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Fades this edge toward the background (black when none) over this fraction of the screen. 0: off");
            if(featherW[e] > 0){
                reg.slider(featherK[e], "%.2f", (std::string(edgeLabel[e]) + " curve##fk").c_str());
                if(ImGui::IsItemHovered()) ImGui::SetTooltip("Shape of the fade: 1 smooth, above 1 the edge stays dark longer, below 1 softer");
            }
        }
        reg.check(featherExport, "Feather PNG exports");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Apply these edge feathers to the Gallery's separate *_look.png export too.");
        ImGui::PopItemWidth();

        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-110 * z);
        ImGui::SeparatorText("Levels");
        reg.slider(levelMin, "%.3f", "Min");
        reg.slider(levelMax, "%.3f", "Max");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Brightness that becomes white (0.617: about x1.6 with clipping)");
        reg.slider(gamma, "%.2f");
        reg.slider(low, "%.3f", "Low");
        reg.slider(high, "%.3f", "High");
        // the transfer curve
        ImVec2 p = ImGui::GetCursorScreenPos();
        float w = ImGui::CalcItemWidth(), h = 80 * z;
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(14, 14, 18, 255));
        ImVec2 prev;
        for(int i = 0; i <= 64; i++){
            float x = i / 64.0f;
            float c = std::min(std::max(x - levelMin.get(), 0.0f) / std::max(1e-4f, levelMax.get() - levelMin.get()), 1.0f);
            c = std::pow(c, 1.0f / std::max(0.01f, gamma.get()));
            c = low + (high - low) * c;
            ImVec2 q(p.x + x * w, p.y + h - c * h);
            if(i) dl->AddLine(prev, q, IM_COL32(150, 165, 255, 255), 1.5f * z);
            prev = q;
        }
        ImGui::Dummy(ImVec2(w, h));
        ImGui::PopItemWidth();

        ImGui::TableNextColumn();
        ImGui::PushItemWidth(-110 * z);
        ImGui::SeparatorText("Header line");
        reg.check(line, "Draw the line");
        reg.slider(lineWidth, "%.1f px", "Thickness");
        reg.slider(lineSoft, "%.1f px", "Softness H");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Soft edges left and right of the line");
        reg.slider(lineSoftV, "%.0f px", "Softness V");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("The line fades in from the top and bottom edges over this many pixels\n(inside the image: nothing falls outside its height)");
        reg.slider(lineAlpha, "%.2f", "Intensity");
        float col[3] = {lineR, lineG, lineB};
        if(ImGui::ColorEdit3("Colour", col)){ lineR = col[0]; lineG = col[1]; lineB = col[2]; }
        reg.slider(lineEase, "%.2f s", "Easing");
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("How long the line takes to follow a header jump (0: exactly with the header)");
        static const std::vector<std::string> curves = {"Exponential", "Linear glide", "Smooth (spring)"};
        reg.combo(lineCurve, curves, "Ease curve");
        reg.slider(lineOffset, "%.0f px", "Offset");
        ImGui::Text("line at %.0f px", post.lineX);
        ImGui::PopItemWidth();
        ImGui::EndTable();
    }
};

} // namespace forms
