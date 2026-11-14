//
//  formsGLSketch.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  The ESPECTRES_2027 sketch's render of one request (Render.pde renderRequest), ported to C++:
//  the same request, the same seeded random streams (formsGLRandom.h, bit exact), the same
//  musical frame (scale, palette, harmony, safey, barSize, units), the same per-item contract
//  (clipBars, shareKey, refSize) and the ported generators, drawn on the CPU canvas
//  (formsGLCanvas.h). The parameter schemas come from the sketch's generators.json.
//
//  What is ported is listed in Sketch::ported(); a request with anything else (a legacy method, a
//  generator not ported yet, an active process slot) is not drawn here: canRender() says so, and
//  Forms GL Box sends it to Processing.
//
//  Porting rules (so the random stream and the numbers stay the sketch's):
//   - every decimal literal is a float (the Processing preprocessor makes 0.5 into 0.5f);
//   - round() is Math.round(float) (pjRound), int() truncates, pow/cos/sqrt go through double;
//   - random() calls in exactly the sketch's order.
//

#pragma once

#include "formsGLRandom.h"
#include "../formsBox/formsCover.h"
#include "formsGLCanvas.h"
#include "formsGLSchemas.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstdint>
#include <fstream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <string>
#include <vector>

namespace formsgl {
// Echo rhythm: whole steps, conventional dotted steps, and sub-step divisions.
inline float quantizedEchoSteps(float value){
    value=std::clamp(value,.25f,32.f);
    float best=std::max(1.f,std::round(value));
    for(float candidate:{.25f,.375f,.5f,.75f,1.5f})
        if(std::fabs(candidate-value)<std::fabs(best-value)) best=candidate;
    return best;
}

using json = nlohmann::json;

// ------------------------------------------------------------------ schemas (generators.json)

struct ParamDef {
    std::string key, type;
    json def, min, max;
    std::vector<std::string> options;
};
struct GenSchema {
    std::string name;
    std::vector<ParamDef> params;
    std::map<std::string, int> index;
    const ParamDef* find(const std::string& k) const { auto it = index.find(k); return it == index.end() ? nullptr : &params[it->second]; }
};
class Schemas {
public:
    bool load(const std::string& path){
        std::ifstream in(path);
        if(!in) return false;
        json j;
        try { in >> j; } catch(...) { return false; }
        return loadJson(j);
    }
    // Forms GL Box's own copy (formsGLSchemas.h)
    bool loadEmbedded(){
        try { return loadJson(json::parse(generatorSchemasJson())); } catch(...) { return false; }
    }
    bool loadJson(const json& j){
        gens.clear();
        for(auto& g : j.value("generators", json::array())){
            GenSchema s;
            s.name = g.value("name", std::string());
            for(auto& p : g.value("params", json::array())){
                ParamDef d;
                d.key = p.value("key", std::string());
                d.type = p.value("type", std::string());
                d.def = p.contains("default") ? p["default"] : json();
                d.min = p.contains("min") ? p["min"] : json();
                d.max = p.contains("max") ? p["max"] : json();
                if(s.name=="hits" && d.key=="level")d.max=4;
                if(p.contains("options")) for(auto& o : p["options"]) if(o.is_string()) d.options.push_back(o.get<std::string>());
                s.index[d.key] = (int)s.params.size();
                s.params.push_back(d);
            }
            gens[s.name] = s;
        }
        return !gens.empty();
    }
    const GenSchema* find(const std::string& n) const { auto it = gens.find(n); return it == gens.end() ? nullptr : &it->second; }
private:
    std::map<std::string, GenSchema> gens;
};

// Processing's JSONObject.getFloat / getInt / getBoolean on a JSON value
inline float jf(const json& v){
    if(v.is_number()) return (float)v.get<double>();
    if(v.is_boolean()) return v.get<bool>() ? 1.0f : 0.0f;
    throw std::runtime_error("not a number");
}
inline int ji(const json& v){
    if(v.is_number_integer()) return (int)v.get<int64_t>();
    if(v.is_number()) return (int)v.get<double>();
    throw std::runtime_error("not an int");
}

// A request's params over the schema defaults (Generators.pde GenParams)
class GenParams {
public:
    GenParams(const GenSchema& s, const json& given) : schema(&s) {
        for(auto& p : s.params) values[p.key] = (given.is_object() && given.contains(p.key)) ? given[p.key] : p.def;
    }
    bool has(const std::string& k) const { return schema->find(k) != nullptr; }
    const ParamDef& def(const std::string& k) const {
        auto* d = schema->find(k);
        if(!d) throw std::runtime_error("no parameter '" + k + "'");
        return *d;
    }
    int i(const std::string& k) const { auto& d = def(k); return pjConstrain(pjRound(jf(values.at(k))), ji(d.min), ji(d.max)); }
    float f(const std::string& k) const { auto& d = def(k); return pjConstrain(jf(values.at(k)), jf(d.min), jf(d.max)); }
    bool b(const std::string& k) const { auto& v = values.at(k); return v.is_boolean() ? v.get<bool>() : jf(v) != 0; }
    int option(const std::string& k) const {
        auto& v = values.at(k);
        std::string s = v.is_string() ? v.get<std::string>() : std::string();
        auto& d = def(k);
        for(int i = 0; i < (int)d.options.size(); i++) if(d.options[i] == s) return i;
        return 0;
    }
    std::array<float, 2> range(const std::string& k) const {
        auto& a = values.at(k);
        auto& d = def(k);
        float lo = pjConstrain(jf(a.at(0)), jf(d.min), jf(d.max));
        float hi = pjConstrain(jf(a.at(a.size() > 1 ? 1 : 0)), jf(d.min), jf(d.max));
        return {std::min(lo, hi), std::max(lo, hi)};
    }
    std::vector<int> list(const std::string& k) const {
        std::vector<int> out;
        for(auto& v : values.at(k)) out.push_back(ji(v));
        return out;
    }
private:
    const GenSchema* schema;
    json values = json::object();
};

// ------------------------------------------------------------------ the sketch

// Processing colours: 0xAARRGGBB
inline int red(uint32_t c){ return (c >> 16) & 255; }
inline int green(uint32_t c){ return (c >> 8) & 255; }
inline int blue(uint32_t c){ return c & 255; }
inline float alphaOf(uint32_t c){ return (float)((c >> 24) & 255); }

// Java's String.hashCode (ASCII / UTF-8 bytes of ASCII keys)
inline int32_t javaHash(const std::string& s){ uint32_t h = 0; for(unsigned char ch : s) h = 31 * h + ch; return (int32_t)h; }
inline int floorMod(int32_t a, int n){ int m = a % n; return m < 0 ? m + n : m; }

// Effects the host can run on the GPU (formsGLGpu.h); false = not done, the CPU does it
struct GpuEffects {
    virtual ~GpuEffects() = default;
    // Optional exact raster paths. False leaves the canvas untouched for the CPU fallback.
    virtual bool kickSweep(Canvas&, float, float, float, float, int){ return false; }
    // cellX / cellY: the noise cell's width and height in px (square unless maskBars sets the width)
    virtual bool mask(std::vector<uint32_t>& px, int W, int H, const SketchPRNG& prng, float ox, float oy, float cellX, float cellY,
                      float gain, float con, float out, bool invert, float threshold, float amount, bool canvasMode) = 0;
};

struct RenderResult {
    std::vector<formscover::Mask> covers;
    std::string error;           // "" = drawn
    std::vector<uint8_t> rgba;   // straight alpha, w x h x 4
    int w = 0, h = 0;
    json itemResults = json::array();
    int renderMs = 0;
};

class Sketch {
public:
    explicit Sketch(const Schemas& s) : schemas(s) {}

    // The generators drawn here (the rest go to Processing)
    static const std::set<std::string>& ported(){ static const std::set<std::string> p = {"sweep", "hits", "pulse", "masses", "shapes", "effects"}; return p; }

    // Whether this request can be drawn here: every item a ported generator, no active process
    std::string whyNot(const json& req) const {
        if(!schemas.find("effects")) return "no generators.json";
        for(auto& item : req.value("items", json::array())){
            if(item.contains("method")) return "legacy method " + item.value("method", std::string());
            std::string gen = item.value("gen", std::string());
            if(!ported().count(gen)) return "generator '" + gen + "' not ported yet";
            const GenSchema* es = schemas.find("effects");
            json proc = gen == "effects" ? item.value("params", json::object()) : item.contains("process") ? item["process"] : item.value("params", json::object());
            try {
                GenParams q(*es, proc);
                std::string u = unsupportedProcess(q);
                if(!u.empty()) return "process '" + u + "' (not ported yet)";
            }
            catch(std::exception& e){ return std::string("bad process: ") + e.what(); }
        }
        return "";
    }

    RenderResult render(const json& req){
        RenderResult out;
        auto t0 = std::chrono::steady_clock::now();
        int layer = req.value("layer", 1);
        int seed = req.value("seed", 0);
        requestSeed = seed;
        int refW = 0;
        if(req.contains("refSize") && req["refSize"].is_array() && !req["refSize"].empty()) refW = ji(req["refSize"][0]);
        int w = 1920, h = 1080;
        if(req.contains("size")){
            auto sz = req["size"];
            if(sz.size() != 2 || ji(sz[0]) < 16 || ji(sz[1]) < 16 || ji(sz[0]) > 16384 || ji(sz[1]) > 16384){ out.error = "request size out of range (16..16384)"; return out; }
            w = ji(sz[0]); h = ji(sz[1]);
        }
        width = w; height = h;
        // a phrase (several pages drawn as one wide image): pixel sizes follow the view, not the canvas
        viewW = req.contains("viewWidth") ? std::clamp(ji(req["viewWidth"]), 16, w) : w;
        UX = viewW / 1920.0f; UY = height / 1080.0f;
        c.allocate(w, h);
        // reset (resetRenderState) and the musical frame
        P.randomSeed(seed);
        P.noiseSeed(seed);
        P.noiseDetail(4, 0.5f);
        builtinScales();
        prng = SketchPRNG(seed);
        renderMillis = JavaRandom(seed).nextInt(1 << 30);
        bars = req.contains("bars") ? jf(req["bars"]) : 4;
        notegrid = req.value("notegrid", 100);
        paleta = req.value("palette", 1);
        selsc = req.value("scale", 5);
        bpm = req.value("bpm", bpm);
        bassDegree = req.value("bassDegree", 0);
        lineWeight = std::clamp(req.value("lineWeight", 1.0f), 0.1f, 16.0f);
        if(req.contains("scaleNotes")){
            std::vector<int> notes;
            for(auto& n : req["scaleNotes"]) notes.push_back(ji(n));
            if(!notes.empty() && notes.size() <= 24){
                if(selsc < 0 || selsc >= CUSTOM_SCALE) selsc = CUSTOM_SCALE;
                scales[selsc] = notes;
            }
        }
        std::fill(std::begin(rootList), std::end(rootList), 0);
        std::fill(std::begin(transposeList), std::end(transposeList), 0);
        if(req.contains("roots")){ int i = 0; for(auto& v : req["roots"]){ if(i >= 60) break; rootList[i++] = ji(v); } }
        if(req.contains("trans")){ int i = 0; for(auto& v : req["trans"]){ if(i >= 60) break; transposeList[i++] = ji(v); } }
        safey = (float)(height / notegrid);
        barSize = width / bars;
        c.background(0);
        std::fill(std::begin(scale), std::end(scale), -1);
        if(selsc < 0 || selsc >= (int)scales.size()) selsc = 5;
        for(int i = 0; i < (int)scales[selsc].size() && i < 24; i++) scale[i] = scales[selsc][i];
        selectpalette(paleta);
        if(req.contains("paletteColors") && req["paletteColors"].is_array() && !req["paletteColors"].empty()){
            auto& cols = req["paletteColors"];
            for(int i = 0; i < 5; i++) customColour[i] = 0xFF000000u | (uint32_t)std::stoul(cols[i % cols.size()].get<std::string>(), nullptr, 16);
        }
        style = Style();
        style.strokeColor = 0xFFFFFFFFu; style.strokeWeight = UY;
        styleStack.clear();
        gc = &c;
        M = Mat(); matStack.clear();
        lastPatternRef.reset();
        patternHistory.clear();
        layerIdx = layer;
        refScale = refW > 0 ? width / (float)refW : 1;
        // the items in order
        for(auto& item : req.value("items", json::array())){
            json ir = json::object();
            auto ti = std::chrono::steady_clock::now();
            std::string err;
            if(item.contains("method")){ ir["method"] = item["method"]; err = "legacy methods are not ported"; }
            else if(item.contains("gen")){
                ir["gen"] = item["gen"];
                std::vector<uint32_t> snap;
                int cx0 = 0, cx1 = width;
                if(item.contains("clipBars")){
                    auto& cb = item["clipBars"];
                    float b0 = jf(cb[0]), b1 = jf(cb[1]);
                    cx0 = pjConstrain(pjRound(b0 / std::max(1.0f, bars) * width), 0, width);
                    cx1 = pjConstrain(pjRound(b1 / std::max(1.0f, bars) * width), 0, width);
                    snap = c.px;
                    ir["clipBars"] = cb;
                    clipX0 = (float)cx0; clipX1 = (float)cx1;
                    clipBar0 = (int)std::floor(b0); clipBar1 = (int)std::ceil(b1);
                }
                // the scene cell's volume: the item's result is blended back toward the canvas
                // before it (1 = as drawn, 0 = not there); effects act at that strength too
                float igain = item.contains("gain") ? std::clamp(jf(item["gain"]), 0.0f, 1.0f) : 1.0f;
                std::vector<uint32_t> before;
                if(igain < 1) before = c.px;
                hasShareKey = item.contains("shareKey");
                itemShareKey = hasShareKey ? item.value("shareKey", std::string()) : std::string();
                // Draw the cover silhouette from an independent copy of this exact render state.
                // It must neither include earlier instruments nor consume their random stream.
                clearSpans.clear();
                // blocking: the request's (the scene layer's switch) and the instrument's own
                blockOn = formscover::blocking(req) && formscover::blocking(item.value("params", json::object()));
                auto cover = formscover::read(item.value("params", json::object()));
                if(cover.enabled && cover.amount > 0 && item.value("gen", std::string()) != "effects"){
                    Sketch silhouette(*this);
                    silhouette.c.allocateLayer(width, height);
                    silhouette.gc = &silhouette.c;
                    silhouette.gpu = nullptr; // the mask canvas belongs to this copy
                    std::string maskError = silhouette.runGenerator(item.value("gen", std::string()), item.value("params", json::object()),
                                            item.contains("process") ? item["process"] : json());
                    if(maskError.empty()){
                        formscover::Mask mask; mask.settings = cover;
                        // Retain at most an HD mask. Max pooling keeps fine strokes at print sizes.
                        float scale = std::min(1.f, std::min(1920.f / viewW, 1080.f / height));
                        mask.w = std::max(1, (int)std::lround(width * scale)); mask.h = std::max(1, (int)std::lround(height * scale));
                        auto alpha = std::make_shared<std::vector<uint8_t>>((size_t)mask.w * mask.h, 0);
                        for(int y=0; y<height; ++y) for(int x=cx0; x<cx1; ++x){
                            uint32_t v = silhouette.c.px[(size_t)y * width + x];
                            int a = (v>>24)&255u; // geometric coverage, independent of the ink colour
                            size_t q=(size_t)std::min(mask.h-1,y*mask.h/height)*mask.w+std::min(mask.w-1,x*mask.w/width);
                            (*alpha)[q]=std::max((*alpha)[q],(uint8_t)std::lround(a*igain));
                        }
                        mask.alpha=alpha; out.covers.push_back(std::move(mask));
                    }
                }
                // An item with its own seed (a scene-wide effect at one bar of the timeline) draws from that
                // seed and leaves the image's random stream as it was: what it does depends on the bar
                // alone, not on the image it lands in.
                const bool ownSeed = item.contains("seed") && item["seed"].is_number();
                PRandom keptRandom = P;
                if(ownSeed){ long itemSeed = (long)item["seed"].get<double>(); P.randomSeed(itemSeed); P.noiseSeed(itemSeed); }
                err = runGenerator(item.value("gen", std::string()), item.value("params", json::object()),
                                   item.contains("process") ? item["process"] : json());
                if(ownSeed) P = keptRandom;
                // Legacy blank/solo blocks clear their own image to black, which becomes
                // transparent on export. Carry the block as an explicit lower-layer eraser
                // instead of losing its solo behavior when the instrument moves layers.
                if(err.empty() && !clearSpans.empty() && igain>0){
                    formscover::Mask mask;mask.settings.enabled=true;
                    mask.settings.amount=igain;
                    mask.settings.growRight=mask.settings.growHeight=mask.settings.feather=mask.settings.release=0;
                    float scale=std::min(1.f,std::min(1920.f/viewW,1080.f/height));
                    mask.w=std::max(1,(int)std::lround(width*scale));mask.h=std::max(1,(int)std::lround(height*scale));
                    auto alpha=std::make_shared<std::vector<uint8_t>>((size_t)mask.w*mask.h,0);
                    for(auto& span:clearSpans){
                        int x0=std::max(cx0,pjRound(span.first)),x1=std::min(cx1,pjRound(span.first+span.second));
                        int a=std::clamp((int)std::floor(x0*mask.w/(double)width),0,mask.w);
                        int b=std::clamp((int)std::ceil(x1*mask.w/(double)width),0,mask.w);
                        for(int y=0;y<mask.h;++y)if(b>a)std::fill(alpha->begin()+y*mask.w+a,alpha->begin()+y*mask.w+b,255);
                    }
                    mask.alpha=alpha;out.covers.push_back(std::move(mask));
                }
                clipX0 = -1e9f; clipX1 = 1e9f; clipBar0 = -1; clipBar1 = -1;
                hasShareKey = false; itemShareKey.clear(); blockOn = true;
                if(!before.empty() && before.size() == c.px.size()){
                    uint32_t gi = (uint32_t)std::lround(igain * 256);
                    for(size_t q = 0; q < c.px.size(); q++){
                        uint32_t a0 = before[q], a1 = c.px[q];
                        if(a0 == a1) continue;
                        uint32_t r = 0;
                        for(int sh = 0; sh < 32; sh += 8){
                            int v0 = (a0 >> sh) & 255, v1 = (a1 >> sh) & 255;
                            r |= (uint32_t)std::clamp(v0 + (((v1 - v0) * (int)gi) >> 8), 0, 255) << sh;
                        }
                        c.px[q] = r;
                    }
                    ir["gain"] = igain;
                }
                if(!snap.empty()){
                    for(int y = 0; y < height; y++){
                        size_t row = (size_t)y * width;
                        for(int x = 0; x < cx0; x++) c.px[row + x] = snap[row + x];
                        for(int x = cx1; x < width; x++) c.px[row + x] = snap[row + x];
                    }
                }
            }else err = "item has neither 'method' nor 'gen'";
            ir["ms"] = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - ti).count();
            if(!err.empty()) ir["error"] = err;
            out.itemResults.push_back(ir);
        }
        refScale = 1;
        out.w = w; out.h = h;
        out.rgba = c.straightAlpha();
        out.renderMs = (int)std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
        return out;
    }

    const Canvas& canvas() const { return c; }

private:
    const Schemas& schemas;
    Canvas c;
    Canvas* gc = &c;   // what the drawing goes to (g): the canvas, or a process layer
    PRandom P;
    SketchPRNG prng;
    int renderMillis = 0;
    int width = 1920, height = 1080;
    int viewW = 1920;                  // the view's width (= width, or a page of a phrase: viewWidth)
    std::vector<std::pair<float,float>> clearSpans;
    float UX = 1, UY = 1;
    float bars = 4, safey = 10, barSize = 480, refScale = 1;
    int notegrid = 100, paleta = 1, selsc = 5, bpm = 120, layerIdx = 0, requestSeed = 0;
    int scale[24] = {};
    std::vector<std::vector<int>> scales;
    int CUSTOM_SCALE = 29;
    uint32_t customColour[5] = {0xFFFFFFFF, 0xFFBF0000, 0xFFFFCC00, 0xFF00139F, 0xFF065535};
    int rootList[60] = {}, transposeList[60] = {};
    int bassDegree = 0;
    float lineWeight = 1;   // the scene's Line weight: multiplies the stroke of the line instruments (notes, harmonics, flow lines, shape outlines)
    // scale degree idx (any sign) as semitones: past the last degree up an octave, below 0 down one
    int degreeSemis(int idx, int len) const { int o = (int)std::floor(idx / (double)std::max(1, len)); return scale[idx - o * len] + 12 * o; }   // the scene's bass degree: bass instruments add it to the bar's degree
    float clipX0 = -1e9f, clipX1 = 1e9f;
    int clipBar0 = -1, clipBar1 = -1;
    bool hasShareKey = false;
    bool blockOn = true;   // this item's black background ("blank what is underneath") is drawn
    std::string itemShareKey;
    uint32_t genImageColour = 0;

    struct StepPattern;
    std::shared_ptr<StepPattern> lastPatternRef;                 // the last stack layer's pattern (per request)
    std::vector<std::shared_ptr<StepPattern>> patternHistory;    // every pattern layer of the image, in order
    struct Style { uint32_t strokeColor = 0xFFFFFFFF, fillColor = 0xFFFFFFFF; bool stroke = true, fill = true; float strokeWeight = 1; int cap = Canvas::ROUND; };
    Style style;

    // ---- Processing's random
    float random(float high){ return P.random(high); }
    float random(float lo, float hi){ return P.random(lo, hi); }
    int sharedIndex(int n) const { return n <= 0 ? 0 : floorMod((int32_t)((uint32_t)31 * (uint32_t)requestSeed + (uint32_t)javaHash(itemShareKey)), n); }
    bool outsideClip(float x, float w) const { return x + std::max(0.0f, w) < clipX0 || x > clipX1; }

