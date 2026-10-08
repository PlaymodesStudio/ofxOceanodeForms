#pragma once

#include "ofxOceanode.h"

#include "formsScroller.h"
#include "formsScrollerFolder.h"
#include "probabilityDropdownList.h"
#include "texUniForms.h"
#ifdef OFXOCEANODE_USE_OSC
#include "multiOscSender.h"
#endif

class ofxOceanodeForms {
public:
    static void registerModels(ofxOceanode &oceanode) {
        oceanode.registerModel<formsScroller>("Forms");
        oceanode.registerModel<formsScrollerFolder>("Forms");
        oceanode.registerModel<probabilityDropdownList>("Forms");
        oceanode.registerModel<texUniForms>("Forms");
#ifdef OFXOCEANODE_USE_OSC
        oceanode.registerModel<multiOscSender>("Forms");
#endif
    }
};
