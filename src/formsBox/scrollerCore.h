//
//  scrollerCore.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  The scrolling of rendered images (Forms Scroller logic): a 3-slot ring loaded from the
//  Schedule, forward and backward wraps, the current image repeated if the next one isn't loaded,
//  and the image under the header reported back to the Schedule. Used by Forms Box (its built-in
//  player) and by the Layer Scroller node. The renderer's PNGs are RGBA with straight alpha
//  (black = transparent): they are drawn premultiplied over black and the output stays opaque.
//

#pragma once

#include "ofMain.h"
#include "formsShortLoader.h"
#include "formsEngine.h"
#include <chrono>

namespace forms {

class ScrollerCore {
public:
    ofFbo fbo;
    std::string inHeader;                 // PNG of the image under the header
    // Phase 3: keepAlpha draws into a transparent FBO with premultiplied colour and real alpha,
    // for the layer composite (Forms Box). Off: opaque over black, as before (Layer Scroller).
    bool keepAlpha = false;
    // Phase 3: each image's own opacity (its scene's layer setting, read live), applied as it is
    // drawn, so an image shows its opacity before it reaches the header too. Only with keepAlpha.
    std::function<float(const Image&)> opacityOf;
    std::function<void(const ofTexture&,float,float,float,float,float,float,float)> drawWithGain;   // texture, x, w, h, gain, fraction, u0, uw
    // The absolute beat under the header, counted from the wraps (each image is bars x 4 beats)
    double headerBeat = 0;
    double headerStart = 0, headerLen = 32;   // the image under the header: its start beat and length
    int defaultBars = 8;                  // for beats while no image is loaded
    // Immediate replace: a new image takes the place of the one under the header as soon as it
    // is ready, instead of entering at the right edge on the next wrap. Same-length replacements
    // keep the beat continuous; a length change keeps the new page on the shared bar grid.
    bool immediate = false;
    bool beatRebased = false;             // length changed the beat mapping: transport must seek
    double beatAt(float phase,float headerPos,int w) const {
        double start=origin,left=beatsOf(leavingIndex),right=beatsOf(appearingIndex);
        if(oldPhase-phase>.5f){
            start+=left;left=right;
            if(!isLoading&&loaded(loadingIndex))right=beatsOf(loadingIndex);
        }else if(phase-oldPhase>.5f){
            right=left;
            if(!isLoading&&loaded(loadingIndex))left=beatsOf(loadingIndex);
            start-=left;
        }
        double c=phase+(w>0?headerPos/w:0);
        return c<1?start+c*left:start+left+(c-1)*right;
    }
    // where the images were placed last frame: left edge x (px) and bars (the grid overlay)
    struct Span { float x; double bars; };
    std::vector<Span> lastSpans;

    // The buffer's format: GL_RGBA16 (16 bits per channel, as the images) unless the node's
    // "16-bit live buffers" option is off (GL_RGBA: 8 bits). Changing it reallocates the buffer.
    int bufferFormat = GL_RGBA16;
    void allocate(int w, int h){
        if(w < 1 || h < 1) return;
        if(fbo.isAllocated() && (int)fbo.getWidth() == w && (int)fbo.getHeight() == h && fboFormat == bufferFormat) return;
        ofFboSettings st;
        st.width = w; st.height = h; st.internalformat = bufferFormat;
        fbo.allocate(st);
        fboFormat = bufferFormat;
    }
    int fboFormat = 0;   // what the buffer was allocated with

