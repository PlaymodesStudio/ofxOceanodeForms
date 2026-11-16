//
//  formsGLBox.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7
//
//  Forms GL Box: the ESPECTRES 2027 node. Forms Box (formsBox.h: the engine, scenes, player, sound,
//  post, window) with its renderer: every request is drawn here, on background threads, by
//  src/formsGL (every generator and process of the ESPECTRES_2027 sketch, ported to C++ and
//  verified against it), into <data>/sessions/<session>/L<layer>_<id>_s<seed>.png + the .json
//  sidecar. An item it cannot draw (a legacy method) is left out of that image and counted. Its
//  parameter schemas are its own (formsGLSchemas.h); from the data folder it reads the font
//  FuturaStd-Bold.otf (letters) and the scenes / presets / library.
//
//  Speed: requests are drawn by a pool of worker threads (results are delivered in the order they
//  were asked for, so the schedule is the same); the pixels go to the scrollers in memory, and the
//  PNG + sidecar are written afterwards by an archive thread (optional: "Archive images"). Editor
//  previews and thumbnails are written first (the editor reads them).
//  Audition and immediate modes (Instruments tab): formsLiveEdit.h.
//

#pragma once

#include "formsBox.h"
#include "../formsGL/formsGLSketch.h"
#include "../formsGL/formsGLFont.h"
#include "../formsGL/formsGLGpu.h"
#include "formsLiveEdit.h"
#include "formsGLExportMedia.h"
#include <future>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <thread>

class formsGLBox : public formsBox {
public:
    formsGLBox() : formsBox(std::string("Forms GL Box")) { describe(); }
#ifdef FORMS_HAS_SC
    explicit formsGLBox(std::vector<serverManager*> servers) : formsBox(std::string("Forms GL Box"), std::move(servers)) { describe(); }
#endif
    ~formsGLBox() override {
        glVideoWriter.reset();
        if(exportRender.valid()) exportRender.wait();
        if(exportMedia.valid()) exportMedia.wait();
        {
            std::lock_guard<std::mutex> lk(m);
            quit = true;
        }
        cv.notify_all();
        for(auto& t : workersPool) if(t.joinable()) t.join();
        {
            std::lock_guard<std::mutex> lk(am);
            archiveQuit = true;
        }
        acv.notify_all();
        if(archiver.joinable()) archiver.join();   // what is queued is written first
    }

