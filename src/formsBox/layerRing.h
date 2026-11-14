//
//  layerRing.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Phase 3: the scrolling of one extra layer (layers 2..4). Its images are placed in absolute
//  beats (Image::startBeat, from the Forms Box engine), each bars x 4 beats long and one view wide.
//  Every frame the image under the header is found from the shared header beat (counted by
//  layer 1's ScrollerCore), and drawn so the header sits at the same beat in every layer: longer
//  layers scroll more slowly (parallax) and all layers cross each beat line at the header together.
//  If the last image ends and nothing follows, it is repeated (a placeholder that a late image
//  replaces). Drawn into a transparent FBO: premultiplied colour and real alpha, for the composite.
//

#pragma once

#include "ofMain.h"
#include "formsShortLoader.h"
#include "formsEngine.h"
#include <chrono>

namespace forms {

class LayerRing {
public:
    ofFbo fbo;
    bool active = true;                       // the current scene has this layer (else no repeats)
    std::function<void(const ofTexture&,float,float,float,float,float,float,float)> drawWithGain;   // texture, x, w, h, gain, fraction, u0, uw
    std::function<float(const Image&)> opacityOf;   // each image's own opacity, read live
    // the first hard cut (a scene change that cuts every layer) at a beat in (from, to], or -1
    std::function<double(double from, double to)> hardCutIn;
    // the scene layer 1 plays (or will play) at a beat; "" if unknown
    std::function<std::string(double beat)> sceneAt;
    // Immediate replace: a ready image takes the slot under the header at once (see ScrollerCore)
    bool immediate = false;

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

    // The image under the header (for the composite settings), or null
    std::shared_ptr<Image> current;
    struct Drawn { std::string png; double start, len; float x; int slice = 0; };   // slice: a phrase's page
    std::vector<Drawn> lastDrawn;             // what the last frame drew, and where (Save frame)

    // Where a beat is on screen: the images are drawn edge to edge from the one under the header,
    // each one view wide at its own length; past them, at `lenAfter` beats per view
    double screenX(const Schedule::Layer& L, double B, double beat, float headerPos, int w, double lenAfter) const {
        int cur = -1;
        for(int i = 0; i < (int)L.slots.size(); i++) if(L.slots[i].start <= B && B < L.slots[i].end()){ cur = i; break; }
        if(cur < 0) return headerPos + (beat - B) / lenAfter * w;
        const auto& c = L.slots[cur];
        double x = headerPos - (B - c.start) / c.len * w;
        double edge = c.start;
        for(int i = cur; i < (int)L.slots.size(); i++){
            const auto& s = L.slots[i];
            if(i > cur && std::fabs(L.slots[i - 1].end() - s.start) > 1e-6) break;
            if(beat < s.end() + 1e-9) return x + (beat - s.start) / s.len * w;
            x += (s.end() - s.start) / s.len * w;
            edge = s.end();
        }
        return x + (beat - edge) / lenAfter * w;
    }

