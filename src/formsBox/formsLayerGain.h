#pragma once
#include "ofMain.h"
namespace forms {
inline const char* layerGainVert(){return R"(
#version 410
uniform mat4 modelViewProjectionMatrix;
layout(location=0)in vec4 position;
layout(location=3)in vec2 texcoord;
out vec2 uv;
void main(){uv=texcoord;gl_Position=modelViewProjectionMatrix*position;}
)";}
inline const char* layerGainBody(){return R"(
in vec2 uv;
uniform float gainAmount;
out vec4 out_color;
void main(){
    vec4 s=texture(tex,uv);
    float a=clamp(s.a*gainAmount,0.0,1.0);
    vec3 premul=clamp(s.rgb*s.a*gainAmount,vec3(0),vec3(a));
    out_color=vec4(a>0.0?premul/a:vec3(0),a);
}
)";}
// Input textures are straight alpha; the scrolling targets blend SRC_ALPHA.
class LayerGain {
    ofShader normal,rectangle;bool tried=false,ok=false;
public:
    void draw(const ofTexture& texture,float x,float w,float h,float gain,float fraction=1,float u0=0,float uw=1){
        gain=ofClamp(gain,0,2);
        bool boost=gain>1;
        ofShader* shader=nullptr;
        if(boost){
            if(!tried){
                tried=true;
                auto make=[](ofShader& sh,const char* sampler){
                    std::string frag=std::string("#version 410\nuniform ")+sampler+" tex;\n"+layerGainBody();
                    bool result=sh.setupShaderFromSource(GL_VERTEX_SHADER,layerGainVert()) && sh.setupShaderFromSource(GL_FRAGMENT_SHADER,frag);
                    if(result){sh.bindDefaults();result=sh.linkProgram();}return result;
                };
                ok=make(normal,"sampler2D")&&make(rectangle,"sampler2DRect");
                if(!ok)ofLogError("Forms Box")<<"layer gain shader failed";
            }
            if(ok){shader=texture.getTextureData().textureTarget==GL_TEXTURE_RECTANGLE?&rectangle:&normal;
                shader->begin();shader->setUniformTexture("tex",texture,0);shader->setUniform1f("gainAmount",gain);}
        }
        ofSetColor(255,255,255,boost?255:255*gain);
        if(fraction>=.99999f&&u0<=0&&uw>=.99999f)texture.draw(x,0,w,h);
        else texture.drawSubsection(x,0,w*fraction,h,texture.getWidth()*u0,0,texture.getWidth()*uw*fraction,texture.getHeight());
        if(shader)shader->end();ofSetColor(255);
    }
};
}