    void setup() override {
        texturePool = formsgl::SharedTexturePool::create(CGLGetCurrentContext(),
                        ofGetUsingArbTex() ? GL_TEXTURE_RECTANGLE_ARB : GL_TEXTURE_2D);
        formsBox::setup();
        engine.requestSink=[this](const forms::fs::path& path,forms::json request){
            stagedRequests[path.string()]=std::move(request);
        };
        editor.drawExportTab = [this](){ drawGLExportTab(); };
        editor.makeThumbnails = [this](){ makeThumbnailsGL(); };
        editor.auditionSwitch = &auditionOn;
        editor.immediateSwitch = &immediateOn;
        editor.thumbnailsTip = "Draws a preview of every instrument here (a few seconds) and keeps it as its thumbnail.\n"
                               "Effects-only instruments are drawn over a test image.";
        editor.drawRendererPanel = [this](){
            ImGui::Text("drawn %d   with items left out %d   errors %d   waiting %d   workers %d",
                        drawnHere.load(), sentOn, errorsHere.load(), (int)queued.load(), (int)workersPool.size());
            if(drawnHere > 0) ImGui::Text("last here: %d ms", lastMs.load());
            {
                auto& st = forms::pipelineStats();
                ImGui::TextDisabled("ms (last / mean): wait %.0f / %.0f   draw %.0f / %.0f   upload %.1f / %.1f   archive %.0f / %.0f (%d queued)   asked -> ready %.0f / %.0f",
                                    st.queue.last, st.queue.mean, st.render.last, st.render.mean, st.upload.last, st.upload.mean,
                                    st.archive.last, st.archive.mean, st.archiveBacklog, st.total.last, st.total.mean);
                ImGui::TextDisabled("Main thread ms (last / peak): player %.1f / %.1f   requests %.1f / %.1f   delivery %.1f / %.1f",
                                    st.player.last,st.player.peak,st.request.last,st.request.peak,st.delivery.last,st.delivery.peak);
            }
            forms::savedWithPatchNote();
            ImGui::SameLine();
            ImGui::TextDisabled("(the two switches below)");
            bool ar = archive.load();
            if(ImGui::Checkbox("Archive images (PNG + sidecar)", &ar)) archive = ar;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Writes every image and its recipe to the session folder, after it is shown\n"
                                                         "(Gallery, replay, panorama and prints need them). Off: nothing is written; faster, no disk.");
            if(archiveErrors > 0){ std::lock_guard<std::mutex> lk(am); ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "archive errors %d: %s", archiveErrors.load(), lastArchiveError.c_str()); }
            if(thumbsAsked) ImGui::Text("thumbnails %d / %d", thumbsDone, thumbsAsked);
            bool g = useGpu.load();
            if(ImGui::Checkbox("GPU texture streaming", &g)) useGpu = g;
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("Full-resolution texture uploads on worker GL contexts.\n"
                                                         "Finished GPU textures enter the scrollers without an upload on the UI thread.\n"
                                                         "Native pixels, filtering and texture coordinates are preserved.");
            bool deep = live16.load();
            if(ImGui::Checkbox("16-bit live buffers", &deep)){ live16 = deep; liveBits = deep ? 16 : 8; }
            if(ImGui::IsItemHovered()) ImGui::SetTooltip("On (default): page textures, layer buffers, layer effects, composite and post-processing\n"
                                                         "hold 16 bits per channel. Off: 8 bits, a fallback if 16 costs frame rate; faint trails\n"
                                                         "and fades then step again on the GPU side. The images are drawn in 16 bits either way;\n"
                                                         "pages already on stage keep the depth they were uploaded with.");
            bool kicks = gpuKicks.load();
            ImGui::BeginDisabled();
            if(ImGui::Checkbox("GPU kick rasterization (8-bit, retired)", &kicks)) gpuKicks = kicks;
            ImGui::EndDisabled();
            if(ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("The integer kick shader reproduced the former 8-bit arithmetic.\n"
                                                         "Images are drawn with 16 bits per channel now: the CPU draws the kicks.");
            if(!gpuError.empty()) ImGui::TextColored(ImVec4(1, 0.5f, 0.3f, 1), "GPU: %s", gpuError.c_str());
            ImGui::TextDisabled("Generators: %s, every process.%s", portedList().c_str(), font ? "" : " Font not found: letters are left out.");
            if(!lastWhyNot.empty()) ImGui::TextDisabled("last: %s", lastWhyNot.c_str());
            if(!schemaError.empty()) ImGui::TextColored(ImVec4(1, 0.4f, 0.3f, 1), "%s", schemaError.c_str());
        };
    }

    void presetSave(ofJson& j) override {
        formsBox::presetSave(j);
        j["formsGLExport"]={{"width",exportWidth},{"height",exportHeight},{"audio",glExportAudio},{"video",glExportVideo},{"layers",glExportSeparate}};
        j["formsGLExactGpu"] = useGpu.load();
        j["formsGLExactGpuKicks"] = gpuKicks.load();
        j["formsGLLive16"] = live16.load();
        j["formsGLArchive"] = archive.load();
        j["formsGLImmediate"] = immediateOn;
    }
    void presetRecallAfterSettingParameters(ofJson& j) override {
        formsBox::presetRecallAfterSettingParameters(j);
        if(j.contains("formsGLExport") && j["formsGLExport"].is_object()){
            auto& e=j["formsGLExport"]; exportWidth=ofClamp(e.value("width",1920),16,16384); exportHeight=ofClamp(e.value("height",1080),16,16384);
            glExportAudio=e.value("audio",false); glExportVideo=e.value("video",false); glExportSeparate=e.value("layers",false);
        }
        if(j.contains("formsGLExactGpu") && j["formsGLExactGpu"].is_boolean()) useGpu = j["formsGLExactGpu"].get<bool>();
        if(j.contains("formsGLExactGpuKicks") && j["formsGLExactGpuKicks"].is_boolean()) gpuKicks = j["formsGLExactGpuKicks"].get<bool>();
        if(j.contains("formsGLLive16") && j["formsGLLive16"].is_boolean()) live16 = j["formsGLLive16"].get<bool>();
        liveBits = live16.load() ? 16 : 8;
        if(j.contains("formsGLArchive") && j["formsGLArchive"].is_boolean()) archive = j["formsGLArchive"].get<bool>();
        if(j.contains("formsGLImmediate") && j["formsGLImmediate"].is_boolean()) immediateOn = j["formsGLImmediate"].get<bool>();
    }

    void update(ofEventArgs& a) override {
        if(texturePool) texturePool->drain();
        loadFont();
        { forms::StageTimer timer(forms::pipelineStats().player); formsBox::update(a); }
        forms::StageTimer deliveryTimer(forms::pipelineStats().delivery);
        // finished renders: to the engine, on the main thread, in the order they were asked for
        std::deque<Done> ready;
        {
            std::lock_guard<std::mutex> lk(m);
            while(ready.size()<2){ // don't deliver an entire worker backlog in one scrolling frame
                auto f = doneBySeq.find(nextDeliver);
                if(f == doneBySeq.end()) break;
                if(f->second.texture){
                    try { if(!f->second.texture->ready()) break; }
                    catch(const std::exception& e){ f->second.error = e.what(); f->second.texture.reset(); }
                }
                bool masksReady=true;
                for(auto& mask:f->second.coverTextures) if(mask){
                    try { if(!mask->ready()){ masksReady=false; break; } }
                    catch(const std::exception& e){ f->second.error=e.what(); break; }
                }
                if(!masksReady) break;
                ready.push_back(std::move(f->second));
                doneBySeq.erase(f);
                nextDeliver++;
            }
        }
        {
            std::lock_guard<std::mutex> lk(am);
            for(double ms : archiveTimes) forms::pipelineStats().archive.add(ms);
            archiveTimes.clear();
            forms::pipelineStats().archiveBacklog = (int)archiveJobs.size();
        }
        for(auto& d : ready){
            if(d.error.empty()) for(size_t i=0;i<d.covers.size();++i)
                if(i<d.coverTextures.size() && d.coverTextures[i]) d.covers[i].texture=attachTexture(d.coverTextures[i]);
            if(d.kind == KIND_AUDITION){ auditionDelivered(d); continue; }
            if(d.kind == KIND_IMMEDIATE || d.kind == KIND_FILTER){ immediateDelivered(d); continue; }
            if(d.recipe && d.error.empty()) keepRecipe(d.id, d.recipe);
            if(!d.thumbRef.empty()){
                thumbsDone++;
                editor.thumbs.forget(d.thumbRef);
                if(!d.error.empty()) engine.note("thumbnail " + d.thumbRef + ": " + d.error);
                if(thumbsDone == thumbsAsked) engine.note("thumbnails: " + std::to_string(thumbsDone) + " drawn");
                continue;
            }
            if(d.error.empty()){
                if(d.queueMs >= 0){ forms::pipelineStats().queue.add(d.queueMs); forms::pipelineStats().render.add(d.renderMs); }
                auto pendingImage = engine.pending.find(d.id);
                if(pendingImage != engine.pending.end()){
                    pendingImage->second->texture = attachTexture(d.texture);
                    pendingImage->second->covers = d.covers;
                    pendingImage->second->stageRgba = d.texture ? nullptr : d.stageRgba;
                    pendingImage->second->stageW = d.stageW;
                    pendingImage->second->stageH = d.stageH;
                    pendingImage->second->pw = d.w;
                    pendingImage->second->ph = d.h;
                }
                // The live schedule retains the GPU texture; CPU pixels can leave after archiving.
                engine.onReady(d.id, d.png, d.json);
                // drawn with an older mute / solo: drawn again (in place) with the current one
                if(d.filterGen != filterGen && d.recipe){ formsgl::json q = *d.recipe; q.erase("result"); submitImmediate(q, KIND_FILTER); }
                // asked for at an older Density (Immediate): drawn again (in place) at the current one
                else if(immediateOn && d.recipe && d.recipe->value("chanceDensity", 0.0f) != engine.density)
                    for(auto& im : stagePages()) if(im && im->id == d.id){ densityRedraw(im); break; }
            }else engine.onError(d.id, "Forms GL Box: " + d.error);
        }
        watchEdits();
        pollGLExport();
    }

    // ---------------------------------------------------------------- audition / immediate

    // audition: the auditioned page alone on the stage, looping with the clock under the header
    bool auditionStage() override {
        if(!auditionOn || !auditionImg || (!auditionImg->texture && !auditionTex.isAllocated()) || !scroller.fbo.isAllocated()) return false;
        ofTexture& texture = auditionImg->texture ? *auditionImg->texture : auditionTex.getTexture();
        ofFbo& f = scroller.fbo;
        float w = f.getWidth(), h = f.getHeight(), ph = clock.phase;
        f.begin();
        ofClear(0, 0, 0, 0);
        ofPushStyle();
        ofEnableAlphaBlending();
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);   // straight alpha -> premultiplied, as the scroller
        ofSetColor(255);
        texture.draw(-ph * w, 0, w, h);
        texture.draw((1 - ph) * w, 0, w, h);
        ofPopStyle();
        f.end();
        return true;
    }
    std::shared_ptr<forms::Image> auditionImage() override { return auditionImg; }

protected:
    void recordTimeline(const forms::fs::path& path,const forms::json& record) override {
        ArchiveJob job; job.appendPath=path.string(); job.appendLine=record.dump()+"\n";
        { std::lock_guard<std::mutex> lock(am); archiveJobs.push_back(std::move(job)); }
        startThreads(); acv.notify_one();
    }
    void send(const std::string& path) override {
        if(path.empty()) return;
        loadSchemas();
        formsgl::json req;
        bool parsed = false;
        auto staged=stagedRequests.find(path);
        bool deferred=staged!=stagedRequests.end();
        if(deferred){ req=std::move(staged->second); stagedRequests.erase(staged); parsed=true; }
        else try {
            std::ifstream in(path);
            if(in){ in >> req; parsed = true; }
        } catch(...) {}
        if(parsed) formsgl::substituteUnderlay(req);
        std::string why;
        try { why = !parsed ? "request unreadable" : schemas ? formsgl::Sketch(*schemas).whyNot(req) : "no parameter schemas"; }
        catch(std::exception& e){ why = std::string("request not understood here: ") + e.what(); }
        if(!parsed || !schemas){
            // nothing to draw with: an error for that image
            std::string rid = ofFilePath::getBaseName(path);
            try { if(parsed) rid = req.value("id", rid); } catch(...) {}
            lastWhyNot = rid + ": " + why;
            sentOn++;
            engine.onError(rid, "Forms GL Box: " + why);
            return;
        }
        if(!why.empty()){
            // drawn anyway: what can't be drawn here is left out of this image
            std::string rid = "?";
            try { rid = req.value("id", std::string("?")); } catch(...) {}
            lastWhyNot = rid + ": " + why + " (left out)";
            sentOn++;
        }
        Job j;
        j.req = req;
        if(deferred) j.requestPath=path;
        j.font = font;
        j.outDir = outputDir(req.value("session", std::string()));
        submitFiltered(std::move(j));
    }

