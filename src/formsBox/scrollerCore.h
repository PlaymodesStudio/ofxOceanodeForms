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
#include "ofxThreadedImageLoader.h"
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
    // The absolute beat under the header, counted from the wraps (each image is bars x 4 beats)
    double headerBeat = 0;
    double headerStart = 0, headerLen = 32;   // the image under the header: its start beat and length
    int defaultBars = 8;                  // for beats while no image is loaded
    // Immediate replace: a new image takes the place of the one under the header as soon as it
    // is ready, instead of entering at the right edge on the next wrap (the beat count stays
    // continuous; the screen shows the new image, wrapped, until the next one)
    bool immediate = false;
    // where the images were placed last frame: left edge x (px) and bars (the grid overlay)
    struct Span { float x; int bars; };
    std::vector<Span> lastSpans;

    void allocate(int w, int h){
        if(w < 1 || h < 1) return;
        if(fbo.isAllocated() && (int)fbo.getWidth() == w && (int)fbo.getHeight() == h) return;
        ofFboSettings st;
        st.width = w; st.height = h; st.internalformat = GL_RGBA;
        fbo.allocate(st);
    }

    // One frame: load, wrap on phase, draw into the FBO, report the header image to L
    void update(Schedule::Layer* L, float phase, float headerPos, int w, int h){
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
            if(e->rgba && e->pw > 0 && e->ph > 0 && e->rgba->size() >= (size_t)e->pw * e->ph * 4){
                // in memory (Forms GL Box): straight to a texture, no PNG round trip
                auto t0 = std::chrono::steady_clock::now();
                images[loadingIndex].setFromPixels(e->rgba->data(), e->pw, e->ph, OF_IMAGE_COLOR_ALPHA);
                pipelineStats().upload.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
            }else{
                loader.loadFromDisk(images[loadingIndex], e->png);
                isLoading = true;
            }
        }
        bool once = L && L->replaceNext && entries[loadingIndex] == L->replaceNext;   // Switch now
        if((immediate || once) && !isLoading && loaded(loadingIndex) && entries[loadingIndex]){
            if(once) L->replaceNext.reset();
            // replace what is under the header now, keeping the beat under it where it was
            float c = phase + (w > 0 ? headerPos / w : 0);
            swapDup = once && !immediate && c < 1;
            double hb = headerBeat;
            int ni = loadingIndex;
            leavingIndex = appearingIndex = ni;
            loadingIndex = (ni + 1) % 3;
            int other = (ni + 2) % 3;
            images[loadingIndex].clear(); entries[loadingIndex].reset();
            images[other].clear(); entries[other].reset();
            origin = hb - c * beatsOf(ni);   // both slots show it: the header beat stays where it was
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
        if(entries[leavingIndex]) lastSpans.push_back({-phase * w, std::max(1, entries[leavingIndex]->bars)});
        if(entries[appearingIndex]) lastSpans.push_back({(1 - phase) * w, std::max(1, entries[appearingIndex]->bars)});
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
    // A page's PNG was rewritten (immediate mode): load it again where it is held
    void reloadPng(const std::string& png){
        for(int i = 0; i < 3; i++){
            auto& e = entries[i];
            if(!e || e->png != png || (isLoading && i == loadingIndex)) continue;
            if(e->rgba && e->pw > 0 && e->ph > 0 && e->rgba->size() >= (size_t)e->pw * e->ph * 4)
                images[i].setFromPixels(e->rgba->data(), e->pw, e->ph, OF_IMAGE_COLOR_ALPHA);
            else images[i].load(png);
        }
    }

    // Saves the current output and which images were showing (for checks)
    void saveFrame(const std::string& base, float phase){
        if(!fbo.isAllocated()) return;
        ofPixels px;
        fbo.readToPixels(px);
        ofSaveImage(px, base + ".png");
        ofBuffer info;
        info.append("phase " + ofToString(phase) + "\n");
        info.append("leaving " + (entries[leavingIndex] ? entries[leavingIndex]->png : std::string("-")) + "\n");
        info.append("appearing " + (entries[appearingIndex] ? entries[appearingIndex]->png : std::string("-")) + "\n");
        ofBufferToFile(base + ".txt", info);
    }

private:
    ofxThreadedImageLoader loader;
    ofImage images[3];
    std::shared_ptr<Image> entries[3];
    int leavingIndex = 0, appearingIndex = 1, loadingIndex = 2;
    bool isLoading = false;
    bool swapDup = false;                 // Switch now: the right-hand copy of the switch image is a placeholder
    float oldPhase = 0;
    double origin = 0;                    // beat of the leaving image's left edge

    // an empty slot (before the first image) takes no beats, so layer 1's first image starts at
    // beat 0, the bar its harmony was made for
    double beatsOf(int i) const { return entries[i] ? 4.0 * entries[i]->bars : 0.0; }

    bool loaded(int i){ return images[i].isAllocated() && images[i].isUsingTexture(); }

    void drawSlot(int i, float x, int w, int h, float headerPos, Schedule::Layer* L){
        if(!loaded(i) || (isLoading && i == loadingIndex)) return;
        float op = (keepAlpha && opacityOf && entries[i]) ? ofClamp(opacityOf(*entries[i]), 0, 1) : 1.0f;
        if(op <= 0) { /* fully transparent: still track the header below */ }
        else{
            ofSetColor(255, 255, 255, 255 * op);   // scales alpha (and so the premultiplied colour)
            images[i].draw(x, 0, w, h);
            ofSetColor(255);
        }
        if(headerPos > x && headerPos <= x + w && entries[i]){
            inHeader = entries[i]->png;
            if(L) L->header = entries[i];
        }
    }
};

} // namespace forms
