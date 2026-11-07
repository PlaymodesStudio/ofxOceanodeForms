//
//  formsLane.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The scene lane: Forms Box's scenes as a timeline Chords lane (the chord-bank instrument the
//  timeline offers HarmonyBox): one row per scene, a block = that scene plays. Where the lane has
//  blocks it chooses the scenes; elsewhere the setlist (or the manual scene) does, as before.
//
//  Images are asked for bars ahead, so the lane is read ahead: with Drive Transport on (the
//  transport's beat is Forms Box's beat under the header), the scene of a layer-1 image is the
//  lane's block at the bar the image starts, read from the timeline's clips with the timeline's own
//  clip-time conversion. With it off, the timeline runs on its own time: the scene the lane plays
//  now is used for the next images.
//
//  Rows keep their order (saved with the patch): a new scene gets a new row at the end, a
//  deleted one keeps its row (marked missing), so the blocks already drawn stay on their scenes.
//

#pragma once

#include "ofxOceanodeNodeModel.h"
#include "imgui.h"
#include <map>
#include <set>
#include <string>
#include <vector>

#if __has_include("ofxOceanodeSuperColliderConfig.h")
#include "ofxOceanodeSuperColliderConfig.h"
#endif
#if defined(OFXOCEANODESC_HAS_TIMELINE) && OFXOCEANODESC_HAS_TIMELINE && __has_include("ofxOceanodeTimeline.h")
#define FORMS_HAS_TIMELINE 1
#include "ofxOceanodeContainer.h"
#include "ofxOceanodeTimeline.h"
#endif

namespace forms {

class SceneLane {
public:
    std::vector<std::string> rows;            // row -> scene name (stable)
    std::set<std::string> hardCut;            // scenes the lane cuts into (every layer at once)
    ofParameter<std::vector<float>> gates;    // the timeline writes it: component k = row k's gate
    std::shared_ptr<ofxOceanodeAbstractParameter> handle;

    // the scenes there are now: rows for new ones (appended)
    void syncRows(const std::vector<std::string>& scenes){
        for(auto& s : scenes) if(std::find(rows.begin(), rows.end(), s) == rows.end()) rows.push_back(s);
        if(gates.get().size() != rows.size()){
            auto v = gates.get(); v.resize(std::max<size_t>(1, rows.size()), 0.0f);
            gates.setWithoutEventNotifications(v);
        }
    }

    // ---- live: the scene the lane plays now (a gate opening takes its scene; when it closes,
    // another still open takes over)
    std::string live() const { return liveRow >= 0 && liveRow < (int)rows.size() ? rows[liveRow] : std::string(); }
    void gatesChanged(const std::vector<float>& v){
        int n = std::min((int)v.size(), (int)rows.size()), opened = -1;
        for(int k = 0; k < n; k++) if(v[k] > 0 && (k >= (int)last.size() || last[k] <= 0)) opened = k;
        last = v;
        if(opened >= 0) liveRow = opened;
        else if(liveRow < 0 || liveRow >= n || v[liveRow] <= 0){
            liveRow = -1;
            for(int k = 0; k < n && liveRow < 0; k++) if(v[k] > 0) liveRow = k;
        }
    }

    // ---- ahead: the lane's scene at a timeline beat ("" = no block there, or no lane)
    // node: Forms Box (its container holds the timeline)
    std::string at(ofxOceanodeNodeModel* node, double beat){
#ifdef FORMS_HAS_TIMELINE
        if(!node || !handle) return "";
        auto* container = node->getHostContainer();
        if(!container) return "";
        refreshIndex(container);
        int bestRow = -1; double bestStart = -1e18;
        auto& manager = container->getTimelineManager();
        for(auto& src : sources){
            const ofxOceanodeTimelineSequence* seq = manager.findTimeline(src.ref);
            if(!seq) continue;
            const ofxOceanodeTimelineTrack* tr = seq->findTrack(src.track);
            if(!tr || tr->muted) continue;
            const auto& track = *tr;
            // a loop region: once past its end the timeline plays it again, so the lane does too
            double tb = beat;
            const auto& pl = seq->play;
            if(pl.loopEnabled && pl.loopEndBeat > pl.loopStartBeat + 1e-9 && tb >= pl.loopEndBeat)
                tb = pl.loopStartBeat + std::fmod(tb - pl.loopStartBeat, pl.loopEndBeat - pl.loopStartBeat);
            for(auto& clip : track.clips){
                if(clip.muted || tb < clip.startBeat || tb >= clip.startBeat + clip.durationBeats) continue;
                double s = ofxOceanodeTimelineClipTime::timelineToSourceBeat(clip, tb);
                for(auto& lane : clip.lanes){
                    if(lane.muted || lane.type != ofxOceanodeTimelineLaneType::MultiGate) continue;
                    if(std::find(lane.bindingIds.begin(), lane.bindingIds.end(), src.binding) == lane.bindingIds.end()) continue;
                    for(int r = 0; r < (int)lane.multiGateRows.size() && r < (int)rows.size(); r++)
                        for(auto& g : lane.multiGateRows[r])
                            if(g.velocity > 0 && g.startBeat <= s + 1e-9 && s < g.end() - 1e-9 && g.startBeat > bestStart){ bestStart = g.startBeat; bestRow = r; }
                }
            }
        }
        return bestRow >= 0 ? rows[bestRow] : std::string();
#else
        (void)node; (void)beat;
        return "";
#endif
    }
    // whether a timeline has the lane (a track bound to the gates)
    bool onTimeline(ofxOceanodeNodeModel* node){
#ifdef FORMS_HAS_TIMELINE
        if(!node || !node->getHostContainer()) return false;
        refreshIndex(node->getHostContainer());
        return !sources.empty();
#else
        (void)node; return false;
#endif
    }

