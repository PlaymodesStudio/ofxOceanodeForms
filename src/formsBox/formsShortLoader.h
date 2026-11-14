//
//  formsShortLoader.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Loads PNGs from disk into 16-bit images (ofShortImage) on a thread, the texture made on the
//  app's update: ofxThreadedImageLoader's loadFromDisk for 16 bits per channel (that addon only
//  takes 8-bit ofImage, which would bring a 16-bit page down to 8 bits on its way to the screen).
//  As there: the image must outlive its load.
//

#pragma once

#include "ofMain.h"
#include <string>

namespace forms {

#ifdef FORMS_STUB_OF   // tests/stub/ofMain.h: no threads, no GL
struct ShortImageLoader { void loadFromDisk(ofShortImage& im, const std::string& p){ im.loaded = true; im.path = p; } };
#else
class ShortImageLoader : public ofThread {
public:
    ShortImageLoader(){ ofAddListener(ofEvents().update, this, &ShortImageLoader::update); startThread(); }
    ~ShortImageLoader(){
        toLoad.close(); toUpdate.close();
        waitForThread(true);
        ofRemoveListener(ofEvents().update, this, &ShortImageLoader::update);
    }
    void loadFromDisk(ofShortImage& image, const std::string& file){
        image.setUseTexture(false);
        toLoad.send(Entry{&image, file});
    }
private:
    struct Entry { ofShortImage* image = nullptr; std::string file; };
    void threadedFunction() override {
        Entry e;
        while(toLoad.receive(e)){
            if(e.image->load(e.file)) toUpdate.send(e);
            else ofLogError("Forms Box") << "couldn't load file: \"" << e.file << "\"";
        }
    }
    // one image per update, so the GL thread is not held for long
    void update(ofEventArgs&){
        Entry e;
        if(toUpdate.tryReceive(e)){ e.image->setUseTexture(true); e.image->update(); }
    }
    ofThreadChannel<Entry> toLoad, toUpdate;
};
#endif

} // namespace forms
