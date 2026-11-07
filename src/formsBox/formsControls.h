//
//  formsControls.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Forms Box's control registry: every setting of the window's tabs is an ofParameter registered
//  here with a key (its escaped name). Any of them can be published to the node with a
//  right-click (and unpublished, unless it is connected), as in the SuperCollider box synths.
//  Values are saved with the patch (all of them, published or not); the published list is
//  restored before the patch's cables (loadBeforeConnections), so connections survive.
//
//  Patches from before this (no "formsPublished" saved) get the old node back: the legacy set of
//  parameters and outputs, published.
//

#pragma once

#include "ofMain.h"
#include "imgui.h"
#include "ofxOceanodeNodeModel.h"
#include "ofxOceanodeShared.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <type_traits>
// the SuperCollider addon's window menus: "Add to Timeline" and MIDI binding (as in the box synths)
#if __has_include("ui/scTimelineMenu.h")
#include "ui/scTimelineMenu.h"
#define FORMS_HAS_TIMELINE_MENU 1
#endif

namespace forms {

class ControlRegistry {
public:
    ofxOceanodeNodeModel* node = nullptr;
#ifdef FORMS_HAS_TIMELINE_MENU
    // A control added to a timeline (or bound to MIDI) without being published is published
    // hidden: the timeline binds node parameters, and the node stays as it is
    scTimelineMenu timeline;
    void setupTimeline(){
        timeline.setup(node, [this](const std::string& k){ publish(k); }, [this](const std::string& k){ unpublishNow(k); },
                       [this](){ return order; });
    }
    void update(){ timeline.update(); }
#else
    void setupTimeline(){}
    void update(){}
#endif

    struct Control {
        std::string key, label;
        bool output = false;
        std::function<std::shared_ptr<ofxOceanodeAbstractParameter>()> add;
        std::function<ofJson()> save;
        std::function<void(const ofJson&)> load;
    };

    // a setting (its value is saved with the patch)
    template<typename T>
    void reg(ofParameter<T>& p){
        Control c;
        c.key = p.getEscapedName();
        c.label = p.getName();
        ofxOceanodeNodeModel* n = node;
        c.add = [n, &p](){ return std::static_pointer_cast<ofxOceanodeAbstractParameter>(n->addParameter(p)); };
        c.save = [&p](){ return ofJson(p.get()); };
        c.load = [&p](const ofJson& j){
            try {
                if constexpr (std::is_same<T, bool>::value){ if(j.is_number()){ p.set(j.get<double>() != 0); return; } }
                if constexpr (std::is_arithmetic<T>::value && !std::is_same<T, bool>::value){
                    if(j.is_number()){ p.set(std::min(p.getMax(), std::max(p.getMin(), (T)j.get<double>()))); return; }   // clamped to its range
                }
                p.set(j.get<T>());
            } catch(...) {}
        };
        controls.push_back(std::move(c));
    }
    // a trigger
    void reg(ofParameter<void>& p){
        Control c;
        c.key = p.getEscapedName();
        c.label = p.getName();
        ofxOceanodeNodeModel* n = node;
        c.add = [n, &p](){ return std::static_pointer_cast<ofxOceanodeAbstractParameter>(n->addParameter(p)); };
        c.save = [](){ return ofJson(); };
        c.load = [](const ofJson&){};
        controls.push_back(std::move(c));
    }
    // an output (nothing saved)
    template<typename T>
    void regOutput(ofParameter<T>& p){
        Control c;
        c.key = p.getEscapedName();
        c.label = p.getName();
        c.output = true;
        ofxOceanodeNodeModel* n = node;
        c.add = [n, &p](){ return std::static_pointer_cast<ofxOceanodeAbstractParameter>(n->addOutputParameter(p)); };
        c.save = [](){ return ofJson(); };
        c.load = [](const ofJson&){};
        controls.push_back(std::move(c));
    }

