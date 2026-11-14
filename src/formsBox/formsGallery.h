//
//  formsGallery.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Phase 5: the Gallery. Every session is archived in <renderer>/sessions/<session>/:
//  L<layer>_<id>_s<seed>.png + .json (the full recipe: scene, seed, harmony, items with their
//  values, render times) and, since Phase 3, timeline.jsonl (when each image played). This reads
//  them, makes small thumbnails in a background thread (cached in <session>/.gallery/), and builds
//  the plan to replay a session through the player without the renderer.
//

#pragma once

#include "ofMain.h"
#include "formsEngine.h"
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace forms {

struct GalleryImage {
    std::string png, json, id, scene;
    int layer = 1;
    unsigned seed = 0;
    std::string base;                  // file name without extension
};

struct GallerySession {
    std::string name;                  // folder name (date_time)
    int images = 0;
    std::set<std::string> scenes;
    bool hasTimeline = false;
};

// Small thumbnails made in a background thread (decode, premultiply over black, shrink), uploaded
// to textures on the main thread a few per frame
class GalleryThumbs {
public:
    ~GalleryThumbs(){ stop(); }

    const ofTexture* get(const std::string& png, const fs::path& cacheDir){
        auto it = tex.find(png);
        if(it != tex.end()) return it->second.isAllocated() ? &it->second : nullptr;
        tex[png];   // placeholder: requested once
        {
            std::lock_guard<std::mutex> lk(m);
            todo.push_back({png, (cacheDir / (fs::path(png).stem().string() + ".jpg")).string()});
        }
        cv.notify_one();
        startIfNeeded();
        return nullptr;
    }

    // main thread: move finished pixels into textures
    void update(int maxPerFrame = 6){
        std::vector<std::pair<std::string, ofPixels>> ready;
        {
            std::lock_guard<std::mutex> lk(m);
            while(!done.empty() && (int)ready.size() < maxPerFrame){ ready.push_back(std::move(done.front())); done.pop_front(); }
        }
        for(auto& [png, px] : ready){ auto& t = tex[png]; t.allocate(px); t.loadData(px); }
    }

    void clear(){ std::lock_guard<std::mutex> lk(m); todo.clear(); done.clear(); tex.clear(); }

private:
    std::map<std::string, ofTexture> tex;
    std::deque<std::pair<std::string, std::string>> todo;   // png, cache jpg
    std::deque<std::pair<std::string, ofPixels>> done;
    std::mutex m;
    std::condition_variable cv;
    std::thread worker;
    std::atomic<bool> running {false};

    void startIfNeeded(){
        if(running) return;
        running = true;
        worker = std::thread([this](){ loop(); });
    }
    void stop(){
        if(!running) return;
        running = false;
        cv.notify_all();
        if(worker.joinable()) worker.join();
    }
    void loop(){
        while(running){
            std::pair<std::string, std::string> job;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this]{ return !todo.empty() || !running; });
                if(!running) return;
                job = todo.front(); todo.pop_front();
            }
            ofPixels px;
            bool ok = false;
            if(fs::exists(job.second)) ok = ofLoadImage(px, job.second);
            if(!ok){
                ofPixels full;
                if(!ofLoadImage(full, job.first)) continue;
                // straight alpha -> over black, then shrink
                if(full.getNumChannels() == 4){
                    ofPixels rgb; rgb.allocate(full.getWidth(), full.getHeight(), OF_PIXELS_RGB);
                    const unsigned char* s = full.getData(); unsigned char* d = rgb.getData();
                    size_t n = full.getWidth() * full.getHeight();
                    for(size_t i = 0; i < n; i++){ int a = s[i * 4 + 3]; d[i * 3] = s[i * 4] * a / 255; d[i * 3 + 1] = s[i * 4 + 1] * a / 255; d[i * 3 + 2] = s[i * 4 + 2] * a / 255; }
                    full = std::move(rgb);
                }
                full.resize(320, 180, OF_INTERPOLATE_BICUBIC);
                px = std::move(full);
                std::error_code ec;
                fs::create_directories(fs::path(job.second).parent_path(), ec);
                ofSaveImage(px, job.second, OF_IMAGE_QUALITY_HIGH);
            }
            std::lock_guard<std::mutex> lk(m);
            done.push_back({job.first, std::move(px)});
        }
    }
};

// Sessions and their images, read on demand
class Gallery {
public:
    fs::path root;                     // <renderer>/sessions
    std::vector<GallerySession> sessions;
    std::vector<GalleryImage> images;  // of the selected session, by id
    std::string selectedSession;
    GalleryThumbs thumbs;