private:
    enum { KIND_PAGE = 0, KIND_AUDITION = 2, KIND_IMMEDIATE = 3, KIND_FILTER = 4 };
    std::map<std::string,forms::json> stagedRequests;
    struct Job { formsgl::json req; std::string outDir,requestPath; std::shared_ptr<const formsgl::Sketch::GlyphFont> font; std::string thumbRef, thumbPath;
                 uint64_t seq = 0; std::chrono::steady_clock::time_point asked; int kind = KIND_PAGE;
                 std::shared_ptr<formsgl::json> original;   // the whole recipe when `req` is drawn without muted instruments
                 uint64_t filterGen = 0; };
    struct Done { std::vector<formscover::Mask> covers; std::string id, png, json, error, thumbRef;
                  std::vector<std::shared_ptr<formsgl::SharedTexture>> coverTextures;
                  std::shared_ptr<formsgl::SharedTexture> texture;
                  std::shared_ptr<const std::vector<uint16_t>> rgba, stageRgba; int w = 0, h = 0, stageW = 0, stageH = 0; double queueMs = -1, renderMs = 0;
                  int kind = 0; std::shared_ptr<formsgl::json> recipe; uint64_t filterGen = 0; };

    #include "formsGLExport.h"

    // ---- the recipes of recent pages (in memory: the files may not be written), for immediate mode
    std::map<std::string, std::shared_ptr<formsgl::json>> recipes;
    std::deque<std::string> recipeOrder;
    void keepRecipe(const std::string& id, std::shared_ptr<formsgl::json> r){
        // A render submitted before a rename may finish afterwards. Use the current
        // image identity when keeping its recipe for subsequent immediate edits.
        auto pages = stagePages();
        auto pending = engine.pending.find(id);
        if(pending != engine.pending.end()) pages.push_back(pending->second);
        for(auto& im : pages) if(im && im->id == id && r && r->value("scene", std::string()) != im->scene){
            r = std::make_shared<formsgl::json>(*r);
            (*r)["scene"] = im->scene;
            break;
        }
        if(!recipes.count(id)) recipeOrder.push_back(id);
        recipes[id] = std::move(r);
        while(recipeOrder.size() > 400){ recipes.erase(recipeOrder.front()); recipeOrder.pop_front(); }
    }
    void sceneWasRenamed(const std::string& from, const std::string& to) override {
        for(auto& [id, recipe] : recipes) if(recipe && recipe->value("scene", std::string()) == from){
            recipe = std::make_shared<formsgl::json>(*recipe);
            (*recipe)["scene"] = to;
        }
        auto it = sceneApplied.find(from);
        if(it != sceneApplied.end()){
            auto state = it->second; state["name"] = to;
            sceneApplied.erase(it); sceneApplied[to] = std::move(state);
        }
        if(auditionScene == from) auditionScene = to;
        if(auditionImg && auditionImg->scene == from) auditionImg->scene = to;
    }
    // the pages on the stage and waiting (every copy of a repeated page)
    std::vector<std::shared_ptr<forms::Image>> stagePages(){
        std::vector<std::shared_ptr<forms::Image>> v;
        for(auto& im : scroller.heldImages()) v.push_back(im);
        for(size_t l = 0; l < engine.schedule->layers.size(); l++){
            auto& L = engine.schedule->layers[l];
            for(auto& im : L.ready) if(im) v.push_back(im);
            for(auto& sl : L.slots) if(sl.img) v.push_back(sl.img);
        }
        return v;
    }

    // ---- watching the edits
    bool auditionOn = false, immediateOn = false, auditionWas = false;
    std::string watchRef, watchDump;
    forms::PresetEditTracker presetEdits;
    std::map<std::string, forms::json> sceneApplied; // scene on stage -> the version its pages show
    void watchEdits(){
        std::string ref = editor.selectedPresetRef();
        auto it = engine.presets.find(ref);
        if(auditionOn != auditionWas){
            auditionWas = auditionOn;
            if(auditionOn){ auditionSeedSet = false; auditionItemsSeed = engine.rng() & 0x7fffffff; requestAudition(); }
            else { auditionImg.reset(); auditionTex.clear(); }
        }
        if(it != engine.presets.end()){
            std::string dump = forms::json(it->second).dump();
            if(ref != watchRef){
                watchRef = ref; watchDump = dump;
                if(auditionOn) requestAudition();
            }else if(dump != watchDump){
                watchDump = dump;
                if(auditionOn) requestAudition();
            }
        }
        forms::Preset beforePreset,afterPreset;
        if(presetEdits.next(engine.presets,immediateOn,immediateInFlight>0,beforePreset,afterPreset))
            immediateFor(beforePreset,afterPreset);
        // scenes on stage: an edit of what a page is drawn with (palette, scale, note grid, a layer's
        // instrument cells) redraws that scene's pages in place (immediate mode). While a batch is in
        // flight the edit waits (tried every frame), so the latest one wins.
        std::set<std::string> onStage;
        for(auto& im : stagePages()) if(im) onStage.insert(im->scene);
        for(auto it = sceneApplied.begin(); it != sceneApplied.end();) it = onStage.count(it->first) ? std::next(it) : sceneApplied.erase(it);
        for(auto& name : onStage){
            auto sit = engine.scenes.find(name);
            if(sit == engine.scenes.end()) continue;
            // Audio assets are shared immutable bytes, not an image-render setting.
            // Never copy a sample's base64 payload into the per-frame edit snapshot.
            forms::Scene editState=sit->second; editState.kickSample.reset();
            forms::json now = editState;
            { auto es = engine.effectiveScale(sit->second); now["_globalPitch"] = {{"notes", es.notes}, {"root", engine.effectiveRoot(sit->second)}}; }   // a Global Pitch change counts as a scene edit
            auto f = sceneApplied.find(name);
            if(f == sceneApplied.end()){ sceneApplied[name] = now; continue; }
            if(f->second == now) continue;
            if(!immediateOn){ f->second = now; continue; }
            if(immediateInFlight > 0){ if(!immediateWaitNoted){ immediateWaitNoted = true; engine.note("immediate: " + name + " edited while a redraw is in flight; drawn again when it lands"); } continue; }
            immediateWaitNoted = false;
            forms::Scene before;
            try { before = f->second.get<forms::Scene>(); } catch(...) { f->second = now; continue; }
            immediateSceneEdit(name, before, sit->second);
            f->second = now;
        }
        // the node's Density: with Immediate on, a change re-picks the instruments of the pages on
        // stage and waiting (patchDensity); while a batch is in flight it waits, so the latest value wins
        if(engine.density != densityApplied){
            if(!immediateOn) densityApplied = engine.density;
            else if(immediateInFlight == 0){ densityApplied = engine.density; immediateDensity(); }
        }
    }
    float densityApplied = 0;
    // one page at the node's Density as it is now (false: nothing to draw again)
    bool densityRedraw(const std::shared_ptr<forms::Image>& im){
        if(!im || im->preview) return false;
        auto rc = recipes.find(im->id);
        auto sit = engine.scenes.find(im->scene);
        if(rc == recipes.end() || !rc->second || sit == engine.scenes.end()) return false;
        const forms::Scene& sc = sit->second;
        formsgl::json req = *rc->second;
        req.erase("result");
        if(req.contains("preview")) return false;
        int li = req.value("layer", 1) - 1;
        if(li < 0 || li >= (int)sc.layers.size() || sc.layers[li].fixed.is_object()) return false;
        if(req.value("chanceDensity", 0.0f) == engine.density) return false;
        forms::Engine::OwnScope ownScope(engine, &sc);   // this scene's own versions of its instruments
        formsgl::json oldItems = req.value("items", formsgl::json::array());
        req = forms::patchDensity(engine, req, sc.layers[li], engine.density, req.value("seed", 0u), li);
        engine.applyLayerEffect(req, sc.layers[li], req.value("seed", 0u));
        forms::Engine::globalEffectsLast(req);
        if(req["items"] == oldItems){ rc->second = std::make_shared<formsgl::json>(req); return false; }   // same picture: only the note of its density
        submitImmediate(req);
        return true;
    }
    void immediateDensity(){
        std::set<std::string> done;
        int n = 0;
        for(auto& im : stagePages()) if(im && done.insert(im->id).second && densityRedraw(im)) n++;
        if(n) engine.note("immediate: density " + std::to_string((int)std::lround(engine.density * 100)) + ", " + std::to_string(n) + " pages drawn again");
    }

    // ---- audition
    std::shared_ptr<formsgl::json> auditionRecipe;
    std::shared_ptr<forms::Image> auditionImg;
    ofShortImage auditionTex;   // 16 bits per channel
    bool auditionBusy = false, auditionDirty = false, auditionSeedSet = false;
    unsigned auditionItemsSeed = 0, auditionSeed = 0;
    forms::Harmony auditionHarmony;
    std::string auditionScene;
    void requestAudition(){
        if(auditionBusy){ auditionDirty = true; return; }
        auto it = engine.presets.find(editor.selectedPresetRef());
        if(it == engine.presets.end()) return;
        std::string scene = editor.previewSceneName();
        if(!engine.scenes.count(scene)) return;
        loadSchemas();
        if(!schemas) return;
        if(scene != auditionScene) auditionSeedSet = false;
        auto sc = engine.scenes.find(scene);
        if(!sc->second.layers.empty()) engine.itemBars = engine.layerBars(sc->second.layers[0]);
        // the user's preview seed stays as it was; the audition keeps its own (same image while editing)
        bool lock = engine.previewLock, set = engine.previewSeedSet;
        unsigned seed = engine.previewSeed; std::string seedScene = engine.previewSeedScene; auto harm = engine.previewHarmony;
        if(auditionSeedSet){ engine.previewLock = true; engine.previewSeedSet = true; engine.previewSeed = auditionSeed; engine.previewHarmony = auditionHarmony; engine.previewSeedScene = scene; }
        else engine.previewLock = false;
        forms::json items = forms::json::array();
        if(engine.onlyEffects(it->second)) for(auto& u : formsgl::glUnderlay()) items.push_back(u);
        for(auto& x : engine.presetItemsSeeded(it->second, auditionItemsSeed)) items.push_back(x);
        auto paths = engine.makePreviews(scene, {{"audition:" + it->first, items}});
        if(!auditionSeedSet){ auditionSeed = engine.previewSeed; auditionHarmony = engine.previewHarmony; auditionScene = scene; auditionSeedSet = true; }
        engine.previewLock = lock; engine.previewSeedSet = set; engine.previewSeed = seed; engine.previewSeedScene = seedScene; engine.previewHarmony = harm;
        if(paths.empty()) return;
        formsgl::json req;
        try { std::ifstream in(paths[0]); in >> req; } catch(...) { return; }
        std::string id = req.value("id", std::string());
        engine.pending.erase(id);           // not for the editor's preview pane
        engine.pendingSince.erase(id);
        formsgl::substituteUnderlay(req);
        Job j; j.req = req; j.font = font; j.kind = KIND_AUDITION;
        auditionBusy = true;
        submit(std::move(j));
    }
    void auditionDelivered(Done& d){
        auditionBusy = false;
        if(d.error.empty() && (d.rgba || d.texture) && auditionOn){
            const auto& stage = d.stageRgba ? d.stageRgba : d.rgba;
            int sw = d.stageRgba ? d.stageW : d.w, sh = d.stageRgba ? d.stageH : d.h;
            if(!d.texture) auditionTex.setFromPixels(stage->data(), sw, sh, OF_IMAGE_COLOR_ALPHA);
            auto img = std::make_shared<forms::Image>();
            const formsgl::json& q = *d.recipe;
            img->id = q.value("id", std::string()); img->scene = q.value("scene", std::string());
            img->layer = 1; img->bars = forms::barsFrom(q, "bars", 8); img->bpm = q.value("bpm", 120.0); img->notegrid = q.value("notegrid", 100);
            img->roots = q.value("roots", std::vector<int>()); img->trans = q.value("trans", std::vector<int>());
            for(auto& n : q.value("scaleNotes", formsgl::json::array())) img->scaleNotes.push_back((float)n.get<double>());
            img->palette = q.value("paletteName", std::string()); img->scale = q.value("scaleName", std::string());
            img->blend = "lighten"; img->gain = 1; img->preview = true;
            img->texture = attachTexture(d.texture);
            img->stageRgba = d.texture ? nullptr : d.stageRgba; img->stageW = d.stageW; img->stageH = d.stageH; img->pw = d.w; img->ph = d.h;
            auditionRecipe = d.recipe;
            auditionImg = img;
        }else if(!d.error.empty()) engine.note("audition: " + d.error);
        if(auditionDirty){ auditionDirty = false; requestAudition(); }
    }

    // ---- immediate mode: the pages that use an edited instrument (or a scene) drawn again in place
    int immediateInFlight = 0;
    bool immediateWaitNoted = false;
    void submitImmediate(const formsgl::json& req, int kind = KIND_IMMEDIATE){
        Job j; j.req = req; j.font = font; j.kind = kind;
        j.outDir = outputDir(req.value("session", std::string()));
        immediateInFlight++;
        submitFiltered(std::move(j));
    }

    // ---- instrument mute / solo: pages are drawn without the muted instruments (or with only the
    // soloed ones); their recipe stays whole, so unmuting draws them again complete. A change redraws
    // every page on the stage and waiting (KIND_FILTER: in place, never written to the archive, which
    // keeps each page as it was first drawn); a page already queued with an older filter is redrawn
    // when it arrives.
    uint64_t filterGen = 1;
    bool filterDirty = false;
    forms::ItemFilter currentFilter() const {
        forms::ItemFilter f;
        auto expand = [&](const std::set<std::string>& refs, std::set<std::string>& out){
            for(auto& r : refs){
                auto it = engine.presets.find(r);
                if(it == engine.presets.end()){ out.insert(r); continue; }
                for(auto& x : forms::itemRefs(engine, it->second)) out.insert(x);
            }
        };
        expand(instMute, f.mute);
        expand(instSolo, f.solo);
        return f;
    }
    void submitFiltered(Job j){
        j.original = std::make_shared<formsgl::json>(j.req);
        auto f = currentFilter();
        if(f.active() && !j.req.contains("preview")) j.req = forms::filterItems(j.req, f);   // editor previews: always whole
        j.filterGen = filterGen;
        submit(std::move(j));
    }
    void instrumentFilterChanged() override {
        filterGen++;
        redrawForFilter();
    }
    void redrawForFilter(){
        if(immediateInFlight > 0){ filterDirty = true; return; }   // after this batch, with the latest sets
        std::set<std::string> done;
        int n = 0;
        for(auto& im : stagePages()){
            if(!done.insert(im->id).second) continue;
            auto rc = recipes.find(im->id);
            if(rc == recipes.end()) continue;
            formsgl::json req = *rc->second;
            req.erase("result");
            submitImmediate(req, KIND_FILTER);
            n++;
        }
        if(n) engine.note("mute / solo: " + std::to_string(n) + " pages drawn again");
    }
    void immediateFor(const forms::Preset& before,const forms::Preset& after){
        const std::string ref=after.ref();
        std::set<std::string> refs = forms::itemRefs(engine, before), done;   // a stack: also the presets it plays
        for(auto& r : forms::itemRefs(engine, after)) refs.insert(r);
        int n = 0;
        for(auto& im : stagePages()){
            if(!done.insert(im->id).second) continue;
            auto rc = recipes.find(im->id);
            if(rc == recipes.end() || !forms::usesPreset(*rc->second, refs)) continue;
            unsigned seed = (unsigned)im->seed ^ (unsigned)std::hash<std::string>()(ref);
            auto req=*rc->second;auto sit=engine.scenes.find(im->scene);int li=req.value("layer",1)-1;
            // A scene with its own version of this instrument sees the edit through it: the shared
            // edit acts only where the scene left the setting alone.
            forms::Preset b=before,a=after;
            const forms::Scene* own=sit!=engine.scenes.end() ? &sit->second : nullptr;
            if(own && own->instruments.is_object()){
                auto d=own->instruments.find(ref);
                if(d!=own->instruments.end()){
                    b=forms::presetWithOwn(before,*d);a=forms::presetWithOwn(after,*d);
                    if(forms::changedKeys(b,a).empty()) continue;   // the scene's version hides the whole edit
                }
            }
            forms::Engine::OwnScope ownScope(engine,own);
            if(sit!=engine.scenes.end() && li>=0 && li<(int)sit->second.layers.size()){
                formsgl::json regular=formsgl::json::array();
                for(auto& item:req.value("items",formsgl::json::array()))if(!item.value("layerEffect",false))regular.push_back(item);
                req["items"]=std::move(regular);
                if(forms::usesPreset(req,refs))req=forms::patchForPreset(engine,req,b,a,seed);
                engine.applyLayerEffect(req,sit->second.layers[li],req.value("seed",0u));
                forms::Engine::globalEffectsLast(req);
            }else req=forms::patchForPreset(engine,req,b,a,seed);
            req.erase("densityBase");   // Density now starts from the page as it is
            submitImmediate(req);
            n++;
        }
        if(n) engine.note("immediate: " + std::to_string(n) + " pages with " + ref + " drawn again");
    }
    // a scene's pages on stage after an edit: its look (palette, scale, note grid) and, per page, its
    // layer's instruments (patchSelection: only what the cell edit changes)
    void immediateSceneEdit(const std::string& name, const forms::Scene& before, const forms::Scene& after){
        std::set<std::string> done;
        int n = 0;
        forms::Engine::OwnScope ownScope(engine, &after);   // this scene's own versions of its instruments
        // the scene's own instrument versions that this edit changed (made, changed or reverted):
        // each as the scene played it before and plays it now
        struct OwnEdit { std::string ref; forms::Preset b, a; std::set<std::string> refs; };
        std::vector<OwnEdit> ownEdits;
        {
            std::set<std::string> keys;
            if(before.instruments.is_object()) for(auto& [k, v] : before.instruments.items()) keys.insert(k);
            if(after.instruments.is_object()) for(auto& [k, v] : after.instruments.items()) keys.insert(k);
            for(auto& k : keys){
                auto lib = engine.presets.find(k);
                if(lib == engine.presets.end()) continue;
                formsgl::json db = before.instruments.is_object() ? before.instruments.value(k, formsgl::json::object()) : formsgl::json::object();
                formsgl::json da = after.instruments.is_object() ? after.instruments.value(k, formsgl::json::object()) : formsgl::json::object();
                if(db == da) continue;
                OwnEdit e; e.ref = k;
                e.b = forms::presetWithOwn(lib->second, db); e.a = forms::presetWithOwn(lib->second, da);
                if(forms::changedKeys(e.b, e.a).empty()) continue;
                e.refs = forms::itemRefs(engine, e.b);
                for(auto& r : forms::itemRefs(engine, e.a)) e.refs.insert(r);
                ownEdits.push_back(std::move(e));
            }
        }
        for(auto& im : stagePages()){
            if(im->scene != name || !done.insert(im->id).second) continue;
            auto rc = recipes.find(im->id);
            if(rc == recipes.end()) continue;
            formsgl::json req = *rc->second;
            req.erase("result");
            bool changed = forms::patchForScene(engine, req, after);
            int li = req.value("layer", 1) - 1;
            if(li >= 0 && li < (int)before.layers.size() && li < (int)after.layers.size() && !after.layers[li].fixed.is_object()){
                const float dens = req.value("chanceDensity", 0.0f);   // the chances the page was drawn with (the node's Density)
                formsgl::json sel = forms::patchSelection(engine, req, forms::withDensity(before.layers[li], dens), forms::withDensity(after.layers[li], dens), req.value("seed", 0u), li);
                if(sel["items"] != req["items"]){ req = sel; changed = true; }
            }
            if(li>=0 && li<(int)after.layers.size()) for(auto& oe : ownEdits){
                // the layer effect's items are drawn anew below; patch the instruments' own items only
                formsgl::json own = req;
                formsgl::json regular = formsgl::json::array();
                for(auto& item : req.value("items", formsgl::json::array())) if(!item.value("layerEffect", false)) regular.push_back(item);
                own["items"] = regular;
                if(!forms::usesPreset(own, oe.refs)) continue;
                unsigned seed = (unsigned)im->seed ^ (unsigned)std::hash<std::string>()(oe.ref);
                own = forms::patchForPreset(engine, own, oe.b, oe.a, seed);
                if(own["items"] != regular){ req["items"] = own["items"]; changed = true; }
            }
            if(li>=0 && li<(int)after.layers.size()){
                // Also move an existing layer effect back to the end after cell additions.
                auto oldItems=req["items"];
                engine.applyLayerEffect(req,after.layers[li],req.value("seed",0u));
                forms::Engine::globalEffectsLast(req);
                if(oldItems!=req["items"])changed=true;
            }
            if(!changed) continue;
            req.erase("densityBase");   // Density now starts from the page as it is
            submitImmediate(req);
            n++;
        }
        if(n) engine.note("immediate: " + std::to_string(n) + " pages of " + name + " drawn again");
    }
    void immediateDelivered(Done& d){
        immediateInFlight = std::max(0, immediateInFlight - 1);
        if(d.error.empty() && (d.rgba || d.texture)){
            keepRecipe(d.id, d.recipe);
            const formsgl::json& q = *d.recipe;
            std::vector<float> notes;
            for(auto& n : q.value("scaleNotes", formsgl::json::array())) notes.push_back((float)n.get<double>());
            std::string png;
            auto texture = attachTexture(d.texture);
            for(auto& im : stagePages()){
                if(im->id != d.id) continue;
                im->rgba.reset(); im->pw = d.w; im->ph = d.h;
                im->texture = texture;
                im->covers = d.covers;
                im->stageRgba = d.texture ? nullptr : d.stageRgba; im->stageW = d.stageW; im->stageH = d.stageH;
                if(!notes.empty()) im->scaleNotes = notes;
                im->palette = q.value("paletteName", im->palette); im->scale = q.value("scaleName", im->scale);
                im->notegrid = q.value("notegrid", im->notegrid);
                png = im->png;
            }
            if(!png.empty()){
                scroller.reloadPng(png);
                for(auto& rg : rings) rg.reloadPng(png);
            }
        }else if(!d.error.empty()) engine.note("immediate " + d.id + ": " + d.error);
        if(immediateInFlight == 0){
            if(filterDirty){ filterDirty = false; redrawForFilter(); }
        }
    }

    // every job gets a sequence number; results are delivered in that order
    void submit(Job j){
        {
            std::lock_guard<std::mutex> lk(m);
            j.seq = nextSeq++;
            j.asked = std::chrono::steady_clock::now();
            jobs.push_back(std::move(j));
            queued = (int)jobs.size();
        }
        startThreads();
        cv.notify_one();
    }
    void startThreads(){
        if(!workersPool.empty()) return;
        unsigned hc = std::thread::hardware_concurrency();
        int n = (int)std::max(1u, std::min(8u, hc > 2 ? hc - 2 : 1u));
        gpus.resize(n);
        for(int i = 0; i < n; i++) workersPool.emplace_back([this, i]{ run(i); });
        archiver = std::thread([this]{ runArchive(); });
    }
    int thumbsAsked = 0, thumbsDone = 0;

    // Make thumbnails: a preview of every instrument, drawn here and kept as thumbnails/<ref>.png
    void makeThumbnailsGL(){
        std::string scene = editor.previewSceneName();
        if(!engine.scenes.count(scene) && !engine.scenes.empty()) scene = engine.scenes.begin()->first;
        if(!engine.scenes.count(scene)){ engine.note("thumbnails: no scene to draw them with"); return; }
        loadSchemas();
        if(!schemas){ engine.note("thumbnails: no parameter schemas"); return; }
        auto sc = engine.scenes.find(scene);
        if(!sc->second.layers.empty()) engine.itemBars = engine.layerBars(sc->second.layers[0]);
        // the user's preview seed is kept as it was
        bool lock = engine.previewLock, set = engine.previewSeedSet;
        unsigned seed = engine.previewSeed; std::string seedScene = engine.previewSeedScene; auto harm = engine.previewHarmony;
        engine.previewLock = false;
        std::vector<std::pair<std::string, forms::json>> variants;
        for(auto& [ref, pr] : engine.presets){
            forms::json items = forms::json::array();
            if(engine.onlyEffects(pr)) for(auto& u : formsgl::glUnderlay()) items.push_back(u);
            for(auto& x : engine.presetItemsSeeded(pr, 1234)) items.push_back(x);
            variants.push_back({ref, items});
        }
        auto paths = engine.makePreviews(scene, variants);
        engine.previewLock = lock; engine.previewSeedSet = set; engine.previewSeed = seed; engine.previewSeedScene = seedScene; engine.previewHarmony = harm;
        std::filesystem::path dir = engine.rendererFolder / "thumbnails";
        int n = 0;
        for(size_t i = 0; i < paths.size() && i < variants.size(); i++){
            formsgl::json req;
            try { std::ifstream in(paths[i]); in >> req; } catch(...) { continue; }
            std::string id = req.value("id", std::string());
            engine.pending.erase(id);          // not a preview for the editor: straight to its thumbnail
            engine.pendingSince.erase(id);
            Job j;
            j.req = req;
            j.font = font;
            j.thumbRef = variants[i].first;
            j.thumbPath = (dir / (variants[i].first + ".png")).string();
            submit(std::move(j));
            n++;
        }
        thumbsAsked = n; thumbsDone = 0;
        engine.note("thumbnails: drawing " + std::to_string(n) + " instruments with " + scene);
    }
    // a render for a thumbnail: over black, 384 x 216 (box filter), thumbnails/<ref>.png
    static bool writeThumb(const formsgl::RenderResult& r, const std::string& path){
        const int TW = 384, TH = 216;
        ofPixels px;
        px.allocate(TW, TH, OF_PIXELS_RGB);
        for(int ty = 0; ty < TH; ty++) for(int tx = 0; tx < TW; tx++){
            int x0 = tx * r.w / TW, x1 = std::max(x0 + 1, (tx + 1) * r.w / TW), y0 = ty * r.h / TH, y1 = std::max(y0 + 1, (ty + 1) * r.h / TH);
            unsigned sum[3] = {0, 0, 0}, n = 0;
            for(int y = y0; y < y1; y++) for(int x = x0; x < x1; x++){
                const uint16_t* s = &r.rgba[((size_t)y * r.w + x) * 4];   // 16-bit straight alpha -> 8-bit over black
                for(int c = 0; c < 3; c++) sum[c] += (unsigned)(((uint64_t)s[c] * s[3] + 32767) / 65535 + 128) / 257;
                n++;
            }
            for(int c = 0; c < 3; c++) px.getData()[((size_t)ty * TW + tx) * 3 + c] = (unsigned char)(sum[c] / std::max(1u, n));
        }
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), ec);
        std::string tmp = path + ".tmp.png";
        if(!ofSaveImage(px, tmp)) return false;
        std::filesystem::rename(tmp, path, ec);
        return !ec;
    }
    std::shared_ptr<const formsgl::Sketch::GlyphFont> font;
    std::atomic<bool> useGpu{true};
    std::atomic<bool> live16{true};   // 16-bit live GPU buffers and page textures (off: 8 bits); saved with the patch
    std::atomic<bool> gpuKicks{false}; // exact shader retained; CPU wins the measured live workload
    std::shared_ptr<formsgl::SharedTexturePool> texturePool;
    std::string gpuError;
    // the sketch's font (letters), read on the main thread: glyph outlines at size 1 (em)
    void loadFont(){
        std::string f = (engine.rendererFolder / "FuturaStd-Bold.otf").string();
        if(font || fontTried == f || !std::filesystem::exists(f)) return;
        fontTried = f;
        font = formsgl::loadGlyphFont(f);   // FreeType (formsGLFont.h): the metrics Processing uses
    }
    std::string fontTried;
    std::mutex m;
    std::condition_variable cv;
    std::deque<Job> jobs;
    std::map<uint64_t, Done> doneBySeq;
    uint64_t nextSeq = 0, nextDeliver = 0;
    std::vector<std::thread> workersPool;
    std::vector<std::unique_ptr<formsgl::GpuEffectsCGL>> gpus;   // one per worker (its own GL context)
    std::mutex largeRenderMutex; // keep multiple print-size canvases from exhausting RAM together
    bool quit = false;
    // the archive: PNG + sidecar written after the image is handed over
    struct ArchiveJob { std::vector<formscover::Mask> covers; std::shared_ptr<const std::vector<uint16_t>> rgba; int w = 0, h = 0; formsgl::json sidecar; std::string png, json, dir, base,appendPath,appendLine; };
    std::mutex am;
    std::condition_variable acv;
    std::deque<ArchiveJob> archiveJobs;
    std::vector<double> archiveTimes;
    std::thread archiver;
    bool archiveQuit = false;
    std::atomic<bool> archive{true};
    std::atomic<int> drawnHere{0}, errorsHere{0}, lastMs{0}, queued{0};
    int sentOn = 0;
    std::string lastWhyNot, schemaError;
    std::shared_ptr<formsgl::Schemas> schemas;   // replaced, never changed, while the worker reads it
    std::string schemasFrom;

    void describe(){
        description = "ESPECTRES 2027. Picks the scene of each image (setlist or manual), its instruments and "
                      "harmony, and draws it (the ESPECTRES_2027 renderer in C++, on background threads). Its "
                      "built-in player scrolls the images (Output) at the BPM of the image under the header and "
                      "moves the header; the actual* outputs are the values of the image under the header; it "
                      "post-processes and sonifies. Show opens the window. Data: bin/data/Forms (inspector: Renderer). "
                      "Follows a Global Pitch node when the patch has one: Scale (with its root and Shift) replaces the scene's scale "
                      "for the next pages (at once with Immediate mode), Transpose bends the sound (Mod 12, Low Scale, Negative and Detune are not followed).";
    }
    static std::string portedList(){
        std::string s;
        for(auto& g : formsgl::Sketch::ported()) s += (s.empty() ? "" : ", ") + g;
        return s;
    }

    // the parameter schemas: Forms GL Box's own (embedded, formsGLSchemas.h). If the renderer folder
    // has no generators.json (the editor reads it), it is written from them.
    void loadSchemas(){
        std::string f = "embedded";
        if(schemas && schemasFrom == f) return;
        auto s = std::make_shared<formsgl::Schemas>();
        bool ok = s->loadEmbedded();
        std::filesystem::path gj = engine.rendererFolder / "generators.json";
        if(ok && !engine.rendererFolder.empty() && std::filesystem::exists(engine.rendererFolder) && !std::filesystem::exists(gj)){
            std::ofstream o(gj);
            o << formsgl::json::parse(formsgl::generatorSchemasJson()).dump(1);
        }
        std::lock_guard<std::mutex> lk(m);   // the worker copies it under the same lock
        if(ok){ schemas = s; schemaError.clear(); }
        else { schemaError = "the embedded schemas are unreadable: nothing can be drawn"; schemas.reset(); }
        schemasFrom = f;
    }

    // where a session's images go
    std::string outputDir(const std::string& session){
        return (engine.rendererFolder / "sessions" / session).string();
    }

    void run(int wi){
        std::shared_ptr<formsgl::Schemas> sch;
        for(;;){
            Job j;
            {
                std::unique_lock<std::mutex> lk(m);
                cv.wait(lk, [this]{ return quit || !jobs.empty(); });
                if(quit){ lk.unlock(); gpus[wi].reset(); return; }
                j = std::move(jobs.front());
                jobs.pop_front();
                queued = (int)jobs.size();
                sch = schemas;
            }
            double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - j.asked).count();
            Done d;
            std::string jid;
            try { jid = j.req.value("id", std::string()); } catch(...) {}
            // nothing may escape a worker thread (it would end the app): any failure is an error for that image
            try {
                if(!j.requestPath.empty()) forms::writeJsonAtomic(j.requestPath,j.original?*j.original:j.req);
                d = render(j, sch, wi);
            }
            catch(std::exception& e){ d = Done(); d.id = jid; d.thumbRef = j.thumbRef; d.kind = j.kind; d.error = std::string("render failed: ") + e.what(); errorsHere++; }
            catch(...){ d = Done(); d.id = jid; d.thumbRef = j.thumbRef; d.kind = j.kind; d.error = "render failed"; errorsHere++; }
            if(d.error.empty() && d.thumbRef.empty()) d.queueMs = waited;
            {
                std::lock_guard<std::mutex> lk(m);
                doneBySeq[j.seq] = std::move(d);
            }
        }
    }

    // writes one archived image: PNG then sidecar, each through a temporary name
    static bool writeArchive(const ArchiveJob& a, double& ms, std::string& err){
        auto t0 = std::chrono::steady_clock::now();
        if(!a.appendPath.empty()){
            forms::fs::create_directories(forms::fs::path(a.appendPath).parent_path());
            std::ofstream file(a.appendPath,std::ios::app);
            if(!file || !(file<<a.appendLine)){err="cannot append session timeline";return false;}
            return true;
        }
        std::error_code ec;
        std::filesystem::create_directories(a.dir, ec);
        std::filesystem::path pngTmp = std::filesystem::path(a.dir) / ("." + a.base + ".tmp.png"), jsonTmp = std::filesystem::path(a.dir) / ("." + a.base + ".tmp.json");
        ofShortPixels px;   // the page as it was drawn: a 16-bit PNG
        px.setFromPixels(a.rgba->data(), a.w, a.h, OF_PIXELS_RGBA);
        if(!ofSaveImage(px, pngTmp.string())){ err = "cannot write " + pngTmp.string(); return false; }
        double pngMs = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        for(size_t i=0;i<a.covers.size();++i){
            auto& mask=a.covers[i]; if(!mask.alpha) continue;
            ofShortPixels pixels; pixels.setFromPixels(mask.alpha->data(),mask.w,mask.h,OF_PIXELS_GRAY);
            std::string name=a.base+"_cover"+std::to_string(i)+".png";
            auto tmp=std::filesystem::path(a.dir)/("."+name+".tmp.png");
            if(!ofSaveImage(pixels,tmp.string())){err="cannot write cover mask";return false;}
            std::filesystem::rename(tmp,std::filesystem::path(a.dir)/name,ec);
            if(ec){err="cannot publish cover mask: "+ec.message();return false;}
        }
        formsgl::json side = a.sidecar;
        side["result"]["archiveMs"] = (int)std::lround(pngMs);
        {
            std::ofstream o(jsonTmp);
            o << side.dump(1);
        }
        std::filesystem::rename(pngTmp, a.png, ec);
        if(!ec) std::filesystem::rename(jsonTmp, a.json, ec);
        if(ec){ err = "cannot move the output: " + ec.message(); return false; }
        ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        return true;
    }
    void runArchive(){
        for(;;){
            ArchiveJob a;
            {
                std::unique_lock<std::mutex> lk(am);
                acv.wait(lk, [this]{ return archiveQuit || !archiveJobs.empty(); });
                if(archiveJobs.empty()) return;   // quit, and everything written
                a = std::move(archiveJobs.front());
                archiveJobs.pop_front();
            }
            double ms = 0; std::string err;
            bool ok = false;
            try { ok = writeArchive(a, ms, err); } catch(std::exception& e){ err = e.what(); } catch(...){ err = "archive failed"; }
            std::lock_guard<std::mutex> lk(am);
            if(ok && a.appendPath.empty()) archiveTimes.push_back(ms);
            else if(!ok){ archiveErrors++; lastArchiveError = a.base + ": " + err; }
        }
    }
    std::atomic<int> archiveErrors{0};
    std::string lastArchiveError;

    Done render(Job& j, const std::shared_ptr<formsgl::Schemas>& sch, int wi){
        Done d;
        auto& req = j.req;
        d.id = req.value("id", std::string());
        if(!sch){ d.error = "no parameter schemas"; errorsHere++; return d; }
        size_t requestPixels = (size_t)1920 * 1080;
        try {
            auto size = req.value("size", std::vector<int>{1920, 1080});
            if(size.size() == 2 && size[0] > 0 && size[1] > 0) requestPixels = (size_t)size[0] * size[1];
        }catch(...){}
        std::unique_lock<std::mutex> largeLock(largeRenderMutex, std::defer_lock);
        if(requestPixels >= (size_t)64 * 1024 * 1024) largeLock.lock();
        formsgl::RenderResult r;
        {
            formsgl::Sketch sk(*sch);
            sk.font = j.font;
            if(useGpu){
                auto& g = gpus[wi];
                if(!g){
                    g = std::make_unique<formsgl::GpuEffectsCGL>(texturePool ? texturePool->context() : nullptr);
                    if(!g->ok()){ std::lock_guard<std::mutex> lk(m); gpuError = g->error; }
                }
                if(g && g->ok()){ g->enableExactRaster = gpuKicks.load(); sk.gpu = g.get(); }
            }
            r = sk.render(req);
        } // release the CPU canvas before uploading/archiving its RGBA output
        d.covers = std::move(r.covers);
        d.kind = j.kind;
        d.filterGen = j.filterGen;
        if(j.kind == KIND_AUDITION){
            if(!r.error.empty()){ d.error = r.error; return d; }
            d.recipe = std::make_shared<formsgl::json>(req);
            d.rgba = std::make_shared<const std::vector<uint16_t>>(std::move(r.rgba)); d.w = r.w; d.h = r.h;
            prepareTexture(d, wi);
            if(d.texture) d.rgba.reset(); // release the large CPU buffer on this worker
            return d;
        }
        if(!j.thumbRef.empty()){
            d.thumbRef = j.thumbRef;
            if(!r.error.empty()) d.error = r.error;
            else if(!writeThumb(r, j.thumbPath)) d.error = "cannot write " + j.thumbPath;
            return d;
        }
        if(!r.error.empty()){ d.error = r.error; errorsHere++; return d; }
        int layer = req.value("layer", 1), seed = req.value("seed", 0);
        std::string base = "L" + std::to_string(layer) + "_" + d.id + "_s" + std::to_string(seed);
        std::filesystem::path png = std::filesystem::path(j.outDir) / (base + ".png"), json = std::filesystem::path(j.outDir) / (base + ".json");
        // a page drawn again (immediate mode) overwrites its files; a new page never does
        if(j.kind != KIND_IMMEDIATE && j.kind != KIND_FILTER && (std::filesystem::exists(png) || std::filesystem::exists(json))){ d.error = png.string() + " already exists"; errorsHere++; return d; }
        // the sidecar: the request, its real size and a result block (as Render.pde writes it)
        req["size"] = {r.w, r.h};
        double waited = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - j.asked).count() - r.renderMs;
        formsgl::json res = {{"png", base + ".png"}, {"renderMs", r.renderMs}, {"canvas", {r.w, r.h}}, {"offscreen", false},
                             {"renderer", "FormsGL"}, {"rendererVersion", "phase7-1"}, {"codeHash", "formsgl"},
                             {"queueMs", (int)std::lround(std::max(0.0, waited))}, {"items", r.itemResults}};
        if(j.kind == KIND_IMMEDIATE) res["redrawn"] = true;   // drawn again after an edit (immediate mode)
        if(!d.covers.empty()){
            res["coverMasks"]=formsgl::json::array();
            for(size_t i=0;i<d.covers.size();++i){
                auto metadata=formscover::write(d.covers[i].settings);
                metadata["png"]=base+"_cover"+std::to_string(i)+".png";
                res["coverMasks"].push_back(metadata);
            }
        }
        req["result"] = res;
        d.recipe = j.original ? j.original : std::make_shared<formsgl::json>(req);   // the whole recipe (mute / solo)
        auto pixels = std::make_shared<const std::vector<uint16_t>>(std::move(r.rgba));
        d.rgba = pixels; d.w = r.w; d.h = r.h;
        d.renderMs = r.renderMs + prepareTexture(d, wi);
        ArchiveJob a;
        a.covers = d.covers;
        a.rgba = pixels; a.w = r.w; a.h = r.h; a.sidecar = req; a.png = png.string(); a.json = json.string(); a.dir = j.outDir; a.base = base;
        bool preview = req.contains("preview");
        if(preview){
            // editor previews: the editor reads the file, so it is written now
            double ms = 0; std::string err;
            if(!writeArchive(a, ms, err)){ d.error = err; errorsHere++; return d; }
        }else if(archive && j.kind != KIND_FILTER){   // an edit's redraw overwrites the old files (queued after them); mute / solo never does
            // (immediate: only a page already archived is rewritten; one still queued is written with
            //  its new pixels when its turn comes, as the archive queue keeps the newest job last)
            std::lock_guard<std::mutex> lk(am);
            archiveJobs.push_back(std::move(a));
            acv.notify_one();
        }
        d.png = png.string();
        d.json = json.string();
        if(j.kind != KIND_IMMEDIATE && j.kind != KIND_FILTER) drawnHere++;
        lastMs = d.renderMs;
        if(d.texture) d.rgba.reset(); // only the archive worker retains CPU pixels, when needed
        return d;
    }

    static std::shared_ptr<ofTexture> attachTexture(const std::shared_ptr<formsgl::SharedTexture>& resource){
        if(!resource) return {};
        auto texture = std::shared_ptr<ofTexture>(new ofTexture, [resource](ofTexture* t){ delete t; });
        texture->setUseExternalTextureID(resource->id);
        auto& data = texture->getTextureData();
        data.textureTarget = resource->target; data.glInternalFormat = resource->internalFormat;
        data.width = data.tex_w = resource->w; data.height = data.tex_h = resource->h;
        data.tex_t = resource->target == GL_TEXTURE_2D ? 1 : resource->w;
        data.tex_u = resource->target == GL_TEXTURE_2D ? 1 : resource->h;
        data.bFlipTexture = false;
        return texture;
    }

    double prepareTexture(Done& d, int wi){
        auto t0 = std::chrono::steady_clock::now();
        if(!d.rgba || d.w < 1 || d.h < 1) return 0;
        d.stageW = d.w; d.stageH = d.h;
        if(texturePool){
            // Upload on the worker even when GPU drawing is disabled: this only
            // transfers the exact CPU pixels, it does not change their rendering.
            auto& gpu=gpus[wi];
            if(!gpu) gpu=std::make_unique<formsgl::GpuEffectsCGL>(texturePool->context());
            if(gpu->ok()){
                const bool eight=!live16.load();
                d.texture=gpu->upload(*d.rgba,d.w,d.h,texturePool,GL_RGBA,eight);
                d.coverTextures.resize(d.covers.size());
                for(size_t i=0;i<d.covers.size();++i){
                    auto& mask=d.covers[i];
                    if(mask.alpha) d.coverTextures[i]=gpu->upload(*mask.alpha,mask.w,mask.h,texturePool,GL_RED,eight);
                }
            }
            if(!d.texture){
                std::lock_guard<std::mutex> lock(m);
                gpuError = gpu->error;
            }
        }
        // Exact CPU fallback: preserve native pixels as well. Never downsample a projection page.
        if(!d.texture) d.stageRgba = d.rgba;
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    }
};