    // ---- the timeline instrument (a chord bank: Chords lane)
    void instrument(ofxOceanodeTimelineInstrument& out, const std::set<std::string>& existing){
        out.gate = handle.get();
        out.voices = (int)rows.size();
#ifdef OFXOCEANODE_HAS_TIMELINE_CHORDS
        out.chords = true;
#endif
        for(int i = 0; i < (int)rows.size(); i++){
            bool missing = !existing.count(rows[i]);
            out.rowNames.push_back(rows[i] + (hardCut.count(rows[i]) ? "  |cut" : "") + (missing ? "  (missing)" : ""));
            // a colour per scene, from its name
            unsigned hsh = 2166136261u; for(char c : rows[i]){ hsh ^= (unsigned char)c; hsh *= 16777619u; }
            ofFloatColor col = ofFloatColor::fromHsb((hsh % 360) / 360.0f, missing ? 0.1f : 0.55f, missing ? 0.4f : 0.9f);
            out.rowColors.push_back(col);
        }
#ifdef OFXOCEANODE_HAS_TIMELINE_CHORD_MENUS
        out.rowMenu = [this](int row){
            if(row < 0 || row >= (int)rows.size()) return;
            const std::string& s = rows[row];
            bool cut = hardCut.count(s) > 0;
            if(ImGui::MenuItem("Hard cut into this scene", nullptr, cut)){ if(cut) hardCut.erase(s); else hardCut.insert(s); }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("On: when the lane changes to %s every layer cuts at once.\nOff: the other layers finish their images first.", s.c_str());
            if(onRowPlay && ImGui::MenuItem("Play this scene now (manual)")) onRowPlay(s);
        };
        out.panel = [this](){ if(drawPanel) drawPanel(); };
        out.panelLabel = "Scenes";
#endif
    }
    std::function<void(const std::string&)> onRowPlay;
    std::function<void()> drawPanel;

    // ---- presets
    void save(ofJson& j) const {
        j["formsLaneRows"] = rows;
        j["formsLaneHardCut"] = std::vector<std::string>(hardCut.begin(), hardCut.end());
    }
    void load(const ofJson& j){
        if(j.contains("formsLaneRows") && j["formsLaneRows"].is_array()){
            rows.clear();
            for(auto& r : j["formsLaneRows"]) if(r.is_string()) rows.push_back(r.get<std::string>());
        }
        if(j.contains("formsLaneHardCut") && j["formsLaneHardCut"].is_array()){
            hardCut.clear();
            for(auto& r : j["formsLaneHardCut"]) if(r.is_string()) hardCut.insert(r.get<std::string>());
        }
        auto v = gates.get(); v.assign(std::max<size_t>(1, rows.size()), 0.0f);
        gates.setWithoutEventNotifications(v);
    }

private:
    std::vector<float> last;
    int liveRow = -1;
#ifdef FORMS_HAS_TIMELINE
    // where the lane is: (timeline, track, binding) of every track bound to the gates, by id (looked
    // up on every read: the timeline may move its data), rebuilt twice a second
    struct Source { ofxOceanodeTimelineManager::TimelineRef ref; std::string track, binding; };
    std::vector<Source> sources;
    float indexedAt = -10;
    void refreshIndex(ofxOceanodeContainer* container){
        float now = ofGetElapsedTimef();
        if(now - indexedAt < 0.5f) return;
        indexedAt = now;
        sources.clear();
        auto& manager = container->getTimelineManager();
        for(auto& ref : manager.allTimelines()){
            if(!ref.canvas.empty() && ref.canvas != "Canvas" && ref.canvas != "0") continue;   // the root canvas's timelines
            const ofxOceanodeTimelineSequence* seq = manager.findTimeline(ref);
            if(!seq) continue;
            for(size_t t = 0; t < seq->tracks.size(); t++)
                for(auto& b : seq->tracks[t].bindings){
                    auto* p = container->findTimelineParameter(b.parameterPath);
                    if(p && p == handle.get()) sources.push_back({ref, seq->tracks[t].id, b.id});
                }
        }
    }
#endif
};

} // namespace forms
