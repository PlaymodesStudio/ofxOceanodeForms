#pragma once

#include "ofxOceanodeNodeModel.h"
#include "ofxThreadedImageLoader.h"

class formsScrollerFolder : public ofxOceanodeNodeModel {
public:
    formsScrollerFolder() : ofxOceanodeNodeModel("Forms Scroller Folder") {}
    
    void setup() {
        invert = false;
        oldPhase = 0;
        currentImageIndex = 0;
        
        addParameter(width.set("Width", 1920, 1, INT_MAX));
        addParameter(height.set("Height", 1080, 1, INT_MAX));
        addParameter(phase.set("Phase", 0, 0, 1));
        addParameter(header.set("Header", 0.25, 0, 1));
        addParameter(selectFolderButton.set("Select Folder", false));
        addParameter(folderPath.set("Folder", ""));
        addOutputParameter(numImages.set("Num Imgs", 0, 0, INT_MAX));
        addOutputParameter(nextFileToLoad.set("Next File", ""));
        addOutputParameter(lastLoadedFile.set("Last File", ""));
        addParameter(nextButton.set("Next Image", false));
        addOutputParameter(currentImageNumber.set("Current", 1, 1, INT_MAX));
        addOutputParameter(output.set("Output", nullptr));
        
        addOutputParameter(beats.set("Beats", 0, 0, INT_MAX));
        addOutputParameter(root.set("Root", 0, 0, 11));
        addOutputParameter(transpose.set("Transpose", 0, -12, 12));
        
        vector<int> defaultScale = {0}; // Default scale with one note
        vector<int> minScale(1, 0); // Minimum value for each note
        vector<int> maxScale(1, 11); // Maximum value for each note (assuming 0-11 for 12 notes in an octave)
        addOutputParameter(scale.set("Scale", defaultScale, minScale, maxScale));
        addOutputParameter(range.set("Range", 0, 0, INT_MAX));

        
        fbo.allocate(width, height);
        listeners.push(width.newListener([this](int &i){
            fbo.allocate(width, height);
        }));
        listeners.push(height.newListener([this](int &i){
            fbo.allocate(width, height);
        }));
        
        listeners.push(folderPath.newListener([this](string &path){
            loadImagesFromFolder(path);
        }));
        
        listeners.push(nextButton.newListener([this](bool &b){
            if(b) {
                loadNextImage();
                nextButton = false;
            }
        }));

        listeners.push(selectFolderButton.newListener([this](bool &b){
            if(b) {
                openFolderDialog();
                selectFolderButton = false;
            }
        }));
    }
    
    void update(ofEventArgs &a) {
            if(oldPhase - phase > 0.5 || (nextButton && nextButton.get())){
                invert = !invert;
                loadNextImage();
            }
            
            fbo.begin();
            ofBackground(0);
            float header1 = invert ? (1-phase)*width : -phase*width;
            float header2 = !invert ? (1-phase)*width : -phase*width;
            ofPushMatrix();
            if(image1.isAllocated()) {
                image1.draw(header1, 0, width, height);
            }
            if(image2.isAllocated()) {
                image2.draw(header2, 0, width, height);
            }
            ofPopMatrix();
            fbo.end();
            
            output = &fbo.getTexture();
            
            oldPhase = phase;
            
            currentImageNumber = currentImageIndex + 1;
        
            updateMusicParameters();

        }
    
private:
    void openFolderDialog() {
        ofFileDialogResult result = ofSystemLoadDialog("Select folder with PNG images", true);
        if(result.bSuccess) {
            folderPath = result.getPath();
        }
    }

    void loadImagesFromFolder(const string &path) {
            ofDirectory dir(path);
            dir.allowExt("png");
            dir.listDir();
            
            imageFiles.clear();
            for(int i = 0; i < dir.size(); i++) {
                imageFiles.push_back(dir.getPath(i));
            }
            
            if(imageFiles.empty()) {
                ofLogWarning("formsScrollerFolder") << "No PNG images found in the selected folder.";
                return;
            }

            std::sort(imageFiles.begin(), imageFiles.end(),
                [](const string &a, const string &b) {
                    string fileNameA = ofFilePath::getBaseName(a);
                    string fileNameB = ofFilePath::getBaseName(b);
                    return stoi(fileNameA) < stoi(fileNameB);
                }
            );
            
            numImages = imageFiles.size();
            currentImageIndex = 0;
            currentImageNumber = 1;  // Reset to 1 when loading new folder
            
            if(numImages > 0) {
                imageLoader.loadFromDisk(image1, imageFiles[0]);
                if(numImages > 1) {
                    imageLoader.loadFromDisk(image2, imageFiles[1]);
                }
                updateFileParameters();
            }
        
        loadAssociatedFiles();

        }
    
    void loadAssociatedFiles() {
            for (const auto& imagePath : imageFiles) {
                string baseName = ofFilePath::removeExt(imagePath);
                string barsPath = baseName + "_bars.txt";
                string scalePath = baseName + "_scale.txt";
                string rootPath = baseName + "_root.txt";
                string transposePath = baseName + "_trans.txt";
                string rangePath = baseName + "_range.txt";

                
                ImageData data;
                data.bars = readIntFromFile(barsPath);
                data.scale = readIntVectorFromFile(scalePath);
                data.roots = readIntVectorFromFile(rootPath);
                data.transposes = readIntVectorFromFile(transposePath);
                data.range = readIntFromFile(rangePath);

                imageDataMap[imagePath] = data;
            }
        }
    