    const Control* find(const std::string& key) const {
        for(auto& c : controls) if(c.key == key) return &c;
        return nullptr;
    }
    bool isPublished(const std::string& key) const { return published.count(key) > 0; }
    bool isConnected(const std::string& key) const {
        auto it = published.find(key);
        return it != published.end() && it->second && (it->second->hasInConnection() || it->second->hasOutConnections());
    }
    bool publish(const std::string& key){
        if(!node || published.count(key)) return false;
        const Control* c = find(key);
        if(!c || node->getParameterGroup().contains(key)) return false;
        if(!separator){ node->addSeparator("Published", ofColor(200)); separator = true; }
        published[key] = c->add();
        order.push_back(key);
        node->parameterGroupChanged.notify(node);
        return true;
    }
    bool unpublish(const std::string& key){
#ifdef FORMS_HAS_TIMELINE_MENU
        if(published.count(key) && !isConnected(key)){ timeline.unpublish(key); return true; }
#endif
        return unpublishNow(key);
    }
    bool unpublishNow(const std::string& key){
        auto it = published.find(key);
        if(!node || it == published.end() || isConnected(key)) return false;
        node->removeParameter(it->second->getName());
        published.erase(it);
        order.erase(std::remove(order.begin(), order.end(), key), order.end());
        if(published.empty() && separator){ node->removeSeparator("Published"); separator = false; }
        node->parameterGroupChanged.notify(node);
        return true;
    }
    const std::vector<std::string>& publishedKeys() const { return order; }

    // ---- presets
    void save(ofJson& j) const {
        auto& v = j["formsControls"] = ofJson::object();
        for(auto& c : controls) if(!c.output){ ofJson x = c.save(); if(!x.is_null()) v[c.key] = x; }
        j["formsPublished"] = order;
#ifdef FORMS_HAS_TIMELINE_MENU
        const_cast<scTimelineMenu&>(timeline).save(j, "formsTimelineHidden");
#endif
    }
    // before the cables: the published set (the saved one; old patches: `legacy`)
    void loadPublished(const ofJson& j, const std::vector<std::string>& legacy){
        std::vector<std::string> wanted;
        if(j.contains("formsPublished") && j["formsPublished"].is_array()){
            for(auto& k : j["formsPublished"]) if(k.is_string()) wanted.push_back(k.get<std::string>());
        }else wanted = legacy;
        auto now = order;
        for(auto& k : now) if(std::find(wanted.begin(), wanted.end(), k) == wanted.end()) unpublishNow(k);
        for(auto& k : wanted) publish(k);
#ifdef FORMS_HAS_TIMELINE_MENU
        timeline.load(j, "formsTimelineHidden");   // the ones only the timeline / MIDI needs: hidden again
#endif
    }
    void loadValues(const ofJson& j){
        if(!j.contains("formsControls") || !j["formsControls"].is_object()) return;
        auto& v = j["formsControls"];
        for(auto& c : controls) if(v.contains(c.key)) c.load(v[c.key]);
    }

