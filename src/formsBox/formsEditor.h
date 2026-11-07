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
#include "formsEngine.h"
#include "formsSoundModel.h"
#include "formsGallery.h"
#include "formsPanels.h"
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
    if(f.rfind("gen/", 0) == 0) return "Instrument";
    return f;
}
inline ImVec4 familyColor(const std::string& f){
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
    std::function<void(bool)> requestNext;                  // render the next image (force)
    std::function<void(const std::string&)> renderScene;    // preview render of a scene
    // editor previews: (tag, items) variants rendered with one scene's harmony and a shared seed
    std::function<void(const std::string&, const std::vector<std::pair<std::string, json>>&)> sendPreviews;
    std::function<void()> makeThumbnails;
    std::string thumbnailsTip = "The renderer draws every legacy method once (a few minutes; renders wait meanwhile).";
    std::string previewSceneName() const;   // the scene previews use (Instruments tab), or the selected one
    std::string selectedPresetRef() const { return selPreset; }
    // Forms GL Box: Audition / Immediate switches next to Preview (null: not shown)
    bool* auditionSwitch = nullptr;
    bool* immediateSwitch = nullptr;
    std::function<void()> reload;
    std::function<void()> newSession;
    std::function<void(int, int)> setImageSize;
    // built-in player (Play tab)
    std::function<bool()> playerOn;
    std::function<const ofTexture*()> playTexture;
    std::function<float()> headerNormalized;
    std::function<void(float)> setHeaderNormalized;
    std::function<void()> drawPlayControls;
    bool* useSetlist = nullptr;
    std::function<void(const std::string&)> playScene;   // set by the node: setlist off, this scene next
    std::function<void(const std::string&)> sendRequest; // set by the node: sends a request path to a renderer
    std::function<std::string()> exportPanorama;          // set by the node: writes the panorama, returns its path or why not
    std::function<std::string(const std::string&)> lookPng; // set by the node: a saved PNG with the window's effects (path, or "")
    const float* kickLevel = nullptr;                     // the kick probe's level (Engine tab)
    std::vector<std::string> kickNames;                   // the kick folder's files (set by the node)
    std::function<void()> drawPostTab, drawSoundTab;      // set by the node: Post-processing and Sonification tabs
    std::function<void()> drawNodeTab;                    // set by the node: what is published
    // The grid overlay of the Play tab's view (filled by the node)
    struct GridInfo {
        bool valid = false;
        int root = 24, notegrid = 100, imageH = 1080, trans = 0, chordRoot = 24;
        std::vector<float> scale;
        float width = 1920;                               // the player's width (px)
        struct Span { int layer; float x, w; int bars; };  // an image on screen: layer (0..3), left edge, width (px), bars
        std::vector<Span> spans;
    };
    std::function<GridInfo()> gridInfo;
    bool showPitchGrid = false, showBeatGrid = false;
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
        headerFromMemory = false;
        headerLoader.loadFromDisk(headerImage, png);
    }
    // the same, from pixels in memory when the image has them (Forms GL Box: the PNG may not exist yet)
    void setHeaderImage(const Image& im){
        if(im.png.empty() || im.png == headerPng) return;
        if(im.rgba && im.pw > 0 && im.ph > 0 && im.rgba->size() >= (size_t)im.pw * im.ph * 4){
            headerPng = im.png;
            headerMemImage.setFromPixels(im.rgba->data(), im.pw, im.ph, OF_IMAGE_COLOR_ALPHA);   // not the loader's image
            headerFromMemory = true;
        }else setHeaderImage(im.png);
    }
    const ofTexture* headerTexture(){
        ofImage& hi = headerFromMemory ? headerMemImage : headerImage;
        return (hi.isAllocated() && hi.isUsingTexture()) ? &hi.getTexture() : nullptr;
    }

    // ------------------------------------------------ node face

    void drawNodeFace(){
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

    // ------------------------------------------------ window

    void drawWindowContent(){
        thumbs.dir = engine->rendererFolder / "thumbnails";
        thumbs.newFrame();
        if(!selected.empty() && !engine->scenes.count(selected)) selected.clear();
        if(selected.empty() && !engine->scenes.empty()) selected = engine->scenes.begin()->first;

        drawTopBar();
        ImGui::Spacing();
        if(ImGui::BeginTabBar("##formsTabs")){
            if(ImGui::BeginTabItem("Play")){ drawPlayTab(); ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Live")){ drawLiveTab(); ImGui::EndTabItem(); }
            if(drawPostTab && ImGui::BeginTabItem("Post-processing")){ drawPostTab(); ImGui::EndTabItem(); }
            if(drawSoundTab && ImGui::BeginTabItem("Sonification")){ drawSoundTab(); ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Scenes")){ drawScenesTab(); ImGui::EndTabItem(); }
            std::string ins = std::string("Instruments") + (dirtyPresets.empty() ? "" : " *") + "###instruments";
            if(ImGui::BeginTabItem(ins.c_str())){ drawInstrumentsTab(); ImGui::EndTabItem(); }
            std::string sl = std::string("Setlist") + (setlistDirty ? " *" : "") + "###setlist";
            if(ImGui::BeginTabItem(sl.c_str())){ drawSetlistTab(); ImGui::EndTabItem(); }
            std::string lib = std::string("Library") + (libraryDirty ? " *" : "") + "###library";
            if(ImGui::BeginTabItem(lib.c_str())){ drawLibraryTab(); ImGui::EndTabItem(); }
            std::string gal = std::string("Gallery") + (engine->replaying ? "  (replaying)" : "") + "###gallery";
            if(ImGui::BeginTabItem(gal.c_str())){ drawGalleryTab(); ImGui::EndTabItem(); }
            if(ImGui::BeginTabItem("Engine")){ drawEngineTab(); ImGui::EndTabItem(); }
            if(drawNodeTab && ImGui::BeginTabItem("Node")){ drawNodeTab(); ImGui::EndTabItem(); }
            ImGui::EndTabBar();
        }
        drawPickerPopup();
    }

private:
    ofxThreadedImageLoader headerLoader;
    ofImage headerMemImage;          // the header image from memory (Forms GL Box)
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
    char presetFilter[64] = "";
    bool justSelected = false;
    int instrumentsFrame = -10;      // the last frame the tab was drawn (opening it: frame gap)
    std::string playingFor;          // the playing scene the tab last followed
    bool useUnderlay = false;
    bool flipView = false;
    std::string stackFor, procTab;
    char libFilterS[64] = "", libFilterP[64] = "";
    int stackLayer = 0;
    int sceneLayer = 0;                 // Scenes tab: the layer being edited
    std::string sceneLayerFor;  // the scroller FBO shows the right way up as is
    char renameBuf[128] = "";
    std::map<std::string, std::array<char, 256>> listBufs;
    ofImage previewImg;
    json previewValues = json::object();
    int seenPreviewCount = 0;

    float z() const { return ofxOceanodeShared::getZoomLevel(); }

    // ------------------------------------------------ top bar: now playing

    void drawTopBar(){
        float zz = z();
        float pw = 256 * zz, ph = pw * 9 / 16;
        ImGui::BeginGroup();
        ImVec2 p = ImGui::GetCursorScreenPos();
        auto* dl = ImGui::GetWindowDrawList();
        dl->AddRectFilled(p, ImVec2(p.x + pw, p.y + ph), IM_COL32(10, 10, 12, 255), 4 * zz);
        if(auto* t = headerTexture()) ImGui::Image(texId(*t), ImVec2(pw, ph));
        else ImGui::Dummy(ImVec2(pw, ph));
        ImGui::EndGroup();
        ImGui::SameLine();
        ImGui::BeginGroup();
        auto& L = engine->schedule->layers[0];
        ImGui::TextDisabled("UNDER THE HEADER");
        ImGui::SetWindowFontScale(1.35f);
        ImGui::Text("%s", L.header ? L.header->scene.c_str() : "-");
        ImGui::SetWindowFontScale(1.0f);
        if(L.header) ImGui::TextDisabled("image %s  |  %d bars  |  %d BPM  |  %s  |  %s", L.header->id.c_str(), L.header->bars, L.header->bpm,
                                         L.header->scale.c_str(), L.header->palette.c_str());
        ImGui::Spacing();
        const Scene* next = engine->currentScene();
        bool sl = useSetlist && *useSetlist;
        if(sl || !playScene) ImGui::Text("Next: %s", next ? next->name.c_str() : "(no scene)");
        else{
            // setlist off: choose the scene to play here
            ImGui::TextUnformatted("Next:");
            ImGui::SameLine();
            ImGui::SetNextItemWidth(220 * zz);
            if(ImGui::BeginCombo("##nextScene", next ? next->name.c_str() : "(no scene)")){
                for(auto& [name, sc] : engine->scenes)
                    if(ImGui::Selectable(name.c_str(), next && next->name == name)) playScene(name);
                ImGui::EndCombo();
            }
        }
        if(ImGui::Checkbox("Use setlist", &sl) && useSetlist) *useSetlist = sl;
        ImGui::SameLine();
        if(ImGui::Button("Render next now")) requestNext(true);
        ImGui::Spacing();
        ImGui::TextDisabled("%d x %d  |  %d rendering  %d ready  shown %d  repeats %d  skipped %d",
                            imageW, imageH, (int)engine->pending.size(), (int)L.ready.size(), L.shown, L.repeats, engine->skipped);
        ImGui::EndGroup();
    }

    // ------------------------------------------------ scenes

    void drawScenesTab();
    void drawLiveTab();

    // The layer stack: top of the stack first. Each row: select, name (bars, blend), opacity,
    // up / down in the stack, kick. Below: the selected layer's bars, blend and kick, + layer,
    // remove. Opacity, blend and order apply live to what is playing.
    bool drawLayerStack(Scene& s);

    // The scene's performance settings (sound levels, stereo, speed modulation, gate, tremolo,
    // header jumps, beat repeat): they take effect when its images reach the header
    bool drawPerformance(Scene& s);

    void newScene(){
        selected = engine->createScene();
        reload();
    }

    int countGroup(const LayerDef& l, int g){
        int n = 0;
        for(int i = 0; i < std::min<int>(l.rows, (int)l.cells.size()); i++) if(!l.cells[i].item.empty() && l.cells[i].group == g) n++;
        return n;
    }

    // The 1920 scenes group methods by family; new methods get the group most used for their family
    int groupForFamily(const std::string& m){ return engine->groupForItem(m); }

    std::string familyOf(const std::string& m);

    bool drawCellCards(Scene& s, LayerDef& l);

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

    void drawMethodsTab();

    void drawPickerPopup();

    void drawBrowser(const std::function<void(const std::string&)>& pick);

    // ------------------------------------------------ instruments (parameterised generators)

    // Colour of a generator family in the Instruments list
    static ImVec4 genColour(const std::string& gen){
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
                    if(!nv.empty()){ v = nv.size() == 1 ? json(nv[0]) : json{{"options", nv}}; changed = true; }
                }
            }
            ImGui::EndGroup();
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("several ticked: one of them per image");
        }else if(p.type == "bool"){
            int mode = v.is_boolean() ? (v.get<bool>() ? 1 : 0) : 2;
            const char* modes[] = {"off", "on", "either (per image)"};
            ImGui::TableNextColumn();
            ImGui::SetNextItemWidth(-FLT_MIN);
            if(ImGui::Combo("##v", &mode, modes, 3)){ v = mode == 2 ? json{{"either", true}} : json(mode == 1); changed = true; }
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
    void drawPlayTab();

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

    void drawEngineTab();

    void drawSizeControl();
};

inline std::string Editor::previewSceneName() const { return !previewScene.empty() ? previewScene : selected; }

} // namespace forms

// The tabs' code (methods of Editor)
#include "formsEditorScenes.h"
#include "formsEditorInstruments.h"
#include "formsEditorGallery.h"
#include "formsEditorTabs.h"