    int readIntFromFile(const string& path) {
            ifstream file(path);
            int value;
            if (file >> value) {
                return value;
            }
            return 0;
        }
        
        vector<int> readIntVectorFromFile(const string& path) {
            ifstream file(path);
            vector<int> values;
            int value;
            while (file >> value) {
                values.push_back(value);
            }
            return values;
        }
        
        void updateMusicParameters() {
            if (imageFiles.empty() || currentImageIndex >= imageFiles.size()) return;
            
            float headerPosition = header * width;
            float scrollPosition = phase * width;
            
            // Calculate the position of the left edge of the current image
            float currentImageLeftEdge = -scrollPosition;
            
            // Determine which image is crossing the header
            int activeImageIndex;
            float positionInImage;
            
            if (currentImageLeftEdge <= headerPosition && headerPosition < currentImageLeftEdge + width) {
                // Header is on the current image
                activeImageIndex = currentImageIndex;
                positionInImage = headerPosition - currentImageLeftEdge;
            } else if (headerPosition >= currentImageLeftEdge + width) {
                // Header is on the next image
                activeImageIndex = (currentImageIndex + 1) % numImages;
                positionInImage = headerPosition - (currentImageLeftEdge + width);
            } else {
                // Header is on the previous image
                activeImageIndex = (currentImageIndex - 1 + numImages) % numImages;
                positionInImage = headerPosition - (currentImageLeftEdge - width);
            }
            
            // Update the current image number (for display purposes)
            currentImageNumber = activeImageIndex + 1;  // Convert 0-based index to 1-based for display
            
            // Manually adjust the index for txt file reading
            int txtIndex = (activeImageIndex - 1 + numImages) % numImages;
            
            string activeImagePath = imageFiles[activeImageIndex];
            string txtImagePath = imageFiles[txtIndex];
            const ImageData& data = imageDataMap[txtImagePath];
            
            
            // Update Beats
            beats = data.bars * 4;
            
            // Update Scale
            if (!data.scale.empty()) {
                scale = data.scale;
            } else {
                scale = {0}; // Default scale if no data is available
            }
            
            // Calculate normalized position and current bar
            float normalizedPosition = positionInImage / width;
            int currentBar = (int)(normalizedPosition * data.bars);
            currentBar = ofClamp(currentBar, 0, data.bars - 1);
            
            // Update Root and Transpose
            if (currentBar < data.roots.size() && currentBar < data.transposes.size()) {
                int newRoot = data.roots[currentBar];
                int newTranspose = data.transposes[currentBar];
                
                if (newRoot != root || newTranspose != transpose) {
                    root = newRoot;
                    transpose = newTranspose;
                }
            } else {
                if (root != 0 || transpose != 0) {
                    root = 0;
                    transpose = 0;
                   
                }
            }
            
            // Update Range parameter
            range = data.range;
        }

        void loadNextImage() {

            currentImageIndex = (currentImageIndex + 1) % numImages;
            int nextIndex = (currentImageIndex + 1) % numImages;

            if(currentImageIndex < imageFiles.size()) {
                string currentImagePath = imageFiles[currentImageIndex];
                
                if(invert) {
                    imageLoader.loadFromDisk(image1, currentImagePath);
                } else {
                    imageLoader.loadFromDisk(image2, currentImagePath);
                }
                
                updateFileParameters();
            } else {
                
            }
        }
    
    void updateFileParameters() {
            if(!imageFiles.empty() && currentImageIndex < imageFiles.size()) {
                lastLoadedFile = imageFiles[currentImageIndex];
                nextFileToLoad = imageFiles[(currentImageIndex + 1) % numImages];
            } else {
                ofLogWarning("formsScrollerFolder") << "Unable to update file parameters. Image files might be empty or index out of bounds.";
            }
        }
    
    struct ImageData {
            int bars;
            vector<int> scale;
            vector<int> roots;
            vector<int> transposes;
            int range;
        };

    ofEventListeners listeners;
    ofParameter<int> width, height;
    ofParameter<float> phase;
    ofParameter<string> folderPath;
    ofParameter<bool> selectFolderButton;
    ofParameter<int> numImages;
    ofParameter<string> nextFileToLoad, lastLoadedFile;
    ofParameter<bool> nextButton;
    ofParameter<ofTexture*> output;
    ofParameter<int> currentImageNumber;
    
    ofParameter<int> beats;
    ofParameter<int> root;
    ofParameter<int> transpose;
    ofParameter<vector<int>> scale;
    ofParameter<float> header;
    ofParameter<int> range;
       
    map<string, ImageData> imageDataMap;
    
    float oldPhase;
    bool invert;
    
    ofImage image1, image2;
    ofxThreadedImageLoader imageLoader;
    ofFbo fbo;
    
    vector<string> imageFiles;
    int currentImageIndex;
};