    // ---- style (Processing's stroke() variants)
    // stroke(int argb, float alpha) with a colour that has alpha bits (colorCalcARGB)
    void strokeARGB(uint32_t argb, float alpha){
        if(alpha == 255.0f){ style.strokeColor = argb; return; }
        int ai = (int)(((argb >> 24) & 255) * pjConstrain(alpha / 255.0f, 0.0f, 1.0f));
        style.strokeColor = ((uint32_t)ai << 24) | (argb & 0xFFFFFF);
    }
    // stroke(float gray, float alpha)
    void strokeGray(float gray, float alpha){
        int g = (int)(pjConstrain(gray / 255.0f, 0.0f, 1.0f) * 255);
        int ai = (int)(pjConstrain(alpha / 255.0f, 0.0f, 1.0f) * 255);
        style.strokeColor = ((uint32_t)ai << 24) | ((uint32_t)g << 16) | ((uint32_t)g << 8) | (uint32_t)g;
    }
    void strokeColour(uint32_t argb){ style.strokeColor = argb; style.stroke = true; }   // stroke(int) with a colour
    // stroke(float r, g, b, a) (colorCalc: each clamped, / 255, x 255, truncated)
    void strokeRGBA(float r, float g, float b, float a){
        auto ch = [](float v){ if(v > 255) v = 255; if(v < 0) v = 0; return (int)((v / 255.0f) * 255); };
        style.strokeColor = ((uint32_t)ch(a) << 24) | ((uint32_t)ch(r) << 16) | ((uint32_t)ch(g) << 8) | (uint32_t)ch(b);
        style.stroke = true;
    }
    void fillColour(uint32_t argb){ style.fillColor = argb; style.fill = true; }
    std::vector<Style> styleStack;
    void pushStyle(){ styleStack.push_back(style); }
    void popStyle(){ if(!styleStack.empty()){ style = styleStack.back(); styleStack.pop_back(); } }
    // point(x, y) with the stroke: weight 1 (or less) one pixel, thicker a square / disc
    // > 0 inside a loop that stamps points every `sweepStep` pixels (images taller than 1080 lines):
    // point() then draws swept stamps (Canvas::sweptPoint). 1080-line images keep the measured path.
    float sweepStep = 0;
    struct SweepScope { float& v; SweepScope(float& ref, float step) : v(ref) { v = step > 1.0001f ? step : 0; } ~SweepScope(){ v = 0; } };
    void point(float x, float y){
        if(!style.stroke) return;
        uint32_t s = style.strokeColor;
        if(sweepStep > 0){ gc->sweptPoint(x, y, std::max(1.0f, style.strokeWeight), sweepStep, red(s), green(s), blue(s), (s >> 24) & 255, style.cap); return; }
        if(style.strokeWeight > 1.0001f) gc->thickPoint(x, y, style.strokeWeight, red(s), green(s), blue(s), (s >> 24) & 255, style.cap);
        else gc->point(x, y, red(s), green(s), blue(s), (s >> 24) & 255, style.cap);
    }
    // line(): horizontal / vertical lines of weight 1 by the measured rule; any other as a stroke
    void line(float x0, float y0, float x1, float y1){
        if(!style.stroke) return;
        uint32_t s = style.strokeColor;
        bool thin = style.strokeWeight <= 1.0001f;
        if(thin && gc->lineAxis(x0, y0, x1, y1, red(s), green(s), blue(s), (s >> 24) & 255, style.cap)) return;
        gc->strokePath({{x0, y0}, {x1, y1}}, std::max(1.0f, style.strokeWeight), style.cap, red(s), green(s), blue(s), (s >> 24) & 255);
    }
    // rect(x, y, w, h), CORNER mode, fill only (box coverage, as Java2D)
    void rect(float x, float y, float w, float h){
        if(style.fill){ uint32_t f = style.fillColor; gc->fillRect(x, y, w, h, red(f), green(f), blue(f), (f >> 24) & 255); }
    }

    void builtinScales(){
        scales = {
            {0, 1, 5, 7, 10}, {0, 3, 5, 7, 10}, {0, 2, 3, 7, 8}, {0, 3, 6, 9}, {0, 2, 4, 6, 8, 10}, {0, 1, 4, 5, 7, 10},
            {0, 3, 7}, {0, 3, 7, 11}, {0, 4, 7, 11}, {0, 3, 7, 10, 14}, {0, 3, 7, 10}, {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},
            {0, 3, 5, 8, 10}, {0, 2, 5, 7, 8}, {0, 2, 3, 5, 7, 8, 11}, {0, 2, 3, 5, 7, 8, 10}, {0, 2, 3, 5, 7, 9, 10},
            {0, 2, 4, 5, 7, 9, 11}, {0, 4, 7}, {0, 4, 7, 11, 14}, {0, 1, 4, 5, 7, 10}, {0, 2, 3, 7, 9, 10}, {0, 2, 3, 6, 7, 9},
            {0, 4, 7, 10, 15}, {0, 3, 7, 9}, {0, 1, 4, 5, 7, 8, 11}, {0, 1, 3, 4, 6, 7, 9, 10}, {0, 3, 7, 10, 14, 20},
            {0, 2, 3, 7, 8, 10},
            {0}   // the free slot for scales from Forms Box (initScaleTable)
        };
        CUSTOM_SCALE = (int)scales.size() - 1;
    }
    void selectpalette(int pal){
        static const std::vector<std::vector<uint32_t>> P_ = {
            {0xffffff, 0xff0000, 0xffd500},                                  // 0 (i % 3)
            {0xfac51c, 0x3b8beb, 0xffa800, 0x1c77ac, 0x0f2862},              // 1
            {0xf0e07d, 0xe1d8c7, 0x65b4bb, 0xe3ab46, 0x56432d},              // 2
            {0xf0e9bf, 0x7e7e74, 0xf06b6b, 0xc34040, 0xd89959},              // 3 = palette5
            {0xffffff, 0xffffff, 0xffffff, 0xffffff, 0xffffff},              // 4
            {0xf2f2f2, 0xddf156, 0x078ca9, 0x53d1ec, 0xc5df16},              // 5 = palette3
            {0xFFECBF, 0xff9700, 0x7B318A, 0xffffff},                        // 6 (i % 4)
            {0xffde8a, 0xffbf1c, 0xff8000, 0xff4000, 0xff0000},              // 7
            {0xffffff, 0xffffff, 0xffffff, 0xff0000, 0xff0000},              // 8
            {0xffffff, 0xffffff, 0x00ffff, 0xff0000, 0xff0000},              // 9
            {0xffffff, 0xffffff, 0xffc000, 0xffc000, 0xffc000},              // 10
            {0xffffff, 0xBCD0FF, 0xDAE4FF, 0x529fff, 0x1f3cff},              // 11
            {0xffffff, 0xffffff, 0x00de3b, 0x00de3b, 0x00de3b},              // 12
            {0xffffff, 0xffffff, 0xfc036b, 0xfc036b, 0xfc036b},              // 13
            {0xffffff, 0xffffff, 0xff8000, 0xff8000, 0xff8000},              // 14
            {0xffffff, 0xffffff, 0x6a00ff, 0x6a00ff, 0x6a00ff},              // 15
            {0xffffff, 0xffffff, 0x6a00ff, 0x035efc, 0x6a00ff},              // 16
            {0xfff0d4, 0x245bff, 0xff0000, 0xffa600, 0xffffff},              // 17
            {0xffffda, 0xccffff, 0xfff5cc, 0xffe9ca, 0xffffff}               // 18
        };
        if(pal < 0 || pal >= (int)P_.size()) return;   // unknown: unchanged, as the sketch
        auto& p = P_[pal];
        for(int i = 0; i < 5; i++) customColour[i] = 0xFF000000u | p[i % p.size()];
    }

    // ---- generators

    float drawIn(const std::array<float, 2>& r){ return r[0] == r[1] ? r[0] : r[0] + random(r[1] - r[0]); }
    // a length from its range: free, whole numbers of steps, or the shortest doubled (x1, x2, x4...) so that
    // an end-anchored note still starts on the grid; one random draw in every mode, none for a fixed length
    float drawLen(const std::array<float, 2>& r, int snap){
        if(snap == 0 || r[0] == r[1]) return drawIn(r);
        float u = random(1);
        if(snap == 1){
            int lo = (int)std::ceil(r[0] - 1e-4f), hi = (int)std::floor(r[1] + 1e-4f);
            if(hi < lo) return r[0];
            return (float)(lo + std::min(hi - lo, (int)(u * (hi - lo + 1))));
        }
        int n = 1;
        while(r[0] * (float)(1 << n) <= r[1] * 1.0001f && n < 16) n++;
        return r[0] * (float)(1 << std::min(n - 1, (int)(u * n)));
    }
    int drawInt(const std::array<float, 2>& r){ int lo = pjRound(r[0]), hi = pjRound(r[1]); return lo == hi ? lo : lo + (int)random((float)(hi - lo + 1)); }
    uint32_t pickColour(int mode){
        if(mode == 3) return genImageColour;
        if(mode == 1) return customColour[0];
        if(mode == 2) return customColour[(int)random(5)];
        return 0xFFFFFFFFu;   // color(255)
    }

    static bool processActive(const GenParams& p){
        return p.f("freezeChance") > 0 || p.f("repeatChance") > 0 || p.f("gateChance") > 0
            || p.i("echoRepeats") > 0 || p.f("scaleFilter") > 0 || p.f("maskAmount") > 0 || p.f("edgeFade") > 0
            || p.f("reverseChance") > 0 || p.f("slotChance") > 0 || p.f("microChance") > 0
            || p.f("fadeBottom") > 0 || p.f("fadeTop") > 0
            || p.f("bandChance") > 0 || p.f("pageWindow") > 0 || p.f("tearWindow") > 0
            || p.f("throwChance") > 0 || p.range("mirrorStrips")[1] > 0 || p.range("cellCopies")[1] > 0 || p.i("tileCount") > 1
            || p.f("rampStart") < 1 || p.f("rampEnd") < 1
            || p.f("seqChunk") > 0 || p.f("saturation") < 1 || p.f("transposeChance") > 0 || p.option("cellGrid") > 0;
    }

    std::string runGenerator(const std::string& gen, const json& params, const json& process){
        const GenSchema* schema = schemas.find(gen);
        if(!schema) return "unknown generator '" + gen + "'";
        if(!ported().count(gen)) return "generator '" + gen + "' not ported";
        try {
            GenParams p(*schema, params);
            if(gen == "effects"){
                if(!unsupportedProcess(p).empty()) return "effects: process not ported";
                processCanvas(p, p.i("steps"));
                return "";
            }
            GenParams q(*schemas.find("effects"), process.is_object() ? process : params);
            if(!unsupportedProcess(q).empty()) return gen + ": process not ported";
            genWithProcesses(gen, p, q);
            return "";
        } catch(const RasterError&){ throw; }
        catch(std::exception& e){
            return gen + ": " + e.what();
        }
    }

    void genDraw(const std::string& gen, GenParams& p){
        if(p.has("colour") && p.option("colour") == 3)
            genImageColour = hasShareKey ? customColour[sharedIndex(5)] : customColour[(int)random(5)];
        if(gen == "sweep") genSweepDraw(p);
        else if(gen == "hits") genHitsDraw(p);
        else if(gen == "pulse") genPulseDraw(p);
        else if(gen == "masses") genMassesDraw(p);
        else if(gen == "shapes") genShapesDraw(p);
    }

    // Legacy.pde spectralGradient: each column drawn upwards from the "a" edge, a cosine profile
    // across the band (powH), the opacity ramp along x (powW)
    void spectralGradient(float x0, float y0a, float y0b, float x1, float y1a, float y1b, float op0, float op1, float powW, float powH){
        style.strokeWeight = 1; style.cap = Canvas::PROJECT;   // strokeWeight(1); strokeCap(PROJECT)
        uint32_t strokeColor = style.strokeColor;
        float w = x1 - x0;
        float hvarup = y1b - y0b;
        float hvardown = y1a - y0a;
        float opvar = op1 - op0;
        for(int i = 0; i < w; i++){
            float ph = i / w;
            float op = (op0 + (ph * opvar));
            op = pjPow(op, powW);
            float lineh = std::fabs((y0b + (ph * hvarup)) - (y0a + (ph * hvardown)));
            float linestart = y0a + (ph * hvardown);
            for(float j = 0; j < lineh; j++){
                float phj = j / lineh;
                phj = 1 - ((pjCos(phj * P_TWO_PI) + 1) / 2.0f);
                phj = pjPow(phj, powH);
                strokeARGB(strokeColor, (alphaOf(strokeColor) / 255.0f) * op * 255 * phj);
                point(x0 + i, linestart - j);
            }
        }
        strokeGray(255, 255);
    }

