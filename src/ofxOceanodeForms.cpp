//
//  ofxOceanodeForms.cpp
//

#include "ofxOceanodeForms.h"
#include "ofxOceanode.h"

#include "formsScroller.h"
#include "formsScrollerFolder.h"
#include "probabilityDropdownList.h"
#include "texUniForms.h"
#include "formsBox/formsGLBox.h"
#include "formsV2/formsGL2Box.h"
#if defined(FORMS_HAS_SC) && __has_include("ofxOceanodeSuperColliderController.h")
#include "ofxOceanodeSuperColliderController.h"
#define FORMS_SC_CONTROLLER 1
#endif
#ifdef OFXOCEANODE_USE_OSC
#include "multiOscSender.h"
#endif

namespace ofxOceanodeForms{

void registerModels(ofxOceanode &o){
    // Layer Scroller, Forms Transport and Forms Header were removed on 2026-11-07 (Forms Box has
    // their work built in); ESPECTRES patches 3-- to 5-- no longer load those nodes.
    o.registerModel<formsScroller>("Forms");
    o.registerModel<formsScrollerFolder>("Forms");
    o.registerModel<probabilityDropdownList>("Forms");
    o.registerModel<texUniForms>("Forms");
#ifdef OFXOCEANODE_USE_OSC
    o.registerModel<multiOscSender>("Forms");
#endif
    // ESPECTRES 2027 (Forms Box, the node with the Processing renderer, was removed on 2026-11-07;
    // patches 6-- and 7-- are archived)
#ifdef FORMS_SC_CONTROLLER
    // the sonification needs the SuperCollider servers: register this collection after
    // ofxOceanodeSuperCollider's (without them it has no sound)
    if(auto sc = o.getController<ofxOceanodeSuperColliderController>()) o.registerModel<formsGLBox>("Forms", sc->getServers());
    else o.registerModel<formsGLBox>("Forms");
    if(auto sc = o.getController<ofxOceanodeSuperColliderController>()) o.registerModel<formsGL2Box>("Forms", sc->getServers());
    else o.registerModel<formsGL2Box>("Forms");
#else
    o.registerModel<formsGLBox>("Forms");
    o.registerModel<formsGL2Box>("Forms");
#endif
}

void registerType(ofxOceanode &o){
    // (the Schedule port type went with the external player, 2026-11-15: Forms GL Box plays its own images)
}

void registerCollection(ofxOceanode &o){
    registerType(o);
    registerModels(o);
}

}