    void scanSessions(){
        sessions.clear();
        if(!fs::exists(root)) return;
        for(auto& e : fs::directory_iterator(root)){
            if(!e.is_directory()) continue;
            GallerySession s; s.name = e.path().filename().string();
            for(auto& f : fs::directory_iterator(e.path())){
                auto n = f.path().filename().string();
                if(f.path().extension() == ".png" && n.size() > 2 && n[0] == 'L' && n.find("_cover")==std::string::npos) s.images++;
            }
            s.hasTimeline = fs::exists(e.path() / "timeline.jsonl");
            if(s.images > 0) sessions.push_back(s);
        }
        std::sort(sessions.begin(), sessions.end(), [](auto& a, auto& b){ return a.name > b.name; });
    }

    void openSession(const std::string& name){
        selectedSession = name;
        images.clear();
        fs::path d = root / name;
        if(!fs::exists(d)) return;
        for(auto& f : fs::directory_iterator(d)){
            if(f.path().extension() != ".png") continue;
            std::string base = f.path().stem().string();   // L<layer>_<id>_s<seed>
            if(base.size() < 4 || base[0] != 'L' || base.find("_cover") != std::string::npos) continue;
            GalleryImage im;
            im.png = f.path().string();
            im.json = (d / (base + ".json")).string();
            im.base = base;
            auto parts = ofSplitString(base, "_");
            if(parts.size() >= 3){ im.layer = ofToInt(parts[0].substr(1)); im.id = parts[1]; im.seed = (unsigned)ofToInt64(parts[2].substr(1)); }
            try { json j = readJsonFile(im.json); im.scene = j.value("scene", std::string()); } catch(...) {}
            images.push_back(im);
        }
        std::sort(images.begin(), images.end(), [](auto& a, auto& b){ return a.id < b.id; });
        for(auto& s : sessions) if(s.name == name){ s.scenes.clear(); for(auto& im : images) s.scenes.insert(im.scene); }
    }

    fs::path cacheDir() const { return root / selectedSession / ".gallery"; }

    // The replay plan: Images as Forms Box schedules them (layer 1 in play order, layers 2-4
    // with their start beats), from the sidecars and, if present, timeline.jsonl
    struct Plan {
        std::deque<std::shared_ptr<Image>> layer1;
        std::map<int, std::deque<std::shared_ptr<Image>>> extra;   // layer index 1..3
        bool fromTimeline = false;
    };
    Plan replayPlan(const std::string& name) const {
        Plan p;
        fs::path d = root / name;
        auto imageFrom = [&](const std::string& jsonPath, const std::string& png) -> std::shared_ptr<Image> {
            try {
                json j = readJsonFile(jsonPath);
                auto im = std::make_shared<Image>();
                im->id = j.value("id", std::string()); im->png = png; im->json = jsonPath;
                im->scene = j.value("scene", std::string()); im->layer = j.value("layer", 1);
                im->bars = barsFrom(j, "bars", 8); im->bpm = j.value("bpm", 120.0); im->notegrid = j.value("notegrid", 100);
                im->snapshot = j.value("snapshot", -1); im->seed = j.value("seed", 0u);
                im->palette = j.value("paletteName", std::string()); im->scale = j.value("scaleName", std::string());
                im->roots = j.value("roots", std::vector<int>()); im->trans = j.value("trans", std::vector<int>());
                for(auto& v : j.value("scaleNotes", json::array())) im->scaleNotes.push_back(v.get<float>());
                im->perf = j.value("performance", json::object());
                im->layerCover = j.value("layerCover",json::object());
                formscover::readMasks(im->covers,j,fs::path(jsonPath).parent_path().string());
                return im;
            } catch(...) { return nullptr; }
        };
        fs::path tl = d / "timeline.jsonl";
        if(fs::exists(tl)){
            p.fromTimeline = true;
            std::ifstream in(tl);
            std::string line;
            double origin = std::numeric_limits<double>::quiet_NaN();
            while(std::getline(in, line)){
                json e; try { e = json::parse(line); } catch(...) { continue; }
                std::string png = e.value("png", std::string());
                if(png.empty() || !fs::exists(png)) continue;
                auto im = imageFrom(fs::path(png).replace_extension(".json").string(), png);
                if(!im) continue;
                im->blend = e.value("blend", im->blend);
                im->gain = e.value("gain", im->gain);
                int layer = e.value("layer", 1);
                double start = e.value("startBeat", 0.0);
                if(layer == 1){
                    if(std::isnan(origin)) origin = start;
                    p.layer1.push_back(im);
                }else{
                    im->startBeat = start;
                    p.extra[layer - 1].push_back(im);
                }
            }
            // extra layers relative to where layer 1's first logged image started
            if(!std::isnan(origin)) for(auto& [l, q] : p.extra) for(auto& im : q) im->startBeat -= origin;
        }else{
            for(auto& f : fs::directory_iterator(d)){
                std::string base = f.path().stem().string();
                if(f.path().extension() != ".png" || base.rfind("L1_", 0) != 0) continue;
                if(auto im = imageFrom((d / (base + ".json")).string(), f.path().string())) p.layer1.push_back(im);
            }
            std::sort(p.layer1.begin(), p.layer1.end(), [](auto& a, auto& b){ return a->id < b->id; });
        }
        return p;
    }
};

} // namespace forms
