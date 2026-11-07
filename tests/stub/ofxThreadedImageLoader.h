#pragma once
#include "ofMain.h"
struct ofxThreadedImageLoader { void loadFromDisk(ofImage& im, const std::string& p){ im.loaded = true; im.path = p; } };