    // ---- widgets: the control, its right-click menu and a line under it when published
    // A window widget that edits a parameter always on the node: its right-click timeline / MIDI menu
    void nodeMenu(const std::string& key){
#ifdef FORMS_HAS_TIMELINE_MENU
        timeline.nodeParameterMenu(key);
#else
        (void)key;
#endif
    }
    bool isHidden(const std::string& key) const {
#ifdef FORMS_HAS_TIMELINE_MENU
        return timeline.isHidden(key);
#else
        (void)key; return false;
#endif
    }
    void menu(const std::string& key){
#ifdef FORMS_HAS_TIMELINE_MENU
        timeline.mark(key);   // on a timeline: a line in its track's colour
#endif
        if(isPublished(key) && !isHidden(key)){
            ImVec2 a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddLine(ImVec2(a.x, b.y), ImVec2(b.x, b.y), IM_COL32(150, 165, 255, 255), 1.5f);
        }
        if(!ImGui::BeginPopupContextItem(("##pub" + key).c_str())) return;
        ImGui::TextDisabled("%s", key.c_str());
        ImGui::Separator();
        // Set value: typed (a number, true / false, a list [a, b], or text)
        if(const Control* c = find(key)){
            if(!c->output){
                static std::string editing;
                static char buf[512];
                if(ImGui::IsWindowAppearing() || editing != key){
                    editing = key;
                    ofJson v = c->save();
                    std::string t = v.is_string() ? v.get<std::string>() : v.dump();
                    snprintf(buf, sizeof(buf), "%s", t.c_str());
                }
                ImGui::TextDisabled("Set value");
                ImGui::SetNextItemWidth(180 * ofxOceanodeShared::getZoomLevel());
                if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
                bool enter = ImGui::InputText("##setValue", buf, sizeof(buf), ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_AutoSelectAll);
                if(enter || ImGui::IsItemDeactivatedAfterEdit()){
                    ofJson v;
                    try { v = ofJson::parse(buf); } catch(...) { v = std::string(buf); }   // not JSON: text
                    if(c->save().is_string() && !v.is_string()) v = std::string(buf);
                    c->load(v);
                    if(enter) ImGui::CloseCurrentPopup();
                }
                ImGui::Separator();
            }
        }
        if(isPublished(key) && isHidden(key)){
#ifdef FORMS_HAS_TIMELINE_MENU
            if(ImGui::MenuItem("Publish to Node")) timeline.reveal(key);
#endif
        }else if(isPublished(key)){
            if(isConnected(key)) ImGui::TextDisabled("Connected: disconnect it to unpublish");
            else if(ImGui::MenuItem("Unpublish from Node")) unpublish(key);
        }else if(node && node->getParameterGroup().contains(key) && !find(key)) ImGui::TextDisabled("Always on the node");
        else if(ImGui::MenuItem("Publish to Node")) publish(key);
#ifdef FORMS_HAS_TIMELINE_MENU
        timeline.menuItems(key);   // Add to Timeline, MIDI
#endif
        ImGui::EndPopup();
    }
    bool slider(ofParameter<float>& p, const char* fmt = "%.3f", const char* label = nullptr, bool log = false){
        float v = p.get();
        bool ch = ImGui::SliderFloat(label ? label : p.getName().c_str(), &v, p.getMin(), p.getMax(), fmt, log ? ImGuiSliderFlags_Logarithmic : 0);
        if(ch) p = v;
        menu(p.getEscapedName());
        return ch;
    }
    bool slider(ofParameter<int>& p, const char* label = nullptr){
        int v = p.get();
        bool ch = ImGui::SliderInt(label ? label : p.getName().c_str(), &v, p.getMin(), p.getMax());
        if(ch) p = v;
        menu(p.getEscapedName());
        return ch;
    }
    bool check(ofParameter<bool>& p, const char* label = nullptr){
        bool v = p.get();
        bool ch = ImGui::Checkbox(label ? label : p.getName().c_str(), &v);
        if(ch) p = v;
        menu(p.getEscapedName());
        return ch;
    }
    bool combo(ofParameter<int>& p, const std::vector<std::string>& names, const char* label = nullptr){
        int v = ofClamp(p.get(), 0, std::max(0, (int)names.size() - 1));
        bool ch = false;
        if(ImGui::BeginCombo(label ? label : p.getName().c_str(), names.empty() ? "" : names[v].c_str())){
            for(int i = 0; i < (int)names.size(); i++) if(ImGui::Selectable(names[i].c_str(), i == v)){ p = i; ch = true; }
            ImGui::EndCombo();
        }
        menu(p.getEscapedName());
        return ch;
    }

    // System tab, "Node: published settings and outputs": every setting and output, a checkbox each (published or not)
    void drawList(){
        float z = ofxOceanodeShared::getZoomLevel();
        ImGui::TextDisabled("What is on the node. A connected one cannot be unpublished (disconnect it first).\n"
                            "Show, Preview, GO, Setlist, Scene, Speed, Pause, Sequence, Seq Fade, New PNG, Header, Header+, Smooth,\n"
                            "Output, Audio, Header Pos, Header Norm, Phase, BPM, Kick, Root Out and Scale Out are always on it.");
        ImGui::SetNextItemWidth(220 * z);
        ImGui::InputTextWithHint("##pubFilter", "filter", filter, sizeof(filter));
        std::string f = ofToLower(filter);
        for(int pass = 0; pass < 2; pass++){
            ImGui::SeparatorText(pass == 0 ? "Settings (inputs)" : "Outputs (the legacy patch's values)");
            if(!ImGui::BeginTable(pass ? "##pubOut" : "##pubIn", 4, ImGuiTableFlags_SizingStretchSame)) continue;
            for(auto& c : controls){
                if(c.output != (pass == 1)) continue;
                if(!f.empty() && ofToLower(c.label).find(f) == std::string::npos) continue;
                ImGui::TableNextColumn();
                bool hid = isHidden(c.key);
                bool pub = isPublished(c.key) && !hid, conn = isConnected(c.key);
                if(conn) ImGui::BeginDisabled();
                std::string lab = c.label + (hid ? "  (timeline)" : "") + "##pub" + c.key;
                if(ImGui::Checkbox(lab.c_str(), &pub)){
#ifdef FORMS_HAS_TIMELINE_MENU
                    if(pub && hid) timeline.reveal(c.key); else
#endif
                    if(pub) publish(c.key); else unpublish(c.key);
                }
                if(conn) ImGui::EndDisabled();
            }
            ImGui::EndTable();
        }
    }

private:
    char filter[64] = "";
    std::vector<Control> controls;
    std::map<std::string, std::shared_ptr<ofxOceanodeAbstractParameter>> published;
    std::vector<std::string> order;
    bool separator = false;
};

} // namespace forms