    void genSweepDraw(GenParams& p){
        int count = p.i("count");
        int colourMode = p.option("colour");
        for(int cc = 0; cc < count; cc++){
            float x0 = drawIn(p.range("start")) * width;
            float x1 = std::min((float)width, x0 + drawIn(p.range("length")) * barSize);
            float fl = drawIn(p.range("fromLow")), fh = drawIn(p.range("fromHigh"));
            float tl = drawIn(p.range("toLow")), th = drawIn(p.range("toHigh"));
            strokeColour(pickColour(colourMode));
            spectralGradient(x0, height * (1 - std::min(fl, fh)), height * (1 - std::max(fl, fh)),
                             x1, height * (1 - std::min(tl, th)), height * (1 - std::max(tl, th)),
                             p.f("opStart") * p.f("level"), p.f("opEnd") * p.f("level"), p.f("powW"), p.f("powH"));
        }
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    // ================================================================== hits (Generators.pde)

    static int euclidean(int idx, int steps, int fills, int shift){ return (((idx + shift) * fills) % steps) < fills ? 1 : 0; }
    static bool containsInt(const std::vector<int>& a, int v){ for(int x : a) if(x == v) return true; return false; }
    static float sigmoid(float ph, float tens){
        tens = -tens;
        float norm = (ph * 2) - 1;
        return (((norm - (norm * tens)) / ((tens - (std::fabs(norm) * 2 * tens)) + 1)) + 1) / 2;
    }
    static int floorDiv(int a, int b){ int q = a / b; if((a % b != 0) && ((a < 0) != (b < 0))) q--; return q; }

    // The euclidean lanes of the pattern block: which steps of the image play
    struct StepPattern {
        Sketch* sk = nullptr;
        int steps = 16, P = 16, lanes = 1;
        float density = 100, repChance = 100;
        int barsMode = 0, playOn = 0, patMode = 0;
        float fillChance = 50;
        std::vector<int> stepChances;
        bool reroll = false, runEnds = false;
        std::vector<int> fillChoices, rests;
        std::array<float, 2> shiftR{{0, 0}};
        std::vector<std::vector<char>> pat;
        int curU = -1;
        bool repOk = true, entered = false;
        std::vector<char> log, elig;
        std::shared_ptr<StepPattern> src, fsrc;
        bool noRepeat = false, lastPlayed = false;
        int follow = 0, startOff = 0, stepBars = 1;
        bool imageSteps = false;   // "span 0 bars": the steps divide the whole image, whatever its length

        void init(Sketch* s, GenParams& p, const std::shared_ptr<StepPattern>& self){
            sk = s;
            steps = p.i("steps");
            P = p.i("patternSteps");
            lanes = p.i("lanes");
            density = p.f("density");
            runEnds = p.option("trigger") == 1;
            fillChoices = p.list("fills");
            shiftR = p.range("shift");
            rests = p.list("rests");
            barsMode = p.option("bars");
            playOn = p.option("playOn");
            repChance = p.f("repChance");
            reroll = p.b("reroll");
            patMode = p.option("pattern");
            fillChance = p.f("fillChance");
            stepChances = p.list("stepChances");
            pat.assign(lanes, std::vector<char>(P, 0));
            stepBars = p.i("stepBars");
            if(stepBars <= 0){ imageSteps = true; stepBars = std::max(1, pjRound((float)sk->bars)); }
            log.assign(std::max(1, total()), 0);
            elig.assign(log.size(), 0);
            noRepeat = p.b("noRepeat");
            startOff = pjRound(p.f("patternStart") * steps / stepBars);
            follow = p.option("follow");
            int back = p.i("followBack");
            if(follow > 0) fsrc = (back == 0 || back > (int)sk->patternHistory.size()) ? sk->lastPatternRef : sk->patternHistory[sk->patternHistory.size() - back];
            sk->patternHistory.push_back(self);
            if(patMode == 3){ src = sk->lastPatternRef; return; }
            if(follow == 0) sk->lastPatternRef = self;
            roll();
        }
        void roll(){
            if(patMode > 0){
                for(int l = 0; l < lanes; l++)
                    for(int k = 0; k < P; k++){
                        float cc = patMode == 1 ? fillChance : (stepChances.empty() ? 100.0f : (float)stepChances[k % stepChances.size()]);
                        bool hit = sk->random(100) < cc;
                        pat[l][k] = hit && !containsInt(rests, k);
                    }
                return;
            }
            for(int l = 0; l < lanes; l++){
                int fills = 0;
                if(!fillChoices.empty()){ int pick = (int)sk->random((float)fillChoices.size()); fills = pjConstrain(fillChoices[pick], 0, P); }
                int shift = sk->drawInt(shiftR);
                for(int k = 0; k < P; k++){
                    bool hit = euclidean(k, P, fills, shift) == 1;
                    if(runEnds) hit = hit && euclidean(k + 1, P, fills, shift) != 1;
                    pat[l][k] = hit && !containsInt(rests, k);
                }
            }
        }
        int total() const { return imageSteps ? steps : stepBars == 1 ? (int)std::ceil(sk->bars * steps) : (int)std::ceil(sk->bars * steps / stepBars); }
        int barOf(int s) const { return imageSteps ? (int)(s * sk->bars / steps) : stepBars == 1 ? s / steps : (s * stepBars) / steps; }
        float stepSize() const { return imageSteps ? sk->width / (float)steps : stepBars == 1 ? sk->barSize / steps : sk->barSize * stepBars / steps; }
        float xOf(int s) const { return imageSteps ? s * stepSize() : stepBars == 1 ? (s / steps) * sk->barSize + (s % steps) * (sk->barSize / steps) : s * stepSize(); }
        void advance(int s){
            int u = startOff == 0 ? s / P : floorDiv(s - startOff, P);
            entered = false;
            if(u == curU) return;
            entered = true;
            if(patMode == 3){ curU = u; repOk = true; return; }
            if(reroll && curU >= 0) roll();
            curU = u;
            repOk = !(playOn == 1 && u % 2 == 0) && !(playOn == 2 && u % 2 == 1);
            if(repOk && repChance < 100) repOk = sk->random(100) < repChance;
        }
        bool barPlays(int bar) const {
            int nb = std::max(1, (int)std::ceil(sk->bars));
            if(barsMode == 1) return bar == 0;
            if(barsMode == 2) return bar == nb - 1;
            if(barsMode == 3) return bar % 2 == 0;
            if(barsMode == 4) return bar % 2 == 1;
            return true;
        }
        bool plays(int s){
            if(patMode == 3){
                advance(s);
                bool r = false;
                if(src){ int i = srcIndex(*src, s); r = i >= 0 && src->log[i]; }
                if(s < (int)log.size()) elig[s] = r;
                r = noRepeatFilter(s, r);
                if(s < (int)log.size()) log[s] = r;
                return r;
            }
            if(follow > 0){
                bool e = false;
                if(fsrc){ int i = srcIndex(*fsrc, s); if(i >= 0) e = follow == 1 ? fsrc->log[i] : (fsrc->elig[i] && !fsrc->log[i]); }
                bool r = playsOwn(s);
                if(s < (int)log.size()) elig[s] = e;
                r = r && e;
                r = noRepeatFilter(s, r);
                if(s < (int)log.size()) log[s] = r;
                return r;
            }
            bool r = playsOwn(s);
            r = noRepeatFilter(s, r);
            if(s < (int)log.size()) log[s] = r;
            return r;
        }
        bool noRepeatFilter(int s, bool r){
            if(!noRepeat || s >= (int)elig.size() || !elig[s]) return r;
            if(r && lastPlayed) r = false;
            lastPlayed = r;
            return r;
        }
        int srcIndex(const StepPattern& o, int s) const {
            float t = stepBars == 1 ? s / (float)steps : s * stepBars / (float)steps;
            float si = o.stepBars == 1 ? t * o.steps : t * o.steps / o.stepBars;
            int i = pjRound(si);
            return std::fabs(si - i) < 1e-3f && i >= 0 && i < (int)o.log.size() ? i : -1;
        }
        bool playsOwn(int s){
            advance(s);
            if(s < startOff) return false;
            if(!repOk || !barPlays(barOf(s))) return false;
            if(follow == 0 && s < (int)elig.size()) elig[s] = true;
            int u = (s - startOff) / P;
            int k = (s - startOff) % P;
            int lane = (lanes == 1 || u % 2 == 0) ? 0 : 1 + ((u / 2) % (lanes - 1));
            if(!pat[lane][k]) return false;
            if(density < 100 && sk->random(100) >= density) return false;
            return true;
        }
    };
    std::shared_ptr<StepPattern> newPattern(GenParams& p){
        auto sp = std::make_shared<StepPattern>();
        sp->init(this, p, sp);
        return sp;
    }

    void blankUnder(float x, float w){
        if(!blockOn) return;   // blocking off (instrument or layer): no black background, no eraser
        clearSpans.push_back({x,w});
        pushStyle();
        style.stroke = false;
        fillColour(0xFF000000u);
        rect(x, 0, w, (float)height);
        popStyle();
    }
    void softBlank(float x, float w, float power){
        pushStyle();
        style.strokeWeight = 1;
        for(int i = 0; i < w; i++){
            strokeGray(0, 255 * pjPow(1 - i / std::max(1.0f, w), power));
            line(x + i, 0, x + i, (float)height);
        }
        popStyle();
    }
    void softBlankProfile(float x, float w, float power, float profile){
        pushStyle();
        style.strokeWeight = 1;
        std::vector<float> col(height);
        for(int y = 0; y < height; y++) col[y] = pjPow(y / (float)height, profile);
        for(int i = 0; i < w; i++){
            float a = 255 * pjPow(1 - i / std::max(1.0f, w), power);
            for(int y = 0; y < height; y++){
                strokeGray(0, a * col[y]);
                point(x + i, (float)y);
            }
        }
        popStyle();
    }

    // ---- cells (Cells.pde, Legacy.pde)
    struct SpectralCell {
        float x, y, w, h, noisesc; int envmode; float power;
        float gain = 1.33f, pow2 = 4;
        bool window = false; float op = 1; bool opAlways = false, columns = false;
        float ranRange = 2000; bool white = false; uint32_t col = 0xFFFFFFFF;
    };
    SpectralCell spectralArgs(float x, float y, float w, float h, float noisesc, int envmode, float power){
        SpectralCell s; s.x = x; s.y = y; s.w = w; s.h = h; s.noisesc = noisesc; s.envmode = envmode; s.power = power;
        s.col = style.strokeColor;
        return s;
    }
    static float spectralEnv(float ph, int envmode, float power){
        switch(envmode){
            case 0: ph = 1; break;
            case 1: ph = 1 - ph; ph = pjPow(ph, power); break;
            case 2: ph = pjPow(ph, power); break;
            case 3: ph = 1 - (std::fabs((ph * 2) - 1)); ph = pjPow(ph, power); break;
        }
        return ph;
    }
    void drawSpectral(const SpectralCell& s){
        float noiseScale = s.noisesc / (height / 1080.0f);
        float sx = s.x / refScale; (void)sx;
        float sy = s.y / refScale;
        float ran = random(s.ranRange);
        float rn = ran * noiseScale * refScale;
        style.cap = Canvas::PROJECT;
        if(!s.white) style.strokeWeight = 1;
        float fr = (float)red(s.col), fg = (float)green(s.col), fb = (float)blue(s.col);
        if(s.columns){
            for(int i = 0; i < s.w; i++){
                float noiseVal = prng.perlin((sy + i * noiseScale), rn);
                noiseVal = pjPow(noiseVal * s.gain, s.pow2);
                float ph = spectralEnv(i / s.w, s.envmode, s.power);
                strokeRGBA(fr, fg, fb, s.opAlways ? noiseVal * 255 * ph * s.op : noiseVal * 255 * ph);
                line(i + s.x, s.y, i + s.x, s.y + s.h);
            }
            return;
        }
        // the envelope across the cell depends only on the column: computed once per cell (pure)
        std::vector<float> envCol;
        for(int j = 0; j < s.w; j++) envCol.push_back(spectralEnv(j / s.w, s.envmode, s.power));
        for(int i = 0; i < s.h; i++){
            float ph2 = 1;
            if(s.window){
                ph2 = i / s.h;
                ph2 = 1 - (std::fabs((ph2 * 2) - 1));
                ph2 = sigmoid(ph2, 0.75f);
            }
            float noiseVal = prng.perlin((s.x / refScale + i * noiseScale), rn);
            noiseVal = pjPow(noiseVal * s.gain, s.pow2);
            if(s.white) style.strokeWeight = 1;
            for(int j = 0; j < s.w; j++){
                float ph = envCol[j];
                if(s.white){ style.strokeWeight = 1; strokeGray(255, noiseVal * 255 * ph); }
                else if(s.window) strokeRGBA(fr, fg, fb, pjConstrain((noiseVal * 255 * ph * ph2 * 2), 0.0f, 255.0f) * s.op);
                else strokeRGBA(fr, fg, fb, s.opAlways ? noiseVal * 255 * ph * s.op : noiseVal * 255 * ph);
                point(s.x + j, i + s.y);
            }
        }
    }
    struct KlankCell { float x, y, h, w, pow; bool soft = false; float feather = 0.1f, alpha = 1; uint32_t col = 0xFFFFFFFF; };
    KlankCell klankArgs(float x, float y, float h, float w, float pw){
        KlankCell k; k.x = x; k.y = y; k.h = h; k.w = w; k.pow = pw; k.col = style.fillColor; return k;
    }
    void drawKlank(const KlankCell& k){
        (void)(int)random(5);   // unused, kept for the random stream (ncol = 5)
        style.strokeWeight = 1;
        style.fill = false;
        float x = k.x, y = k.y, h = k.h, w = k.w, pw = k.pow;
        float fr = (float)red(k.col), fg = (float)green(k.col), fb = (float)blue(k.col);
        if(!k.soft){
            for(float j = 0; j < w; j++){
                float ph = 1 - (j / w);
                ph = pjPow(ph, pw);
                strokeRGBA(fr, fg, fb, ph * 255 * 0.75f);
                line(x + j, y, x + j, y + h);
            }
            return;
        }
        for(float j = 0; j < w; j++){
            float phx;
            if(j < w * 0.1f) phx = 1.0f;
            else {
                float decayPos = (j - (w * 0.1f)) / (w * 0.9f);
                phx = 1 - decayPos;
                phx = pjPow(phx, pw);
            }
            float topEdge = h * k.feather, bottomEdge = h * k.feather;
            for(float kk = 0; kk < h; kk++){
                float phy = 1.0f;
                if(kk < topEdge){ phy = kk / topEdge; phy = pjPow(phy, 0.5f); }
                else if(kk > h - bottomEdge){ phy = (h - kk) / bottomEdge; phy = pjPow(phy, 0.5f); }
                float finalAlpha = phx * phy * 255 * k.alpha;
                strokeRGBA(fr, fg, fb, finalAlpha);
                point(x + j, y + kk);
            }
        }
    }
    void taikoSweep(float x, float ystart, float w, float power,float level=1){
        // This routine samples directly in output pixels. Keep the stroke one pixel wide;
        // inheriting UY (8 px at 8K) makes every sample overlap its neighbours, brightening
        // the kick and multiplying the raster work as resolution grows.
        float previousWeight = style.strokeWeight;
        style.strokeWeight = 1;
        float h = height - ystart;
        if(level==1 && style.stroke && gpu && (size_t)width * height >= gpuMinPixels &&
           gpu->kickSweep(*gc, x, ystart, w, power, style.cap)){
            style.strokeWeight = previousWeight;
            return;
        }
        const bool draw = style.stroke;
        const int cap = style.cap;
        // Each x column writes a disjoint set of pixels, so spread the expensive per-pixel
        // power/envelope work over the CPU cores. Avoid mutating the shared Sketch style in the
        // workers; Canvas::point keeps the same Java2D ROUND-cap blend as point().
        parallelRows((int)std::ceil(w), [&](int ix){
            float i = (float)ix;
            float ph = i / w;
            float ph2 = 1 - (i / w);
            ph = pjPow(ph, 0.25f);
            ph2 = pjPow(ph2, power * 2);
            float y2 = (ph * h) + ystart;
            float h2 = height - y2;
            for(float j = 0; j < h2; j++){
                float ph3 = pjPow(j / h2, power / 2);
                float alphaFloat = (ph2 * 255 * ph3)*level;
                int alpha = (int)(pjConstrain(alphaFloat / 255.0f, 0.0f, 1.0f) * 255);
                if(draw) gc->point(i + x, j + y2, 255, 255, 255, alpha, cap);
            }
        });
        style.strokeWeight = previousWeight;
    }
    void gradblock_op(float x, float y, float w, float h, float pw, float op){
        uint32_t sc = style.strokeColor;
        style.strokeWeight = 1;
        for(float j = 0; j < h; j++){
            float ph = j / h;
            ph = 1 - ((pjCos(ph * P_TWO_PI) + 1) / 2.0f);
            ph = pjPow(ph, pw);
            strokeRGBA((float)red(sc), (float)green(sc), (float)blue(sc), 255 * ph * op);
            line(x, y + j, x + w, y + j);
        }
    }
    void taiko(float x, float y, float w, float h){
        style.cap = Canvas::PROJECT;
        style.strokeWeight = 1;
        style.fill = false;
        float powran = random(1, 2);
        for(float j = 0; j < w; j++){
            float ph = 1 - (j / w);
            ph = pjPow(ph, 6 * powran);
            strokeGray(255, ph * 255);
            for(float k = y; k < y + h; k++){
                float ph2 = (k - y) / (h);
                ph2 = pjPow(ph2, 2 * powran);
                strokeGray(255, ph2 * ph * 255);
                point(x + j, k);
            }
        }
    }

    // One hit of a cell in the band [yTop, yBot] from x over w
    void hitCell(GenParams& p, int cell, float x, float yTop, float yBot, float w, float level, uint32_t col){
        float h = std::max(1.0f, yBot - yTop);
        if(cell == 0){
            auto stR = p.range("strips");
            auto shR = p.range("stripHeight");
            if(stR[1] > 0 || shR[1] > 0){
                int n = 0;
                if(stR[1] > 0 && shR[1] <= 0) n = std::max(1, drawInt(stR));
                float sh = n > 0 ? h / n : 0;
                auto scR = p.range("stripContrast");
                float scc = p.f("stripColourChance");
                float syy = yTop;
                for(int i = 0; n > 0 ? i < n : syy < yTop + h; i++){
                    float y0 = n > 0 ? yTop + i * sh : syy;
                    float hh;
                    if(n > 0) hh = sh;
                    else { float d = drawIn(shR); hh = std::max(1.0f, std::min(yTop + h - syy, d * height)); }
                    syy += hh;
                    if(random(100) >= p.f("stripChance")) continue;
                    SpectralCell cc = spectralArgs(x, y0, w, std::max(1.0f, hh), p.f("noiseScale"), p.option("noiseEnvelope"), p.f("envPower"));
                    cc.pow2 = scR[1] > 0 ? drawIn(scR) : p.f("contrast");
                    cc.gain = p.f("noiseGain");
                    cc.window = p.b("window");
                    cc.op = level;
                    cc.opAlways = p.b("levelAlways");
                    if(scc > 0 && random(100) < scc) cc.col = customColour[(int)random(5)];
                    if(p.option("noiseAxis") == 1){ cc.columns = true; cc.ranRange = 20000; }
                    drawSpectral(cc);
                }
                return;
            }
            SpectralCell cc = spectralArgs(x, yTop, w, h, p.f("noiseScale"), p.option("noiseEnvelope"), p.f("envPower"));
            cc.pow2 = p.f("contrast");
            cc.gain = p.f("noiseGain");
            cc.window = p.b("window");
            cc.op = level;
            cc.opAlways = p.b("levelAlways");
            if(p.option("noiseAxis") == 1){ cc.columns = true; cc.ranRange = 20000; }
            drawSpectral(cc);
        }else if(cell == 1){
            auto kpR = p.range("kickPowerHit");
            float kp = kpR[1] > 0 ? std::max(0.1f, (float)drawInt(kpR)) : p.f("kickPower");
            taikoSweep(x, yTop, w, kp,level);
        }else if(cell == 2 || cell == 3){
            KlankCell k = klankArgs(x, yTop, h, w, p.f("klankPower"));
            k.soft = cell == 3;
            k.feather = p.f("feather");
            k.alpha = level;
            drawKlank(k);
        }else if(cell == 4){
            gradblock_op(x, yTop, w, h, p.f("blockPower"), level);
        }else if(cell == 5){
            float cy = (yTop + yBot) / 2, e = drawIn(p.range("gradEnd"));
            spectralGradient(x, yBot, yTop, x + w, cy + (yBot - cy) * e, cy - (cy - yTop) * e, level, 0, p.f("gradPowW"), p.f("gradPowH"));
        }else if(cell == 6){
            float cy = (yTop + yBot) / 2;
            spectralGradient(x, cy + 1.5f * h, cy - 1.5f * h, x + w / 4, cy + h / 2, cy - h / 2, level, 0, 2, 4);
            float pw = 3 + random(2);   // the arguments in Java's order
            float ph = 1 + random(2);
            spectralGradient(x, cy + h / 2, cy - h / 2, x + w, cy + h, cy - h, level, 0, pw, ph);
            strokeColour(col);
        }else if(cell == 7){
            taiko(x, yTop, w, h);
        }else if(cell == 8){
            int n = pjRound(p.f("speckleDensity") * w * h / (100 * UY * UY));
            style.strokeWeight = p.f("speckleSize") * UY;
            for(int i = 0; i < n; i++){
                float a = random(255) * level;
                strokeRGBA((float)red(col), (float)green(col), (float)blue(col), a);
                float px_ = x + random(w);
                float py_ = yTop + random(h);
                point(px_, py_);
            }
        }else if(cell == 9){
            int cols = std::max(1, drawInt(p.range("dotCols")));
            int rows = std::max(1, drawInt(p.range("dotRows")));
            style.strokeWeight = p.f("speckleSize") * UY;
            strokeRGBA((float)red(col), (float)green(col), (float)blue(col), 255 * level);
            for(int a = 0; a < cols; a++)
                for(int b = 0; b < rows; b++) point(x + (a + 0.5f) * w / cols, yTop + (b + 0.5f) * h / rows);
        }else{
            float cyc = drawIn(p.range("lfoBandCycles")), amt = p.f("lfoBandAmount"), lp = p.f("lfoBandPower");
            float cy = (yTop + yBot) / 2;
            style.strokeWeight = 1;
            for(int i = 0; i < w; i++){
                float ph = i / w;
                float env = spectralEnv(ph, p.option("noiseEnvelope"), p.f("envPower"));
                float l = pjPow((1 - pjCos(P_TWO_PI * ph * cyc)) / 2, lp);
                float half = h / 2 * (1 - amt + amt * l);
                for(float y = -half; y < half; y++){
                    float pv = pjPow(pjCos(P_HALF_PI * y / std::max(1.0f, half)), 2);
                    strokeRGBA((float)red(col), (float)green(col), (float)blue(col), 255 * level * env * pv);
                    point(x + i, cy + y);
                }
            }
        }
    }

    void genHitsDraw(GenParams& p){
        auto spp = newPattern(p);
        StepPattern& sp = *spp;
        int steps = sp.steps;
        float stepsize = sp.imageSteps ? sp.stepSize() : sp.stepBars == 1 ? barSize / steps : sp.stepSize();
        float lenUnit = sp.imageSteps ? barSize : stepsize;   // image steps: lengths are in bars
        bool bandRep = p.option("bandPick") == 1;
        int bandU = INT32_MIN;
        float bandJ = 0, bandHJ = 0;
        auto lenR = p.range("length");
        auto lenImgR = p.range("lengthImage");
        int lenSnap = p.option("lengthSnap");
        auto jitR = p.range("bandJitter");
        auto hjR = p.range("heightJitter");
        auto levelJR = p.range("levelJitter");
        float lo = p.f("bandLow"), hi = p.f("bandHigh");
        if(hi < lo){ float t = lo; lo = hi; hi = t; }
        int cell = p.option("cell");
        int colourMode = p.option("colour");
        float level = p.f("level");
        bool anchorEnd = p.option("anchor") == 1;
        float clearChance = p.f("clearChance");
        bool clearRep = p.option("clearScope") == 1;
        auto clearLenR = p.range("clearLength");
        float duck = p.f("duckChance");
        auto burstR = p.range("burstCount");
        int P = sp.P;
        int total = sp.total();
        for(int s = 0; s < total; s++){
            int bar = sp.barOf(s);
            sp.advance(s);
            if(clearRep && clearChance > 0 && sp.entered && sp.repOk && sp.barPlays(bar) && random(100) < clearChance)
                blankUnder((s / P) * P * stepsize, P * stepsize);
            if(!sp.plays(s)) continue;
            float w = lenImgR[1] > 0 ? drawIn(lenImgR) * width : drawLen(lenR, lenSnap) * lenUnit;
            float j, yTop, yBot;
            if(bandRep){
                if(sp.curU != bandU){
                    bandU = sp.curU;
                    float d = drawIn(jitR);
                    float sgn = random(1) < 0.5f ? -1.0f : 1.0f;
                    bandJ = d * sgn;
                    bandHJ = drawIn(hjR);
                }
                j = bandJ;
                yTop = height * (1 - pjConstrain(hi + j, 0.0f, std::max(1.0f, hi)));
                yBot = height * (1 - pjConstrain(lo + j - bandHJ, 0.0f, 1.0f));
            }else{
                float d = drawIn(jitR);
                float sgn = random(1) < 0.5f ? -1.0f : 1.0f;
                j = d * sgn;
                yTop = height * (1 - pjConstrain(hi + j, 0.0f, std::max(1.0f, hi)));
                float hj = drawIn(hjR);
                yBot = height * (1 - pjConstrain(lo + j - hj, 0.0f, 1.0f));
            }
            float x = (sp.stepBars != 1 || sp.imageSteps) ? (anchorEnd ? sp.xOf(s) + stepsize - w : sp.xOf(s))
                    : anchorEnd ? bar * barSize + (s % steps + 1) * stepsize - w : bar * barSize + (s % steps) * stepsize;
            if(!clearRep && clearChance > 0 && random(100) < clearChance){
                float cw = clearLenR[1] > 0 ? drawIn(clearLenR) * stepsize : w;
                blankUnder(anchorEnd ? x + w - cw : x, cw);
            }
            if(duck > 0 && random(100) < duck){
                if(p.f("duckProfile") > 0) softBlankProfile(x, p.f("duckLength") * barSize, p.f("duckPower"), p.f("duckProfile"));
                else softBlank(x, p.f("duckLength") * barSize, p.f("duckPower"));
            }
            uint32_t col = pickColour(colourMode);
            float lv = level * drawIn(levelJR);
            int nb = burstR[1] > 1 ? std::max(1, drawInt(burstR)) : 1;
            float bc = 0, gpow = 1;
            bool shrink = false;
            if(nb > 1){
                bc = drawIn(p.range("burstCentre"));
                if(p.f("burstWidthGrow") > 0 || p.f("burstHeightGrow") > 0 || p.f("burstDrift") != 0) gpow = drawIn(p.range("burstGrowPower"));
                shrink = p.f("burstShrink") > 0 && random(100) < p.f("burstShrink");
            }
            int nLeft = bc > 0 ? pjConstrain(pjRound(nb * bc), 0, nb) : 0;
            for(int bi = 0; bi < nb; bi++){
                float bx = x, bw = w, bT = yTop, bB = yBot;
                if(nb > 1){
                    float bp = p.f("burstPower");
                    float gph = 0;
                    if(bc <= 0){
                        float f0 = pjPow(bi / (float)nb, bp), f1 = pjPow((bi + 1) / (float)nb, bp);
                        bx = x + w * f0;
                        bw = std::max(1.0f, w * (f1 - f0));
                        gph = bi / (float)nb;
                    }else if(bi < nLeft){
                        int q = nLeft - 1 - bi;
                        float f0 = pjPow(q / (float)nLeft, bp), f1 = pjPow((q + 1) / (float)nLeft, bp);
                        bx = x + w * bc * (1 - f1);
                        bw = std::max(1.0f, w * bc * (f1 - f0));
                        gph = q / (float)nLeft;
                    }else{
                        int q = bi - nLeft, nr = nb - nLeft;
                        float f0 = pjPow(q / (float)nr, bp), f1 = pjPow((q + 1) / (float)nr, bp);
                        bx = x + w * (bc + (1 - bc) * f0);
                        bw = std::max(1.0f, w * (1 - bc) * (f1 - f0));
                        gph = q / (float)nr;
                    }
                    float ramp = nb > 1 ? bi / (float)(nb - 1) : 1;
                    if(shrink) ramp = 1 - ramp;
                    if(p.f("burstSizePower") != 1) ramp = pjPow(ramp, p.f("burstSizePower"));
                    float sf = pjLerp(p.f("burstStart"), 1, ramp);
                    if(p.f("burstHeightGrow") > 0) sf *= 1 + pjPow(gph, gpow) * p.f("burstHeightGrow");
                    float cy = (yTop + yBot) / 2, hh = (yBot - yTop) / 2 * sf;
                    if(p.f("burstDrift") != 0) cy += pjPow(gph, gpow) * p.f("burstDrift") * height;
                    bT = cy - hh;
                    bB = cy + hh;
                    if(p.f("burstWidth") > 0){
                        float sw = p.f("burstWidth") * UY;
                        if(p.f("burstWidthGrow") > 0) sw *= 1 + pjPow(gph, 2 * gpow) * p.f("burstWidthGrow");
                        bw = std::max(1.0f, sw);
                    }
                }
                strokeColour(col);
                fillColour(col);
                if(!outsideClip(bx, bw)) hitCell(p, cell, bx, bT, bB, bw, lv, col);
            }
        }
        style.fill = false;
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    // ================================================================== pulse (Generators.pde)

    static float pjLog(float a){ return (float)std::log((double)a); }
    static float log2f_(float x){ return pjLog(x) / pjLog(2); }
    static float p2f(float pitch){ return 440 * (pjPow(2, (pitch - 69) / 12)); }
    static float f2p(float freq){ return 69 + 12 * log2f_(freq / 440); }
    float lfomod(float ph, int shape, float cycles, float noisey){
        float result = 0;
        switch(shape){
            case 0: result = 1 - std::fmod(ph * cycles, 1.0f); break;
            case 1: result = std::fmod(ph * cycles, 1.0f); break;
            case 2: result = 1 - (((pjCos(ph * P_TWO_PI * cycles)) + 1) / 2.0f); break;
            case 3: result = (((pjCos((ph + 0.5f) * P_TWO_PI * cycles)) + 1) / 2.0f); break;
            case 4:
                result = prng.perlin(ph * cycles * 2, noisey / (cycles * 3));
                result = pjConstrain(result, 0.0f, 1.0f);
                result = pjPow(result, 1);
                break;
        }
        return result;
    }
    static float envelope(float ph, int shape){
        float result = 0;
        switch(shape){
            case 0: result = 1 - std::fmod(ph, 1.0f); break;
            case 1: result = std::fmod(ph, 1.0f); break;
            case 2: result = 1 - (((pjCos(ph * P_TWO_PI)) + 1) / 2); break;
        }
        return result;
    }
    int arpOffset(int mode, int k, int n, int step){
        int idx = k % std::max(1, n);
        if(mode == 1) return idx * step;
        if(mode == 2) return (n - idx) * step;
        if(mode == 3) return idx < n / 2 ? idx * step : (n - idx) * step;
        if(mode == 4) return idx < n / 2 ? (n - idx) * step : idx * step;
        if(mode == 5) return (int)random((float)(n * step));
        return 0;
    }
    int walkStep(int arpStep, int walkMin){
        if(walkMin <= 0) return (int)random((float)-arpStep, (float)(arpStep + 1));
        int mag = walkMin + (int)random((float)(std::max(0, arpStep - walkMin) + 1));
        return random(1) < 0.5f ? -mag : mag;
    }

    struct NoteCell {
        float x = 0, w = 0, p0 = 0, p1 = 0;
        bool slide = false;
        int partials = 1, partialmode = 0, firstpartial = 0;
        bool modeSetsStart = true, phaseFromStart = false;
        float hicutpow = 0, locutpow = 0;
        int ampshape = 0; float ampow = 0;
        int lfoshape = 0; float lfocycles = 0; int lfohp = 0; float lfopow = 0;
        float tremamp = 0, tremcyc = 0, vibamp = 0, vibcyc = 0;
        float detune = 0, partDet = 0, vol = 1;
        bool transpose = false, safe = true, pgStyle = false;
        float spanLo = 1, spanHi = 1; int spanAlign = 0;
        float lfoCycLo = 0, lfoCycHi = 0, taper = 0;
        uint32_t col = 0xFFFFFFFF;
    };
    void drawNote(const NoteCell& n){
        float baseWeight = style.strokeWeight;
        style.cap = Canvas::PROJECT;
        float p0 = n.p0, p1 = n.p1;
        float noteh = height / (float)notegrid;
        float ranpartamps = 0, partamp = 1, partialprob = 50;
        int start = n.firstpartial;
        float inc = 1;
        switch(n.partialmode){
            case 0: partialprob = 100; inc = 1; ranpartamps = 0; if(n.modeSetsStart) start = 0; break;
            case 1: partialprob = 100; inc = 2; ranpartamps = 0; if(n.modeSetsStart) start = 0; p0 = p0 - 12; p1 = p1 - 12; break;
            case 2: partialprob = 100; inc = 2; ranpartamps = 0; if(n.modeSetsStart) start = 1; break;
            case 3: partialprob = 100; inc = 1; ranpartamps = 1; if(n.modeSetsStart) start = 0; break;
            case 4: partialprob = 50; inc = 1; ranpartamps = 0; if(n.modeSetsStart) start = 0; break;
            case 5: partialprob = 20; inc = 1; ranpartamps = 0; if(n.modeSetsStart) start = 0; break;
        }
        float sy = n.safe ? safey : 0;
        float phStart = n.phaseFromStart ? (float)start : 0;
        float fr = (float)red(n.col), fg = (float)green(n.col), fb = (float)blue(n.col);
        for(float j = (float)start; j < n.partials + start; j += inc){
            float partdetune = random(n.partDet);
            float partYoffset = partdetune * noteh;
            float y0 = height - (f2p((p2f(p0) * j)) * noteh) - sy;
            float ydiff = 0;
            if(n.slide) ydiff = (height - (f2p((p2f(p1) * j)) * noteh)) - y0;
            float ph1 = (j - phStart) / n.partials;
            float locut = pjPow(ph1, n.locutpow);
            float hicut = pjPow(1 - ph1, n.hicutpow);
            if(ranpartamps == 1) partamp = random(1); else partamp = 1;
            if(random(100) < partialprob && !(n.slide && j == 0)){
                float L = n.w, xo = 0, lc = n.lfocycles;
                if(n.spanLo < 1 || n.spanHi < 1){
                    L = n.w * (n.spanLo + random(n.spanHi - n.spanLo));
                    xo = n.spanAlign == 1 ? (n.w - L) / 2 : n.spanAlign == 2 ? n.w - L : n.spanAlign == 3 ? random(n.w - L) : 0;
                }
                if(n.lfoCycHi > 0) lc = n.lfoCycLo + random(n.lfoCycHi - n.lfoCycLo);
                if(n.taper > 0) style.strokeWeight = baseWeight * pjPow(std::max(0.0f, 1 - ph1), n.taper);
                SweepScope sweep(sweepStep, UY);
                for(float i = 0; i < L; i += UY){
                    float ph2 = i / L;
                    float ySum = 0;
                    if(n.slide) ySum = ydiff * (((1 - pjCos(ph2 * P_PI)) + 1) / 2);
                    float mod = lfomod(ph2, n.lfoshape, lc, y0);
                    float env = envelope(ph2, n.ampshape);
                    float trem = lfomod(ph2, 2, n.tremcyc, 0);
                    float vib = (lfomod(ph2, 2, n.vibcyc, 0) * 2) - 1;
                    trem = trem * n.tremamp * hicut * locut * (pjPow(env, 2));
                    vib = vib * n.vibamp * (pjPow(env, 2));
                    int tr = 0;
                    if(n.transpose){ int bi = (int)((n.x + xo + i) / barSize); tr = (int)(transposeList[std::max(0, std::min(59, bi))] * noteh); }
                    if(n.ampow != 0) env = pjPow(env, n.ampow); else env = 1;
                    if(n.lfopow != 0){
                        if(n.lfohp == 1) mod = pjPow(mod, 1 + ((1 - ph1) * n.lfopow));
                        else mod = pjPow(mod, 1 + ((ph1) * n.lfopow));
                    }else mod = 1;
                    float alpha = ((env * mod * hicut * locut * partamp * n.vol) - trem) * 255;
                    alpha = pjConstrain(alpha, 0.0f, 255.0f);
                    int ya, yb;
                    if(n.slide){ ya = (int)(y0 + n.detune + ySum); yb = (int)(y0 + ySum - n.detune); }
                    else { ya = (int)(y0 + n.detune); yb = (int)(y0 - n.detune); }
                    if(n.pgStyle){
                        strokeARGB(n.col, alpha);
                        style.strokeWeight = 2 * (height / 1080.0f) * lineWeight;
                        point(n.x + xo + i, ya + vib + partYoffset - tr);
                        point(n.x + xo + i, yb + vib + partYoffset - tr);
                        strokeColour(n.col);
                    }else{
                        strokeRGBA(fr, fg, fb, alpha);
                        point(n.x + xo + i, ya + vib + partYoffset - tr);
                        point(n.x + xo + i, yb + vib + partYoffset - tr);
                    }
                }
            }
        }
    }
    struct AdditiveCell {
        float p = 0, x = 0, w = 0, partials = 1; int partmode = 0, envmode = 1;
        float pow = 4, detune = 0; bool tilt = true, triple = true, legacyDraw = false; uint32_t col = 0xFFFFFFFF;
    };
    void drawAdditive(const AdditiveCell& a){
        style.cap = Canvas::ROUND;
        style.strokeWeight = 2 * (height / 1080.0f) * lineWeight;
        float f = p2f(a.p);
        float noteheight = height / (float)notegrid;
        float x0 = a.x, x1 = x0 + a.w;
        float fr = (float)red(a.col), fg = (float)green(a.col), fb = (float)blue(a.col);
        for(int i = 0; i < a.partials; i++){
            float phi = i / a.partials;
            float freqs = i * f;
            float pitches = f2p(freqs);
            float y = height - (pitches * noteheight) - safey;
            if(a.legacyDraw){ float start = 0, maxlen = width - start, minlen = 10; random(maxlen - minlen); }
            bool pinta = true;
            switch(a.partmode){
                case 0: pinta = true; break;
                case 1: pinta = i % 2 == 0; break;
                case 2: pinta = i % 2 == 1; break;
                case 3: pinta = (int)random(2) != 0; break;
            }
            if(pinta){
                SweepScope sweep(sweepStep, UY);
                for(float j = x0; j < x1; j += UY){
                    float ph = 1 - ((j - x0) / (x1 - x0));
                    if(a.envmode == 1) ph = pjPow(ph, a.pow);
                    if(a.envmode == 3){ ph = 1 - ph; ph = pjPow(ph, a.pow); }
                    if(a.envmode == 2){ ph = 1 - (std::fabs((ph * 2) - 1)); ph = pjPow(ph, a.pow / 2); }
                    if(a.envmode == 0) ph = 1;
                    if(a.tilt) strokeRGBA(fr, fg, fb, ph * 255 * (1 - phi));
                    else strokeRGBA(fr, fg, fb, (ph * 255));
                    point(j, y);
                    if(a.triple){ point(j, y + a.detune); point(j, y - a.detune); }
                }
            }
        }
    }

    void genPulseDraw(GenParams& p){
        auto spp = newPattern(p);
        StepPattern& sp = *spp;
        int steps = sp.steps;
        int P = sp.P;
        float noteh = height / (float)notegrid;
        float stepsize = sp.imageSteps ? sp.stepSize() : sp.stepBars == 1 ? barSize / steps : sp.stepSize();
        float lenUnit = sp.imageSteps ? barSize : stepsize;   // image steps: lengths are in bars
        auto lenR = p.range("length");
        auto lenImgR = p.range("lengthImage");
        int lenSnap = p.option("lengthSnap");
        auto glideR = p.range("glide");
        auto partR = p.range("partials");
        float skew = p.f("partialsSkew");
        float octUp = p.f("octaveUp");
        std::vector<float> len(P);
        std::vector<int> parts(P), oct(P);
        for(int k = 0; k < P; k++){
            len[k] = lenImgR[1] > 0 ? drawIn(lenImgR) * width : drawLen(lenR, lenSnap) * lenUnit;
            parts[k] = pjRound(partR[0] + pjPow(random(1), skew) * (partR[1] - partR[0]));
            oct[k] = random(100) < octUp ? 12 : 0;
        }
        int cell = p.option("cell");
        int colourMode = p.option("colour");
        float thick = p.f("thickness") * UY * lineWeight;
        int degree = p.i("degree");
        int octave = p.i("octave");
        int len0 = (int)scales[selsc].size();
        int arp = p.option("arp"), arpSteps = p.i("arpSteps"), arpStep = p.i("arpStep");
        bool anchorEnd = p.option("anchor") == 1;
        float clearChance = p.f("clearChance");
        bool clearRep = p.option("clearScope") == 1;
        auto clearLenR = p.range("clearLength");
        int arpStart = p.i("arpStart");
        bool followRoot = p.b("followRoot"), along = p.b("transposeAlongNote");
        int bassDeg = p.b("bass") ? bassDegree : 0;
        auto voicesR = p.range("voices"), chordStepR = p.range("chordStep"), voiceOctR = p.range("voiceOctaves"), voiceVolR = p.range("voiceVolume");
        int voicing = p.option("voicing");
        bool randomStart = p.option("chordStart") == 1;
        std::vector<int> offsets = p.list("voiceOffsets");
        float volume = p.f("volume");
        int total = sp.total();
        float riffC = p.f("riffChance");
        std::vector<char> riffOn(P, 0);
        std::vector<int> riffDeg(P, 0);
        if(riffC > 0){
            int rr = p.i("riffRange") > 0 ? p.i("riffRange") : len0;
            for(int q = 0; q < P; q++){ riffOn[q] = random(100) < riffC; riffDeg[q] = (int)random((float)rr); }
        }
        bool legato = p.b("legato");
        auto ovR = p.range("legatoOverlap");
        std::vector<char> pre;
        if(legato){ pre.assign(total, 0); for(int s = 0; s < total; s++) pre[s] = sp.plays(s); }
        int walkPos = 0;
        int walkMin = p.i("walkMin");
        bool walkLoop = p.b("walkLoop");
        std::vector<int> walkArr;
        if(arp == 6 && walkLoop){
            walkArr.assign(P, 0);
            int wp = 0;
            for(int q = 0; q < P; q++){
                if(q > 0) wp = pjConstrain(wp + walkStep(arpStep, walkMin), -len0, 4 * len0);
                walkArr[q] = wp;
            }
        }
        bool chordStep = p.b("chordPerStep");
        std::vector<int> cNv, cInc, cOct, cStart;
        if(chordStep){ cNv.assign(P, -1); cInc.assign(P, 0); cOct.assign(P, 0); cStart.assign(P, 0); }
        for(int s = 0; s < total; s++){
            int bar = sp.barOf(s);
            int k = s % P;
            if(legato){
                if(!pre[s]) continue;
            }else{
                sp.advance(s);
                if(clearRep && clearChance > 0 && sp.entered && sp.repOk && sp.barPlays(bar) && random(100) < clearChance)
                    blankUnder((s / P) * P * stepsize, P * stepsize);
                if(!sp.plays(s)) continue;
            }
            float noteLen = len[k];
            if(legato){
                int nx = s + 1;
                while(nx < total && !pre[nx]) nx++;
                noteLen = (nx - s) * stepsize;
                if(ovR[0] != 1 || ovR[1] != 1) noteLen *= drawIn(ovR);
            }
            int rootPart = (followRoot ? rootList[std::min(59, bar)] : 0) + bassDeg;
            int idx;
            if(arp == 6 && walkLoop) idx = std::max(0, rootPart + degree + walkArr[k]);
            else if(arp == 6){
                int stp = walkMin > 0 ? walkStep(arpStep, walkMin) : (int)random((float)-arpStep, (float)(arpStep + 1));
                walkPos = pjConstrain(walkPos + stp, -len0, 4 * len0);
                idx = std::max(0, rootPart + degree + walkPos);
            }else idx = rootPart + degree + arpOffset(arp, k + arpStart, arpSteps, arpStep);
            if(riffC > 0 && riffOn[k]) idx = rootPart + riffDeg[k];
            if(randomStart){
                if(chordStep){
                    if(cNv[k] < 0) cStart[k] = (int)random((float)len0);
                    idx = rootPart + cStart[k];
                }else idx = rootPart + (int)random((float)len0);
            }
            float x = (sp.stepBars != 1 || sp.imageSteps) ? (anchorEnd ? sp.xOf(s) + stepsize - noteLen + thick / 2 : sp.xOf(s) + thick / 2)
                    : anchorEnd ? bar * barSize + (s % steps + 1) * stepsize - noteLen + thick / 2
                                : bar * barSize + (s % steps) * stepsize + thick / 2;
            if(!clearRep && clearChance > 0 && random(100) < clearChance){
                float cw = clearLenR[1] > 0 ? drawIn(clearLenR) * stepsize : noteLen;
                blankUnder(anchorEnd ? x - thick / 2 + noteLen - cw : x - thick / 2, cw + thick);
            }
            int nv, inc;
            if(chordStep && cNv[k] >= 0){ nv = cNv[k]; inc = cInc[k]; }
            else {
                nv = !offsets.empty() ? (int)offsets.size() : std::max(1, drawInt(voicesR));
                inc = (nv > 1 && voicing == 0) ? std::max(1, drawInt(chordStepR)) : 0;
                if(chordStep){ cNv[k] = nv; cInc[k] = inc; cOct[k] = drawInt(voiceOctR); }
            }
            for(int v = 0; v < nv; v++){
                int vi = idx;
                if(offsets.empty()){
                    if(voicing == 1 && (nv > 1 || randomStart)) vi = rootPart + (int)random((float)len0);
                    else vi = idx + v * inc;
                }
                int octs = chordStep ? cOct[k] : drawInt(voiceOctR);
                int pitch = degreeSemis(vi, len0) + (along ? 0 : transposeList[std::min(59, bar)]) + 12 * octave + oct[k]
                          + 12 * octs + (!offsets.empty() ? offsets[v] : 0);
                float vol = volume * drawIn(voiceVolR);
                uint32_t col = pickColour(colourMode);
                strokeColour(col);
                style.strokeWeight = thick;
                if(cell == 0){
                    NoteCell n;
                    n.x = x; n.p0 = (float)pitch; n.p1 = (float)pitch; n.w = noteLen;
                    n.partials = parts[k]; n.partialmode = p.option("partialMode"); n.firstpartial = 0;
                    n.hicutpow = p.f("hicut"); n.locutpow = p.f("locut"); n.ampshape = p.option("envelope"); n.ampow = p.f("envPower");
                    n.lfoshape = p.option("lfoShape"); n.lfocycles = p.f("lfoCycles"); n.lfohp = 0; n.lfopow = p.f("lfoPower");
                    n.tremamp = p.f("tremolo"); n.tremcyc = p.f("tremCycles"); n.vibamp = p.f("vibrato") * noteh; n.vibcyc = p.f("vibCycles");
                    n.detune = p.f("detune") * noteh; n.partDet = p.f("partialDetune"); n.vol = vol;
                    n.col = style.strokeColor;
                    n.transpose = along;
                    auto spR = p.range("partialSpan");
                    n.spanLo = spR[0]; n.spanHi = spR[1];
                    n.spanAlign = p.option("partialAlign");
                    auto plc = p.range("partialLfoCycles");
                    n.lfoCycLo = plc[0]; n.lfoCycHi = plc[1];
                    n.taper = p.f("partialTaper");
                    if(glideR[0] != 0 || glideR[1] != 0){ n.p1 = pitch + drawIn(glideR); n.slide = true; n.safe = false; }
                    if(!outsideClip(n.x, n.w)) drawNote(n);
                }else if(cell == 2){
                    float y = height - pitch * noteh - safey;
                    float ox = random(1000), gr = p.f("ribbonGrain") / UY, rmin = p.f("ribbonMin");
                    style.strokeWeight = 1;
                    for(int i = 0; i < noteLen; i++){
                        float ph = i / noteLen;
                        float env = envelope(ph, p.option("envelope"));
                        if(p.f("envPower") != 0) env = pjPow(env, p.f("envPower")); else env = 1;
                        float rn = pjConstrain(prng.perlin(ox + i * gr, ox) * 1.33f, 0.0f, 1.0f);
                        if(p.f("ribbonPower") != 1) rn = pjPow(rn, p.f("ribbonPower"));
                        float th = thick * (rmin + (1 - rmin) * rn);
                        strokeRGBA((float)red(col), (float)green(col), (float)blue(col), 255 * env * vol);
                        line(x + i, y - th / 2, x + i, y + th / 2);
                    }
                    style.strokeWeight = thick;
                }else{
                    AdditiveCell a;
                    a.p = (float)pitch; a.x = x; a.w = noteLen; a.partials = (float)parts[k]; a.partmode = std::min(p.option("partialMode"), 3);
                    a.col = style.strokeColor;
                    a.envmode = p.option("envelope") == 2 ? 2 : (p.option("envelope") == 1 ? 3 : 1);
                    a.pow = p.f("envPower");
                    a.detune = p.f("detune") * noteh;
                    if(!p.b("additiveTilt")) a.tilt = false;
                    drawAdditive(a);
                }
            }
        }
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    // ================================================================== processes (Processes.pde)
    // Ported: band, echo, scale filter, noise mask, page window, tear window, edge fade, top /
    // bottom fades, saturation, opacity ramp. The others are refused by unsupportedProcess() (the
    // request then goes to Processing).

    static std::string unsupportedProcess(const GenParams&){ return ""; }   // every process is ported
    bool barsOutsideClip(int b, int span) const { return clipBar0 >= 0 && (b + span <= clipBar0 || b >= clipBar1); }
    bool procCanvas = false;
    int procBarsMode = 0;
    // rows [0, n) split over the CPU cores (each row independent: the result is the same as in order)
    template<class F> static void parallelRows(int n, F f){
        int T = (int)std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        if(n < 64 || T == 1){ for(int y = 0; y < n; y++) f(y); return; }
        std::vector<std::thread> th;
        for(int t = 0; t < T; t++) th.emplace_back([&, t]{ for(int y = t; y < n; y += T) f(y); });
        for(auto& x : th) x.join();
    }
    uint32_t scaleAlpha(uint32_t cc, float k) const {
        if(procCanvas){
            int r = pjConstrain(pjRound(((cc >> 16) & 0xFF) * k), 0, 255);
            int gg = pjConstrain(pjRound(((cc >> 8) & 0xFF) * k), 0, 255);
            int b = pjConstrain(pjRound((cc & 0xFF) * k), 0, 255);
            return 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)b;
        }
        int a = (cc >> 24) & 0xFF;
        int na = pjConstrain(pjRound(a * k), 0, 255);
        return ((uint32_t)na << 24) | (cc & 0x00FFFFFFu);
    }
    int level(uint32_t cc) const {
        if(procCanvas) return std::max((int)((cc >> 16) & 0xFF), std::max((int)((cc >> 8) & 0xFF), (int)(cc & 0xFF)));
        return (cc >> 24) & 0xFF;
    }
    uint32_t silencePx() const { return procCanvas ? 0xFF000000u : 0u; }
    // An echo copy (colour of `s`, alpha `sa`) onto a pixel of the instrument's own layer: the
    // level is the louder of the two, as before, and the colour goes gradually from one to the
    // other around the place where they are equally loud (weights: the alphas squared, so the
    // louder one still dominates away from there). Taking the whole pixel of the louder one
    // instead cut a hard outline where an echo crossed a note of another colour.
    static uint32_t echoPx(uint32_t d, uint32_t s, int sa){
        int da = (int)((d >> 24) & 0xFF);
        if(sa <= 0) return d;
        if(da <= 0) return ((uint32_t)sa << 24) | (s & 0x00FFFFFFu);
        const int wd = da * da, ws = sa * sa, wt = wd + ws;
        uint32_t out = (uint32_t)std::max(da, sa) << 24;
        for(int sh = 16; sh >= 0; sh -= 8)
            out |= (uint32_t)(((int)((d >> sh) & 0xFF) * wd + (int)((s >> sh) & 0xFF) * ws + wt / 2) / wt) << sh;
        return out;
    }
    int contentColumn(const std::vector<uint32_t>& px, int W, int H, int x0, int x1) const {
        for(int x = std::max(0, x0); x < std::min(W, x1); x++)
            for(int y = 0; y < H; y++) if(level(px[(size_t)y * W + x]) > 0) return x;
        return -1;
    }
    int noteOnset(const std::vector<uint32_t>& px, int W, int H, int b, int steps, float stepsize){
        int s0 = (int)random((float)steps);
        for(int k = 0; k < steps; k++){
            int s = (s0 + k) % steps;
            int xa = pjRound(b * barSize + s * stepsize);
            int x = contentColumn(px, W, H, xa, xa + std::max(1, pjRound(stepsize / 2)));
            if(x >= 0) return x;
        }
        return -1;
    }
    bool procBarOk(int b) const {
        int nb = std::max(1, (int)std::ceil(bars));
        if(procBarsMode == 1) return b == 0;
        if(procBarsMode == 2) return b == nb - 1;
        if(procBarsMode == 3) return b % 2 == 0;
        if(procBarsMode == 4) return b % 2 == 1;
        return true;
    }
    static float bandKeep(int mode, float y, float cy, float hh, float fade, float curve){
        float d = std::fabs(y - cy);
        if(mode == 0){
            if(d <= hh) return 1;
            if(d <= hh + fade) return pjPow(1 - (d - hh) / std::max(1.0f, fade), curve);
            return 0;
        }
        if(d <= hh) return 0;
        if(d <= hh + fade) return pjPow((d - hh) / std::max(1.0f, fade), curve);
        return 1;
    }

    void applyProcesses(std::vector<uint32_t>& px, int W, int H, GenParams& p, int steps){
        procBarsMode = p.option("processBars");
        float stepsize = barSize / steps;
        float noteh = H / (float)notegrid;
        int nBars = (int)std::ceil(bars);

        // freeze
        float fc = p.f("freezeChance");
        if(fc > 0){
            auto lenR = p.range("freezeLength");
            int fstart = p.option("freezeStart");
            std::vector<int> fAtList = p.list("freezeAtList");
            float fAt = p.f("freezeAt");
            std::vector<int> fSlots = p.list("freezeSlotList");
            bool toEnd = p.option("freezeTo") == 1;
            auto hold = [&](int x0, int x1){ for(int x = x0 + 1; x < x1; x++) for(int y = 0; y < H; y++) px[(size_t)y * W + x] = px[(size_t)y * W + x0]; };
            if(fstart == 3){
                std::vector<int> chain = p.list("freezeChain");
                if(chain.empty()) chain = {16};
                float xb = 0;
                int first = p.i("freezeChainFirst");
                bool firstSlice = true;
                while(xb < W){
                    int l16 = firstSlice && first > 0 ? first : (chain.size() > 1 ? chain[(int)random((float)chain.size())] : chain[0]);
                    firstSlice = false;
                    float len = std::max(1, l16) * barSize / 16;
                    int xa = (int)xb;
                    bool on = procBarOk((int)(xb / barSize)) && (fc >= 100 || random(100) < fc);
                    if(on && xa >= 0 && xa < W) hold(xa, std::min(W, xa + (int)len));
                    xb += len;
                }
            }else if(!fAtList.empty() || fAt >= 0){
                float unit = p.option("freezeUnit") == 1 ? 0.5f : p.option("freezeUnit") == 2 ? 0.25f : 1;
                int div = p.i("freezeAtDiv");
                int nUnits = pjRound(nBars / unit);
                for(int u = 0; u < nUnits; u++){
                    float ub = u * unit;
                    if(!procBarOk((int)ub)) continue;
                    if(random(100) >= fc) continue;
                    float pos = !fAtList.empty() ? (fAtList.size() > 1 ? fAtList[(int)random((float)fAtList.size())] : fAtList[0]) / (float)div : fAt;
                    int x0 = (int)((ub + pos * unit) * barSize);
                    int len = toEnd ? (int)(barSize * (1 - pos) * unit) : (int)(drawIn(lenR) * barSize);
                    if(x0 < 0 || x0 >= W) continue;
                    hold(x0, std::min(W, x0 + len));
                }
            }else for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                if(fstart == 2){
                    int fg = p.i("freezeGrid");
                    float gw = barSize / fg;
                    for(int k = 0; k < fg; k++){
                        if(!fSlots.empty() && !containsInt(fSlots, k)) continue;
                        if(random(100) >= fc) continue;
                        int xa = pjRound(b * barSize + k * gw), xb = std::min(W, pjRound(b * barSize + (k + 1) * gw));
                        if(xa >= W) continue;
                        if(xa >= 0) hold(xa, xb);
                    }
                    continue;
                }
                if(random(100) >= fc) continue;
                int x0 = fstart == 1 ? pjRound(b * barSize) : noteOnset(px, W, H, b, steps, stepsize);
                int len = toEnd ? 0 : pjRound(drawIn(lenR) * barSize);
                if(x0 < 0) continue;
                x0 += std::max(1, pjRound(stepsize / 8));
                if(toEnd) len = pjRound((b + 1) * barSize) - x0;
                if(x0 < W) hold(x0, std::min(W, x0 + len));
            }
        }

        // beat repeat
        float rc = p.f("repeatChance");
        if(rc > 0){
            auto lenR = p.range("repeatLength");
            auto cntR = p.range("repeatCount");
            auto pitR = p.range("repeatPitch");
            float gate = p.f("repeatGate");
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                if(random(100) >= rc) continue;
                float L = drawIn(lenR) * barSize;
                auto cfR = p.range("repeatCountF");
                float nf = cfR[1] > 0 ? std::max(1.0f, drawIn(cfR)) : 0;
                int n = nf > 0 ? std::max(1, (int)nf) : std::max(2, drawInt(cntR));
                int semis = drawInt(pitR);
                int startMode = p.option("repeatStart");
                float rAt = p.f("repeatAt");
                int onset = rAt >= 0 ? pjRound((b + rAt) * barSize)
                          : startMode == 1 ? pjRound(b * barSize) : startMode == 2 ? pjRound((b + 0.5f) * barSize) : noteOnset(px, W, H, b, steps, stepsize);
                if(onset < 0) continue;
                float xs = (float)onset;
                int srcMode = p.option("repeatSource");
                int srcStart;
                if(p.f("repeatSourceAt") >= 0) srcStart = pjRound((b + p.f("repeatSourceAt")) * barSize);
                else if(srcMode == 1) srcStart = pjRound(b * barSize);
                else if(srcMode == 2){ int slot = (int)random((float)p.i("repeatSourceSlots")); srcStart = pjRound(b * barSize + slot * barSize / p.i("repeatSourceSlots")); }
                else srcStart = pjRound(xs);
                float sw = nf > 0 ? L / nf : L / n;
                // cut-short tail (legacy direct-copy loops, e.g. fx83 / fx46 / fx116): with a fractional count and a fixed
                // repeatCount above int(nf), slices from int(nf) on are drawn too, clipped to the repeat length; the gate
                // silence (sw * (1 - gate)) sits at the end of the clipped slice. Otherwise the tail stays original. No random drawn.
                int nTot = (nf > 0 && cntR[0] == cntR[1] && pjRound(cntR[0]) > n) ? pjRound(cntR[0]) : n;
                int xEnd = pjRound(xs + L);
                std::vector<uint32_t> src = px;
                for(int i = 0; i < nTot; i++){
                    int dy = -pjRound(semis * (std::min(1.0f, i / (float)std::max(1, n - 1))) * noteh);
                    int dx0 = pjRound(xs + i * sw);
                    int wSl = i < n ? pjRound(sw) : std::min(pjRound(sw), xEnd - dx0);
                    float openW = i < n ? sw * gate : wSl - sw * (1 - gate);
                    for(int j = 0; j < wSl; j++){
                        int xd = dx0 + j, xsrc = srcStart + j;
                        if(xd < 0 || xd >= W || xsrc >= W) continue;
                        bool open = j < openW;
                        for(int y = 0; y < H; y++){
                            int ys = y - dy;
                            px[(size_t)y * W + xd] = (open && ys >= 0 && ys < H && xsrc >= 0) ? src[(size_t)ys * W + xsrc] : silencePx();
                        }
                    }
                }
            }
        }

        // reverse
        float rv = p.f("reverseChance");
        if(rv > 0){
            int slots = p.i("reverseSlots");
            bool oddOnly = p.b("reverseOddOnly");
            std::vector<int> slotList = p.list("reverseSlotList");
            bool mirrorPrev = p.option("reverseMode") == 1;
            float sw = barSize / slots;
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                for(int k = 0; k < slots; k++){
                    if(oddOnly && k % 2 == 0) continue;
                    if(!slotList.empty() && !containsInt(slotList, k)) continue;
                    if(random(100) >= rv) continue;
                    int xa = pjRound(b * barSize + k * sw), xb = std::min(W, pjRound(b * barSize + (k + 1) * sw));
                    if(mirrorPrev){
                        int pa = pjRound(b * barSize + (k - 1) * sw);
                        if(k == 0 || pa < 0) continue;
                        for(int i = 0; i < xb - xa; i++){
                            int sx = xa - 1 - i, dx = xa + i;
                            if(sx < pa || dx >= W) break;
                            for(int y = 0; y < H; y++) px[(size_t)y * W + dx] = px[(size_t)y * W + sx];
                        }
                        continue;
                    }
                    for(int i = 0; i < (xb - xa) / 2; i++){
                        int l = xa + i, r = xb - 1 - i;
                        for(int y = 0; y < H; y++) std::swap(px[(size_t)y * W + l], px[(size_t)y * W + r]);
                    }
                }
            }
        }

