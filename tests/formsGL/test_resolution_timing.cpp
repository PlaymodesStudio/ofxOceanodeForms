// Run on a Forms data copy. Compare rhythmic preset envelopes at 1080p and 4K.
#include "formsEngine.h"
#include "formsGLSketch.h"
#include <cassert>
#include <cstdio>
using namespace forms;
static std::vector<double> envelope(const formsgl::RenderResult& r){
    std::vector<double> v(1920,0);
    for(int x=0;x<r.w;++x)for(int y=0;y<r.h;++y){size_t i=((size_t)y*r.w+x)*4;v[x*1920/r.w]+=(double)(r.rgba[i]+r.rgba[i+1]+r.rgba[i+2])*r.rgba[i+3]/65535./257.;}
    double mx=*std::max_element(v.begin(),v.end());for(auto& x:v)x/=std::max(1.,mx);return v;
}
static void checkTiming(const std::string& label,const std::vector<double>& a,const std::vector<double>& b){
    double energy=0;for(double v:a)energy+=v*v;
    if(energy<1e-12){
        for(double v:b)assert(v==0);
        std::printf("%s: both renders silent; no onset to compare\n",label.c_str());return;
    }
    auto error=[&](int shift){
        double sum=0;for(int x=30;x<1890;++x)sum+=std::pow(a[x]-b[x+shift],2);return sum;
    };
    double zero=error(0),best=zero;int shift=0;
    for(int d=-30;d<=30;++d){double err=error(d);if(err<best){best=err;shift=d;}}
    std::printf("%s: best 4K offset %d reference pixels; error %.4f / aligned %.4f\n",label.c_str(),shift,best,zero);
    std::fflush(stdout);assert(std::abs(shift)<=1);
}
int main(int argc,char** argv){
    assert(argc==2 || argc==3);Engine e;e.rendererFolder=argv[1];e.loadAll();formsgl::Schemas schemas;assert(schemas.loadEmbedded());
    std::vector<std::pair<std::string,json>> captured;
    if(argc==3){
        std::map<int,fs::path> latest;
        for(auto& f:fs::directory_iterator(argv[2]))if(f.path().extension()==".json"){
            auto j=json::parse(std::ifstream(f.path()));if(j.value("scene",std::string())!="TOMITA_MULTILAYER" || j.value("size",std::vector<int>{})!=std::vector<int>{3840,2160})continue;
            int layer=j.value("layer",0);if(!latest.count(layer) || f.path().filename()>latest[layer].filename())latest[layer]=f.path();
        }
        for(auto& kv:latest)captured.push_back({kv.second.filename().string(),json::parse(std::ifstream(kv.second))});
    }
    for(auto entry:std::vector<std::pair<std::string,int>>{{"pulse/trill loop",4},{"pulse/counter eight",4},{"hits/open hats",3},{"hits/complex hats",3},{"hits/kick quarters",2},{"pulse/arp loop block",2},{"hits/diffuse snare",1},{"pulse/quarter arp loop",5}}){
        e.itemBars=entry.second;
        json req={{"size",{1920,1080}},{"bars",entry.second},{"seed",123},{"items",e.presetItemsSeeded(e.presets.at(entry.first),123)}};
        auto a=formsgl::Sketch(schemas).render(req);assert(a.error.empty());auto va=envelope(a);a.rgba.clear();
        req["size"]={3840,2160};auto b=formsgl::Sketch(schemas).render(req);assert(b.error.empty());auto vb=envelope(b);
        checkTiming(entry.first,va,vb);
    }
    for(auto& entry:captured){
        auto req=entry.second;req["size"]={1920,1080};auto a=formsgl::Sketch(schemas).render(req);assert(a.error.empty());auto va=envelope(a);a.rgba.clear();
        req["size"]={3840,2160};auto b=formsgl::Sketch(schemas).render(req);assert(b.error.empty());auto vb=envelope(b);
        checkTiming(entry.first,va,vb);
    }
}
