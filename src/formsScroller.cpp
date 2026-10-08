//
//  formsScroller.cpp
//  OceanodeScroller
//
//  Created by Eduard Frigola on 20/2/25.
//

#include "formsScroller.h"

formsScroller::formsScroller() : ofxOceanodeNodeModel("Forms Scroller"){
    
}

void formsScroller::setup(){
    oldPhase = 0;
//    images[0].load("1.png");
//    images[1].load("2.png");
    leavingImageIndex = 0;
    apearingImageIndex = 1;
    loadingImageIndex = 2;
    loadingImageBackupIndex = -1;
    tryLoadInNextUpdate = false;
    isLoadingImage = false;
    
    addParameter(width.set("Width", 1920, 1, INT_MAX));
    addParameter(height.set("Height", 1080, 1, INT_MAX));
    addParameter(phase.set("Phase", 0, 0, 1));
    addParameter(headerPos.set("Header Pos", 100, 0, INT_MAX));
    //addParameter(opacity.set("Opacity", 0, 0, 1));
    //addParameter(transpose.set("Transpose", 0, 0, 11));
    //addParameter(bpm.set("BPM", 120, 0, 999));
    //addParameter(bars.set("Bars", 0, 0, 32));
    //addParameter(scale.set("Scale", {0}, {0}, {24}));
    //addParameter(notegrid.set("Notegrid", 120, 24, 999));
    addParameter(nextFileToLoad.set("Next File", ""));
    addParameter(currentFileInHeader.set("In Header", ""));
    addOutputParameter(output.set("Output", nullptr));
    
    fbo.allocate(width, height);
    listeners.push(width.newListener([this](int &i){
        fbo.allocate(width, height);
    }));
    listeners.push(height.newListener([this](int &i){
        fbo.allocate(width, height);
    }));
    
    fbo.allocate(width, height);
    
    listeners.push(nextFileToLoad.newListener([this](string &s){
        if(ofFile::doesFileExist(s) && lastLoadedIntoImage != s){
            if(isLoadingImage){ //is loading
                tryLoadInNextUpdate = true;
            }else{
                lastLoadedIntoImage = nextFileToLoad;
                int indexToLoad = loadingImageIndex;
                if(loadingImageBackupIndex != -1) indexToLoad = loadingImageBackupIndex;
                imagesFilenames[indexToLoad] = nextFileToLoad;
                imageLoader.loadFromDisk(images[indexToLoad], nextFileToLoad);
                isLoadingImage = true;
            }
        }
    }));
}


void formsScroller::update(ofEventArgs &a){
    int indexToLoad = loadingImageIndex;
    if(loadingImageBackupIndex != -1) indexToLoad = loadingImageBackupIndex;
    if(images[indexToLoad].isAllocated() && images[indexToLoad].isUsingTexture()){
        if(tryLoadInNextUpdate){
            lastLoadedIntoImage = nextFileToLoad;
            int indexToLoad = loadingImageIndex;
            if(loadingImageBackupIndex != -1) indexToLoad = loadingImageBackupIndex;
            imagesFilenames[indexToLoad] = nextFileToLoad;
            imageLoader.loadFromDisk(images[indexToLoad], nextFileToLoad);
            tryLoadInNextUpdate = false;
            isLoadingImage = true;
        }else{
            if(leavingImageIndex + apearingImageIndex + loadingImageIndex != 3 && isLoadingImage && loadingImageBackupIndex == -1){
                loadingImageBackupIndex = 3 - (loadingImageIndex + apearingImageIndex);
                images[loadingImageBackupIndex].clear();
            }
            isLoadingImage = false;
        }
    }
    

    
    //detecció de edge
    if(oldPhase - phase > 0.5){
        if(images[loadingImageIndex].isAllocated() && images[loadingImageIndex].isUsingTexture()){
            leavingImageIndex = apearingImageIndex;
            apearingImageIndex = loadingImageIndex;
            if(loadingImageBackupIndex != -1){
                loadingImageIndex = loadingImageBackupIndex;
                loadingImageBackupIndex = -1;
            }else{
                loadingImageIndex = 3 - (leavingImageIndex + apearingImageIndex);
                images[loadingImageIndex].clear();
            }
        }else{
            leavingImageIndex = apearingImageIndex;
        }
    }
    
    //Backwards
    if(phase-oldPhase > 0.5 ){
        if(images[loadingImageIndex].isAllocated() && images[loadingImageIndex].isUsingTexture()){
            apearingImageIndex = leavingImageIndex;
            leavingImageIndex = loadingImageIndex;
            if(loadingImageBackupIndex != -1){
                loadingImageIndex = loadingImageBackupIndex;
                loadingImageBackupIndex = -1;
            }else{
                loadingImageIndex = 3 - (leavingImageIndex + apearingImageIndex);
                images[loadingImageIndex].clear();
            }
        }else{
            apearingImageIndex = leavingImageIndex;
        }
    }

    fbo.begin();
    ofBackground(0);
    float header1 = -phase*width;
    float header2 = (1-phase)*width;
    ofPushMatrix();
    if(images[leavingImageIndex].isAllocated() && images[leavingImageIndex].isUsingTexture()){
        images[leavingImageIndex].draw(header1, 0, width, height);
        if(headerPos > header1 && headerPos <= (header1 + width)){
            currentFileInHeader = imagesFilenames[leavingImageIndex];
        }
    }
    if(images[apearingImageIndex].isAllocated() && images[apearingImageIndex].isUsingTexture()){
        images[apearingImageIndex].draw(header2, 0, width, height);
        if(headerPos > header2 && headerPos <= (header2 + width)){
            currentFileInHeader = imagesFilenames[apearingImageIndex];
        }
    }
    ofPopMatrix();
    fbo.end();
    
    output = &fbo.getTexture();
    
    oldPhase = phase;
}
