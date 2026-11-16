//
//  formsEditor.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The Forms Box window: scene editor, method browser (thumbnails), setlist editor, library of
//  scales and palettes, engine status. Edits change the engine's scenes immediately (so a
//  preview render uses them) and are written to disk with Save.
//

#pragma once

#include "ofMain.h"
#include "ofxOceanodeShared.h"
#include "ofxThreadedImageLoader.h"
#include "imgui.h"
#include "formsUiNotes.h"
#include "formsEngine.h"
#include "formsSoundModel.h"
#include "formsGallery.h"
#include "formsPanels.h"
#include "formsTheme.h"
#include "formsUndo.h"
#include "formsPerform.h"
#include "formsHarmonyText.h"
#include "formsAssistantV1.h"
#include "ofxOceanodeAIAssistant.h"
#include <future>
#include <array>
#include <functional>
#include <set>

namespace forms {

// ---------------------------------------------------------------- textures

inline ImTextureID texId(const ofTexture& t){ return (ImTextureID)(uintptr_t)t.getTextureData().textureID; }

// Method thumbnails (renderer folder /thumbnails/<method>.png), loaded on demand, a few per frame
class ThumbCache {
public:
    fs::path dir;
    void newFrame(){ budget = 6; }
    void clear(){ images.clear(); missing.clear(); }
    const ofTexture* get(const std::string& name){
        auto it = images.find(name);
        if(it != images.end()) return &it->second.getTexture();
        if(missing.count(name) || budget <= 0) return nullptr;
        budget--;
        fs::path p = dir / (name + ".png");
        if(!fs::exists(p)){ missing.insert(name); return nullptr; }
        ofImage& img = images[name];
        if(!img.load(p.string())){ images.erase(name); missing.insert(name); return nullptr; }
        return &img.getTexture();
    }
    bool any() const { return !images.empty(); }
    void forget(const std::string& name){ images.erase(name); missing.erase(name); }
private:
    std::map<std::string, ofImage> images;
    std::set<std::string> missing;
    int budget = 6;
};

// Drawn before the compared item in previews (the test harness underlay)
// clouds first (the background), then the grid and the iridescence on top, as in the tests
inline const char* UNDERLAY[] = {"t3_clouds", "r12_fullEuclidean", "h2_iridescence"};

inline ImU32 hexColor(const std::string& hex, int alpha = 255){
    unsigned v = 0;
    try { v = std::stoul(hex, nullptr, 16); } catch(...) {}
    return IM_COL32((v >> 16) & 0xFF, (v >> 8) & 0xFF, v & 0xFF, alpha);
}

inline std::string familyLabel(const std::string& f){
    if(f == "legacy/h") return "Harmony";
    if(f == "legacy/r") return "Rhythm";
    if(f == "legacy/t") return "Texture";
    if(f == "legacy/fx") return "FX";
    if(f.rfind("cat/", 0) == 0) return categoryLabel(f.substr(4));
    if(f.rfind("gen/", 0) == 0) return "Instrument";
    return f;
}
// a method family ("legacy/h", "cat/pads") as the legacy family it belongs to: legacy/h, legacy/r, legacy/t, legacy/g, legacy/fx
inline std::string familyRoot(const std::string& f){
    if(f.rfind("cat/", 0) == 0) return "legacy/" + categoryFamily(f.substr(4));
    return f;
}
inline ImVec4 familyColor(const std::string& f){
    if(theme::enabled()) return theme::rgb(theme::familyHex(theme::familyOfGen(f.rfind("gen/", 0) == 0 ? f.substr(4) : familyRoot(f))));
    if(f.rfind("cat/", 0) == 0){
        std::string c = f.substr(4);
        if(c == "bass") return ImVec4(0.95f, 0.60f, 0.20f, 1);
        if(c == "melody") return ImVec4(0.98f, 0.85f, 0.35f, 1);
        if(c == "chords") return ImVec4(0.90f, 0.70f, 0.30f, 1);
        if(c == "pads") return ImVec4(0.85f, 0.75f, 0.45f, 1);
        if(c == "arps") return ImVec4(1.00f, 0.80f, 0.20f, 1);
        if(c == "geometry") return ImVec4(0.55f, 0.90f, 0.60f, 1);
        return familyColor(familyRoot(f));
    }
    if(f == "legacy/h") return ImVec4(0.95f, 0.75f, 0.25f, 1);
    if(f == "legacy/r") return ImVec4(0.95f, 0.40f, 0.35f, 1);
    if(f == "legacy/t") return ImVec4(0.45f, 0.75f, 0.95f, 1);
    if(f == "legacy/fx") return ImVec4(0.70f, 0.55f, 0.95f, 1);
    if(f.rfind("gen/", 0) == 0) return ImVec4(0.35f, 0.90f, 0.75f, 1);
    return ImVec4(0.7f, 0.7f, 0.7f, 1);
}

// ---------------------------------------------------------------- the editor

class Editor {
public:
    Engine* engine = nullptr;
    ofParameter<bool>* autoGo = nullptr;
    ofParameter<float>* barMult = nullptr;                  // the node's Bar Multiplier / Bar Add (Stage > Layers)
    ofParameter<int>* barAdd = nullptr;
    ofParameter<float>* density = nullptr;                  // the node's Density (Stage > Layers)
    std::function<void(const std::string&)> nodeParamMenu;  // right-click menu of a node setting (publish, timeline)
    // set by the node: why no new images are being asked for ("" while they are), and how to resume
    std::function<std::string()> noNewImagesWhy;
    std::function<void()> resumeImages;
    std::function<void(bool)> requestNext;                  // render the next image (force)
    std::function<void()> previousImage;                    // layer 1's previous image back under the header
    std::function<bool()> canPreviousImage;
    std::function<void(const std::string&)> renderScene;    // preview render of a scene
    // editor previews: (tag, items) variants rendered with one scene's harmony and a shared seed
    std::function<void(const std::string&, const std::vector<std::pair<std::string, json>>&)> sendPreviews;
    std::function<void()> makeThumbnails;
    std::string thumbnailsTip = "The renderer draws every legacy method once (a few minutes; renders wait meanwhile).";
    std::string previewSceneName() const;   // the scene previews use (Instruments tab), or the selected one
    std::string selectedPresetRef() const { return selPreset; }
    void editScene(const std::string& name){selected=name;stageFollow=false;showTab="Scenes";}
    bool drawLayerEffectPreset(Scene& scene,int layer);
    std::function<bool(Scene&)> browseKickSample;
    std::function<bool(Scene&,const std::string&)> selectKickSample;
    std::string kickSampleError;
    // Forms GL Box: Audition / Immediate switches next to Preview (null: not shown)
    bool* auditionSwitch = nullptr;
    bool* immediateSwitch = nullptr;
    std::function<void()> reload;
    std::function<void()> newSession;
    std::function<void(int, int)> setImageSize;
    // the transport panel (drawn by the node)
    std::function<void()> drawPlayControls;
    bool* useSetlist = nullptr;
    std::function<void(const std::string&)> playScene;   // set by the node: setlist off, this scene next
    std::function<void(const std::string&)> sendRequest; // set by the node: sends a request path to a renderer
    std::function<std::string()> exportPanorama;          // set by the node: writes the panorama, returns its path or why not
    std::function<std::string(const std::string&)> lookPng; // set by the node: a saved PNG with the window's effects (path, or "")
    const float* kickLevel = nullptr;                     // the kick probe's level (System tab)
    std::vector<std::string> kickNames;                   // the kick folder's files (set by the node)
    std::function<void()> drawPostTab, drawSoundTab, drawExportTab; // node-owned tabs
    std::function<void()> drawNodeTab;                    // set by the node: what is published (System tab)
    std::function<void()> drawRendererPanel;              // set by the node: the renderer's state (System tab)
    std::function<void(float)> drawKickMeter;   // set by the node: the kick strip's level now, with this threshold marked
    ofParameter<bool>* layerFxOn = nullptr;                 // the node's "Layer effects on"
    bool drawLayerFxScene(LayerDef& L, int layer);          // a scene layer's own effects (LayerDef::fx)
    bool auditioning() const { return auditionSwitch && *auditionSwitch; }
    // mute / solo (set by the node; performance state, not in the scene): bit 1 muted, bit 2 soloed
    std::function<int(int)> layerMS;
    std::function<void(int, int)> toggleLayerMS;              // layer, 0 mute / 1 solo
    std::function<int(const std::string&)> instMS;
    std::function<void(const std::string&, int)> toggleInstMS; // preset ref, 0 mute / 1 solo
    std::function<std::string()> muteSummary;                 // "" when nothing is muted / soloed
    std::function<void()> clearMuteSolo;
    // a small M / S toggle (lit when on); true when clicked
    bool msButton(const char* label, bool on, bool solo, const char* tip){
        ImVec4 c = solo ? ImVec4(0.85f, 0.70f, 0.15f, 1) : ImVec4(0.85f, 0.30f, 0.25f, 1);
        ImGui::PushStyleColor(ImGuiCol_Button, on ? c : ImVec4(0.18f, 0.20f, 0.24f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? c : ImVec4(0.26f, 0.28f, 0.34f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.05f, 0.05f, 0.05f, 1) : ImVec4(0.8f, 0.8f, 0.8f, 1));
        bool r = ImGui::SmallButton(label);
        ImGui::PopStyleColor(3);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        return r;
    }
    // M and S for an instrument (preset ref), on one line
    void instMSButtons(const std::string& ref){
        if(!instMS || !toggleInstMS) return;
        int st = instMS(ref);
        ImGui::PushID(ref.c_str());
        if(msButton("M", st & 1, false, "Mute this instrument: the pages on stage are drawn again without it (not saved)")) toggleInstMS(ref, 0);
        ImGui::SameLine(0, 2);
        if(msButton("S", st & 2, true, "Solo: only the soloed instruments are drawn (not saved)")) toggleInstMS(ref, 1);
        ImGui::PopID();
    }
    std::string panoramaResult;
    int imageW = 1920, imageH = 1080;

    ThumbCache thumbs;
    std::string selected;                  // scene being edited
    std::set<std::string> dirty;           // scenes with unsaved edits
    std::set<std::string> dirtyPresets;    // instrument presets with unsaved edits
    bool setlistDirty = false, libraryDirty = false;
    std::string importPath;   // Import from 1920 (the node sets it: the 1920 patch in the data folder)

    // the image under the header, for the previews
    void setHeaderImage(const std::string& png){
        if(png.empty() || png == headerPng) return;
        headerPng = png;
        headerGpuTexture.reset();
        headerFromMemory = false;
        headerLoader.loadFromDisk(headerImage, png);
    }
    // the same, from pixels in memory when the image has them (Forms GL Box: the PNG may not exist yet)
    void setHeaderImage(const Image& im){
        if(im.png.empty() || (im.png == headerPng && (!im.texture || im.texture == headerGpuTexture))) return;
        if(im.texture){ headerPng = im.png; headerGpuTexture = im.texture; return; }
        headerGpuTexture.reset();
        if(im.rgba && im.pw > 0 && im.ph > 0 && im.rgba->size() >= (size_t)im.pw * im.ph * 4){
            headerPng = im.png;
            headerMemImage.setFromPixels(im.rgba->data(), im.pw, im.ph, OF_IMAGE_COLOR_ALPHA);   // not the loader's image
            headerFromMemory = true;
        }else if(im.stageRgba && im.stageW > 0 && im.stageH > 0 && im.stageRgba->size() >= (size_t)im.stageW * im.stageH * 4){
            headerPng = im.png;
            headerMemImage.setFromPixels(im.stageRgba->data(), im.stageW, im.stageH, OF_IMAGE_COLOR_ALPHA);
            headerFromMemory = true;
        }else setHeaderImage(im.png);
    }
    const ofTexture* headerTexture(){
        if(headerGpuTexture) return headerGpuTexture.get();
        if(headerFromMemory) return (headerMemImage.isAllocated() && headerMemImage.isUsingTexture()) ? &headerMemImage.getTexture() : nullptr;
        return (headerImage.isAllocated() && headerImage.isUsingTexture()) ? &headerImage.getTexture() : nullptr;
    }

    // ------------------------------------------------ node face

    void drawAutoGo(bool compact = false){
        if(!autoGo) return;
        bool on = autoGo->get();
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.15f, 0.60f, 0.32f, 1) : ImVec4(0.42f, 0.23f, 0.12f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.20f, 0.72f, 0.40f, 1) : ImVec4(0.56f, 0.32f, 0.16f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
        if(ImGui::Button(on ? "Auto GO: ON" : "Auto GO: OFF", compact ? ImVec2(0, 0) : ImVec2(240 * z(), 32 * z()))) *autoGo = !on;
        ImGui::PopStyleColor(3);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Automatically request fresh image seeds on all layers.\nOff: playback continues; queued images may still arrive.\nGO / Render next now still requests an image manually. Saved with the patch.");
    }

    void drawNodeFace(){
        drawAutoGo();
        float z = ofxOceanodeShared::getZoomLevel();
        float w = 240 * z, h = w * 9 / 16;
        ImVec2 p = ImGui::GetCursorScreenPos();
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + w, p.y + h), IM_COL32(12, 12, 14, 255), 4 * z);
        if(auto* t = headerTexture()){
            ImGui::Image(texId(*t), ImVec2(w, h));
        }else{
            ImGui::Dummy(ImVec2(w, h));
            dl->AddText(ImVec2(p.x + 8 * z, p.y + h / 2 - 7 * z), IM_COL32(120, 120, 120, 255), "waiting for the first image");
        }
        dl->AddRect(p, ImVec2(p.x + w, p.y + h), IM_COL32(70, 70, 80, 255), 4 * z);
        auto& L = engine->schedule->layers[0];
        std::string now = L.header ? L.header->scene : "-";
        const Scene* next = engine->currentScene();
        // wrapped to the preview's width, so the node does not grow with long scene names
        ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + w);
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", now.c_str());
        auto coming = engine->predictedPlan(engine->layer1NextBar + 1);   // the lane's choice included
        ImGui::TextDisabled("next %s", !coming.empty() ? coming[0].scene.c_str() : next ? next->name.c_str() : "-");
        ImGui::TextDisabled("%d ready, %d rendering", (int)L.ready.size(), (int)engine->pending.size());
        if(double load = engine->renderLoad(); load > 1.0){
            ImGui::TextColored(load > 1.25 ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(1, 0.75f, 0.3f, 1), "renderer %.0f%% busy", load * 100);
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("The scene's layers need new images faster than the renderer makes them (%.1f s each).\n"
                                                         "Above 100%% an extra layer can stay empty after a hard cut until its image comes.\nRepeat on some layers helps.", engine->renderSeconds);
        }
        ImGui::PopTextWrapPos();
    }

    // ------------------------------------------------ where the user stands
    // One colour per level of the tab map: main tabs blue, their sections teal, a section's sub-tabs violet.
    // shade 1 = the lit tab / button, 2 = hovered
    static ImVec4 levelColor(int level, int shade){
        static const ImVec4 c[3][3] = {
            {ImVec4(0.55f, 0.80f, 1.00f, 1), ImVec4(0.25f, 0.50f, 0.70f, 1), ImVec4(0.30f, 0.58f, 0.80f, 1)},
            {ImVec4(0.50f, 0.90f, 0.80f, 1), ImVec4(0.16f, 0.50f, 0.45f, 1), ImVec4(0.20f, 0.60f, 0.54f, 1)},
            {ImVec4(0.80f, 0.65f, 1.00f, 1), ImVec4(0.42f, 0.32f, 0.66f, 1), ImVec4(0.50f, 0.40f, 0.76f, 1)}};
        return c[std::max(0, std::min(level, 2))][std::max(0, std::min(shade, 2))];
    }
    // a sub-tab bar in its level's colour (1 = a main tab's sections, 2 = a section's sub-tabs)
    void pushTabLevel(int level){
        ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.13f, 0.15f, 0.19f, 1));
        ImGui::PushStyleColor(ImGuiCol_TabHovered, levelColor(level, 2));
        ImGui::PushStyleColor(ImGuiCol_TabActive, levelColor(level, 1));
    }
    void popTabLevel(){ ImGui::PopStyleColor(3); }

    // ------------------------------------------------ window

    void drawWindowContent(){
        thumbs.dir = engine->rendererFolder / "thumbnails";
        thumbs.newFrame();
        if(!selected.empty() && !engine->scenes.count(selected)) selected.clear();
        if(selected.empty() && !engine->scenes.empty()) selected = engine->scenes.begin()->first;

        applyInstAction();
        drawTopBar();
        // the main tabs: taller and blue, so the first level reads apart from the sub-tabs (teal, then violet)
        float zt = z();
        auto mainStyle = [zt](){
            ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(16 * zt, 7 * zt));
            ImGui::PushStyleColor(ImGuiCol_Tab, ImVec4(0.13f, 0.15f, 0.19f, 1));
            ImGui::PushStyleColor(ImGuiCol_TabHovered, levelColor(0, 2));
            ImGui::PushStyleColor(ImGuiCol_TabActive, levelColor(0, 1));
        };
        auto mainStyleEnd = [](){ ImGui::PopStyleColor(3); ImGui::PopStyleVar(); };
        // a main tab: its content is drawn with the editor's own style
        auto mainTab = [&](const char* label, ImGuiTabItemFlags flags, const std::function<void()>& content){
            if(!ImGui::BeginTabItem(label, nullptr, flags)) return;
            mainStyleEnd();
            content();
            mainStyle();
            ImGui::EndTabItem();
        };
        mainStyle();
        bool mainBar = ImGui::BeginTabBar("##formsTabs");
        if(!mainBar) mainStyleEnd();
        if(mainBar){
            // Stage (what plays, all at once) | composing | the output; Gallery and System on the right edge
            bool toStage = showTab == "Stage" || showTab == "Scenes";
            mainTab("Stage", toStage ? ImGuiTabItemFlags_SetSelected : 0, [this](){ drawStageTab(); });
            // Compose: scenes, instruments and the library in one tab, a mode switch at the top
            bool toCompose = showTab == "Compose" || showTab == "Instruments" || showTab == "Library";
            std::string comp = std::string("Library") + ((dirtyPresets.empty() && !libraryDirty) ? "" : " *") + (auditioning() ? "  (audition)" : "") + "###compose";
            mainTab(comp.c_str(), toCompose ? ImGuiTabItemFlags_SetSelected : 0, [this](){ drawComposeTab(); });
            // Programme: the setlist and the gallery of pages (replay, freeze, promote)
            bool toProgramme = showTab == "Programme" || showTab == "Setlist" || showTab == "Gallery";
            std::string prog = std::string("Programme") + (setlistDirty ? " *" : "") + (engine->replaying ? "  (replaying)" : "") + "###programme";
            mainTab(prog.c_str(), toProgramme ? ImGuiTabItemFlags_SetSelected : 0, [this](){ drawProgrammeTab(); });
            // Output: post-processing, the sound engine, export (the node's own panels)
            if(drawPostTab || drawSoundTab || drawExportTab){
                bool toOutput = showTab == "Output" || showTab == "Post-processing" || showTab == "Sonification" || showTab == "Export";
                mainTab("Output###output", toOutput ? ImGuiTabItemFlags_SetSelected : 0, [this](){ drawOutputTab(); });
            }
            std::string as = std::string("Assistant") + (aiAuditioning ? "  (Keep / Discard)" : "") + "###assistant";
            mainTab(as.c_str(), 0, [this](){ drawAssistantTab(); });
            mainTab("System", ImGuiTabItemFlags_Trailing, [this](){ drawSystemTab(); });
            showTab.clear();   // a requested tab is selected once
            ImGui::EndTabBar();
            mainStyleEnd();
        }
        drawPickerPopup();
        drawInstDeleteModal();
        drawInstrumentWindow();
        drawCompareWindow();
        // undo / redo keys while the window has focus (not while typing)
        if(ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) && !ImGui::GetIO().WantTextInput){
            if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiMod_Shift | ImGuiKey_Z)) doUndo(true);
            else if(ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Z)) doUndo(false);
        }
        trackUndo();
    }

    // ------------------------------------------------ v1 backports (formsEditorBackports.h)
    UndoStack undo;
    PerformState perform;
    std::function<void(bool)> setHardCut;   // set by the node: its Hard Cut setting
    std::function<void()> switchScene;      // set by the node: Switch now (the next scene replaces what plays)
    std::function<bool()> getHardCut;
    ofxOceanodeAIAssistant ai;
    json aiSaved;                            // the assistant's settings from the patch (model)
    void tick();                             // every frame, from the node's update
    void drawFrameStats();
    bool drawHarmonyText(Scene& s);