        // gate
        float gcc = p.f("gateChance");
        if(gcc > 0){
            auto divR = p.range("gateDivisions");
            auto openR = p.range("gateOpen");
            int shape = p.option("gateShape");
            auto gPart = p.range("gatePart");
            float gCurve = p.f("gateCurve");
            float gox = shape == 4 ? random(1000) : 0;
            float gBars = p.f("gateBars");
            std::vector<int> gShapes = p.list("gateShapes");
            int fmShape = p.option("gateFmShape");
            float fmPow = p.f("gateFmShapePower");
            auto geR = p.range("gateEuclidSteps");
            auto scaleCol = [&](int x, float k){ for(int y = 0; y < H; y++) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k); };
            if(geR[1] > 0){
                int est = std::max(1, drawInt(geR));
                int efl = drawInt(p.range("gateEuclidFills"));
                int esh = (int)random((float)est);
                std::vector<int> divs = p.list("gateEuclidDivs");
                if(divs.empty()) divs = {4};
                auto epR = p.range("gateEuclidPower");
                float swc = p.f("gateSwellChance");
                float st16 = barSize / 16;
                std::vector<float> miss(W, 1.0f);
                std::vector<char> gated(W, 0);
                for(int b = 0; b < nBars; b++){
                    if(barsOutsideClip(b, 1)) continue;
                    if(!procBarOk(b)) continue;
                    if(random(100) >= gcc) continue;
                    float bx = b * barSize;
                    for(int x = std::max(0, pjRound(bx)); x < std::min(W, pjRound((b + 1) * barSize)); x++) gated[x] = 1;
                    if(swc > 0 && b > 0 && random(100) < swc){
                        float sw = barSize / random(1.5f, 3);
                        float pw = random(2, 8);
                        for(int x = std::max(0, pjRound(bx - sw)); x < std::min(W, pjRound(bx)); x++) miss[x] *= 1 - pjPow((x - (bx - sw)) / sw, pw);
                    }
                    for(int j = 0; j < 16; j++){
                        float sx = bx + j * st16;
                        if(swc > 0 && j > 0 && j % 4 == 0 && random(100) < swc){
                            float sw = st16 * random(1, 3);
                            float pw = random(2, 8);
                            for(int x = std::max(0, pjRound(sx - sw)); x < std::min(W, pjRound(sx)); x++) miss[x] *= 1 - pjPow((x - (sx - sw)) / sw, pw);
                        }
                        if(euclidean(j, est, efl, esh) == 1){
                            float bw = barSize / (divs.size() > 1 ? divs[(int)random((float)divs.size())] : divs[0]);
                            bw = std::min(bw, (b + 1) * barSize - sx);
                            float pw = drawIn(epR);
                            for(int x = std::max(0, pjRound(sx)); x < std::min(W, pjRound(sx + bw)); x++) miss[x] *= 1 - pjPow(1 - (x - sx) / bw, pw);
                        }
                    }
                }
                for(int x = 0; x < W; x++){
                    if(!gated[x]) continue;
                    float k = 1 - miss[x];
                    if(k >= 1) continue;
                    scaleCol(x, k);
                }
            }else if(gBars > 0 || !gShapes.empty() || fmShape > 0){
                float ub = gBars > 0 ? gBars : 1;
                int nU = std::max(1, (int)std::ceil(bars / ub));
                for(int u = 0; u < nU; u++){
                    float u0 = u * ub;
                    if(!procBarOk((int)u0)) continue;
                    if(random(100) >= gcc) continue;
                    int shp = gShapes.size() > 1 ? gShapes[(int)random((float)gShapes.size())] : gShapes.size() == 1 ? gShapes[0] : shape;
                    shp = pjConstrain(shp, 0, 5);
                    int d = std::max(1, drawInt(divR));
                    float open = drawIn(openR);
                    float w = ub * barSize / d;
                    float gx0 = u0 * barSize;
                    for(int x = pjRound(gx0); x < std::min(W, pjRound((u0 + ub) * barSize)); x++){
                        if(x < 0) continue;
                        if(gPart[0] > 0 || gPart[1] < 1){
                            float fr = (x - gx0) / (ub * barSize);
                            if(fr < gPart[0] || fr >= gPart[0] + gPart[1]) continue;
                        }
                        float ph = std::fmod(x - gx0, w) / w;
                        float env;
                        if(shp == 4){
                            float nz = prng.perlin(gox + x * p.f("gateNoiseScale") / UX, 0.5f);
                            env = (1 + pjSinF(P_TWO_PI * nz * 2 * p.f("gateMaxCycles"))) / 2;
                            if(fmShape > 0){
                                float q = ph < open ? ph / open : 1;
                                float e2 = ph >= open ? 0 : fmShape == 1 ? 1 - q : fmShape == 2 ? q : 1 - std::fabs(q * 2 - 1);
                                env *= fmPow != 1 ? pjPow(e2, fmPow) : e2;
                            }
                        }else if(ph >= open) env = 0;
                        else {
                            float q = ph / open;
                            if(shp == 1) env = 1 - q;
                            else if(shp == 2) env = q;
                            else if(shp == 3) env = 1 - std::fabs(q * 2 - 1);
                            else if(shp == 5) env = 1 - pjPow(q, 1 / gCurve);
                            else env = 1;
                        }
                        if(gCurve != 1 && shp != 5) env = pjPow(env, gCurve);
                        if(env >= 1) continue;
                        scaleCol(x, env);
                    }
                }
            }else for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                if(random(100) >= gcc) continue;
                int d = std::max(1, drawInt(divR));
                float open = drawIn(openR);
                float w = barSize / d;
                int xb = pjRound(b * barSize);
                for(int x = std::max(0, xb); x < std::min(W, pjRound((b + 1) * barSize)); x++){
                    if(gPart[0] > 0 || gPart[1] < 1){
                        float fr = (x - b * barSize) / barSize;
                        if(fr < gPart[0] || fr >= gPart[0] + gPart[1]) continue;
                    }
                    float ph = std::fmod(x - b * barSize, w) / w;
                    float env;
                    if(shape == 4){
                        float nz = prng.perlin(gox + x * p.f("gateNoiseScale") / UX, 0.5f);
                        env = (1 + pjSinF(P_TWO_PI * nz * 2 * p.f("gateMaxCycles"))) / 2;
                    }else if(ph >= open) env = 0;
                    else {
                        float q = ph / open;
                        if(shape == 1) env = 1 - q;
                        else if(shape == 2) env = q;
                        else if(shape == 3) env = 1 - std::fabs(q * 2 - 1);
                        else if(shape == 5) env = 1 - pjPow(q, 1 / gCurve);
                        else env = 1;
                    }
                    if(gCurve != 1 && shape != 5) env = pjPow(env, gCurve);
                    if(env >= 1) continue;
                    scaleCol(x, env);
                }
            }
        }

        // slot gate
        float scc = p.f("slotChance");
        if(scc > 0){
            float s0 = p.f("slotStart"), sl = p.f("slotLength");
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                if(random(100) >= scc) continue;
                int xa = pjRound((b + s0) * barSize), xb = std::min(W, pjRound((b + std::min(1.0f, s0 + sl)) * barSize));
                for(int x = std::max(0, xa); x < xb; x++) for(int y = 0; y < H; y++) px[(size_t)y * W + x] = silencePx();
            }
        }

        // micro gates
        float mcc = p.f("microChance");
        if(mcc > 0){
            auto mw = p.range("microWidth");
            for(int q = 1; q < nBars * 4; q++){
                if(!procBarOk(q / 4)) continue;
                if(random(100) >= mcc) continue;
                float xq = q * barSize / 4;
                int xa = pjRound(xq - drawIn(mw) * stepsize), xb = std::min(W, pjRound(xq));
                for(int x = std::max(0, xa); x < xb; x++) for(int y = 0; y < H; y++) px[(size_t)y * W + x] = silencePx();
            }
        }

        // band filter
        float bc = p.f("bandChance");
        if(bc > 0){
            int mode = p.option("bandMode");
            int div = p.i("bandDivisions");
            bool perBar = p.option("bandUnit") == 1;
            auto part = p.range("bandPart"); auto cR = p.range("bandCenter"); auto hR = p.range("bandHeight"); auto fR = p.range("bandFade");
            float curve = p.f("bandCurve"), wander = p.f("bandWander");
            auto nsR = p.range("bandNoiseScale");
            float ncon = p.f("bandNoiseContrast");
            float sw = barSize / div;
            float wox = random(1000);
            float woy = random(1000);
            std::vector<float> keep(H);
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                bool barOn = !perBar || random(100) < bc;
                for(int k = 0; k < div; k++){
                    float fr = k / (float)div;
                    if(fr < part[0] || fr >= part[0] + part[1]) continue;
                    if(!barOn) continue;
                    if(!perBar && random(100) >= bc) continue;
                    float cy = H * (1 - drawIn(cR));
                    float hh = H * drawIn(hR) / 2;
                    float fade = H * drawIn(fR);
                    float ns = drawIn(nsR) / (H / 1080.0f);
                    float noy = random(1000);
                    int xa = pjRound(b * barSize + k * sw), xb = std::min(W, pjRound(b * barSize + (k + 1) * sw));
                    if(mode == 2) for(int y = 0; y < H; y++){
                        float a = pjConstrain(pjPow(prng.perlin(noy + y * ns, noy) * 1.33f, ncon), 0.0f, 1.0f);
                        keep[y] = (1 - a) * (1 - a);
                    } else for(int y = 0; y < H; y++) keep[y] = bandKeep(mode, (float)y, cy, hh, fade, curve);
                    for(int x = std::max(0, xa); x < xb; x++){
                        if(wander > 0 && mode != 2){
                            float u = x / (wander * barSize);
                            float wcy = H * (1 - prng.perlin(wox + u, 0.5f)), whh = H * (0.03f + 0.4f * prng.perlin(woy + u, 1.5f));
                            for(int y = 0; y < H; y++){
                                float kk = bandKeep(mode, (float)y, wcy, whh, fade, curve);
                                if(kk < 1) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], kk);
                            }
                        }else{
                            for(int y = 0; y < H; y++) if(keep[y] < 1) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], keep[y]);
                        }
                    }
                }
            }
        }

        // rearrange: mirror strips, cell copies, grid shuffle, tiles
        auto msR = p.range("mirrorStrips");
        if(msR[1] > 0){
            int n = std::max(1, drawInt(msR));
            bool alongY = p.option("mirrorAxis") == 1;
            int srcMode = p.option("mirrorSource");
            int s = srcMode == 1 ? 0 : srcMode == 2 ? n - 1 : (int)random((float)n);
            std::vector<uint32_t> src = px;
            int len = alongY ? H : W;
            float sw = len / (float)n;
            for(int i = 0; i < n; i++){
                if(i == s) continue;
                bool flip = std::abs(i - s) % 2 == 1;
                int a0 = pjRound(i * sw), a1 = std::min(len, pjRound((i + 1) * sw)), s0 = pjRound(s * sw), s1 = std::min(len, pjRound((s + 1) * sw));
                for(int a = a0; a < a1; a++){
                    int off = a - a0;
                    int sa = flip ? s1 - 1 - off : s0 + off;
                    if(sa < s0 || sa >= s1) continue;
                    if(alongY) for(int x = 0; x < W; x++) px[(size_t)a * W + x] = src[(size_t)sa * W + x];
                    else for(int y = 0; y < H; y++) px[(size_t)y * W + a] = src[(size_t)y * W + sa];
                }
            }
        }
        auto ccR = p.range("cellCopies");
        if(ccR[1] > 0){
            int n = drawInt(ccR);
            float cw = p.f("cellWidth") * barSize, chr = p.f("cellHeight");
            float ch = chr > 0 ? chr * noteh : (float)H;
            bool same = p.option("cellDest") == 1;
            float swd = p.f("cellStretchW"), sht = p.f("cellStretchH");
            for(int cc = 0; cc < n; cc++){
                int w = std::max(1, std::min(W, pjRound(cw))), h = std::max(1, std::min(H, pjRound(ch)));
                int sx = (int)random((float)(W - w + 1));
                int sy = (int)random((float)(H - h + 1));
                int dw = swd > 0 ? std::max(1, std::min(W, pjRound(swd * barSize))) : w, dh = sht > 0 ? std::max(1, std::min(H, pjRound(sht * noteh))) : h;
                int dmode = p.option("cellDest");
                int dx = same ? sx : (dmode == 2 ? std::min(sx, W - dw) : (int)random((float)(W - dw + 1)));
                int dy = same ? sy : (dmode == 3 ? std::min(sy, H - dh) : (int)random((float)(H - dh + 1)));
                std::vector<uint32_t> cell((size_t)w * h);
                for(int y = 0; y < h; y++) for(int x = 0; x < w; x++) cell[(size_t)y * w + x] = px[(size_t)(sy + y) * W + sx + x];
                for(int y = 0; y < dh && dy + y < H; y++)
                    for(int x = 0; x < dw && dx + x < W; x++)
                        px[(size_t)(dy + y) * W + dx + x] = cell[(size_t)std::min(h - 1, y * h / dh) * w + std::min(w - 1, x * w / dw)];
            }
        }
        int cg = p.option("cellGrid");
        if(cg == 3){
            float gwf = p.f("cellWidth") * barSize;
            float ghf = p.f("cellHeight") > 0 ? p.f("cellHeight") * noteh : (float)H;
            int gw = std::max(1, (int)gwf), gh = std::max(1, (int)ghf);
            int rows = std::max(1, (int)(H / ghf));
            std::vector<uint32_t> cellBuf((size_t)gw * gh);
            const uint32_t OUT = 1;   // outside (not copied)
            for(float cx = 0; cx < W; cx += gwf){
                for(float cy = (float)H; cy > 0; cy -= ghf){
                    int sy = (int)(H - (int)random((float)rows) * ghf) - (int)safey;
                    int dy = (int)(H - (int)random((float)rows) * ghf) - (int)safey;
                    int x0 = (int)cx;
                    for(int y = 0; y < gh; y++) for(int x = 0; x < gw; x++){
                        int ys = sy + y, xs = x0 + x;
                        cellBuf[(size_t)y * gw + x] = (ys >= 0 && ys < H && xs < W) ? px[(size_t)ys * W + xs] : OUT;
                    }
                    for(int y = 0; y < gh; y++) for(int x = 0; x < gw; x++){
                        int yd = dy + y, xd = x0 + x;
                        uint32_t v = cellBuf[(size_t)y * gw + x];
                        if(v != OUT && yd >= 0 && yd < H && xd < W) px[(size_t)yd * W + xd] = v;
                    }
                }
            }
        }else if(cg > 0){
            int gw = std::max(1, pjRound(p.f("cellWidth") * barSize));
            int gh = p.f("cellHeight") > 0 ? std::max(1, pjRound(p.f("cellHeight") * noteh)) : H;
            int cols = std::max(1, W / gw), rows = std::max(1, H / gh);
            std::vector<uint32_t> src = px;
            for(int r = 0; r < rows; r++)
                for(int cc = 0; cc < cols; cc++){
                    int sc2 = (int)random((float)cols);
                    int sr = cg == 1 ? r : (int)random((float)rows);
                    for(int y = 0; y < gh; y++)
                        for(int x = 0; x < gw; x++){
                            int dyy = r * gh + y, dxx = cc * gw + x, syy = sr * gh + y, sxx = sc2 * gw + x;
                            if(dyy < H && dxx < W && syy < H && sxx < W) px[(size_t)dyy * W + dxx] = src[(size_t)syy * W + sxx];
                        }
                }
        }
        int tiles = p.i("tileCount");
        if(tiles > 1){
            std::vector<uint32_t> src = px;
            int tw2 = std::max(1, W / tiles);
            for(int y = 0; y < H; y++)
                for(int x = 0; x < W; x++) px[(size_t)y * W + x] = src[(size_t)y * W + std::min(W - 1, (x % tw2) * tiles)];
        }

        // sequence
        float sqc = p.f("seqChunk");
        if(sqc > 0){
            float cw = sqc * barSize;
            int nch = std::max(1, (int)std::ceil(W / cw));
            int cpb = std::max(1, pjRound(1 / sqc));
            int smode = p.option("seqSource");
            std::vector<int> order = p.list("seqOrder");
            if(order.empty()) order = {0};
            if(smode == 2){
                order.assign(std::max(1, p.i("seqRandomCount")), 0);
                for(auto& o : order) o = (int)random((float)cpb);
            }
            float sg = p.f("seqGate");
            std::vector<uint32_t> src = px;
            for(int d = 0; d < nch; d++){
                int bar = (int)(d * sqc);
                if(!procBarOk(bar)) continue;
                int s;
                if(smode == 3) s = (d / cpb) * cpb + (order.size() > 1 ? order[(int)random((float)order.size())] : order[0]);
                else if(smode == 0) s = (d / cpb) * cpb + order[(d % cpb) % order.size()];
                else s = order[d % order.size()];
                int xd0 = pjRound(d * cw), xs0 = pjRound(s * cw), wd = pjRound(cw);
                for(int j = 0; j < wd; j++){
                    int xd = xd0 + j, xsrc = xs0 + j;
                    if(xd >= W) break;
                    bool open = j < cw * sg;
                    for(int y = 0; y < H; y++) px[(size_t)y * W + xd] = (open && xsrc < W && xsrc >= 0) ? src[(size_t)y * W + xsrc] : silencePx();
                }
            }
        }

        // transpose
        float trc = p.f("transposeChance");
        if(trc > 0){
            std::vector<int> semis = p.list("transposeSemis");
            if(!semis.empty()){
                std::vector<uint32_t> src = px;
                int tb = p.i("transposeBars");
                for(int b = 0; b < nBars; b += tb){
                    if(barsOutsideClip(b, tb)) continue;
                    if(!procBarOk(b)) continue;
                    if(random(100) >= trc) continue;
                    int dy = -pjRound(semis[(int)random((float)semis.size())] * noteh);
                    for(int x = pjRound(b * barSize); x < std::min(W, pjRound((b + tb) * barSize)); x++)
                        for(int y = 0; y < H; y++){
                            int ys = y - dy;
                            px[(size_t)y * W + x] = (ys >= 0 && ys < H) ? src[(size_t)ys * W + x] : silencePx();
                        }
                }
            }
        }

        // echo throws
        float thc = p.f("throwChance");
        if(thc > 0){
            auto srcR = p.range("throwSource"); auto dR = p.range("throwDelay");
            int reps = p.i("throwRepeats");
            float fbk = p.f("throwFeedback"), duck = p.f("throwDuck");
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                if(!procBarOk(b)) continue;
                if(random(100) >= thc) continue;
                int xs = pjRound(b * barSize + (int)random(4) * barSize / 4);
                int seg = std::max(1, pjRound(drawIn(srcR) * stepsize));
                int del = std::max(1, pjRound(drawIn(dR) * stepsize));
                std::vector<uint32_t> src = px;
                if(duck > 0 && random(100) < duck)
                    for(int x = std::max(0, xs + seg); x < std::min(W, xs + seg + del * reps); x++) for(int y = 0; y < H; y++) px[(size_t)y * W + x] = silencePx();
                for(int k = 1; k <= reps; k++){
                    float amp = pjPow(fbk, (float)k);
                    for(int j = 0; j < seg; j++){
                        int xd = xs + k * del + j, xsrc = xs + j;
                        if(xd >= W || xsrc >= W) break;
                        if(xd < 0 || xsrc < 0) continue;
                        for(int y = 0; y < H; y++){
                            uint32_t s = src[(size_t)y * W + xsrc], d = px[(size_t)y * W + xd];
                            if(procCanvas){
                                int r = std::min(255, (int)((d >> 16) & 0xFF) + pjRound(((s >> 16) & 0xFF) * amp));
                                int gg = std::min(255, (int)((d >> 8) & 0xFF) + pjRound(((s >> 8) & 0xFF) * amp));
                                int bb = std::min(255, (int)(d & 0xFF) + pjRound((s & 0xFF) * amp));
                                px[(size_t)y * W + xd] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
                            }else{
                                px[(size_t)y * W + xd] = echoPx(d, s, pjRound(((s >> 24) & 0xFF) * amp));
                            }
                        }
                    }
                }
            }
        }

        // echo
        int er = p.i("echoRepeats");
        if(er > 0){
            float delay = quantizedEchoSteps(drawIn(p.range("echoDelay"))) * stepsize;
            float fb = p.f("echoFeedback");
            std::vector<uint32_t> src = px;
            for(int k = 1; k <= er; k++){
                float amp = pjPow(fb, (float)k);
                if(amp < 0.01f) break;
                // Round each absolute position, not one rounded delay multiplied
                // by k: the latter drifts at widths with fractional step pixels.
                int dx = std::max(1, pjRound(delay * k));
                for(int y = 0; y < H; y++){
                    size_t row = (size_t)y * W;
                    for(int x = dx; x < W; x++){
                        uint32_t s = src[row + x - dx];
                        if(procCanvas){
                            uint32_t d = px[row + x];
                            int r = std::min(255, (int)((d >> 16) & 0xFF) + pjRound(((s >> 16) & 0xFF) * amp));
                            int gg = std::min(255, (int)((d >> 8) & 0xFF) + pjRound(((s >> 8) & 0xFF) * amp));
                            int bb = std::min(255, (int)(d & 0xFF) + pjRound((s & 0xFF) * amp));
                            px[row + x] = 0xFF000000u | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
                        }else{
                            px[row + x] = echoPx(px[row + x], s, pjRound(((s >> 24) & 0xFF) * amp));
                        }
                    }
                }
            }
        }

        // scale filter
        float sf = p.f("scaleFilter");
        if(sf > 0){
            float half = p.f("scaleWidth") / 2;
            const std::vector<int>& notes = scales[selsc];
            std::vector<float> k(H);
            for(int b = 0; b < nBars; b++){
                if(barsOutsideClip(b, 1)) continue;
                int tr = transposeList[std::min(59, b)];
                for(int y = 0; y < H; y++){
                    float semis = (H - safey - y) / noteh - tr;
                    float best = 99;
                    for(int n : notes){
                        float d = std::fmod(std::fmod(semis - n, 12.0f) + 18, 12.0f) - 6;
                        best = std::min(best, std::fabs(d));
                    }
                    k[y] = best <= half ? 1 : 1 - sf;
                }
                for(int x = pjRound(b * barSize); x < std::min(W, pjRound((b + 1) * barSize)); x++)
                    for(int y = 0; y < H; y++) if(k[y] < 1) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k[y]);
            }
        }

        // noise mask
        float ma = p.f("maskAmount");
        if(ma > 0){
            float cell = std::max(1.0f, p.f("maskScale") * noteh);
            // its length along time: maskBars bars when set (the same at any zoom), else square
            float mbars = p.has("maskBars") ? p.f("maskBars") : 0.0f;
            float cellX = mbars > 0 ? std::max(1.0f, mbars * barSize) : cell;
            float con = p.f("maskContrast");
            bool minv = p.b("maskInvert");
            float mth = p.f("maskThreshold");
            float mgain = p.f("maskGain"), mout = p.f("maskOut");
            float ox = random(200);
            float oy = random(200);
            bool onGpu = gpu && (size_t)W * H >= gpuMinPixels && gpu->mask(px, W, H, prng, ox, oy, cellX, cell, mgain, con, mout, minv, mth, ma, procCanvas);
            if(!onGpu) parallelRows(H, [&](int y){
                for(int x = 0; x < W; x++){
                    size_t i = (size_t)y * W + x;
                    if(level(px[i]) == 0) continue;
                    float nv = pjConstrain(pjPow(pjConstrain(prng.perlin(ox + x / cellX, oy + y / cell) * mgain, 0.0f, 1.0f), con) * mout, 0.0f, 1.0f);
                    if(minv) nv = 1 - nv;
                    if(mth > 0) nv = nv >= mth ? 1 : 0;
                    px[i] = scaleAlpha(px[i], 1 - ma + ma * nv);
                }
            });
        }

        // page window
        float pw = p.f("pageWindow");
        if(pw > 0){
            auto span = p.range("pageSpan");
            bool full = span[0] <= 0 && span[1] >= 1;
            float sa = span[0] * W, sb = span[1] * W;
            for(int x = 0; x < W; x++){
                float k = full ? pjPow((1 - pjCos(P_TWO_PI * x / W)) / 2, pw)
                               : (x < sa || x > sb ? 0 : pjPow((1 - pjCos(P_TWO_PI * (x - sa) / std::max(1.0f, sb - sa))) / 2, pw));
                for(int y = 0; y < H; y++) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k);
            }
        }

        // tear window
        float tw = p.f("tearWindow");
        if(tw > 0){
            int tc = p.i("tearCount");
            float tpy = p.f("tearPowerY");
            bool custom = p.option("tearPlace") == 1;
            std::vector<float> txs(tc), tws(tc), ths(tc), tys(tc), tls(tc);
            float tpx = p.f("tearPowerX");
            auto tlR = p.range("tearLevel");
            bool tcol = p.b("tearColour");
            std::vector<uint32_t> tcs(tc, 0);
            for(int i = 0; i < tc; i++){
                if(custom){
                    txs[i] = drawIn(p.range("tearX")) * W;
                    tws[i] = std::min(W - txs[i], drawIn(p.range("tearWidth")) * W);
                    ths[i] = std::max(1.0f, drawIn(p.range("tearHeight")) * H);
                    tys[i] = random(std::max(0.0f, H - ths[i]));
                }else{
                    txs[i] = W / 3.0f + random(W * 2 / 3.0f);
                    tws[i] = random(W - txs[i]);
                    ths[i] = random(H / 2.0f, H / 1.5f);
                    tys[i] = random(H / 6.0f, H - ths[i]);
                }
                tls[i] = drawIn(tlR);
                if(tcol) tcs[i] = customColour[(int)random(5)];
            }
            bool tearExtra = tlR[0] < 1 || tcol;
            parallelRows(H, [&](int y){
                for(int x = 0; x < W; x++){
                    float k = 0;
                    int best = -1;
                    for(int i = 0; i < tc; i++){
                        float tx = txs[i], tww = tws[i], th = ths[i], ty = tys[i];
                        float kx = (x < tx || x > tx + tww) ? 0 : pjPow((1 - pjCos(P_TWO_PI * (x - tx) / std::max(1.0f, tww))) / 2, tpx >= 0 ? tpx : tw / 3);
                        float ky = (y < ty || y > ty + th) ? 0 : pjPow((1 - pjCos(P_TWO_PI * (y - ty) / th)) / 2, tpy >= 0 ? tpy : 2 * tw / 3);
                        float kk = kx * ky;
                        if(tearExtra) kk *= tls[i];
                        if(kk > k){ k = kk; best = i; }
                    }
                    size_t i = (size_t)y * W + x;
                    if(tcol && best >= 0){
                        uint32_t cc = px[i], tcc = tcs[best];
                        int r = (int)((cc >> 16) & 0xFF) * (int)((tcc >> 16) & 0xFF) / 255;
                        int gg = (int)((cc >> 8) & 0xFF) * (int)((tcc >> 8) & 0xFF) / 255;
                        int bb = (int)(cc & 0xFF) * (int)(tcc & 0xFF) / 255;
                        px[i] = (cc & 0xFF000000u) | ((uint32_t)r << 16) | ((uint32_t)gg << 8) | (uint32_t)bb;
                    }
                    if(k < 1) px[i] = scaleAlpha(px[i], k);
                }
            });
        }

        // edge fade
        float ef = p.f("edgeFade");
        float ewd = p.f("edgeWander");
        if(ef > 0 && ewd > 0){
            float ol = random(100);
            float orr = random(100);
            float rate = p.f("edgeWanderRate") / (H / 1080.0f);
            bool inv = p.option("edgeCurve") == 1;
            for(int y = 0; y < H; y++){
                float wl = ef * W * (1 - ewd + ewd * prng.perlin(ol + y * rate, 1));
                float wr = ef * W * (1 - ewd + ewd * prng.perlin(orr + y * rate, 1));
                for(int side = 0; side < 2; side++){
                    float fw = side == 0 ? wl : wr;
                    for(int i = 0; i < std::min((float)W, std::ceil(fw)); i++){
                        float k = inv ? 1 - pjPow(1 - i / fw, 2) : pjPow(i / fw, 2);
                        int x = side == 0 ? i : W - 1 - i;
                        px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k);
                    }
                }
            }
        }else if(ef > 0){
            int fw = std::max(1, pjRound(ef * W));
            for(int x = 0; x < std::min(fw, W); x++){
                float k = p.option("edgeCurve") == 1 ? 1 - pjPow(1 - x / (float)fw, 2) : pjPow(x / (float)fw, 2);
                for(int y = 0; y < H; y++){
                    px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k);
                    px[(size_t)y * W + (W - 1 - x)] = scaleAlpha(px[(size_t)y * W + (W - 1 - x)], k);
                }
            }
        }

        // top / bottom fades
        float fbm = p.f("fadeBottom"), ftp = p.f("fadeTop");
        float fbe = p.f("fadeBottomEnd"), fte = p.f("fadeTopEnd"), fcv = p.f("fadeCurve");
        float fwd = p.f("fadeWander");
        int fshape = p.option("fadeShape");
        bool sweepFade = fbe >= 0 || fte >= 0 || fcv != 1 || fwd > 0 || fshape == 1;
        float fwo = fwd > 0 ? random(1000) : 0;
        float fwo2 = (fwd > 0 && p.b("fadeWanderSplit")) ? random(1000) : fwo;
        if(sweepFade && (fbm > 0 || ftp > 0 || fbe > 0 || fte > 0)){
            for(int x = 0; x < W; x++){
                float u = x / (float)W;
                float db = pjLerp(fbm, fbe >= 0 ? fbe : fbm, u) * H, dt = pjLerp(ftp, fte >= 0 ? fte : ftp, u) * H;
                if(fwd > 0){
                    float wv = 1 - fwd + fwd * pjConstrain(prng.perlin(fwo + x / (p.f("fadeWanderBars") * barSize), 0.5f) * 1.33f, 0.0f, 1.0f);
                    float wv2 = fwo2 == fwo ? wv : 1 - fwd + fwd * pjConstrain(prng.perlin(fwo2 + x / (p.f("fadeWanderBars") * barSize), 0.5f) * 1.33f, 0.0f, 1.0f);
                    db *= wv;
                    dt *= wv2;
                }
                for(int i = 0; i < std::min((float)H, std::ceil(std::max(db, dt))); i++){
                    if(i < db){ int y = H - 1 - i; px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], fshape == 1 ? 1 - pjPow(1 - i / db, fcv) : pjPow(i / db, fcv)); }
                    if(i < dt){ int y = i; px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], fshape == 1 ? 1 - pjPow(1 - i / dt, fcv) : pjPow(i / dt, fcv)); }
                }
            }
        }else if(fbm > 0 || ftp > 0){
            int hb = pjRound(fbm * H), ht = pjRound(ftp * H);
            for(int i = 0; i < std::min(H, std::max(hb, ht)); i++){
                for(int pass = 0; pass < 2; pass++){
                    int fh = pass == 0 ? hb : ht;
                    if(i >= fh) continue;
                    float k = i / (float)fh;
                    int y = pass == 0 ? H - 1 - i : i;
                    for(int x = 0; x < W; x++) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k);
                }
            }
        }

        // saturation
        float sat = p.f("saturation");
        if(sat < 1){
            for(auto& cc : px){
                int r = (cc >> 16) & 0xFF, gg = (cc >> 8) & 0xFF, b = cc & 0xFF;
                float l = 0.299f * r + 0.587f * gg + 0.114f * b;
                int nr = pjConstrain(pjRound(l + (r - l) * sat), 0, 255), ng = pjConstrain(pjRound(l + (gg - l) * sat), 0, 255), nb = pjConstrain(pjRound(l + (b - l) * sat), 0, 255);
                cc = (cc & 0xFF000000u) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
            }
        }

        // opacity ramp
        float r0 = p.f("rampStart"), r1 = p.f("rampEnd");
        if(r0 < 1 || r1 < 1){
            float rp = p.f("rampPower");
            for(int x = 0; x < W; x++){
                float k = pjLerp(r0, r1, pjPow(x / (float)W, rp));
                for(int y = 0; y < H; y++) px[(size_t)y * W + x] = scaleAlpha(px[(size_t)y * W + x], k);
            }
        }
    }

    // the effects generator: the processes on the whole image drawn so far
    void processCanvas(GenParams& p, int steps){
        procCanvas = true;
        applyProcesses(c.px, width, height, p, steps);
        procCanvas = false;
    }

    // a generator through its own layer when a process is on (Generators.pde genWithProcesses)
    // "tileBars" (process: rearrange): the instrument is drawn as if the image were that many bars
    // long (its pattern, its harmony, its processes: page window, mask...), and that drawing is
    // repeated across the whole image. Every repetition is the same picture, whatever the image's
    // length: an instrument made for a short page keeps its rhythm on a longer one (Bar Multiplier).
    void genTiled(const std::string& gen, GenParams& p, GenParams& q, float tileBars){
        const int fullW = width; const float fullBars = bars;
        const double tilePx = (double)fullW * tileBars / fullBars;
        const int tw = std::max(1, (int)std::ceil(tilePx - 1e-6));
        int steps = p.has("steps") ? p.i("steps") : 16;
        Canvas layerC;
        layerC.allocateLayer(tw, height);
        Style outer = style;
        std::vector<Style> outerStack = styleStack;
        style = Style();
        style.strokeColor = 0xFF000000u; style.fillColor = 0xFFFFFFFFu; style.strokeWeight = UY; style.cap = Canvas::ROUND;
        styleStack.clear();
        Canvas* prev = gc;
        gc = &layerC;
        width = tw; bars = tileBars;                // (barSize is the same: the bars keep their width)
        auto restore = [&](){ gc = prev; style = outer; styleStack = outerStack; width = fullW; bars = fullBars; };
        try {
            genDraw(gen, p);
            gc = prev; style = outer; styleStack = outerStack;
            if(processActive(q)) applyProcesses(layerC.px, tw, height, q, steps);
        } catch(...) { restore(); throw; }
        restore();
        Canvas full;
        full.allocateLayer(fullW, height);
        for(int k = 0; ; k++){
            const int x0 = (int)std::lround(k * tilePx);
            if(x0 >= fullW) break;
            const int n = std::min({tw, fullW - x0, std::max(1, (int)std::lround((k + 1) * tilePx) - x0)});
            for(int y = 0; y < height; y++)
                std::copy_n(layerC.px.begin() + (size_t)y * tw, n, full.px.begin() + (size_t)y * fullW + x0);
        }
        c.drawLayer(full);
    }

    void genWithProcesses(const std::string& gen, GenParams& p, GenParams& q){
        {
            const float tileBars = q.has("tileBars") ? q.f("tileBars") : 0;
            if(tileBars > 0 && tileBars < bars - 1e-4f){ genTiled(gen, p, q, tileBars); return; }
        }
        if(!processActive(q)){ genDraw(gen, p); return; }
        int steps = p.has("steps") ? p.i("steps") : 16;
        if(q.option("processOn") == 1){ genDraw(gen, p); processCanvas(q, steps); return; }
        // the layer: a transparent ARGB buffer with a fresh style (createGraphics), weight UY
        Canvas layerC;
        layerC.allocateLayer(width, height);
        Style outer = style;
        std::vector<Style> outerStack = styleStack;
        style = Style();
        style.strokeColor = 0xFF000000u; style.fillColor = 0xFFFFFFFFu; style.strokeWeight = UY; style.cap = Canvas::ROUND;
        styleStack.clear();
        Canvas* prev = gc;
        gc = &layerC;
        try { genDraw(gen, p); }
        catch(...) { gc = prev; style = outer; styleStack = outerStack; throw; }
        gc = prev; style = outer; styleStack = outerStack;
        applyProcesses(layerC.px, width, height, q, steps);
        c.drawLayer(layerC);
    }

    // ================================================================== masses (Generators.pde)

    static float pjSinF(float a){ return (float)std::sin((double)a); }
    static float bezierPoint(float a, float b, float c_, float d, float t){
        float t1 = 1.0f - t;
        return (a * t1 + 3 * b * t) * t1 * t1 + (3 * c_ * t1 + d * t) * t * t;
    }
    static float pdist(float x1, float y1, float x2, float y2){ return pjSqrt((x2 - x1) * (x2 - x1) + (y2 - y1) * (y2 - y1)); }
    // bezier(...) with the stroke
    void bezier(float x1, float y1, float cx1, float cy1, float cx2, float cy2, float x2, float y2){
        if(!style.stroke) return;
        uint32_t s = style.strokeColor;
        gc->strokeBezier({x1, y1}, {cx1, cy1}, {cx2, cy2}, {x2, y2}, style.strokeWeight, style.cap, red(s), green(s), blue(s), (s >> 24) & 255);
    }
    // Legacy.pde portamentoPG: a glide from (x0, y0) to (x1, y1) along a sigmoid, with vibrato and
    // a noise wobble, one vertex per pixel, stroked (SQUARE cap)
    void portamentoPG(float x0, float y0, float x1, float y1, float tens, float vib, float perlinfreq, float perlinampinsemitones){
        style.cap = Canvas::SQUARE;
        float noteheight = height / (float)notegrid;
        float distX = x1 - x0 + 1;
        float distY = y1 - y0;
        float pointnum = distX;
        float noisey = random(1000);
        float noisey2 = random(100);
        style.fill = false;
        std::vector<Canvas::Pt> pts;
        for(float i = 0; i < pointnum; i++){
            float ph = i / pointnum;
            float perl = P.noise((ph * perlinfreq) + noisey, noisey2);
            perl = ((perl * 2) - 1) * (perlinampinsemitones * noteheight);
            float sinamp = (1 + pjSinF(ph * 4)) / 2;
            float sinus = (pjSinF(ph * 48) * sinamp) * (noteheight / 2);
            float x = x0 + (ph * distX);
            float y = (sinus * vib) + y0 + (sigmoid(ph, tens) * distY) + perl;
            pts.push_back({x, y});
        }
        if(style.stroke){
            uint32_t s = style.strokeColor;
            gc->strokePath(pts, style.strokeWeight, style.cap, red(s), green(s), blue(s), (s >> 24) & 255);
        }
    }

    std::vector<float> massPitches(int lines, bool chord, const std::array<float, 2>& stepR, int octStart, int octRange, bool follow, int bar, int bassDeg = 0){
        int len = (int)scales[selsc].size();
        std::vector<float> out(lines);
        int root = (follow ? rootList[std::min(59, bar)] : 0) + bassDeg;
        int tr = follow ? transposeList[std::min(59, bar)] : 0;
        if(chord){
            int startnote = (int)random((float)len) + root;
            int inc = stepR[0] < 1 ? std::max(0, drawInt(stepR)) : std::max(1, drawInt(stepR));
            for(int i = 0; i < lines; i++){
                int idx = startnote + i * inc;
                out[i] = (float)(degreeSemis(idx, len) + 12 * octStart + tr);
            }
        }else{
            for(int i = 0; i < lines; i++){
                int idx = (int)random((float)len) + root;
                int o = (int)random((float)octRange);
                out[i] = (float)(12 * o + degreeSemis(idx, len) + 12 * octStart + tr);
            }
        }
        return out;
    }
    std::vector<float> massWalk(std::vector<int>& deg, int step, int octStart, int octRange, bool follow, int bar, int bassDeg = 0){
        int len = (int)scales[selsc].size();
        std::vector<float> out(deg.size());
        int tr = follow ? transposeList[std::min(59, bar)] : 0;
        for(size_t i = 0; i < deg.size(); i++){
            if(step > 0) deg[i] = std::abs(deg[i] + (int)random((float)-step, (float)(step + 1))) % len;
            else deg[i] = (int)random((float)len);
            int idx = deg[i] + bassDeg;
            out[i] = (float)(degreeSemis(idx, len) + 12 * octStart + 12 * (int)random((float)octRange) + tr);
        }
        return out;
    }

    void genMassHarmonics(GenParams& p){
        int n = std::max(1, drawInt(p.range("harmonics")));
        float pw = p.f("harmonicsPower");
        float segBars = p.f("segmentBars"), off = p.f("segmentOffset");
        int octStart = p.i("octaveStart");
        bool follow = p.b("followHarmony");
        int bassDeg = p.b("bass") ? bassDegree : 0;
        bool glide = p.option("curve") == 1;
        float tension = p.f("tension");
        int copies = p.i("lineCopies");
        float noteh = height / (float)notegrid;
        float spread = p.f("copyDetune") * noteh;
        int len = (int)scales[selsc].size();
        int nBars = std::max(1, (int)std::ceil(bars));
        uint32_t col = pickColour(p.option("colour"));
        style.fill = false;
        style.strokeWeight = p.f("thickness") * UY * lineWeight;
        std::vector<float> xs;
        xs.push_back(0);
        for(float b = off > 0 ? off : segBars; b < bars; b += segBars) xs.push_back(b);
        xs.push_back(bars);
        for(int s = 0; s < (int)xs.size() - 1; s++){
            int b0 = std::min(nBars - 1, (int)xs[s]), b1 = std::min(nBars - 1, (int)xs[s + 1]);
            float x0 = xs[s] * barSize, x1 = std::min((float)width, xs[s + 1] * barSize);
            for(int j = 0; j < n; j++){
                float a = p.option("harmonicsFade") == 1 ? 1 - pjSqrt(j / (float)n) : pjPow(1 - j / (float)n, pw);
                int hn = j * p.i("harmonicStep") + 1;
                float y[2];
                int bb[2] = {b0, b1};
                for(int e = 0; e < 2; e++){
                    int idx = rootList[std::min(59, bb[e])] + bassDeg;
                    float base = (float)(degreeSemis(idx, len) + 12 * octStart + (follow ? transposeList[std::min(59, bb[e])] : 0));
                    y[e] = height - f2p(p2f(base) * hn) * noteh - safey;
                }
                strokeRGBA((float)red(col), (float)green(col), (float)blue(col), 255 * a);
                if(p.f("harmonicsTaper") > 0) style.strokeWeight = p.f("thickness") * UY * lineWeight * (1 - p.f("harmonicsTaper") * pjPow(j / (float)n, p.f("harmonicsTaperPower")));
                for(int c2 = 0; c2 < copies; c2++){
                    float dy = copies == 1 ? 0 : pjMap((float)c2, 0, (float)(copies - 1), -spread, spread);
                    if(glide) portamentoPG(x0, y[0] + dy, x1, y[1] + dy, p.f("glideTension"), p.f("vibrato"), p.f("wobbleFreq"), p.f("wobble"));
                    else {
                        float w = x1 - x0;
                        bezier(x0, y[0] + dy, x0 + w * tension, y[0] + dy, x1 - w * tension, y[1] + dy, x1, y[1] + dy);
                    }
                }
            }
        }
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    void genMassesDraw(GenParams& p){
        if(p.option("pitchMode") == 1){ genMassHarmonics(p); return; }
        int lines = p.i("lines");
        float segBars = p.f("segmentBars");
        float chordC = p.f("chordChance"), holdC = p.f("holdChance");
        auto stepR = p.range("chordStep");
        int octStart = p.i("octaveStart"), octRange = p.i("octaveRange");
        bool follow = p.b("followHarmony");
        int bassDeg = p.b("bass") ? bassDegree : 0;
        bool glide = p.option("curve") == 1;
        float tension = p.f("tension");
        float noteh = height / (float)notegrid;
        int colourMode = p.option("colour");
        std::vector<uint32_t> lineCol(lines);
        for(int i = 0; i < lines; i++) lineCol[i] = pickColour(colourMode);
        int nBars = std::max(1, (int)std::ceil(bars));
        float conv = p.f("convergeChance");
        auto dashR = p.range("dash"); auto gapR = p.range("dashGap");
        bool walkMode = p.option("pitchMode") == 2;
        std::vector<int> walkDeg(walkMode ? lines : 0, 0);
        std::vector<float> cur;
        if(walkMode) cur = massWalk(walkDeg, 0, octStart, octRange, follow, 0, bassDeg);
        else { bool ch = random(100) < chordC; cur = massPitches(lines, ch, stepR, octStart, octRange, follow, 0, bassDeg); }
        int convN = p.i("convergeNote");
        float dashC = p.f("dashChance");
        int nSeg = std::max(1, (int)std::ceil(bars / segBars));
        style.fill = false;
        style.strokeWeight = p.f("thickness") * UY * lineWeight;
        for(int j = 0; j < nSeg; j++){
            float x0 = j * segBars * barSize;
            float x1 = std::min((float)width, (j + 1) * segBars * barSize);
            int bar = std::min(nBars - 1, (int)((j + 1) * segBars));
            bool hold = random(100) < holdC;
            std::vector<float> next;
            if(hold) next = cur;
            else if(walkMode) next = massWalk(walkDeg, p.i("walkStep"), octStart, octRange, follow, bar, bassDeg);
            else { bool ch = random(100) < chordC; next = massPitches(lines, ch, stepR, octStart, octRange, follow, bar, bassDeg); }
            if(!hold && conv > 0 && random(100) < conv){
                if(convN >= 0) for(int i = 0; i < lines; i++) next[i] = (float)(convN + (follow ? transposeList[std::min(59, bar)] : 0));
                else for(int i = 1; i < lines; i++) next[i] = next[0];
            }
            for(int i = 0; i < lines; i++){
                float y0 = height - cur[i] * noteh - safey;
                float y1 = height - next[i] * noteh - safey;
                strokeColour(lineCol[i]);
                if(glide) portamentoPG(x0, y0, x1, y1, p.f("glideTension"), p.f("vibrato"), p.f("wobbleFreq"), p.f("wobble"));
                else {
                    float w = x1 - x0;
                    if(dashR[1] > 0 && (dashC >= 100 || random(100) < dashC)){
                        float dl = drawIn(dashR) * width;
                        float gl = drawIn(gapR) * width;
                        int nseg = std::max(2, pjRound(w));
                        float acc = 0;
                        for(int q = 0; q < nseg; q++){
                            float ta = q / (float)nseg, tb = (q + 1) / (float)nseg;
                            float ax = bezierPoint(x0, x0 + w * tension, x1 - w * tension, x1, ta), ay = bezierPoint(y0, y0, y1, y1, ta);
                            float bx = bezierPoint(x0, x0 + w * tension, x1 - w * tension, x1, tb), by = bezierPoint(y0, y0, y1, y1, tb);
                            if(std::fmod(acc, dl + gl) < dl) line(ax, ay, bx, by);
                            acc += pdist(ax, ay, bx, by);
                        }
                    }else bezier(x0, y0, x0 + w * tension, y0, x1 - w * tension, y1, x1, y1);
                }
            }
            cur = next;
        }
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    // ================================================================== shapes (Shapes.pde)

    // the transform (pushMatrix / translate / rotate), applied to points before they are drawn
    struct Mat { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };   // x' = a x + c y + e, y' = b x + d y + f
    Mat M;
    std::vector<Mat> matStack;
    void pushMatrix(){ matStack.push_back(M); }
    void popMatrix(){ if(!matStack.empty()){ M = matStack.back(); matStack.pop_back(); } }
    void translate(float x, float y){ M.e += M.a * x + M.c * y; M.f += M.b * x + M.d * y; }
    void rotate(float t){
        float cs = pjCos(t), sn = pjSin(t);
        Mat n = M;
        n.a = M.a * cs + M.c * sn; n.b = M.b * cs + M.d * sn;
        n.c = -M.a * sn + M.c * cs; n.d = -M.b * sn + M.d * cs;
        M = n;
    }
    Canvas::Pt xf(float x, float y) const { return {M.a * x + M.c * y + M.e, M.b * x + M.d * y + M.f}; }

    // a shape (outline in user space): filled and / or stroked, as Processing does (fill first)
    void drawShape(const std::vector<Canvas::Pt>& user, bool closedShape){
        std::vector<Canvas::Pt> dev;
        for(auto& p : user) dev.push_back(xf(p.x, p.y));
        if(style.fill && closedShape){ uint32_t f = style.fillColor; gc->fillPolygon(dev, red(f), green(f), blue(f), (f >> 24) & 255); }
        if(style.stroke){ uint32_t s = style.strokeColor; gc->strokePath(dev, std::max(0.1f, style.strokeWeight), style.cap, red(s), green(s), blue(s), (s >> 24) & 255, true, closedShape); }
    }
    static std::vector<Canvas::Pt> ellipsePts(float cx, float cy, float w, float h){
        std::vector<Canvas::Pt> q;
        int n = std::max(32, std::min(720, (int)((std::fabs(w) + std::fabs(h)) * 1.6f)));
        for(int k = 0; k < n; k++){ float t = k * 6.28318531f / n; q.push_back({cx + w / 2 * std::cos(t), cy + h / 2 * std::sin(t)}); }
        return q;
    }
    // An ellipse as Java2D builds it: four cubic Beziers (k = 0.5522847498). Stroked, only their end
    // points snap to pixel centres (the control points move with them); filled, nothing snaps.
    void drawEllipse(float cx, float cy, float w, float h){
        const float k = 0.5522847498f;
        float rx = w / 2, ry = h / 2;
        Canvas::Pt E[4] = {{cx + rx, cy}, {cx, cy + ry}, {cx - rx, cy}, {cx, cy - ry}};
        Canvas::Pt C[4][2] = {{{cx + rx, cy + ry * k}, {cx + rx * k, cy + ry}}, {{cx - rx * k, cy + ry}, {cx - rx, cy + ry * k}},
                              {{cx - rx, cy - ry * k}, {cx - rx * k, cy - ry}}, {{cx + rx * k, cy - ry}, {cx + rx, cy - ry * k}}};
        Canvas::Pt dE[4], dC[4][2];
        for(int i = 0; i < 4; i++){ dE[i] = xf(E[i].x, E[i].y); dC[i][0] = xf(C[i][0].x, C[i][0].y); dC[i][1] = xf(C[i][1].x, C[i][1].y); }
        auto flatten = [&](const Canvas::Pt* e, Canvas::Pt (*cc)[2]){
            std::vector<Canvas::Pt> out;
            for(int i = 0; i < 4; i++){
                Canvas::Pt a0 = e[i], a1 = e[(i + 1) % 4], c0 = cc[i][0], c1 = cc[i][1];
                float est = std::fabs(c0.x - a0.x) + std::fabs(c0.y - a0.y) + std::fabs(c1.x - c0.x) + std::fabs(c1.y - c0.y) + std::fabs(a1.x - c1.x) + std::fabs(a1.y - c1.y);
                int n = std::max(8, std::min(1024, (int)(est / 1.5f)));
                for(int q = 0; q < n; q++){
                    float t = q / (float)n, u = 1 - t;
                    out.push_back({u * u * u * a0.x + 3 * u * u * t * c0.x + 3 * u * t * t * c1.x + t * t * t * a1.x,
                                   u * u * u * a0.y + 3 * u * u * t * c0.y + 3 * u * t * t * c1.y + t * t * t * a1.y});
                }
            }
            return out;
        };
        if(style.fill){ uint32_t f = style.fillColor; gc->fillPolygon(flatten(dE, dC), red(f), green(f), blue(f), (f >> 24) & 255); }
        if(style.stroke){
            Canvas::Pt sE[4], sC[4][2];
            for(int i = 0; i < 4; i++){ sE[i] = {Canvas::snapPC(dE[i].x, style.cap), Canvas::snapPC(dE[i].y, style.cap)}; }
            for(int i = 0; i < 4; i++){
                // each control point moves with the end point it belongs to
                sC[i][0] = {dC[i][0].x + sE[i].x - dE[i].x, dC[i][0].y + sE[i].y - dE[i].y};
                int j = (i + 1) % 4;
                sC[i][1] = {dC[i][1].x + sE[j].x - dE[j].x, dC[i][1].y + sE[j].y - dE[j].y};
            }
            uint32_t st = style.strokeColor;
            gc->strokePath(flatten(sE, sC), std::max(0.1f, style.strokeWeight), style.cap, red(st), green(st), blue(st), (st >> 24) & 255, false, true);
        }
    }
    static std::vector<Canvas::Pt> roundRectPts(float x, float y, float w, float h, float r){
        r = std::max(0.0f, std::min(r, std::min(std::fabs(w), std::fabs(h)) / 2));
        std::vector<Canvas::Pt> q;
        if(r <= 0){ q = {{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}; return q; }
        int n = std::max(4, (int)(r * 1.6f));
        auto arc = [&](float cx, float cy, float a0){ for(int k = 0; k <= n; k++){ float t = a0 + k * 1.5707963f / n; q.push_back({cx + r * std::cos(t), cy + r * std::sin(t)}); } };
        arc(x + w - r, y + r, -1.5707963f); arc(x + w - r, y + h - r, 0); arc(x + r, y + h - r, 1.5707963f); arc(x + r, y + r, 3.14159265f);
        return q;
    }

    struct V2 { float x = 0, y = 0; };
    struct ShapeItem {
        int kind = 0;
        float cx = 0, cy = 0, r = 0, hx = 0, hy = 0, th = 0;
        V2 ta, tb, tc;   // triangle
        V2 p0, p1;
        float S = 0, w = 0, h = 0, weight = 0, dash = 0, dgap = 0, amp = 0, phase = 0; int cyc = 0;
        V2 bez[4];
        std::vector<float> radii;
        char ch = 'A';
        float tx = 0, tw = 0, y0 = 0, y1 = 0, y2 = 0, y3 = 0, dev = 0, rn = 0, rn2 = 0, ymin = 0, ymax = 0, tmin = 0, tmax = 0;
        bool noisyW = false;
    };
    static bool segIntersect(V2 p0, V2 p1, V2 p2, V2 p3){
        float x1 = p0.x, y1 = p0.y, x2 = p1.x, y2 = p1.y, x3 = p2.x, y3 = p2.y, x4 = p3.x, y4 = p3.y;
        float uA = ((x4 - x3) * (y1 - y3) - (y4 - y3) * (x1 - x3)) / ((y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1));
        float uB = ((x2 - x1) * (y1 - y3) - (y2 - y1) * (x1 - x3)) / ((y4 - y3) * (x2 - x1) - (x4 - x3) * (y2 - y1));
        return uA >= 0 && uA <= 1 && uB >= 0 && uB <= 1;
    }
    static float orient(V2 a, V2 b, V2 c){ return (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x); }
    static bool onSegment(V2 a, V2 b, V2 p, float eps){
        return p.x <= std::max(a.x, b.x) + eps && p.x + eps >= std::min(a.x, b.x) && p.y <= std::max(a.y, b.y) + eps && p.y + eps >= std::min(a.y, b.y) && std::fabs(orient(a, b, p)) <= eps;
    }
    static bool edgesIntersect(V2 p1, V2 p2, V2 q1, V2 q2, float eps){
        if(std::max(p1.x, p2.x) + eps < std::min(q1.x, q2.x)) return false;
        if(std::max(q1.x, q2.x) + eps < std::min(p1.x, p2.x)) return false;
        if(std::max(p1.y, p2.y) + eps < std::min(q1.y, q2.y)) return false;
        if(std::max(q1.y, q2.y) + eps < std::min(p1.y, p2.y)) return false;
        float o1 = orient(p1, p2, q1), o2 = orient(p1, p2, q2), o3 = orient(q1, q2, p1), o4 = orient(q1, q2, p2);
        if(o1 * o2 < -eps && o3 * o4 < -eps) return true;
        if(std::fabs(o1) <= eps && onSegment(p1, p2, q1, eps)) return true;
        if(std::fabs(o2) <= eps && onSegment(p1, p2, q2, eps)) return true;
        if(std::fabs(o3) <= eps && onSegment(q1, q2, p1, eps)) return true;
        if(std::fabs(o4) <= eps && onSegment(q1, q2, p2, eps)) return true;
        return false;
    }
    static float signP(V2 p1, V2 p2, V2 p3){ return (p1.x - p3.x) * (p2.y - p3.y) - (p2.x - p3.x) * (p1.y - p3.y); }
    static bool pointInTri(V2 p, const ShapeItem& t, float eps){
        float d1 = signP(p, t.ta, t.tb), d2 = signP(p, t.tb, t.tc), d3 = signP(p, t.tc, t.ta);
        bool hasNeg = d1 < -eps || d2 < -eps || d3 < -eps, hasPos = d1 > eps || d2 > eps || d3 > eps;
        return !(hasNeg && hasPos);
    }
    static bool trianglesOverlap(const ShapeItem& a, const ShapeItem& b, float eps){
        V2 A[3] = {a.ta, a.tb, a.tc}, B[3] = {b.ta, b.tb, b.tc};
        for(int i = 0; i < 3; i++) for(int j = 0; j < 3; j++) if(edgesIntersect(A[i], A[(i + 1) % 3], B[j], B[(j + 1) % 3], eps)) return true;
        if(pointInTri(a.ta, b, eps) || pointInTri(a.tb, b, eps) || pointInTri(a.tc, b, eps)) return true;
        if(pointInTri(b.ta, a, eps) || pointInTri(b.tb, a, eps) || pointInTri(b.tc, a, eps)) return true;
        return false;
    }
    static bool obbOverlap(float cxA, float cyA, float hxA, float hyA, float thA, float cxB, float cyB, float hxB, float hyB, float thB, float gap){
        hxA += gap; hyA += gap; hxB += gap; hyB += gap;
        float cA = pjCos(thA), sA = pjSin(thA), cB = pjCos(thB), sB = pjSin(thB);
        float uAx = cA, uAy = sA, vAx = -sA, vAy = cA, uBx = cB, uBy = sB, vBx = -sB, vBy = cB;
        float tx = cxB - cxA, ty = cyB - cyA;
        float axes[4][2] = {{uAx, uAy}, {vAx, vAy}, {uBx, uBy}, {vBx, vBy}};
        for(auto& ax : axes){
            float distC = std::fabs(tx * ax[0] + ty * ax[1]);
            float rA = hxA * std::fabs(ax[0] * uAx + ax[1] * uAy) + hyA * std::fabs(ax[0] * vAx + ax[1] * vAy);
            float rB = hxB * std::fabs(ax[0] * uBx + ax[1] * uBy) + hyB * std::fabs(ax[0] * vBx + ax[1] * vBy);
            if(distC > rA + rB) return false;
        }
        return true;
    }
    bool shapesOverlap(const ShapeItem& a, const ShapeItem& b, float gapPx) const {
        int k = a.kind;
        if(k == 2 || k == 4) return false;
        if(k == 0 || k == 1) return segIntersect(a.p0, a.p1, b.p0, b.p1);
        if(k == 6) return trianglesOverlap(a, b, 0);
        if(k == 3) return obbOverlap(a.cx, a.cy, a.hx, a.hy, a.th, b.cx, b.cy, b.hx, b.hy, b.th, gapPx);
        if(k == 7){
            if(pdist(a.cx, a.cy, b.cx, b.cy) >= a.r + b.r + gapPx) return false;
            return obbOverlap(a.cx, a.cy, a.hx, a.hy, a.th, b.cx, b.cy, b.hx, b.hy, b.th, gapPx);
        }
        return pdist(a.cx, a.cy, b.cx, b.cy) < a.r + b.r + gapPx;
    }
    float brownianvalue(float idx, float w, float noisesc, float maxfreq, float ran){
        P.noiseDetail(1, 0.1f);
        noisesc = noisesc / (height / 1080.0f);
        float ph = idx / w; (void)ph;
        float noiseVal = prng.perlin((idx * noisesc), ran * noisesc);
        noiseVal = pjPow(noiseVal * 2, 1);
        ph = std::fmod(noiseVal * maxfreq, 1.0f);
        return (1 + pjSin(P_TWO_PI * ph)) / 2;
    }
    void dashline(float x0, float y0, float x1, float y1, float dash, float gap){
        float spacing[2] = {dash, gap};
        float distance = pdist(x0, y0, x1, y1);
        float xs[2], ys[2], drawn = 0;
        if(distance <= 0) return;
        for(int i = 0; i < 2; i++){ xs[i] = pjLerp(0, (x1 - x0), spacing[i] / distance); ys[i] = pjLerp(0, (y1 - y0), spacing[i] / distance); }
        int i = 0; bool drawLine = true;
        while(drawn < distance){
            if(drawLine) line(x0, y0, x0 + xs[i], y0 + ys[i]);
            x0 += xs[i]; y0 += ys[i];
            drawn = drawn + pjSqrt(xs[i] * xs[i] + ys[i] * ys[i]);
            i = (i + 1) % 2;
            drawLine = !drawLine;
        }
    }

    // flow field: particles along a Perlin angle field
    void shapesFlow(GenParams& p){
        int n = p.i("particles");
        auto stR = p.range("flowSteps");
        float sc = p.f("flowScale") * height, str = p.f("flowStrength");
        auto thickR = p.range("thickness");
        auto alphaR = p.range("alpha");
        int colourMode = p.option("colour");
        float noteH = height / (float)notegrid;
        float oz = random(1000);
        for(int i = 0; i < n; i++){
            float x = random((float)width);
            float y = random((float)height);
            int steps = std::max(1, drawInt(stR));
            uint32_t col = pickColour(colourMode);
            float a = drawIn(alphaR);
            style.strokeWeight = std::max(UY, drawIn(thickR) * noteH) * lineWeight;
            for(int s = 0; s < steps; s++){
                float ang = prng.perlin(oz + x / sc, oz + y / sc) * P_TWO_PI * str;
                x += pjCos(ang) * UY;
                y += pjSin(ang) * UY;
                if(x < 0 || x >= width || y < 0 || y >= height){ x = random((float)width); y = random((float)height); }
                float w = (1 - pjCos(P_TWO_PI * s / (float)steps)) / 2;
                strokeRGBA((float)red(col), (float)green(col), (float)blue(col), 255 * a * w);
                point(x, y);
            }
        }
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }

    // letters: the glyph outlines of the sketch's font (FuturaStd-Bold), given by the host (the app
    // reads the font file); without them a letter shape cannot be drawn here
public:
    struct Glyph { float advance = 0; std::vector<std::vector<Canvas::Pt>> contours; };   // units: size 1 (em), y down, baseline 0
    struct GlyphFont { float ascent = 0, descent = 0; std::map<char, Glyph> glyphs; };
    std::shared_ptr<const GlyphFont> font;   // set by the host before render()
    // Exact GPU raster paths for large live pages. Unsupported operations retain the CPU path.
    GpuEffects* gpu = nullptr;
    size_t gpuMinPixels = (size_t)1920 * 1080 * 2;
private:

    bool shapesSample(GenParams& p, ShapeItem& s, int kind, float S, float ar, float th, bool rotOn, float weight,
                      float W, float H, float noteH, float m, float xLo, float xHi, float ref, const std::array<float, 2>& sizeR, float minS,
                      bool edgeSize, bool twoPoints, float gapPx){
        s.kind = kind; s.S = S; s.th = th; s.weight = weight;
        float yLo = m, yHi = H - m;
        if(xHi <= xLo || yHi <= yLo) return false;
        if(kind == 0 || kind == 1){
            if(twoPoints){
                float cap = sizeR[1] * ref;
                float x0 = random(xLo, xHi); float y0 = random(yLo, yHi); float x1 = random(xLo, xHi); float y1 = random(yLo, yHi);
                float d = pdist(x0, y0, x1, y1);
                if(d < 1) return false;
                if(d > cap){ float k = cap / d; x1 = x0 + (x1 - x0) * k; y1 = y0 + (y1 - y0) * k; }
                s.p0 = {x0, y0}; s.p1 = {x1, y1};
            }else{
                float x0 = random(xLo, std::max(xLo, xHi - S)); float y0 = random(yLo, yHi);
                bool found = false; V2 p1;
                for(int k = 0; k < 100; k++){
                    float phi = rotOn ? th : random(P_TWO_PI);
                    V2 c_ = {x0 + S * pjCos(phi), y0 + S * pjSin(phi)};
                    if(c_.x >= xLo && c_.x <= xHi && c_.y >= yLo && c_.y <= yHi){ p1 = c_; found = true; break; }
                    if(rotOn) break;
                }
                if(!found) return false;
                s.p0 = {x0, y0}; s.p1 = p1;
            }
            if(kind == 1){ s.dash = drawIn(p.range("dash")) * W; s.dgap = drawIn(p.range("dashGap")) * W; }
            return true;
        }
        if(kind == 2){
            float R = S * 0.5f;
            float cxMin = xLo + R, cxMax = xHi - R, cyMin = yLo + R, cyMax = yHi - R;
            int guard = 0;
            while((cxMax <= cxMin || cyMax <= cyMin) && guard++ < 10){ R *= 0.9f; cxMin = xLo + R; cxMax = xHi - R; cyMin = yLo + R; cyMax = yHi - R; }
            if(cxMax <= cxMin || cyMax <= cyMin) return false;
            float cx = random(cxMin, cxMax); float cy = random(cyMin, cyMax);
            float a0 = random(P_TWO_PI);
            float sp = p.f("spread") * (P_PI / 180.0f);
            float jit = random(-sp, sp);
            V2 P0 = {cx + pjCos(a0) * R, cy + pjSin(a0) * R};
            V2 P3 = {cx + pjCos(a0 + P_PI + jit) * R, cy + pjSin(a0 + P_PI + jit) * R};
            float chx = P3.x - P0.x, chy = P3.y - P0.y, cm = pjSqrt(chx * chx + chy * chy);
            float L = std::max(cm, R);
            float dx = cm != 0 ? chx / cm : 0, dy = cm != 0 ? chy / cm : 0;
            float nx = -dy, ny = dx;
            float bend = p.f("bend");
            float t1 = random(0.25f, 0.45f); float t2 = random(0.55f, 0.75f);
            float k1 = random(-bend, bend) * R; float k2 = random(-bend, bend) * R;
            V2 P1 = {P0.x + dx * (L * t1) + nx * k1, P0.y + dy * (L * t1) + ny * k1};
            V2 P2 = {P0.x + dx * (L * t2) + nx * k2, P0.y + dy * (L * t2) + ny * k2};
            s.bez[0] = P0; s.bez[1] = P1; s.bez[2] = P2; s.bez[3] = P3;
            for(auto& q : s.bez){ q.x = pjConstrain(q.x, xLo, xHi); q.y = pjConstrain(q.y, yLo, yHi); }
            s.cx = cx; s.cy = cy; s.r = R;
            return true;
        }
        if(kind == 3){
            s.cyc = drawInt(p.range("cycles"));
            s.amp = std::min(drawIn(p.range("amplitude")) * noteH, std::min(W, H) * 0.25f);
            s.phase = random(P_TWO_PI);
            s.hx = S * 0.5f; s.hy = s.amp + weight * 0.5f;
            float ct = std::fabs(pjCos(th)), st = std::fabs(pjSin(th));
            float extX = s.hx * ct + s.hy * st, extY = s.hx * st + s.hy * ct;
            if(xHi - extX <= xLo + extX || yHi - extY <= yLo + extY) return false;
            s.cx = random(xLo + extX, xHi - extX); s.cy = random(yLo + extY, yHi - extY);
            return true;
        }
        if(kind == 4){
            float w = S;
            float y = random(H);
            if(xHi - w < xLo) return false;
            float x = random(xLo, xHi - w);
            int sig = (int)random(2) * 2 - 1;
            float h = sig > 0 ? random(H - y - 100 * UY) : -random(y);
            s.rn = (float)(int)random(10000000); s.rn2 = (float)(int)random(10000000);
            float ymin = m, ymax = H - m;
            float basePad = std::max(6 * UY, noteH * 16 * 0.5f);
            float y0 = pjConstrain(y, ymin + basePad, ymax - basePad);
            float y3 = pjConstrain(y + h, ymin + basePad, ymax - basePad);
            if(y3 < y0){ float t = y0; y0 = y3; y3 = t; }
            float kBase = (ymax - ymin) * drawIn(p.range("drift"));
            float k1 = kBase * random(0.7f, 1.3f);
            float k2a = random(0.7f, 1.3f);
            float k2 = kBase * k2a * (random(1) < 0.5f ? -1 : 1);
            s.y0 = y0; s.y3 = y3;
            s.y1 = pjConstrain(y0 + k1, ymin + basePad, ymax - basePad);
            s.y2 = pjConstrain(y3 - k2, ymin + basePad, ymax - basePad);
            s.dev = drawIn(p.range("wander")) * noteH;
            s.tx = x; s.tw = w; s.ymin = ymin; s.ymax = ymax;
            auto tr = p.range("thickness");
            s.tmin = tr[0] * noteH; s.tmax = tr[1] * noteH;
            s.noisyW = p.b("thicknessNoise");
            return true;
        }
        if(kind == 5){
            if(edgeSize){
                float x = random(xLo, xHi); float y = random(yLo, yHi);
                float d = std::min(std::min(x, W - x), std::min(y, H - y));
                float lo = std::max(minS, sizeR[0] * d), hi = sizeR[1] * d;
                S = hi > lo ? random(lo, hi) : lo;
                s.cx = x; s.cy = y;
            }else{
                float rx = S * ar * 0.5f, ry = S * 0.5f;
                float ext = std::max(rx, ry);
                if(xHi - ext <= xLo + ext || yHi - ext <= yLo + ext) return false;
                s.cx = random(xLo + ext, xHi - ext); s.cy = random(yLo + ext, yHi - ext);
            }
            s.S = S; s.w = S * ar; s.h = S;
            s.r = std::max(s.w, s.h) * 0.5f;
            return true;
        }
        if(kind == 6){
            float x0 = random(xLo, xHi); float y0 = random(yLo, yHi);
            float x1 = pjConstrain(x0 + random(-S / 2, S / 2), xLo, xHi);
            float y1 = pjConstrain(y0 + random(-S / 2, S / 2), yLo, yHi);
            float x2 = pjConstrain(x0 + random(-S / 2, S / 2), xLo, xHi);
            float y2 = pjConstrain(y0 + random(-S / 2, S / 2), yLo, yHi);
            float A = pdist(x1, y1, x2, y2), B = pdist(x0, y0, x2, y2), C = pdist(x0, y0, x1, y1);
            float maxSide = std::max(A, std::max(B, C)), minSide = std::min(A, std::min(B, C));
            if(maxSide > S || minSide < minS) return false;
            float twiceArea = std::fabs((x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0));
            if(maxSide <= 0 || twiceArea / maxSide < 0.25f * maxSide) return false;
            auto deg = [](float r_){ return r_ * (180.0f / P_PI); };
            float angA = deg((float)std::acos((double)pjConstrain((B * B + C * C - A * A) / std::max(2 * B * C, 1e-6f), -1.0f, 1.0f)));
            float angB = deg((float)std::acos((double)pjConstrain((A * A + C * C - B * B) / std::max(2 * A * C, 1e-6f), -1.0f, 1.0f)));
            float angC = 180 - angA - angB;
            if(std::min(angA, std::min(angB, angC)) < 22) return false;
            s.ta = {x0, y0}; s.tb = {x1, y1}; s.tc = {x2, y2};
            return true;
        }
        if(kind == 7){
            s.w = S * ar; s.h = S / ar;
            s.hx = s.w * 0.5f; s.hy = s.h * 0.5f;
            float ct = pjCos(th), st = pjSin(th);
            float ax = std::fabs(ct) * s.hx + std::fabs(st) * s.hy, ay = std::fabs(st) * s.hx + std::fabs(ct) * s.hy;
            float cxMin = xLo + gapPx * 0.5f + ax, cxMax = xHi - gapPx * 0.5f - ax;
            float cyMin = yLo + gapPx * 0.5f + ay, cyMax = yHi - gapPx * 0.5f - ay;
            if(cxMax <= cxMin || cyMax <= cyMin) return false;
            s.cx = random(cxMin, cxMax); s.cy = random(cyMin, cyMax);
            s.r = 1.02f * pjSqrt(s.hx * s.hx + s.hy * s.hy);
            return true;
        }
        if(kind == 8){
            float R0 = S;
            auto wob = p.range("wobble");
            float c_ = pjPow(random(1.0f), 2.0f);
            float amp = pjLerp(wob[0], wob[1], c_);
            float freq = pjLerp(0.25f, 0.85f, c_);
            int N = (int)pjLerp(20, 48, c_);
            int symK = (random(100) < p.f("symmetryChance")) ? (int)random(3, 6) : 0;
            float symAmt = (symK > 0) ? random(0.0f, amp * 0.25f) : 0.0f;
            float rMaxPre = R0 * (1.35f + amp + std::fabs(symAmt));
            float padX = rMaxPre + gapPx, padY = rMaxPre + gapPx;
            if(xHi - padX <= xLo + padX || yHi - padY <= yLo + padY) return false;
            float cx = random(xLo + padX, xHi - padX); float cy = random(yLo + padY, yHi - padY);
            std::vector<float> r(N), tmp(N);
            float step = P_TWO_PI / N;
            float phase = random(1000);
            for(int i = 0; i < N; i++){
                float a = i * step;
                float mm = (P.noise(phase + a * freq) - 0.5f) * 2.0f;
                float sv = (symK > 0) ? pjCos(symK * a) : 0.0f;
                float base = 0.03f * pjSin(a + phase * 0.31f);
                r[i] = std::max(R0 * (1.0f + amp * mm + symAmt * sv + base), R0 * 0.62f);
            }
            for(int pass = 0; pass < 3; pass++){
                for(int i = 0; i < N; i++) tmp[i] = (r[(i - 1 + N) % N] + 2 * r[i] + r[(i + 1) % N]) * 0.25f;
                for(int i = 0; i < N; i++) tmp[i] = 0.5f * tmp[i] + 0.25f * (tmp[(i - 1 + N) % N] + tmp[(i + 1) % N]);
                r = tmp;
            }
            float rMax = -1e9f;
            for(int i = 0; i < N; i++) rMax = std::max(rMax, r[i]);
            rMax *= 1.06f;
            float allowed = std::min(std::min(cx - xLo - gapPx, (xHi - gapPx) - cx), std::min(cy - yLo - gapPx, (yHi - gapPx) - cy));
            if(rMax > allowed){
                float k = std::max(0.0001f, allowed / rMax);
                for(int i = 0; i < N; i++) r[i] *= k;
                rMax = -1e9f;
                for(int i = 0; i < N; i++) rMax = std::max(rMax, r[i]);
                rMax *= 1.06f;
                if(rMax > allowed) return false;
            }
            s.cx = cx; s.cy = cy; s.r = rMax; s.radii = r;
            return true;
        }
        // letter
        int lm = p.option("letters");
        char ch = (char)('A' + (int)random(26));
        if(lm == 1 || (lm == 2 && random(1) < 0.5f)) ch = (char)std::tolower(ch);
        if(!font) throw std::runtime_error("letters need the sketch's font (FuturaStd-Bold.otf)");
        float sz = std::max(1.0f, S);
        auto git = font->glyphs.find(ch);
        float tw = git != font->glyphs.end() ? git->second.advance * sz : 0, tht = (font->ascent + font->descent) * sz;
        float rb = 0.5f * pjSqrt(tw * tw + tht * tht) + 2 * UY;
        float pad = rb + gapPx * 0.5f;
        if(xHi - pad <= xLo + pad || yHi - pad <= yLo + pad) return false;
        s.cx = random(xLo + pad, xHi - pad); s.cy = random(yLo + pad, yHi - pad);
        s.r = rb; s.ch = ch;
        return true;
    }

    void shapesTrace(const ShapeItem& s, float noteH){
        for(float j = 0; j < s.tw; j += 0.025f){
            float ph = j / s.tw;
            float t = ph * ph * (3.0f - 2.0f * ph);
            float bell = pjSin(P_PI * t);
            float yBase = bezierPoint(s.y0, s.y1, s.y2, s.y3, t);
            float n1 = brownianvalue(j, 100000, 0.005f, 1, s.rn);
            float w = s.weight;
            if(s.noisyW) w = pjConstrain(s.tmin + n1 * s.tmax * ((t / 0.75f) + 0.25f), 0.1f, noteH * 16);
            float n2 = brownianvalue(j, 100000, 0.005f, 1, s.rn2);
            float yNoise = (((n1 - 0.5f) * 2.0f) * 0.6f + ((n2 - 0.5f) * 2.0f) * 0.4f) * s.dev;
            yNoise *= pjLerp(0.35f, 1.10f, bell);
            float pad = (w * 0.5f) + 1.0f * UY;
            float maxUp = (s.ymax - pad) - yBase, maxDown = yBase - (s.ymin + pad);
            if(yNoise > maxUp) yNoise = maxUp;
            if(yNoise < -maxDown) yNoise = -maxDown;
            style.strokeWeight = w;
            point(s.tx + j, yBase + yNoise);
        }
    }

    void shapesPaint(const ShapeItem& s, uint32_t col, float al, bool fillIt, float corner, float noteH){
        float a = alphaOf(col) * al;
        int k = s.kind;
        bool open = k <= 4;
        float fr = (float)red(col), fg = (float)green(col), fb = (float)blue(col);
        if(open || !fillIt){
            style.fill = false;
            strokeRGBA(fr, fg, fb, a);
            style.strokeWeight = std::max(0.1f, s.weight * lineWeight);
        }else{
            style.stroke = false;
            auto ch = [](float v){ if(v > 255) v = 255; if(v < 0) v = 0; return (int)((v / 255.0f) * 255); };
            style.fillColor = ((uint32_t)ch(a) << 24) | ((uint32_t)ch(fr) << 16) | ((uint32_t)ch(fg) << 8) | (uint32_t)ch(fb);
            style.fill = true;
        }
        if(k == 0) line(s.p0.x, s.p0.y, s.p1.x, s.p1.y);
        else if(k == 1) dashline(s.p0.x, s.p0.y, s.p1.x, s.p1.y, s.dash, std::max(1.0f, s.dgap));
        else if(k == 2) bezier(s.bez[0].x, s.bez[0].y, s.bez[1].x, s.bez[1].y, s.bez[2].x, s.bez[2].y, s.bez[3].x, s.bez[3].y);
        else if(k == 3){
            pushMatrix(); translate(s.cx, s.cy); rotate(s.th);
            std::vector<Canvas::Pt> v;
            float step = std::max(2 * UX, (s.hx * 2) / 200.0f);
            for(float x = -s.hx; x <= s.hx; x += step){
                float t = pjMap(x, -s.hx, s.hx, 0, P_TWO_PI * s.cyc);
                v.push_back({x, pjSin(t + s.phase) * s.amp});
            }
            bool f = style.fill; style.fill = false;   // endShape() of a noFill shape: the stroke only
            drawShape(v, false);
            style.fill = f;
            popMatrix();
        }else if(k == 4) shapesTrace(s, noteH);
        else if(k == 5){
            pushMatrix(); translate(s.cx, s.cy); rotate(s.th);
            drawEllipse(0, 0, s.w, s.h);
            popMatrix();
        }else if(k == 6) drawShape({{s.ta.x, s.ta.y}, {s.tb.x, s.tb.y}, {s.tc.x, s.tc.y}}, true);
        else if(k == 7){
            pushMatrix(); translate(s.cx, s.cy); rotate(s.th);
            drawShape(roundRectPts(-s.w / 2, -s.h / 2, s.w, s.h, corner), true);
            popMatrix();
        }else if(k == 8){
            int N = (int)s.radii.size();
            float step = P_TWO_PI / N;
            pushMatrix(); translate(s.cx, s.cy);
            // curveVertex with curveTightness(0.96), closed: Catmull-Rom segments as cubic Beziers
            std::vector<Canvas::Pt> cv;
            for(int i = -2; i < N + 2; i++){ int idx = ((i % N) + N) % N; cv.push_back({s.radii[idx] * pjCos(idx * step), s.radii[idx] * pjSin(idx * step)}); }
            float tight = 0.96f, sc = (1 - tight) / 6;
            std::vector<Canvas::Pt> outl;
            for(size_t q = 1; q + 2 < cv.size(); q++){
                Canvas::Pt p0 = cv[q - 1], p1 = cv[q], p2 = cv[q + 1], p3 = cv[q + 2];
                Canvas::Pt c1 = {p1.x + (p2.x - p0.x) * sc, p1.y + (p2.y - p0.y) * sc}, c2 = {p2.x - (p3.x - p1.x) * sc, p2.y - (p3.y - p1.y) * sc};
                int n = 12;
                for(int k2 = (q == 1 ? 0 : 1); k2 <= n; k2++){
                    float t = k2 / (float)n, u = 1 - t;
                    outl.push_back({u * u * u * p1.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * p2.x,
                                    u * u * u * p1.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * p2.y});
                }
            }
            drawShape(outl, true);
            popMatrix();
        }else{
            // the letter: its outline at size S, centred (textAlign CENTER, CENTER), rotated
            auto git = font->glyphs.find(s.ch);
            if(git == font->glyphs.end()) return;
            float sz = std::max(1.0f, s.S);
            float tw = git->second.advance * sz;
            float yoff = font->ascent * sz / 2;   // Processing's textAlign(CENTER, CENTER): the baseline at y + ascent / 2
            pushMatrix(); translate(s.cx, s.cy); rotate(s.th);
            std::vector<std::vector<Canvas::Pt>> polys;
            for(auto& c_ : git->second.contours){
                std::vector<Canvas::Pt> q;
                for(auto& pt : c_) q.push_back(xf(pt.x * sz - tw / 2, pt.y * sz + yoff));
                polys.push_back(q);
            }
            popMatrix();
            uint32_t f = style.fillColor;
            if(style.fill) gc->fillPolygons(polys, red(f), green(f), blue(f), (f >> 24) & 255);
            else { uint32_t st = style.strokeColor; for(auto& q : polys) gc->strokePath(q, std::max(0.1f, style.strokeWeight), style.cap, red(st), green(st), blue(st), (st >> 24) & 255, true, true); }
        }
    }

    void genShapesDraw(GenParams& p){
        static const char* SHAPE_MODES[] = {"line", "dashed line", "curve", "sine", "trace", "ellipse", "triangle", "rect", "blob", "letter", "flow"};
        int kind = p.option("shape");
        if(std::string(SHAPE_MODES[std::min(10, kind)]) == "flow"){ shapesFlow(p); return; }
        float W = (float)width, H = (float)height;
        float noteH = H / (float)notegrid;
        float m = p.f("margin") * H;
        float xLo = m, xHi = W - m;
        int sizeOf = p.option("sizeOf");
        auto sizeR = p.range("size");
        float minS = p.f("minSize") * noteH;
        auto aspR = p.range("aspect");
        float bias = p.f("aspectBias");
        auto rotR = p.range("rotation");
        float rotC = p.f("rotationChance");
        bool rotOn = !(rotR[0] == 0 && rotR[1] == 0);
        bool twoPoints = p.option("lineMode") == 0;
        bool avoid = p.option("overlap") == 1;
        float gapPx = p.f("gap") * noteH;
        int attempts = p.i("attempts");
        float chance = p.f("chance");
        auto thickR = p.range("thickness");
        auto alphaR = p.range("alpha");
        int colourMode = p.option("colour");
        bool fillIt = p.option("paint") == 0;
        int cap = p.option("cap");
        float ref = sizeOf == 1 ? std::max(W, H) : (sizeOf == 2 ? W : (sizeOf == 3 ? H : std::min(W, H)));
        bool edgeSize = sizeOf == 4;
        pushStyle();
        bool addWas = gc->addMode;
        if(p.option("blend") == 1) gc->addMode = true;
        style.cap = cap == 0 ? Canvas::ROUND : (cap == 1 ? Canvas::SQUARE : Canvas::PROJECT);
        std::vector<ShapeItem> placed;
        int n = drawInt(p.range("count"));
        for(int i = 0; i < n; i++){
            if(chance < 100 && random(100) >= chance) continue;
            for(int a = 0; a < attempts; a++){
                float S = 0;
                if(!edgeSize && !(twoPoints && (kind == 0 || kind == 1))) S = std::max(drawIn(sizeR) * ref, minS);
                float ar = 1;
                if(bias > 0){ float u = random(1); float d = drawIn(aspR); ar = pjLerp(1, d, pjPow(u, bias)); }
                else ar = drawIn(aspR);
                float th = 0;
                if(rotOn) th = (rotC >= 100 || random(100) < rotC) ? drawIn(rotR) * (P_PI / 180.0f) : 0;
                float weight = drawIn(thickR) * noteH;
                ShapeItem s;
                if(!shapesSample(p, s, kind, S, ar, th, rotOn, weight, W, H, noteH, m, xLo, xHi, ref, sizeR, minS, edgeSize, twoPoints, gapPx)) continue;
                bool clash = false;
                if(avoid) for(auto& q : placed) if(shapesOverlap(s, q, gapPx)){ clash = true; break; }
                if(clash) continue;
                placed.push_back(s);
                uint32_t col = pickColour(colourMode);
                float al = drawIn(alphaR);
                shapesPaint(s, col, al, fillIt, p.f("corner") * noteH, noteH);
                break;
            }
        }
        gc->addMode = addWas;
        popStyle();
        style.fill = false;
        strokeGray(255, 255);
        style.strokeWeight = UY;
    }
};

// ------------------------------------------------------------------ the test underlay
// The sketch's test underlay (legacy t3_clouds, r12_fullEuclidean, h2_iridescence, not ported) is
// replaced by a busy image from ported generators: noise bands, a note grid, kicks, a sweep
inline json glUnderlay(){
    using J = json;
    return J::array({
        J{{"gen", "hits"}, {"params", {{"cell", "noise band"}, {"steps", 2}, {"patternSteps", 2}, {"fills", {2}}, {"density", 100}, {"length", {2, 2}}, {"bandLow", 0.05}, {"bandHigh", 0.95}, {"level", 0.3}, {"levelAlways", true}, {"contrast", 6}, {"colour", "palette"}}}},
        J{{"gen", "pulse"}, {"params", {{"density", 85}, {"fills", {5, 7}}, {"partials", {3, 8}}, {"colour", "palette"}}}},
        J{{"gen", "hits"}, {"params", {{"cell", "kick sweep"}, {"steps", 4}, {"patternSteps", 4}, {"fills", {4}}, {"density", 100}, {"length", {1, 1}}}}},
        J{{"gen", "sweep"}, {"params", {{"count", 2}}}}
    });
}
inline void substituteUnderlay(json& req){
    if(!req.contains("items") || !req["items"].is_array()) return;
    json out = json::array();
    bool replaced = false;
    for(auto& it : req["items"]){
        bool legacyUnder = false;
        if(it.is_object() && it.contains("method") && it["method"].is_string()){
            std::string m = it["method"].get<std::string>();
            legacyUnder = m == "t3_clouds" || m == "r12_fullEuclidean" || m == "h2_iridescence";
        }
        if(legacyUnder){ if(!replaced) for(auto& u : glUnderlay()) out.push_back(u); replaced = true; }
        else out.push_back(it);
    }
    if(replaced) req["items"] = out;
}


} // namespace formsgl
