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
#include "formsBox/formsBox.h"
#include "formsBox/formsGLBox.h"
#if defined(FORMS_HAS_SC) && __has_include("ofxOceanodeSuperColliderController.h")
#include "ofxOceanodeSuperColliderController.h"
#define FORMS_SC_CONTROLLER 1
#endif
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
#ifdef FORMS_SC_CONTROLLER
    // the sonification needs the SuperCollider servers: register this collection after
    // ofxOceanodeSuperCollider's (without them Forms Box has no sound)
    if(auto sc = o.getController<ofxOceanodeSuperColliderController>()){
        o.registerModel<formsBox>("Forms", sc->getServers());
        o.registerModel<formsGLBox>("Forms", sc->getServers());   // Phase 7: the renderer in the app
    }else{
        o.registerModel<formsBox>("Forms");
        o.registerModel<formsGLBox>("Forms");
    }
#else
    o.registerModel<formsBox>("Forms");
    o.registerModel<formsGLBox>("Forms");
#endif
#endif
}

void registerType(ofxOceanode &o){
    // ESPECTRES 2027: Forms Box -> Layer Scroller
    o.registerType<forms::Schedule*>("FormsSchedule", nullptr);
}

void registerCollection(ofxOceanode &o){
    registerType(o);
    registerModels(o);
}

}