    // Reset layers: a wrap happens on this frame's phase (the slots have not moved yet): wait a frame
    bool wrapsAt(float phase) const { return std::fabs(oldPhase - phase) > 0.5f; }
    // Reset layers: the image under the header starts again, with its left edge at the header and
    // that edge at `beat` (a bar line, chosen by the node). It fills both slots (its tail shows left
    // of the header); the image that was to come next still comes next. Returns the phase the clock
    // must take, or -1 when it cannot be done on this frame (an image is loading, nothing plays).
    float restartHeaderImage(Schedule::Layer* L, float phase, float headerPos, int w, double beat){
        if(isLoading || w <= 0) return -1;
        float c = phase + headerPos / w;
        const int cur = c < 1 ? leavingIndex : appearingIndex;
        if(!entries[cur] || !loaded(cur)) return -1;
        // the image waiting to come in after it, and (header still in the leaving image) the one after that
        int next = -1;
        if(c < 1 && appearingIndex != cur && loaded(appearingIndex)){
            next = appearingIndex;
            if(loadingIndex != cur && loadingIndex != next && entries[loadingIndex] && L) L->ready.push_front(entries[loadingIndex]);   // back in the queue
        }else if(loadingIndex != cur && loaded(loadingIndex)) next = loadingIndex;
        leavingIndex = appearingIndex = cur;
        loadingIndex = next >= 0 ? next : (cur + 1) % 3;
        const int other = 3 - cur - loadingIndex;
        images[other].clear(); entries[other].reset();
        if(next < 0){ images[loadingIndex].clear(); entries[loadingIndex].reset(); }
        // the appearing copy's left edge at the header: phase + headerPos / w = 1 (header at the
        // left edge of the view: phase 0, and the leaving copy starts there)
        float np = 1.0f - headerPos / w;
        if(np >= 1.0f || np < 0.0f){ np = 0; origin = beat; }
        else{
            while(np + headerPos / w < 1.0f) np = std::nextafter(np, 2.0f);
            if(np >= 1.0f){ np = 0; origin = beat; } else origin = beat - beatsOf(cur);
        }
        oldPhase = np;   // no wrap is read into this jump
        swapDup = false;
        return np;
    }

