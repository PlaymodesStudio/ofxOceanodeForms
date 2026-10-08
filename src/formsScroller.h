//
//  formsScroller.h
//  OceanodeScroller
//
//  Created by Eduard Frigola on 20/2/25.
//

#ifndef formsScroller_h
#define formsScroller_h

#include "ofxOceanodeNodeModel.h"
#include "ofxThreadedImageLoader.h"

class formsScroller : public ofxOceanodeNodeModel{
public:
    formsScroller();
    
    void setup();
    void update(ofEventArgs &a);
    
private:
    ofEventListeners listeners;
    ofParameter<int> width, height;
    ofParameter<float> phase;
    //ofParameter<float> opacity, bpm;
    ofParameter<int> headerPos;
    //ofParameter<int> transpose, bars, notegrid;
    //ofParameter<vector<int>> scale;
    ofParameter<string> nextFileToLoad;
    ofParameter<string> currentFileInHeader;
    ofParameter<ofTexture*> output;
    
    float oldPhase;
    
    ofxThreadedImageLoader imageLoader;
    string lastLoadedIntoImage;
    
    ofImage images[3];
    std::string imagesFilenames[3];
    int leavingImageIndex;
    int apearingImageIndex;
    int loadingImageIndex;
    int loadingImageBackupIndex;
    
    bool isLoadingImage;
    bool tryLoadInNextUpdate;
    
    ofFbo fbo;
};

#endif /* formsScroller_h */
