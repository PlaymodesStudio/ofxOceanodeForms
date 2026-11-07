// Headless stand-in for the few openFrameworks calls the player uses (tests only): nothing is
// drawn; an FBO is always "allocated" and an image is "loaded" as soon as it is asked for.
#pragma once
#include <string>
#include <vector>
#include <map>
#include <set>
#include <memory>
#include <functional>
#include <algorithm>
#include <cmath>
#define GL_RGBA 0x1908
#define GL_SRC_ALPHA 0x0302
#define GL_ONE_MINUS_SRC_ALPHA 0x0303
#define GL_ONE 1
#define GL_ZERO 0
inline void glBlendFuncSeparate(int, int, int, int){}
struct ofFboSettings { int width = 0, height = 0, internalformat = 0; };
struct ofFbo {
    int w = 0, h = 0; bool alloc = false;
    void allocate(const ofFboSettings& s){ w = s.width; h = s.height; alloc = true; }
    bool isAllocated() const { return alloc; }
    float getWidth() const { return (float)w; } float getHeight() const { return (float)h; }
    void begin(){} void end(){}
    void readToPixels(struct ofPixels&) const {}
};
struct ofPixels {};
struct ofBuffer { void append(const std::string&){} };
inline void ofBufferToFile(const std::string&, const ofBuffer&){}
inline void ofSaveImage(const ofPixels&, const std::string&){}
enum ofImageType { OF_IMAGE_GRAYSCALE, OF_IMAGE_COLOR, OF_IMAGE_COLOR_ALPHA };
struct ofImage {
    bool loaded = false; std::string path;
    void setFromPixels(const unsigned char*, int, int, ofImageType){ loaded = true; }
    bool load(const std::string& p){ path = p; loaded = true; return true; }
    bool isAllocated() const { return loaded; } bool isUsingTexture() const { return loaded; }
    void clear(){ loaded = false; path.clear(); }
    void draw(float, float, float, float){} void drawSubsection(float, float, float, float, float, float, float, float){}
    float getWidth() const { return 1920; } float getHeight() const { return 1080; }
};
inline void ofClear(int, int, int, int){} inline void ofBackground(int){}
inline void ofPushStyle(){} inline void ofPopStyle(){} inline void ofEnableAlphaBlending(){}
inline void ofSetColor(int, int = 0, int = 0, int = 255){}
inline float ofClamp(float v, float a, float b){ return std::min(b, std::max(a, v)); }
template<typename T> std::string ofToString(const T& v, int = 0){ return std::to_string(v); }
