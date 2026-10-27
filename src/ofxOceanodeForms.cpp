//
//  ofxOceanodeForms.cpp
//

#include "ofxOceanodeForms.h"
#include "ofxOceanode.h"

#include "formsScroller.h"
#include "formsScrollerFolder.h"
#include "probabilityDropdownList.h"
#include "texUniForms.h"
#ifdef OFXOCEANODE_USE_OSC
#include "multiOscSender.h"
#endif

namespace ofxOceanodeForms{

void registerModels(ofxOceanode &o){
    o.registerModel<formsScroller>("Forms");
    o.registerModel<formsScrollerFolder>("Forms");
    o.registerModel<probabilityDropdownList>("Forms");
    o.registerModel<texUniForms>("Forms");
#ifdef OFXOCEANODE_USE_OSC
    o.registerModel<multiOscSender>("Forms");
#endif
}

void registerCollection(ofxOceanode &o){
    registerModels(o);
}

}
