#pragma once

// Declarations only: the node headers and the bodies live in ofxOceanodeForms.cpp, so
// including this header does not compile every node of the addon again.

class ofxOceanode;

namespace ofxOceanodeForms{

void registerModels(ofxOceanode &o);
void registerCollection(ofxOceanode &o);

}