    // One frame: load, wrap on phase, draw into the FBO, report the header image to L
    void update(Schedule::Layer* L, float phase, float headerPos, int w, int h){
        beatRebased = false;
        allocate(w, h);
        if(isLoading && loaded(loadingIndex)) isLoading = false;
        // Switch now: an old image waiting in the loading slot gives way to the switch image
        if(L && L->replaceNext && !isLoading && entries[loadingIndex] && entries[loadingIndex] != L->replaceNext){
            images[loadingIndex].clear(); entries[loadingIndex].reset();
        }
        if(!isLoading && L && !L->ready.empty() && !loaded(loadingIndex)){
            entries[loadingIndex] = L->ready.front();
            L->ready.pop_front();
            images[loadingIndex].clear();
            const auto& e = entries[loadingIndex];
            const auto& pixels = e->stageRgba ? e->stageRgba : e->rgba;
            int imageW = e->stageRgba ? e->stageW : e->pw;
            int imageH = e->stageRgba ? e->stageH : e->ph;
            if(e->texture && e->texture->isAllocated()){
                // Already resident: no texture allocation or upload on the scrolling thread.
            }else if(pixels && imageW > 0 && imageH > 0 && pixels->size() >= (size_t)imageW * imageH * 4){
                // in memory (Forms GL Box): upload the viewport-sized copy, without a PNG round trip
                auto t0 = std::chrono::steady_clock::now();
                images[loadingIndex].setFromPixels(pixels->data(), imageW, imageH, OF_IMAGE_COLOR_ALPHA);
                pipelineStats().upload.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }else{
                loader.loadFromDisk(images[loadingIndex], e->png);
                isLoading = true;
            }
        }
        bool once = L && L->replaceNext && entries[loadingIndex] == L->replaceNext;   // Switch now
        if((immediate || once) && !isLoading && loaded(loadingIndex) && entries[loadingIndex]){
            if(once) L->replaceNext.reset();
            // Evaluate the old mapping at THIS frame's phase. Using last frame's
            // headerBeat shifts the origin by a frame on every replacement.
            float c = phase + (w > 0 ? headerPos / w : 0);
            swapDup = once && !immediate && c < 1;
            bool hadPage = entries[leavingIndex] || entries[appearingIndex];
            double frameOrigin = origin;
            if(oldPhase - phase > 0.5) frameOrigin += beatsOf(leavingIndex);
            else if(phase - oldPhase > 0.5) frameOrigin -= beatsOf(leavingIndex);
            double hb = c < 1 ? frameOrigin + c * beatsOf(leavingIndex)
                              : frameOrigin + beatsOf(leavingIndex) + (c - 1) * beatsOf(appearingIndex);
            int ni = loadingIndex;
            leavingIndex = appearingIndex = ni;
            loadingIndex = (ni + 1) % 3;
            int other = (ni + 2) % 3;
            images[loadingIndex].clear(); entries[loadingIndex].reset();
            images[other].clear(); entries[other].reset();
            double candidate = hadPage ? hb - c * beatsOf(ni) : origin;
            // Images encode bars from their left edge. A fractional origin makes
            // layer 1's sixteenths disagree with every extra layer's absolute grid.
            // (on bar lines; a page shorter than a bar: on its own length's grid)
            const double grid = std::min(4.0, std::max(4.0 / kBarGrid, beatsOf(ni)));
            origin = std::round(candidate / grid) * grid;
            beatRebased = hadPage && std::fabs(origin - candidate) > 1e-6;
            // The new page occupies both slots; an input wrap on this same frame
            // must not advance its freshly assigned origin a second time.
            oldPhase = phase;
            if(L) L->shown++;
        }
        // Switch now: the copy of the switch image entering at the right gives way to the next
        // image as soon as it is loaded (before the header reaches it), so it plays once
        if(swapDup){
            float c = phase + (w > 0 ? headerPos / w : 0);
            if(c >= 1 || oldPhase - phase > 0.5) swapDup = false;
            else if(!isLoading && loaded(loadingIndex) && entries[loadingIndex] && loadingIndex != leavingIndex && loadingIndex != appearingIndex){
                appearingIndex = loadingIndex;
                loadingIndex = 3 - (leavingIndex + appearingIndex);
                images[loadingIndex].clear(); entries[loadingIndex].reset();
                swapDup = false;
                if(L) L->shown++;
            }
        }
        // forward wrap
        if(oldPhase - phase > 0.5){
            origin += beatsOf(leavingIndex);   // the image that left was this many beats
            if(!isLoading && loaded(loadingIndex)){
                leavingIndex = appearingIndex;
                appearingIndex = loadingIndex;
                loadingIndex = 3 - (leavingIndex + appearingIndex);
                images[loadingIndex].clear();
                entries[loadingIndex].reset();
                if(L) L->shown++;
            }else{
                leavingIndex = appearingIndex;   // nothing new: the current image comes in again
                if(L) L->repeats++;
            }
        }
        // backward wrap
        if(phase - oldPhase > 0.5){
            if(!isLoading && loaded(loadingIndex)){
                appearingIndex = leavingIndex;
                leavingIndex = loadingIndex;
                loadingIndex = 3 - (leavingIndex + appearingIndex);
                images[loadingIndex].clear();
                entries[loadingIndex].reset();
            }else{
                appearingIndex = leavingIndex;
            }
            origin -= beatsOf(leavingIndex);   // the image that came back in
        }
        lastSpans.clear();
        if(entries[leavingIndex]) lastSpans.push_back({-phase * w, std::max(1.0 / kBarGrid, entries[leavingIndex]->bars)});
        if(entries[appearingIndex]) lastSpans.push_back({(1 - phase) * w, std::max(1.0 / kBarGrid, entries[appearingIndex]->bars)});
        // beat under the header: the leaving image spans [origin, origin + its beats), the
        // appearing one follows it
        float c = phase + (w > 0 ? headerPos / w : 0);
        if(c < 1){ headerStart = origin; headerLen = beatsOf(leavingIndex); headerBeat = origin + c * headerLen; }
        else{ headerStart = origin + beatsOf(leavingIndex); headerLen = beatsOf(appearingIndex); headerBeat = headerStart + (c - 1) * headerLen; }
        if(fbo.isAllocated()){
            fbo.begin();
            if(keepAlpha) ofClear(0, 0, 0, 0); else ofBackground(0);
            ofPushStyle();
            ofEnableAlphaBlending();
            // straight alpha → premultiplied colour (over black, opaque; or with its alpha)
            if(keepAlpha) glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
            else glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
            drawSlot(leavingIndex, -phase * w, w, h, headerPos, L);
            drawSlot(appearingIndex, (1 - phase) * w, w, h, headerPos, L);
            ofPopStyle();
            fbo.end();
        }
        oldPhase = phase;
    }

