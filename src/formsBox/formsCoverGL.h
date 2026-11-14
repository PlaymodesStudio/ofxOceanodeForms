#pragma once
#include "ofMain.h"
#include "formsCover.h"
#include <functional>
#include <array>
#include <cmath>

namespace formscover {
// Builds every source mask before changing any destination, so covers cannot feed back
// into one another. Shared by playback and high-resolution/video exports.
class Compositor {
public:
    struct Source {
        Settings settings;
        int layer=0;
        float pixelsPerBeat=1;
        bool alphaChannel=false;
        std::function<void(int,int)> draw;
    };
    static ofTexture* texture(Mask& m){
        if(m.texture && m.texture->isAllocated()) return m.texture.get();
        if(!m.alpha && !m.png.empty()){
            ofPixels p;
            if(ofLoadImage(p,m.png)){
                auto a=std::make_shared<std::vector<uint8_t>>((size_t)p.getWidth()*p.getHeight());
                for(size_t i=0;i<a->size();++i) (*a)[i]=p[i*p.getNumChannels()];
                m.w=p.getWidth(); m.h=p.getHeight(); m.alpha=a;
            }
        }
        if(!m.alpha || m.w<1 || m.h<1) return nullptr;
        m.texture=std::make_shared<ofTexture>();
        m.texture->allocate(m.w,m.h,GL_R8,false);
        m.texture->loadData(m.alpha->data(),m.w,m.h,GL_RED);
        m.texture->setTextureMinMagFilter(GL_LINEAR,GL_LINEAR);
        m.texture->setTextureWrap(GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);
        return m.texture.get();
    }
    // u0 / uw: the window of the mask drawn (a phrase's page: its slice of the width)
    static void drawMask(Mask& m, float x, float width, float visible, float height, float opacity=1, float u0=0, float uw=1){
        auto* t=texture(m); if(!t || width<=0 || visible<=0) return;
        ofSetColor(255*opacity,255*opacity,255*opacity,255);
        t->drawSubsection(x,0,visible,height,t->getWidth()*u0,0,t->getWidth()*uw*visible/width,t->getHeight());
    }
    void apply(const std::vector<ofFbo*>& layers, const std::vector<int>& order,
               const std::vector<Source>& sources, int w, int h){
        if(sources.empty() || w<1 || h<1) return;
        if(!setup()) return;
        int mw=std::max(1,std::min(w,960)), mh=std::max(1,(int)std::lround(h*(double)mw/w));
        allocate(a,mw,mh); allocate(b,mw,mh);
        struct Group { Source source; std::vector<const Source*> members; };
        std::vector<Group> groups;
        for(auto& src:sources){
            if(!src.settings.enabled || src.settings.amount<=0) continue;
            // Union repeated pages / items that share one cover, before growing or feathering.
            auto same=[&](const Group& g){ return g.source.layer==src.layer && g.source.alphaChannel==src.alphaChannel &&
                g.source.pixelsPerBeat==src.pixelsPerBeat && g.source.settings==src.settings; };
            auto it=std::find_if(groups.begin(),groups.end(),same);
            if(it==groups.end()){ groups.push_back({src,{&src}}); } else it->members.push_back(&src);
        }
        if(masks.size()>groups.size()) masks.resize(groups.size());
        while(masks.size()<groups.size()) masks.emplace_back(std::make_unique<ofFbo>());
        ofPushStyle();
        for(size_t i=0;i<groups.size();++i){
            auto& group=groups[i]; auto& s=group.source;
            a.begin(); ofClear(0,0,0,0); ofEnableBlendMode(OF_BLENDMODE_DISABLED);
            glEnable(GL_BLEND); glBlendEquation(GL_MAX); glBlendFunc(GL_ONE,GL_ONE);
            for(auto* member:group.members) member->draw(mw,mh);
            glBlendEquation(GL_FUNC_ADD); glDisable(GL_BLEND); a.end();
            pass(a,b,-1,0,0,0,s.alphaChannel);
            int gx=std::lround(s.settings.growRight*mw/1920.f), gy=std::lround(s.settings.growHeight*mh/1080.f);
            pass(b,a,0,gx); pass(a,b,1,gy);
            // Exact linear release: logarithmic max-propagation instead of a long tap loop.
            float release=s.settings.release*s.pixelsPerBeat*mw/w;
            for(int offset=1; offset<mw && offset<=std::ceil(release); offset*=2){
                pass(b,a,2,0,release,offset); std::swap(a,b);
            }
            int fx=std::lround(s.settings.feather*.5f*mw/1920.f), fy=std::lround(s.settings.feather*.5f*mh/1080.f);
            for(int k=0;k<2;++k){ pass(b,a,3,fx); pass(a,b,4,fy); }
            allocate(*masks[i],mw,mh);
            masks[i]->begin(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); ofSetColor(255); b.draw(0,0); masks[i]->end();
        }
        for(size_t i=0;i<groups.size();++i){
            auto& src=groups[i].source;
            auto source=std::find(order.begin(),order.end(),src.layer);
            if(source==order.end()) continue;
            for(size_t rank=0;rank<order.size();++rank){
                int target=order[rank];
                if(!affects(src.settings,(int)(source-order.begin()),(int)rank) || target<0 || target>=(int)layers.size() || !layers[target] || !layers[target]->isAllocated()) continue;
                layers[target]->begin(); ofEnableBlendMode(OF_BLENDMODE_DISABLED); glEnable(GL_BLEND); glBlendEquation(GL_FUNC_ADD);
                glBlendFunc(src.settings.mode=="black" ? GL_ONE : GL_ZERO,GL_ONE_MINUS_SRC_ALPHA);
                cover.begin(); cover.setUniformTexture("uMask",masks[i]->getTexture(),0);
                cover.setUniform2f("uSize",w,h); cover.setUniform1f("uAmount",src.settings.amount);
                ofSetColor(255); ofDrawRectangle(0,0,w,h); cover.end(); glDisable(GL_BLEND); layers[target]->end();
            }
        }
        glBlendEquation(GL_FUNC_ADD); ofPopStyle();
    }
private:
    ofFbo a,b;
    std::vector<std::unique_ptr<ofFbo>> masks;
    ofShader filter,cover;
    bool tried=false,ok=false;
    static void allocate(ofFbo& f,int w,int h){
        if(f.isAllocated() && f.getWidth()==w && f.getHeight()==h) return;
        ofFboSettings st; st.width=w; st.height=h; st.internalformat=GL_RGBA16F; st.textureTarget=GL_TEXTURE_2D;
        f.allocate(st); f.getTexture().setTextureMinMagFilter(GL_LINEAR,GL_LINEAR);
        f.getTexture().setTextureWrap(GL_CLAMP_TO_EDGE,GL_CLAMP_TO_EDGE);
    }
    void pass(ofFbo& src,ofFbo& dst,int kind,int radius,float release=0,int offset=0,bool alpha=false){
        dst.begin(); ofEnableBlendMode(OF_BLENDMODE_DISABLED);
        filter.begin(); filter.setUniformTexture("uSrc",src.getTexture(),0);
        filter.setUniform2i("uSize",src.getWidth(),src.getHeight()); filter.setUniform1i("uPass",kind);
        filter.setUniform1i("uRadius",radius); filter.setUniform1i("uOffset",offset);
        filter.setUniform1i("uAlpha",alpha); filter.setUniform1f("uRelease",release);
        ofSetColor(255); ofDrawRectangle(0,0,dst.getWidth(),dst.getHeight()); filter.end(); dst.end();
    }
    bool setup(){
        if(tried) return ok;
        tried=true;
        std::string vert="#version 410\nuniform mat4 modelViewProjectionMatrix; in vec4 position; void main(){gl_Position=modelViewProjectionMatrix*position;}";
        std::string frag=R"(#version 410
uniform sampler2D uSrc;
uniform ivec2 uSize;
uniform int uPass,uRadius,uOffset,uAlpha;
uniform float uRelease;
out vec4 outColor;
float m(ivec2 p){if(any(lessThan(p,ivec2(0)))||any(greaterThanEqual(p,uSize)))return 0.;return texelFetch(uSrc,p,0).r;}
void main(){
    ivec2 p=ivec2(gl_FragCoord.xy);float v=0.;
    if(uPass<0){vec4 c=texelFetch(uSrc,p,0);v=clamp((uAlpha!=0?c.a:c.r)/.02,0.,1.);}
    else if(uPass==2){v=max(m(p),m(p-ivec2(uOffset,0))-float(uOffset)/max(.0001,uRelease));}
    else {ivec2 d=(uPass==0||uPass==3)?ivec2(1,0):ivec2(0,1);float n=0.;
        // Horizontal growth and feather sample only earlier columns: their output
        // extends to the right, never before the source's onset. Height stays symmetric.
        int first=uPass==3?-2*uRadius:-uRadius;
        int last=(uPass==0||uPass==3)?0:uRadius;
        for(int i=first;i<=last;++i){ivec2 q=p+d*i;
            if(uPass<2)v=max(v,m(q));
            else if(all(greaterThanEqual(q,ivec2(0)))&&all(lessThan(q,uSize))){v+=m(q);n+=1.;}}
        if(uPass>=3)v/=max(1.,n);
    }outColor=vec4(v);
})";
        std::string apply=R"(#version 410
uniform sampler2D uMask;uniform vec2 uSize;uniform float uAmount;out vec4 outColor;
void main(){float k=clamp(texture(uMask,gl_FragCoord.xy/uSize).r*uAmount,0.,1.);outColor=vec4(0,0,0,k);}
)";
        auto build=[&](ofShader& sh,const std::string& fs){
            if(!sh.setupShaderFromSource(GL_VERTEX_SHADER,vert)||!sh.setupShaderFromSource(GL_FRAGMENT_SHADER,fs))return false;
            sh.bindDefaults();return sh.linkProgram();
        };
        ok=build(filter,frag)&&build(cover,apply);
        if(!ok) ofLogError("Forms Cover")<<"Could not compile the cover compositor";
        return ok;
    }
};
} // namespace formscover