    void update(Schedule::Layer& L, double B, float headerPos, int w, int h){
        allocate(w, h);
        // 1. ready images take their place (a real image replaces a repeat placeholder)
        while(!L.ready.empty()){
            auto img = L.ready.front();
            L.ready.pop_front();
            double len = 4.0 * std::max(1.0 / kBarGrid, img->bars);
            bool once = L.replaceNext && img == L.replaceNext;   // Switch now: this one replaces, once
            if(once) L.replaceNext.reset();
            if(immediate || once){
                // it takes the place of the image under the header (same start), and what it overlaps goes
                double st = std::ceil(B / 4.0) * 4.0 - len;
                for(auto& sl : L.slots) if(sl.start <= B && B < sl.end()){ st = sl.start; break; }
                if(st > B) st = len < 4.0 ? std::floor(B / len + 1e-9) * len : B;   // a page shorter than a bar: on its own grid
                for(auto it = L.slots.begin(); it != L.slots.end();){
                    // (Switch now: also everything after it, e.g. repeats of the old image)
                    if((it->start < st + len - 1e-6 && it->end() > st + 1e-6) || (once && it->start >= st - 1e-6)) it = L.slots.erase(it); else ++it;
                }
                img->startBeat = st; img->cutBeat = -1;
                if(once){ onceId = img->id; onceStart = st; }
                Schedule::Layer::Slot sl; sl.img = img; sl.start = st; sl.len = len;
                auto pos = std::find_if(L.slots.begin(), L.slots.end(), [&](const Schedule::Layer::Slot& x){ return x.start > st; });
                L.slots.insert(pos, sl);
                continue;
            }
            double start = img->startBeat >= 0 ? img->startBeat : (L.scheduledEnd >= 0 ? L.scheduledEnd : std::ceil(B / 4.0) * 4.0);
            // a place that is already on screen (or past) is taken: an image never pops in
            // mid-view. It goes to the next free place after what has been shown instead.
            // Switch now: the switch image's next copies follow it (they arrive with it: no pop-in)
            bool follows = !onceId.empty() && img->id == onceId && img->repeatIndex > 0;
            if(follows){ start = onceStart + img->repeatIndex * len; img->startBeat = start; img->cutBeat = -1; }
            bool taken = !follows && screenX(L, B, start, headerPos, w, len) < w;   // its left edge would be inside the view
            if(!follows) for(auto& sl : L.slots) if(sl.seen && sl.start < start + len - 1e-6 && sl.end() > start + 1e-6) taken = true;
            if(taken){
                // a phrase's page belongs where it is in the phrase: a late one is dropped (moved,
                // it would show the wrong part of the music); the pages after it still come
                if(img->slices > 1){ L.retimed++; continue; }
                double end = std::ceil(B / 4.0) * 4.0;
                for(auto& sl : L.slots) if(sl.seen || !sl.repeat) end = std::max(end, sl.end());
                // Keep the page on its original layer cycle. Starting at the current bar
                // offsets different-length layers every time a render arrives late.
                end = start + std::max(0.0, std::ceil((end - start) / len - 1e-9)) * len;
                // still visible at that place? then it waits one more image length
                for(int guard = 0; guard < 64 && screenX(L, B, end, headerPos, w, len) < w; guard++) end += len;
                // only where its own scene plays (a late image does not carry its scene into
                // another one); there, cut at the next hard cut
                if(sceneAt){ std::string sc = sceneAt(end); if(!sc.empty() && sc != img->scene){ L.retimed++; continue; } }
                start = end;
                img->startBeat = start;
                img->cutBeat = -1;
                if(hardCutIn){ double c = hardCutIn(start, start + len); if(c > start + 1e-6 && c < start + len - 1e-6) img->cutBeat = c; }
                L.retimed++;
            }
            // drop what it overlaps that hasn't been on screen (repeat placeholders)
            for(auto it = L.slots.begin(); it != L.slots.end();){
                if(!it->seen && it->start < start + len - 1e-6 && it->end() > start + 1e-6) it = L.slots.erase(it);
                else ++it;
            }
            // a hard cut at the start of this image cuts the one before it there
            for(auto& o : L.slots) if(o.start < start - 1e-6 && o.end() > start + 1e-6) o.cut = start;
            Schedule::Layer::Slot sl; sl.img = img; sl.start = start; sl.len = len;
            if(img->cutBeat > start) sl.cut = img->cutBeat;
            auto pos = std::find_if(L.slots.begin(), L.slots.end(), [&](const Schedule::Layer::Slot& x){ return x.start > start; });
            L.slots.insert(pos, sl);
        }
        // 2. nothing after the last image, and the place after it is about to enter the view at
        // the right edge (or the header has passed its end): repeat it, so the view never shows a
        // hole that a late image would pop into (only while the scene has this layer)
        while(active && !L.slots.empty()){
            auto& last = L.slots.back();
            double end = last.end();
            bool needed = end <= B || screenX(L, B, end, headerPos, w, last.len) < w;
            if(!needed) break;
            // never across a hard cut: an image from before a cut is not repeated after it (the
            // new scene's image comes, or nothing if that scene has no such layer)
            double imgStart = last.img->startBeat >= 0 ? last.img->startBeat : last.start;
            if(hardCutIn && hardCutIn(imgStart, end) >= 0) break;
            Schedule::Layer::Slot r = last;
            r.start = end; r.repeat = true; r.seen = false; r.cut = -1;
            if(hardCutIn){ double c = hardCutIn(end, end + r.len); if(c > end + 1e-6 && c < end + r.len - 1e-6) r.cut = c; }
            L.slots.push_back(r);
            L.repeats++;
        }
        // 2b. a gap between two images (a late image placed after a jump ahead) in the same scene,
        // about to enter the view: repeat the image before it, as above, so the layer shows no
        // hole while the renderer cannot keep up (very short pages)
        for(int guard = 0; active && guard < 64; guard++){
            bool filled = false;
            for(size_t i = 0; i + 1 < L.slots.size(); i++){
                auto& a = L.slots[i]; const auto& n = L.slots[i + 1];
                double end = a.end();
                if(n.start - end < 1e-6) continue;
                if(!(end <= B || screenX(L, B, end, headerPos, w, a.len) < w)) break;
                if(sceneAt && sceneAt(end) != a.img->scene) continue;      // another scene there: its own images
                double imgStart = a.img->startBeat >= 0 ? a.img->startBeat : a.start;
                if(hardCutIn && hardCutIn(imgStart, end) >= 0) continue;
                Schedule::Layer::Slot r = a;
                r.start = end; r.repeat = true; r.seen = false; r.cut = -1;
                if(n.start < end + r.len - 1e-6) r.cut = n.start;           // up to the next image
                if(hardCutIn){ double c = hardCutIn(end, end + r.len); if(c > end + 1e-6 && (r.cut < 0 || c < r.cut)) r.cut = c; }
                L.slots.insert(L.slots.begin() + (long)i + 1, r);
                L.repeats++;
                filled = true;
                break;
            }
            if(!filled) break;
        }
        // 3. forget images well behind the header (kept a while for beat repeats)
        while(L.slots.size() > 1 && L.slots[1].end() < B - 16) L.slots.pop_front();
        L.scheduledEnd = L.slots.empty() ? -1 : L.slots.back().end();

        // 4. which slot is under the header
        int cur = -1;
        for(int i = 0; i < (int)L.slots.size(); i++)
            if(L.slots[i].start <= B && B < L.slots[i].end()){ cur = i; break; }
        std::shared_ptr<Image> now = cur >= 0 ? L.slots[cur].img : nullptr;
        if(now && now != current){ L.shown++; L.header = now; }
        if(!now) L.header.reset();
        current = now;

        // 5. placement: the current image's left edge at headerPos - u * w; neighbours that touch
        // it at -w / +w. Before the first image (a layer starting) it comes in at the first
        // image's speed.
        // each image is one view wide; a cut image shows only up to its cut
        auto vis = [&](const Schedule::Layer::Slot& s){ return (float)((s.end() - s.start) / s.len) * w; };
        std::vector<std::pair<int, float>> draw;   // slot, x
        if(cur >= 0){
            auto& c = L.slots[cur];
            float u = (float)((B - c.start) / c.len);
            L.phase = u;
            float x0 = headerPos - u * w;
            draw.push_back({cur, x0});
            float x = x0;
            for(int i = cur - 1; i >= 0 && x > 0; i--){
                if(std::fabs(L.slots[i].end() - L.slots[i + 1].start) > 1e-6) break;
                x -= vis(L.slots[i]); draw.push_back({i, x});
            }
            x = x0;
            for(int i = cur + 1; i < (int)L.slots.size(); i++){
                if(std::fabs(L.slots[i - 1].end() - L.slots[i].start) > 1e-6){
                    // after a gap (its scene has no such layer there): the next image comes in at its
                    // own speed, as it will once the gap reaches the header, so it never pops in
                    auto& s = L.slots[i];
                    float xs = std::max(x + vis(L.slots[i - 1]), headerPos + (float)((s.start - B) / s.len) * w);
                    if(xs < w) draw.push_back({i, xs});
                    break;
                }
                x += vis(L.slots[i - 1]);
                if(x >= w) break;
                draw.push_back({i, x});
            }
        }else{
            L.phase = 0;
            for(int i = 0; i < (int)L.slots.size(); i++){
                auto& s = L.slots[i];
                if(s.start > B){
                    float x = headerPos + (float)((s.start - B) / s.len) * w;
                    if(x < w) draw.push_back({i, x});
                    break;
                }
            }
        }

        // 6. textures: load what is drawn or next, free the rest
        std::set<std::string> want;
        for(auto& [i, x] : draw) want.insert(L.slots[i].img->png);
        if(cur >= 0 && cur + 1 < (int)L.slots.size()) want.insert(L.slots[cur + 1].img->png);
        for(auto it = images.begin(); it != images.end();){
            if(!want.count(it->first) && it->first != loadingPng) it = images.erase(it);
            else ++it;
        }
        if(!loadingPng.empty()){
            auto& im = images[loadingPng];
            if(im && im->isAllocated() && im->isUsingTexture()) loadingPng.clear();
        }
        if(loadingPng.empty()){
            for(auto& p : want){
                bool resident = false;
                for(auto& sl : L.slots) if(sl.img && sl.img->png == p && sl.img->texture){ resident = true; break; }
                if(resident) continue;
                if(images.count(p)) continue;
                auto im = std::make_shared<ofShortImage>();
                images[p] = im;
                // in memory (Forms GL Box): straight to a texture
                std::shared_ptr<Image> src;
                for(auto& sl : L.slots) if(sl.img && sl.img->png == p && (sl.img->stageRgba || sl.img->rgba)){ src = sl.img; break; }
                const auto& pixels = src ? (src->stageRgba ? src->stageRgba : src->rgba) : std::shared_ptr<const std::vector<uint16_t>>{};
                int imageW = src ? (src->stageRgba ? src->stageW : src->pw) : 0;
                int imageH = src ? (src->stageRgba ? src->stageH : src->ph) : 0;
                if(src && pixels && imageW > 0 && imageH > 0 && pixels->size() >= (size_t)imageW * imageH * 4){
                    auto t0 = std::chrono::steady_clock::now();
                    im->setFromPixels(pixels->data(), imageW, imageH, OF_IMAGE_COLOR_ALPHA);
                    pipelineStats().upload.add(std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count());
                    continue;   // loaded: look at the next one this frame too
                }
                loader.loadFromDisk(*im, p);
                loadingPng = p;
                break;
            }
        }

        lastDrawn.clear();
        for(auto& [i, x] : draw){
            L.slots[i].seen = true;
            lastDrawn.push_back({L.slots[i].img->png, L.slots[i].start, L.slots[i].len, x, L.slots[i].img->slice});
        }
        // 7. draw
        if(!fbo.isAllocated()) return;
        fbo.begin();
        ofClear(0, 0, 0, 0);
        ofPushStyle();
        ofEnableAlphaBlending();
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
        for(auto& [i, x] : draw){
            auto resident = L.slots[i].img->texture;
            auto it = images.find(L.slots[i].img->png);
            if(!resident && (it == images.end() || it->first == loadingPng)) continue;
            ofTexture* texture = resident.get();
            if(!texture){
                auto& im = it->second;
                if(!(im && im->isAllocated() && im->isUsingTexture())) continue;
                texture = &im->getTexture();
            }
            float vw = vis(L.slots[i]);
            float op = opacityOf ? ofClamp(opacityOf(*L.slots[i].img), 0, 2) : 1.0f;
            if(op <= 0) continue;
            ofSetColor(255, 255, 255, 255 * op);
            // a phrase's page: only its window of the image
            float u0 = (float)L.slots[i].img->sliceU0(), uw = (float)L.slots[i].img->sliceUW();
            if(drawWithGain)drawWithGain(*texture,x,w,h,op,vw/w,u0,uw);
            else if(vw >= w - 0.01f && uw >= 0.99999f) texture->draw(x, 0, w, h);
            else texture->drawSubsection(x, 0, vw, h, u0 * texture->getWidth(), 0, uw * vw / w * texture->getWidth(), texture->getHeight());
            ofSetColor(255);
        }
        ofPopStyle();
        fbo.end();
    }

    bool drawable(const std::shared_ptr<Image>& im) const {
        if(!im) return false;
        if(im->texture && im->texture->isAllocated()) return true;
        auto it=images.find(im->png);
        return it!=images.end() && im->png!=loadingPng && it->second && it->second->isAllocated() && it->second->isUsingTexture();
    }

    // A page's PNG was rewritten (immediate mode): it loads again (the next frames)
    void reloadPng(const std::string& png){
        if(png == loadingPng) return;   // still loading the old one: it is re-read below once done
        images.erase(png);
    }

private:
    ShortImageLoader loader;
    std::map<std::string, std::shared_ptr<ofShortImage>> images;   // by PNG path
    std::string loadingPng;
    std::string onceId;                       // the last Switch now image, and where it was placed
    double onceStart = 0;
};

} // namespace forms