private:
    ofxThreadedImageLoader headerLoader;
    ofShortImage headerMemImage;     // the header image from memory (Forms GL Box): 16 bits per channel
    std::shared_ptr<ofTexture> headerGpuTexture;
    bool headerFromMemory = false;
    ofImage headerImage;
    std::string headerPng;

    // picker popup state
    bool openPicker = false;
    std::function<void(const std::string&)> onPick;
    char search[128] = "";
    int familyFilter = 0;      // 0 all, 1 h, 2 r, 3 t, 4 fx, 5 unused
    float thumbSize = 150;
    std::string libSelScale, libSelPalette;
    std::string bankFilter = "*";   // Scenes tab: the bank listed ("*" all)
    char newBankBuf[64] = "";
    int editW = 1920, editH = 1080;
    bool sizeEditing = false;
    // instruments tab
    std::string selPreset, renameFor, previewScene, previewShownRef, listActive;
    std::string sceneInstrumentEditRef; // Scenes tab: preset opened from a cell's context menu
    // The scene whose own version of that instrument the window edits ("" = the shared instrument,
    // as the Library edits it). Set when the window is opened, so "follow what plays" cannot retarget it.
    std::string sceneInstrumentEditScene;
    void openInstrument(const std::string& ref, const std::string& scene = ""){ sceneInstrumentEditRef = ref; sceneInstrumentEditScene = engine->scenes.count(scene) ? scene : std::string(); }
    std::string sceneStackFor;
    int sceneStackLayer = 0;
    char presetFilter[64] = "";
    bool justSelected = false;
    std::string showTab;             // a tab to bring to the front on the next frame ("Scenes")
    int instrumentsFrame = -10;      // the last frame the tab was drawn (opening it: frame gap)
    std::string playingFor;          // the playing scene the tab last followed
    bool useUnderlay = false;
    std::string stackFor, procTab;
    char libFilterS[64] = "", libFilterP[64] = "";
    int stackLayer = 0;
    int sceneLayer = 0;                 // Scenes tab: the layer being edited
    std::string sceneLayerFor;  // the scroller FBO shows the right way up as is
    char renameBuf[128] = "";
    char saveAsBuf[128] = ""; std::string saveAsError;
    std::map<std::string, std::array<char, 256>> listBufs;
    ofImage previewImg;
    json previewValues = json::object();

    float z() const { return ofxOceanodeShared::getZoomLevel(); }

    // v1 backports (formsEditorBackports.h)
    bool undoWasActive = false, compareOpen = false;
    int undoIdle = 0;
    char harmonyBuf[256] = "";
    std::string harmonyFor;
    std::array<float, 240> frameMs {};
    size_t frameAt = 0;
    bool aiSetup = false, aiKey = false, aiBusy = false, aiRetried = false, aiAuditioning = false, aiIsPreset = true, aiAuditionWas = false;
    int aiMode = 0;
    char aiPrompt[2048] = "";
    std::future<ofxOceanodeAIAssistant::TextResult> aiFuture;
    std::string aiStatus, aiAnswer, aiUser, aiTempRef, aiTargetRef, aiSelBefore;
    std::vector<std::string> aiProblems, aiNotes;   // problems of a rejected answer; slips fixed in an accepted one
    json aiProposal, aiBefore;
    void trackUndo();
    void doUndo(bool redo);
    void drawUndoButtons();
    void drawCompareWindow();
    void queueScene(const std::string& name);
    void queueSetlistEntry(int i);
    void storeSnapshot(int k);
    void recallSnapshot(int k);
    bool pad(const std::string& label, const std::string& sub, int state, ImVec2 size, ImU32 accent);
    static bool aiKeyPresent();
    void setupAssistant();
    std::string assistCurrent() const;
    void sendAssist(const std::string& user, bool retry);
    void pollAssistant();
    void handleAssistAnswer(const std::string& text);
    void startAssistAudition(const json& j);
    void endAssistAudition(bool keep);
    void drawAssistantTab();

    // ------------------------------------------------ top bar: now playing

    void drawImmediateButton(){
        if(!immediateSwitch) return;
        float zz = z();
        bool on = *immediateSwitch;
        ImGui::PushStyleColor(ImGuiCol_Button, on ? ImVec4(0.85f, 0.45f, 0.10f, 1) : ImVec4(0.20f, 0.20f, 0.24f, 1));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? ImVec4(0.95f, 0.55f, 0.15f, 1) : ImVec4(0.28f, 0.28f, 0.34f, 1));
        ImGui::PushStyleColor(ImGuiCol_Text, on ? ImVec4(0.05f, 0.05f, 0.05f, 1) : ImVec4(0.85f, 0.85f, 0.85f, 1));
        if(ImGui::Button(on ? "LIVE EDITS  on" : "Live edits  off")) *immediateSwitch = !on;
        ImGui::PopStyleColor(3);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Live edits (was Immediate). On: every edit goes straight into the composition. The pages on screen and waiting are\n"
                                                     "drawn again in place: an instrument's parameters, a scene's palette, scale, note grid, bass degree\n"
                                                     "and its layers' instrument cells. Redrawn pages overwrite their saved files.\n"
                                                     "Act at once anyway: BPM, root, opacity, blend, post-processing, sound, mute / solo.\n"
                                                     "From the next page only: harmony degrees, bars, repeat.");
        (void)zz;
    }
    void drawTopBar(){
        float zz = z();
        auto& L = engine->schedule->layers[0];
        float th = 34 * zz, tw = th * 16 / 9;
        ImVec2 p = ImGui::GetCursorScreenPos();
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + tw, p.y + th), IM_COL32(10, 10, 12, 255), 3 * zz);
        if(auto* t = headerTexture()) ImGui::Image(texId(*t), ImVec2(tw, th)); else ImGui::Dummy(ImVec2(tw, th));
        if(ImGui::IsItemHovered()){
            if(L.header) ImGui::SetTooltip("image %s  |  %g bars  |  %g BPM  |  %s  |  %s\n%d x %d  |  %d rendering  %d ready  shown %d  repeats %d  skipped %d",
                                           L.header->id.c_str(), L.header->bars, L.header->bpm, L.header->scale.c_str(), L.header->palette.c_str(),
                                           imageW, imageH, (int)engine->pending.size(), (int)L.ready.size(), L.shown, L.repeats, engine->skipped);
            else ImGui::SetTooltip("waiting for the first image");
        }
        ImGui::SameLine(0, 10 * zz);
        ImGui::BeginGroup();
        // line 1: the scene under the header, the next one, the alerts
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1, 1, 1, 1), "%s", L.header ? L.header->scene.c_str() : "-");
        const Scene* next = engine->currentScene();
        auto coming = engine->predictedPlan(engine->layer1NextBar + 1);
        ImGui::SameLine(0, 12 * zz);
        bool sl = useSetlist && *useSetlist;
        if(sl || !playScene) ImGui::TextDisabled("next: %s", !coming.empty() ? coming[0].scene.c_str() : next ? next->name.c_str() : "-");
        else{
            ImGui::TextDisabled("next:"); ImGui::SameLine();
            ImGui::SetNextItemWidth(180 * zz);
            if(ImGui::BeginCombo("##nextScene", next ? next->name.c_str() : "(no scene)")){
                for(auto& [name, sc] : engine->scenes) if(ImGui::Selectable(name.c_str(), next && next->name == name)) playScene(name);
                ImGui::EndCombo();
            }
        }
        if(double load = engine->renderLoad(); load > 1.0){ ImGui::SameLine(0, 12 * zz); ImGui::TextColored(load > 1.25 ? ImVec4(1, 0.4f, 0.3f, 1) : ImVec4(1, 0.75f, 0.3f, 1), "renderer %.0f%% busy", load * 100); }
        if(muteSummary){ std::string ms = muteSummary(); if(!ms.empty()){ ImGui::SameLine(0, 12 * zz); ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1), "%s", ms.c_str()); ImGui::SameLine(); if(clearMuteSolo && ImGui::SmallButton("clear")) clearMuteSolo(); } }
        if(auditioning()){ ImGui::SameLine(0, 12 * zz); ImGui::TextColored(ImVec4(1, 0.55f, 0.2f, 1), "AUDITION: the edited instrument alone on stage"); }
        drawUnsaved();
        // line 2: the switches
        drawAutoGo(true);
        ImGui::SameLine(0, 8 * zz);
        if(ImGui::Checkbox("Setlist", &sl) && useSetlist) *useSetlist = sl;
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Follow the setlist (off: choose the next scene here or with the pads)");
        ImGui::SameLine(0, 8 * zz);
        drawImmediateButton();
        ImGui::SameLine(0, 8 * zz);
        if(ImGui::Button("Next image now")) requestNext(true);
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Ask for the next image at once: palette, scale and harmony edits show sooner");
        if(previousImage){
            ImGui::SameLine(0, 4 * zz);
            bool can = canPreviousImage && canPreviousImage();
            ImGui::BeginDisabled(!can);
            if(ImGui::Button("Previous image")) previousImage();
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
                ImGui::SetTooltip("Bring back the image layer 1 played before this one: it takes the place of the one playing, now.\n"
                                  "Press again to go one further back (the last 4 are kept, within the scene that plays).\n"
                                  "With Auto GO on, new images follow when the page ends; with it off the recovered image stays.");
        }
        ImGui::SameLine(0, 12 * zz);
        drawUndoButtons();
        ImGui::EndGroup();
        drawNoNewImages();
    }
    // The image on stage repeats when nothing asks for a new one: say so, and why, where it cannot be missed
    void drawNoNewImages(){
        std::string why = noNewImagesWhy ? noNewImagesWhy() : std::string();
        if(why.empty()) return;
        ImGui::AlignTextToFramePadding();
        ImGui::TextColored(ImVec4(1, 0.45f, 0.2f, 1), "No new images: %s. The image on stage repeats.", why.c_str());
        if(resumeImages){
            ImGui::SameLine(0, 10 * z());
            if(ImGui::Button("Resume images")) resumeImages();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Turns Auto GO and the session sequence back on; the session timer starts again from 0");
        }
    }
    // What is edited and not yet in its file, in one place, with one button to keep it all
    void saveAllUnsaved(){
        for(auto& n : std::set<std::string>(dirty)){ auto it = engine->scenes.find(n); if(it != engine->scenes.end()) engine->saveScene(it->second); }
        dirty.clear();
        for(auto& r : std::set<std::string>(dirtyPresets)){ auto it = engine->presets.find(r); if(it != engine->presets.end()) engine->savePreset(it->second); }
        dirtyPresets.clear();
        if(setlistDirty){ engine->saveSetlist(); setlistDirty = false; }
        if(libraryDirty){ engine->saveLibrary(); libraryDirty = false; }
    }
    void drawUnsaved(){
        if(dirty.empty() && dirtyPresets.empty() && !setlistDirty && !libraryDirty) return;
        float zz = z();
        std::string what;
        auto add = [&](const std::string& x){ what += (what.empty() ? "" : ", ") + x; };
        if(!dirty.empty()) add(ofToString(dirty.size()) + (dirty.size() == 1 ? " scene" : " scenes"));
        if(!dirtyPresets.empty()) add(ofToString(dirtyPresets.size()) + (dirtyPresets.size() == 1 ? " instrument" : " instruments"));
        if(setlistDirty) add("the setlist");
        if(libraryDirty) add("scales / palettes");
        ImGui::SameLine(0, 12 * zz);
        ImGui::TextColored(ImVec4(1, 0.7f, 0.25f, 1), "unsaved: %s", what.c_str());
        if(ImGui::IsItemHovered()){
            std::string list;
            for(auto& n : dirty) list += "scene  " + n + "\n";
            for(auto& r : dirtyPresets) list += "instrument  " + r + "\n";
            if(setlistDirty) list += "the setlist\n";
            if(libraryDirty) list += "scales / palettes\n";
            ImGui::SetTooltip("%s", list.c_str());
        }
        ImGui::SameLine(0, 6 * zz);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.75f, 0.45f, 0.1f, 1));
        if(ImGui::SmallButton("Save all")) saveAllUnsaved();
        ImGui::PopStyleColor();
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("Write every edited scene, instrument, the setlist and the library to their files");
    }
    // ------------------------------------------------ scenes

    // Compose: Scenes | Instruments | Library behind one tab (the three old tabs' bodies)
    int composeMode = 0;   // 0 scenes, 1 instruments, 2 library
    int programmeMode = 0; // 0 setlist, 1 gallery
    int outputMode = 0;    // 0 post-processing, 1 sound engine, 2 export
    // a row of mode buttons (the lit one is the mode); labels with a dirty mark where asked
    void modeButtons(int& mode, const std::vector<std::pair<std::string, bool>>& modes, const char* hint){
        float zz = z();
        for(int i = 0; i < (int)modes.size(); i++){
            if(i) ImGui::SameLine(0, 2 * zz);
            bool on = mode == i;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? levelColor(1, 1) : ImVec4(0.16f, 0.18f, 0.22f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? levelColor(1, 2) : ImVec4(0.22f, 0.26f, 0.32f, 1));
            std::string lab = modes[i].first + (modes[i].second ? " *" : "");
            if(ImGui::Button(lab.c_str(), ImVec2(130 * zz, 0))) mode = i;
            ImGui::PopStyleColor(2);
        }
        if(hint && *hint){ ImGui::SameLine(0, 16 * zz); ImGui::TextDisabled("%s", hint); }
        ImGui::Separator();
    }
    void drawProgrammeTab(){
        if(showTab == "Setlist") programmeMode = 0; else if(showTab == "Gallery") programmeMode = 1;
        modeButtons(programmeMode, {{"Setlist", setlistDirty}, {"Gallery", false}},
                    programmeMode == 0 ? "the order of the concert: scenes, images per entry, hard cuts" : "every page drawn so far: replay a session, freeze a page as a layer, promote one to a scene");
        if(programmeMode == 0) drawSetlistTab(); else drawGalleryTab();
    }
    void drawOutputTab(){
        if(showTab == "Post-processing") outputMode = 0; else if(showTab == "Sonification") outputMode = 1; else if(showTab == "Export") outputMode = 2;
        std::vector<std::pair<std::string, bool>> modes;
        std::vector<int> which;
        if(drawPostTab){ modes.push_back({"Post-processing", false}); which.push_back(0); }
        if(drawSoundTab){ modes.push_back({"Sound engine", false}); which.push_back(1); }
        if(drawExportTab){ modes.push_back({"Export", false}); which.push_back(2); }
        int idx = 0; for(int i = 0; i < (int)which.size(); i++) if(which[i] == outputMode) idx = i;
        modeButtons(idx, modes, outputMode == 0 ? "the image after the layers: masks, feathers, levels, the header line (saved with the patch)"
                               : outputMode == 1 ? "the sound of the whole node: partials, FX, EQ, compressor, kick samples, ducking (saved with the patch; a scene's own sound is in Stage > Sound)"
                               : "render the setlist to video and audio");
        outputMode = which.empty() ? 0 : which[std::max(0, std::min(idx, (int)which.size() - 1))];
        if(outputMode == 0 && drawPostTab) drawPostTab(); else if(outputMode == 1 && drawSoundTab) drawSoundTab(); else if(outputMode == 2 && drawExportTab) drawExportTab();
    }
    // Library (was Compose): the instruments and the scales / palettes every scene picks from. Scenes
    // are edited in Stage only.
    void drawComposeTab(){
        if(showTab == "Instruments") composeMode = 1; else if(showTab == "Library") composeMode = 2;
        if(composeMode < 1 || composeMode > 2) composeMode = 1;
        float zz = z();
        const char* names[3] = {"", "Instruments", "Scales & palettes"};
        bool dirtyMode[3] = {false, !dirtyPresets.empty(), libraryDirty};
        for(int i = 1; i < 3; i++){
            if(i > 1) ImGui::SameLine(0, 2 * zz);
            bool on = composeMode == i;
            ImGui::PushStyleColor(ImGuiCol_Button, on ? levelColor(1, 1) : ImVec4(0.16f, 0.18f, 0.22f, 1));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, on ? levelColor(1, 2) : ImVec4(0.22f, 0.26f, 0.32f, 1));
            std::string lab = std::string(names[i]) + (dirtyMode[i] ? " *" : "");
            if(ImGui::Button(lab.c_str(), ImVec2(150 * zz, 0))) composeMode = i;
            ImGui::PopStyleColor(2);
        }
        ImGui::SameLine(0, 16 * zz);
        ImGui::TextDisabled(composeMode == 1 ? "every instrument preset: browse, edit, preview, add to the scene being edited in Stage"
                            : "the scales and palettes every scene picks from");
        ImGui::Separator();
        if(composeMode == 1) drawInstrumentsTab(); else drawLibraryTab();
    }
    // Stage: the output, the scene under the header in six cards (layers, colour & harmony, levels &
    // stereo, live faders, movement, kick) and the scene pads, on one screen (formsEditorStage.h)
    void drawStageTab();
    // The one scene editor (Stage): the scene being edited is the one playing while "follow what plays"
    // is ticked, else any scene picked from the list.
    bool stageFollow = true;
    std::string stageSelectedSeen;   // the scene the editor showed last: a change made elsewhere (Gallery, Assistant, New) is kept
    bool layerSettingsBelow = false; // drawLayerStack: the selected layer's settings under the stack (not beside it)
    bool stageShowOutput = true;   // Stage's left column: the show's settings (every scene, transport)
    void drawSceneWorkspace(const std::string& playing);
    void drawSceneList();
    bool drawSceneHeader(Scene& s);
    bool drawLayerInstruments(Scene& s);
    bool drawGlobalEffects(Scene& s);   // the scene's effects on every layer
    // the instrument editor, in its own floating window (sceneInstrumentEditRef: the instrument it shows)
    void drawInstrumentWindow();
    bool drawSceneLook(Scene& s);                       // palette, scale, note grid, BPM, root, bass degree, harmony
    // right-click menus (formsEditorMenus.h) and their clipboard
    json clipValue; std::string clipKey;
    void sceneFieldMenu(Scene& s, const char* id, const json& value, const json& def, const std::function<void(Scene&, const json&)>& set, bool& changed);
    void perfParamMenu(Scene& s, const PerfParam& p, json& v, bool& changed);
    void instrumentParamMenu(const ParamDef& p, json& v, bool& changed, bool& erase);
    // The one right-click menu of an instrument, wherever it is listed: mute / solo, edit, send to a layer
    // (move, when it sits in a scene's cell; add, from the library), remove from the layer, delete.
    // scene / layer / cell: the cell it is listed from ("" / -1 in the library). The change is done at
    // the start of the next frame (the lists are being drawn when the menu is clicked).
    struct InstAction { int kind = 0; std::string scene, ref; int layer = -1, cell = -1, target = -1; };   // 1 move, 2 add, 3 remove from layer
    InstAction instAction;
    std::string instDeleteAsk;      // a preset to delete, once confirmed
    void instrumentMenuItems(const std::string& ref, const std::string& scene = "", int layer = -1, int cell = -1);
    bool instrumentMenu(const char* id, const std::string& ref, const std::string& scene = "", int layer = -1, int cell = -1);
    void applyInstAction();
    void drawInstDeleteModal();
    bool drawSceneSound(Scene& s, bool columns = false); // levels & stereo, movement, kick: three columns, or folds
    bool drawKickSettings(Scene& s);                    // the scene's kick (Stage and Compose)
    bool drawPerfParam(Scene& s, const PerfParam& p, float itemWidth = 0);   // one performance setting
    bool drawPerformanceCard(Scene& s, const std::vector<std::string>& keys, const std::vector<std::string>& more);
    // Only the settings a scene changes are shown: those that differ from their default, and those just
    // added with "+ add setting" (kept per scene while the editor is open). Nothing is removed: every
    // setting is one pick away.
    bool drawPerformanceSparse(Scene& s, const std::vector<std::string>& keys, const char* id);
    std::map<std::string, std::set<std::string>> perfShown;   // scene -> settings shown although at default
    void drawLiveFaders();
    void drawScenePads();

    // The layer stack: top of the stack first. Each row: select, name (bars, blend), opacity,
    // up / down in the stack, kick. Below: the selected layer's bars, blend and kick, + layer,
    // remove. Opacity, blend and order apply live to what is playing.
    bool drawLayerStack(Scene& s, bool compact = false);   // compact: the rows only (Stage)

    // The scene's performance settings (sound levels, stereo, speed modulation, gate, tremolo,
    // header jumps, beat repeat): they take effect when its images reach the header

    void newScene(){
        selected = engine->createScene();
        reload();
    }

    int countGroup(const LayerDef& l, int g){
        int n = 0;
        for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(!l.cells[i].item.empty() && l.cells[i].group == g) n++;
        return n;
    }

    std::string familyOf(const std::string& m);


    static std::string shortName(const std::string& m){
        if(isPresetRef(m)){
            std::string s = m.substr(m.find('/') + 1);
            return s.size() > 22 ? s.substr(0, 21) + "…" : s;
        }
        auto u = m.find('_');
        std::string s = u == std::string::npos ? m : m.substr(u + 1);
        return s.size() > 22 ? s.substr(0, 21) + "…" : s;
    }

    void methodTooltip(const std::string& m){
        ImGui::BeginTooltip();
        if(const ofTexture* t = thumbs.get(m)) ImGui::Image(texId(*t), ImVec2(384 * z() * 0.8f, 216 * z() * 0.8f));
        std::string f = familyOf(m);
        ImGui::TextColored(familyColor(f), "%s", m.c_str());
        ImGui::TextDisabled("%s  |  used in %d scenes", familyLabel(f).c_str(), engine->scenesUsing(m));
        ImGui::EndTooltip();
    }

    bool paletteCombo(const char* label, std::string& ref){
        bool changed = false;
        float zz = z();
        const PaletteDef* cur = engine->findPalette(ref);
        ImGui::SetNextItemWidth(220 * zz);
        if(ImGui::BeginCombo(label, cur ? cur->name.c_str() : ref.c_str())){
            for(auto& p : engine->paletteLib){
                ImVec2 pos = ImGui::GetCursorScreenPos();
                if(ImGui::Selectable(("        " + p.name).c_str(), cur && cur->name == p.name)){ ref = p.name; changed = true; }
                swatches(p, pos, 5 * zz);
            }
            ImGui::EndCombo();
        }
        if(cur){ ImVec2 pos = ImGui::GetCursorScreenPos(); swatches(*cur, pos, 14 * zz, true); ImGui::Dummy(ImVec2(80 * zz, 14 * zz)); }
        return changed;
    }

    void swatches(const PaletteDef& p, ImVec2 pos, float s, bool big = false){
        auto* dl = ImGui::GetWindowDrawList();
        for(int i = 0; i < (int)p.colors.size() && i < 8; i++)
            dl->AddRectFilled(ImVec2(pos.x + i * (s + 1), pos.y + 2), ImVec2(pos.x + i * (s + 1) + s, pos.y + 2 + (big ? s : 11 * z())), hexColor(p.colors[i]));
    }

    bool scaleCombo(const char* label, std::string& ref){
        bool changed = false;
        const ScaleDef* cur = engine->findScale(ref);
        ImGui::SetNextItemWidth(220 * z());
        if(ImGui::BeginCombo(label, cur ? cur->name.c_str() : ref.c_str())){
            for(auto& d : engine->scaleLib){
                if(ImGui::Selectable(d.name.c_str(), cur && cur->name == d.name)){ ref = d.name; changed = true; }
                ImGui::SameLine(200 * z());
                ImGui::TextDisabled("%s", notesText(d.notes).c_str());
            }
            ImGui::EndCombo();
        }
        if(cur) ImGui::TextDisabled("notes %s", notesText(cur->notes).c_str());
        return changed;
    }

    static std::string notesText(const std::vector<int>& n){
        std::string s;
        for(size_t i = 0; i < n.size(); i++) s += (i ? " " : "") + ofToString(n[i]);
        return s;
    }

    // 12 draggable bars (the Director's weight sliders)
    bool weightBars(const char* label, std::vector<float>& w, int used, bool degrees, const ScaleDef* sd){
        float zz = z();
        if(w.size() < 12) w.resize(12, 0);
        float width = 12 * 22 * zz, height = 48 * zz;
        ImGui::Text("%s", label);
        ImGui::PushID(label);
        ImVec2 p = ImGui::GetCursorScreenPos();
        ImGui::InvisibleButton("##bars", ImVec2(width, height));
        bool changed = false;
        if(ImGui::IsItemActive()){
            ImVec2 m = ImGui::GetIO().MousePos;
            int i = (int)((m.x - p.x) / (width / 12));
            if(i >= 0 && i < 12){ w[i] = ofClamp(1.0f - (m.y - p.y) / height, 0, 1); changed = true; }
        }
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), ImGui::GetColorU32(ImGuiCol_FrameBg));
        float sw = width / 12;
        for(int i = 0; i < 12; i++){
            bool inScale = !degrees || i < used;
            ImU32 col = inScale ? ImGui::GetColorU32(ImGuiCol_PlotHistogram) : IM_COL32(90, 90, 90, 255);
            dl->AddRectFilled(ImVec2(p.x + i * sw + 1, p.y + height * (1 - w[i])), ImVec2(p.x + (i + 1) * sw - 2, p.y + height), col);
            std::string t = degrees ? (sd && i < (int)sd->notes.size() ? ofToString(sd->notes[i]) : "") : "+" + ofToString(i);
            dl->AddText(ImVec2(p.x + i * sw + 3, p.y + height + 1), IM_COL32(150, 150, 150, 255), t.c_str());
        }
        ImGui::Dummy(ImVec2(width, 14 * zz));
        if(degrees) ImGui::TextDisabled("chance of each scale degree as the bar's root (grey: outside the scale, wraps)");
        else ImGui::TextDisabled("chance of each transpose in semitones");
        ImGui::PopID();
        return changed;
    }

    // ------------------------------------------------ methods browser / picker


    void drawPickerPopup();

    void drawBrowser(const std::function<void(const std::string&)>& pick);

    // ------------------------------------------------ instruments (parameterised generators)

    // Colour of a generator family in the Instruments list
    static ImVec4 genColour(const std::string& gen){
        if(theme::enabled()) return theme::rgb(theme::familyHex(theme::familyOfGen(gen)));   // the design system's family colours
        if(gen == "pulse") return ImVec4(0.95f, 0.75f, 0.25f, 1);
        if(gen == "hits") return ImVec4(0.95f, 0.45f, 0.38f, 1);
        if(gen == "masses") return ImVec4(0.55f, 0.75f, 1.00f, 1);
        if(gen == "sweep") return ImVec4(0.60f, 0.90f, 0.85f, 1);
        if(gen == "shapes") return ImVec4(0.85f, 0.60f, 1.00f, 1);
        if(gen == "effects") return ImVec4(0.75f, 0.75f, 0.78f, 1);
        if(gen == "stack") return ImVec4(0.55f, 0.95f, 0.55f, 1);
        return ImVec4(0.7f, 0.7f, 0.7f, 1);
    }

    void drawInstrumentsTab();
    void drawSceneInstrumentEditor(std::string ref);
    struct SceneUse { std::string ref; int layer; float prob; int depth; int cell = -1; };
    // includeOff: also cells at 0 % (an editable list keeps a row it is dragging to 0)
    std::vector<SceneUse> sceneUses(const std::string& sceneName, bool includeOff = false) const;
    // the cell's chance per image as a drag at the right of a list row; marks the scene unsaved
    bool cellChanceDrag(const std::string& sceneName, const SceneUse& u, float width);
    // the cell's volume (gain 0..1, shown as %) next to it; marks the scene unsaved
    bool cellGainDrag(const std::string& sceneName, const SceneUse& u, float width);
    // move a cell of a scene layer to another cell's place (drawing order); marks the scene unsaved
    bool moveSceneCell(const std::string& sceneName, int layer, int from, int to);
    bool moveSceneCellToLayer(const std::string& sceneName, int layer, int cell, int target);
    float instListW = 340;   // Instruments tab: list width (resizable)

    // The parameters in tabs, one per group ("pattern", "pitch", "cell"...). The process groups share
    // one "processes" tab with a list of the processes on the left. A dot marks the tabs (and
    // processes) where the preset sets something.
    // A menu of presets by generator (inside an open combo or popup); returns the picked ref
    std::string presetMenu(const char* label, const std::string& exclude);

    // Linked picks as JSON text: [{"steps": 4, "patternSteps": 8}, {"steps": 6, "patternSteps": 12, "weight": 2}]
    std::map<std::string, std::string> linkedText;
    std::map<std::string, std::string> linkedError;
    std::string linkedFor;
    bool drawLinkedEditor(json& linked, const std::string& key);

    bool drawParamTabs(const GenDef& g, json& params);
    std::string groupNotInUse(const GenDef& g, const std::string& grp, const json& params);
    bool drawCoverParams(json& params);
    bool drawStackBlocking(json& params);

    bool beginParamTable(){
        float zz = z();
        if(!ImGui::BeginTable("##prows", 4, ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) return false;
        ImGui::TableSetupColumn("label", ImGuiTableColumnFlags_WidthStretch, 0.85f);
        ImGui::TableSetupColumn("vary", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFrameHeight());
        ImGui::TableSetupColumn("value", ImGuiTableColumnFlags_WidthStretch, 1.5f);
        ImGui::TableSetupColumn("reset", ImGuiTableColumnFlags_WidthFixed, 18 * zz);
        return true;
    }

    // One parameter (a table row): label, 'vary' tick, control, reset. Writes into params; true if it changed.
    bool paramRow(const ParamDef& p, json& params){
        float zz = z();
        bool changed = false, erase = false;
        bool set = params.contains(p.key);
        json v = set ? params[p.key] : p.def;
        ImGui::PushID(p.key.c_str());
        // one table row: label | vary | value | reset (the table clips each column, so nothing overlaps)
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        ImGui::AlignTextToFramePadding();
        if(set) ImGui::TextUnformatted(p.label.c_str());
        else ImGui::TextDisabled("%s", p.label.c_str());
        if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s\n(%s, %s)", p.label.c_str(), p.key.c_str(), p.type.c_str());
        ImGui::TableNextColumn();

        if(p.isNumber() && v.is_object() && v.contains("options")){
            // a list of values, one picked per image
            std::string txt;
            for(auto& o : v["options"]) if(o.is_number()) txt += (txt.empty() ? "" : ", ") + ofToString(o.get<float>());
            auto& buf = listBufs[p.key];
            if(listActive != p.key) snprintf(buf.data(), buf.size(), "%s", txt.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##v", buf.data(), buf.size());
            if(ImGui::IsItemActive()) listActive = p.key;
            else if(listActive == p.key) listActive.clear();
            if(ImGui::IsItemDeactivatedAfterEdit()){
                json l = json::array();
                for(auto& tok : ofSplitString(buf.data(), ",", true, true)){
                    try { float f = std::stof(tok); l.push_back(p.type == "int" ? json((int)std::lround(f)) : json(f)); } catch(...) {}
                }
                if(l.size() == 1) v = l[0];
                else if(!l.empty()) v = json{{"options", l}};
                changed = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("one of these values per image (comma-separated)");
        }else if(p.isNumber()){
            bool vary = v.is_object();
            if(ImGui::Checkbox("##vary", &vary)){
                float x = v.is_number() ? v.get<float>() : (v.is_object() ? v.value("min", p.min) : p.min);
                if(vary){
                    float span = (p.max - p.min) * 0.1f;
                    if(p.type == "int") span = std::max(1.0f, std::round(span));
                    v = json{{"min", x}, {"max", std::min(p.max, x + span)}};
                }else v = x;
                changed = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("vary: a new value from the range for each image");
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(p.type == "int"){
                if(vary){
                    int a = (int)std::lround(v.value("min", p.min)), b = (int)std::lround(v.value("max", p.max));
                    if(ImGui::DragIntRange2("##v", &a, &b, 0.15f, (int)p.min, (int)p.max)){ v = json{{"min", a}, {"max", b}}; changed = true; }
                }else{
                    int x = v.is_number() ? (int)std::lround(v.get<float>()) : (int)p.min;
                    if(ImGui::SliderInt("##v", &x, (int)p.min, (int)p.max)){ v = x; changed = true; }
                }
            }else{
                float sp = (p.max - p.min) / 400.0f;
                if(vary){
                    float a = v.value("min", p.min), b = v.value("max", p.max);
                    if(ImGui::DragFloatRange2("##v", &a, &b, sp, p.min, p.max, "%.3g", "%.3g", ImGuiSliderFlags_AlwaysClamp)){ v = json{{"min", a}, {"max", b}}; changed = true; }
                }else{
                    float x = v.is_number() ? v.get<float>() : p.min;
                    if(ImGui::SliderFloat("##v", &x, p.min, p.max, "%.3g")){ v = x; changed = true; }
                }
            }
        }else if(p.type == "enum"){
            std::vector<std::string> on;
            if(v.is_string()) on.push_back(v.get<std::string>());
            else if(v.is_object()) for(auto& o : v.value("options", json::array())) if(o.is_string()) on.push_back(o.get<std::string>());
            // The cover mode (eraser / black) is also the cover's switch: with the cover off nothing is
            // ticked (the instrument draws normally), ticking one turns the cover on in that mode, and
            // unticking the last one turns it off ("Cover enabled" above follows).
            const bool coverMode = p.key == "coverMode";
            bool coverOff = false;
            if(coverMode){
                auto en = params.find("coverEnable");
                coverOff = en == params.end() || (en->is_boolean() && !en->get<bool>()) || (en->is_number() && en->get<double>() < .5);
                if(coverOff) on.clear();
            }
            ImGui::TableNextColumn();
            ImGui::BeginGroup();
            float x0 = ImGui::GetCursorPosX(), maxX = x0 + ImGui::GetContentRegionAvail().x;
            for(size_t i = 0; i < p.options.size(); i++){
                bool b = std::find(on.begin(), on.end(), p.options[i]) != on.end();
                if(i){
                    float next = ImGui::GetItemRectMax().x - ImGui::GetWindowPos().x + ImGui::GetStyle().ItemSpacing.x
                               + ImGui::CalcTextSize(p.options[i].c_str()).x + ImGui::GetFrameHeight() * 1.5f;
                    if(next < maxX) ImGui::SameLine();
                }
                if(ImGui::Checkbox(p.options[i].c_str(), &b)){
                    std::vector<std::string> nv;
                    for(auto& o : p.options){
                        bool isOn = o == p.options[i] ? b : std::find(on.begin(), on.end(), o) != on.end();
                        if(isOn) nv.push_back(o);
                    }
                    if(!nv.empty()){ v = nv.size() == 1 ? json(nv[0]) : json{{"options", nv}}; changed = true; if(coverMode && coverOff) params["coverEnable"] = true; }
                    else if(coverMode){ params["coverEnable"] = false; changed = true; }   // none ticked: no cover
                }
            }
            ImGui::EndGroup();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip(coverMode ? "none ticked: no cover (the instrument draws normally); both ticked: one of them per image" : "several ticked: one of them per image");
        }else if(p.type == "bool"){
            int mode = v.is_boolean() ? (v.get<bool>() ? 1 : 0) : 2;
            const char* modes[] = {"off", "on", "either (per image)"};
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::Combo("##v", &mode, modes, 3)){ v = mode == 2 ? json{{"either", true}} : json(mode == 1); changed = true; }
        }else if(p.isRange() && v.is_object() && v.contains("options")){
            // a list of ranges, one picked per image: "0 : 1, 0.1 : 0.75"
            std::string txt;
            for(auto& o : v["options"]) if(o.is_array() && o.size() >= 2 && o[0].is_number() && o[1].is_number())
                txt += (txt.empty() ? "" : ", ") + ofToString(o[0].get<float>()) + " : " + ofToString(o[1].get<float>());
            auto& buf = listBufs[p.key];
            if(listActive != p.key) snprintf(buf.data(), buf.size(), "%s", txt.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##v", buf.data(), buf.size());
            if(ImGui::IsItemActive()) listActive = p.key;
            else if(listActive == p.key) listActive.clear();
            if(ImGui::IsItemDeactivatedAfterEdit()){
                json l = json::array();
                bool ints = p.type == "intRange";
                for(auto& tok : ofSplitString(buf.data(), ",", true, true)){
                    auto ab = ofSplitString(tok, ":", true, true);
                    try {
                        float a = ofClamp(std::stof(ab.at(0)), p.min, p.max), b = ofClamp(std::stof(ab.size() > 1 ? ab[1] : ab[0]), p.min, p.max);
                        if(b < a) std::swap(a, b);
                        l.push_back(ints ? json{(int)std::lround(a), (int)std::lround(b)} : json{a, b});
                    } catch(...) {}
                }
                if(l.size() == 1) v = l[0];
                else if(!l.empty()) v = json{{"options", l}};
                changed = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("one of these ranges per image, as  low : high  separated by commas\n(a single range turns it back into the two-handle slider)");
        }else if(p.isRange()){
            float a = v.is_array() && v.size() > 0 ? v[0].get<float>() : p.min;
            float b = v.is_array() && v.size() > 1 ? v[1].get<float>() : a;
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(p.type == "intRange"){
                int ia = (int)std::lround(a), ib = (int)std::lround(b);
                if(ImGui::DragIntRange2("##v", &ia, &ib, 0.15f, (int)p.min, (int)p.max)){ v = json{ia, ib}; changed = true; }
            }else{
                if(ImGui::DragFloatRange2("##v", &a, &b, (p.max - p.min) / 400.0f, p.min, p.max, "%.3g", "%.3g", ImGuiSliderFlags_AlwaysClamp)){ v = json{a, b}; changed = true; }
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("per element: the generator draws a value in this range for each step / note");
        }else if(p.type == "intList"){
            // "1, 2, 3" is a list; "5, 7 | -5" is {"options": [[5, 7], [-5]]}: one list per image
            auto listText = [](const json& l){
                std::string t;
                if(l.is_array()) for(size_t i = 0; i < l.size(); i++) t += (i ? ", " : "") + ofToString(l[i].is_number() ? (int)std::lround(l[i].get<float>()) : 0);
                return t;
            };
            std::string txt;
            if(v.is_array()) txt = listText(v);
            else if(v.is_object() && v.contains("options") && v["options"].is_array())
                for(size_t i = 0; i < v["options"].size(); i++) txt += (i ? " | " : "") + listText(v["options"][i]);
            auto& buf = listBufs[p.key];
            if(listActive != p.key) snprintf(buf.data(), buf.size(), "%s", txt.c_str());
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            ImGui::InputText("##v", buf.data(), buf.size());
            if(ImGui::IsItemActive()) listActive = p.key;
            else if(listActive == p.key) listActive.clear();
            if(ImGui::IsItemDeactivatedAfterEdit()){
                json alts = json::array();
                for(auto& part : ofSplitString(buf.data(), "|", true, true)){
                    json l = json::array();
                    for(auto& tok : ofSplitString(part, ",", true, true)){
                        try { l.push_back(std::stoi(tok)); } catch(...) {}
                    }
                    alts.push_back(l);
                }
                if(alts.size() > 1) v = json{{"options", alts}};
                else v = alts.empty() ? json::array() : alts[0];
                changed = true;
            }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("comma-separated whole numbers (%g..%g); separate lists with | to pick one per image", p.min, p.max);
        }else{
            ImGui::TableNextColumn();
            ImGui::TextDisabled("(%s)", p.type.c_str());
        }

        instrumentParamMenu(p, v, changed, erase);   // right-click on the control
        ImGui::TableNextColumn();
        if(set){
            if(ImGui::SmallButton("x")) erase = true;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("back to the default (%s)", p.def.dump().c_str());
        }
        ImGui::PopID();
        if(erase){ params.erase(p.key); return true; }
        if(changed) params[p.key] = v;
        return changed;
    }

    // The last preview: picture and the values the image was made with
    void drawPreviewPane(const std::string& ref);

    // A finished preview: show it, keep the values it used, write the preset's thumbnail
    void pollPreview(){
        while(!engine->previewsDone.empty()){
            auto [tag, png] = engine->previewsDone.front();
            engine->previewsDone.pop_front();
            if(tag.rfind("print", 0) == 0){
                // Gallery prints: the raw render stays; the one listed has the layer effects and the post look
                std::string shown = png;
                if(lookPng){ std::string lp = lookPng(png); if(!lp.empty()) shown = lp; }
                printsDone.push_front({tag, shown}); while(printsDone.size() > 10) printsDone.pop_back();
                continue;
            }
            showPreview(tag, png);
        }
    }

    void showPreview(const std::string& tag, const std::string& png){
        ofImage img;
        if(!img.load(png)) return;
        if(img.getPixels().getNumChannels() == 4){
            // straight alpha → over black, like the scroller shows it
            ofPixels rgb;
            const ofPixels& src = img.getPixels();
            rgb.allocate(src.getWidth(), src.getHeight(), OF_PIXELS_RGB);
            const unsigned char* s = src.getData();
            unsigned char* d = rgb.getData();
            size_t n = src.getWidth() * src.getHeight();
            for(size_t i = 0; i < n; i++){
                unsigned a = s[i * 4 + 3];
                d[i * 3] = s[i * 4] * a / 255; d[i * 3 + 1] = s[i * 4 + 1] * a / 255; d[i * 3 + 2] = s[i * 4 + 2] * a / 255;
            }
            img.setFromPixels(rgb);
        }
        if(tag.rfind("legacy:", 0) == 0){

            return;
        }
        previewImg = img;
        previewShownRef = tag;
        previewValues = json::object();
        try {
            fs::path side = fs::path(png).replace_extension(".json");
            json sc = readJsonFile(side);
            const json items = sc.value("items", json::array());
            for(auto& it : items) if(it.contains("gen")) previewValues = it.value("params", json::object());
        } catch(...) {}
        if(!previewShownRef.empty()){
            ofImage th = img;
            th.resize(384, 216);
            fs::path tp = thumbs.dir / (previewShownRef + ".png");
            std::error_code ec;
            fs::create_directories(tp.parent_path(), ec);
            th.save(tp.string());
            thumbs.forget(previewShownRef);
        }
    }

    // Adds a method (or preset) to the scene selected in the Scenes tab
    void addMethodToScene(const std::string& m){
        auto it = engine->scenes.find(selected);
        if(it == engine->scenes.end() || it->second.layers.empty()) return;
        int li = (sceneLayerFor == selected) ? ofClamp(sceneLayer, 0, (int)it->second.layers.size() - 1) : 0;
        if(!engine->addItemToScene(it->second, li, m)) return;
        dirty.insert(selected);
        engine->note("added " + m + " to " + selected + ", layer " + ofToString(li + 1));
    }

    // ------------------------------------------------ gallery: archived sessions

    Gallery gallery;
    bool galleryScanned = false;
    std::deque<std::pair<std::string, std::string>> printsDone;   // (tag, png) of finished prints
    int galleryPick = -1, galleryLayer = 0;
    char galleryFilter[64] = "";
    ofImage galleryBig;
    std::string galleryBigFor;
    json galleryRecipe;

    void drawGalleryTab();

    // ------------------------------------------------ play: the scroller and its controls

    // The built-in player's picture with the header line (drag it to move the header) and its
    // controls (drawn by Forms Box)

    // ------------------------------------------------ setlist

    void drawSetlistTab();

    // ------------------------------------------------ library

    void drawLibraryTab();

    template<typename T>
    void libraryList(std::vector<T>& lib, std::string& sel, bool isScale){
        float zz = z();
        if(ImGui::Button(isScale ? "New##s" : "New##p")){
            T d; d.name = isScale ? "new scale" : "new palette";
            for(int i = 2; std::any_of(lib.begin(), lib.end(), [&](const T& x){ return x.name == d.name; }); i++) d.name = (isScale ? "new scale " : "new palette ") + ofToString(i);
            lib.push_back(d); sel = d.name; libraryDirty = true;
        }
        ImGui::SameLine();
        if(ImGui::Button(isScale ? "Duplicate##s" : "Duplicate##p")){
            for(auto& d : lib) if(d.name == sel){
                T c = d; c.name = d.name + " copy"; c.legacyIndex = -1;
                if constexpr (std::is_same<T, ScaleDef>::value) c.source.clear();
                lib.push_back(c); sel = c.name; libraryDirty = true; break;
            }
        }
        ImGui::SameLine();
        int used = 0;
        for(auto& [n, s] : engine->scenes) if((isScale ? s.scale : s.palette) == sel) used++;
        bool fromCatalog = false;
        if constexpr (std::is_same<T, ScaleDef>::value) for(auto& d : lib) if(d.name == sel && !d.source.empty()) fromCatalog = true;
        ImGui::BeginDisabled(used > 0 || fromCatalog);
        if(ImGui::Button(isScale ? "Delete##s" : "Delete##p")){
            lib.erase(std::remove_if(lib.begin(), lib.end(), [&](const T& x){ return x.name == sel; }), lib.end());
            sel.clear(); libraryDirty = true;
        }
        ImGui::EndDisabled();
        if(used > 0 && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Used by %d scenes", used);
        char* filt = isScale ? libFilterS : libFilterP;
        ImGui::SetNextItemWidth(-1);
        ImGui::InputTextWithHint(isScale ? "##fs" : "##fp", "filter", filt, 64);
        std::string q = ofToLower(filt);
        ImGui::BeginChild(isScale ? "##sl" : "##pl", ImVec2(0, 220 * zz), ImGuiChildFlags_Borders);
        for(auto& d : lib){
            if(!q.empty() && ofToLower(d.name).find(q) == std::string::npos) continue;
            ImVec2 p = ImGui::GetCursorScreenPos();
            std::string label = isScale ? d.name : "            " + d.name;
            bool cat = false;
            if constexpr (std::is_same<T, ScaleDef>::value) cat = !d.source.empty();
            if(cat) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.55f, 0.8f, 1, 1));
            if(ImGui::Selectable((label + "##" + d.name).c_str(), sel == d.name)) sel = d.name;
            if(cat) ImGui::PopStyleColor();
            if constexpr (std::is_same<T, ScaleDef>::value) if(ImGui::IsItemHovered()) ImGui::SetTooltip("%s%s", notesText(d.notes).c_str(), cat ? ("   (" + d.source + ")").c_str() : "");
            if constexpr (std::is_same<T, PaletteDef>::value) swatches(d, p, 10 * zz);
        }
        ImGui::EndChild();
    }

    bool nameEditor(std::string& name, bool isScale){
        char buf[128];
        strncpy(buf, name.c_str(), sizeof(buf)); buf[sizeof(buf) - 1] = 0;
        ImGui::SetNextItemWidth(220 * z());
        if(ImGui::InputText("Name (Enter)", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue)){
            std::string nn = buf;
            bool taken = false;
            if(isScale){ for(auto& d : engine->scaleLib) if(d.name == nn) taken = true; }
            else { for(auto& d : engine->paletteLib) if(d.name == nn) taken = true; }
            if(!nn.empty() && !taken){
                engine->renameLibraryRefs(isScale, name, nn);
                (isScale ? libSelScale : libSelPalette) = nn;
                name = nn;
                libraryDirty = true;
                return true;
            }
        }
        return false;
    }

    // ------------------------------------------------ engine

    void drawSystemTab();

    void drawSizeControl();
    void drawEveryScene();   // Stage, left column: bar multiplier / add, density, image size (the patch's, not a scene's)
};

inline std::string Editor::previewSceneName() const { return !previewScene.empty() ? previewScene : selected; }

} // namespace forms

// The tabs' code (methods of Editor)
#include "formsEditorScenes.h"
#include "formsEditorInstruments.h"
#include "formsEditorGallery.h"
#include "formsEditorTabs.h"
#include "formsEditorBackports.h"
#include "formsEditorStage.h"
#include "formsEditorMenus.h"