    // The images it holds (on screen or loading), for Forms GL Box's immediate mode
    std::vector<std::shared_ptr<Image>> heldImages() const {
        std::vector<std::shared_ptr<Image>> v;
        for(auto& e : entries) if(e) v.push_back(e);
        return v;
    }
    struct VisiblePage { std::shared_ptr<Image> image; float x; };
    std::vector<VisiblePage> visiblePages() {
        std::vector<VisiblePage> out;
        if(loaded(leavingIndex) && !(isLoading && leavingIndex==loadingIndex) && entries[leavingIndex])
            out.push_back({entries[leavingIndex],-oldPhase*fbo.getWidth()});
        if(loaded(appearingIndex) && !(isLoading && appearingIndex==loadingIndex) && entries[appearingIndex])
            out.push_back({entries[appearingIndex],(1-oldPhase)*fbo.getWidth()});
        return out;
    }
    // A page's PNG was rewritten (immediate mode): load it again where it is held
    void reloadPng(const std::string& png){
        for(int i = 0; i < 3; i++){
            auto& e = entries[i];
            if(!e || e->png != png || (isLoading && i == loadingIndex)) continue;
            if(e->texture && e->texture->isAllocated()) continue;
            const auto& pixels = e->stageRgba ? e->stageRgba : e->rgba;
            int imageW = e->stageRgba ? e->stageW : e->pw;
            int imageH = e->stageRgba ? e->stageH : e->ph;
            if(pixels && imageW > 0 && imageH > 0 && pixels->size() >= (size_t)imageW * imageH * 4)
                images[i].setFromPixels(pixels->data(), imageW, imageH, OF_IMAGE_COLOR_ALPHA);
            else images[i].load(png);
        }
    }

    // Saves the current output and which images were showing (for checks)
    void saveFrame(const std::string& base, float phase){
        if(!fbo.isAllocated()) return;
        ofShortPixels px;   // 16 bits per channel, as the buffer
        fbo.readToPixels(px);
        ofSaveImage(px, base + ".png");
        ofBuffer info;
        info.append("phase " + ofToString(phase) + "\n");
        info.append("leaving " + (entries[leavingIndex] ? entries[leavingIndex]->png : std::string("-")) + "\n");
        info.append("appearing " + (entries[appearingIndex] ? entries[appearingIndex]->png : std::string("-")) + "\n");
        ofBufferToFile(base + ".txt", info);
    }

private:
    ShortImageLoader loader;
    ofShortImage images[3];               // 16 bits per channel (in memory: the renderer's pixels; from disk: the PNG)
    std::shared_ptr<Image> entries[3];
    int leavingIndex = 0, appearingIndex = 1, loadingIndex = 2;
    bool isLoading = false;
    bool swapDup = false;                 // Switch now: the right-hand copy of the switch image is a placeholder
    float oldPhase = 0;
    double origin = 0;                    // beat of the leaving image's left edge

    // an empty slot (before the first image) takes no beats, so layer 1's first image starts at
    // beat 0, the bar its harmony was made for
    double beatsOf(int i) const { return entries[i] ? 4.0 * entries[i]->bars : 0.0; }

    bool loaded(int i) const { return (entries[i] && entries[i]->texture && entries[i]->texture->isAllocated()) ||
                               (images[i].isAllocated() && images[i].isUsingTexture()); }

    void drawSlot(int i, float x, int w, int h, float headerPos, Schedule::Layer* L){
        if(!loaded(i) || (isLoading && i == loadingIndex)) return;
        float op = (keepAlpha && opacityOf && entries[i]) ? ofClamp(opacityOf(*entries[i]), 0, 2) : 1.0f;
        if(op <= 0) { /* fully transparent: still track the header below */ }
        else{
            ofSetColor(255, 255, 255, 255 * op);   // scales alpha (and so the premultiplied colour)
            // a phrase's page: only its window of the image
            float u0 = entries[i] ? (float)entries[i]->sliceU0() : 0.f, uw = entries[i] ? (float)entries[i]->sliceUW() : 1.f;
            if(drawWithGain){
                const auto& texture=entries[i] && entries[i]->texture ? *entries[i]->texture : images[i].getTexture();
                drawWithGain(texture,x,w,h,op,1,u0,uw);
            }else if(uw >= 0.99999f){
                if(entries[i] && entries[i]->texture) entries[i]->texture->draw(x, 0, w, h);
                else images[i].draw(x, 0, w, h);
            }else{
                const ofTexture& t = entries[i] && entries[i]->texture ? *entries[i]->texture : images[i].getTexture();
                t.drawSubsection(x, 0, w, h, t.getWidth() * u0, 0, t.getWidth() * uw, t.getHeight());
            }
            ofSetColor(255);
        }
        if(headerPos > x && headerPos <= x + w && entries[i]){
            inHeader = entries[i]->png;
            if(L) L->header = entries[i];
        }
    }
};

} // namespace forms
