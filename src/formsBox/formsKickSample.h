#pragma once
// Portable scene-owned audio. The JSON contains the bytes; cached files are disposable.
#include <nlohmann/json.hpp>
#include <filesystem>
#include <fstream>
#include <vector>
#include <memory>
#include <sstream>
#include <iomanip>
#include <stdexcept>
#include <algorithm>
#include <cstdint>
#include <iterator>

namespace formskick {
using json=nlohmann::json;
struct Asset { std::string name,id,data; };
inline std::string fingerprint(const std::vector<uint8_t>& bytes){
    uint64_t h=14695981039346656037ull;
    for(auto b:bytes){h^=b;h*=1099511628211ull;}
    std::ostringstream s;s<<std::hex<<std::setw(16)<<std::setfill('0')<<h;return s.str();
}
inline std::string encode(const std::vector<uint8_t>& bytes){
    static const char* table="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;out.reserve((bytes.size()+2)/3*4);
    for(size_t i=0;i<bytes.size();i+=3){
        uint32_t v=(uint32_t)bytes[i]<<16;
        if(i+1<bytes.size())v|=(uint32_t)bytes[i+1]<<8;
        if(i+2<bytes.size())v|=bytes[i+2];
        out+=table[(v>>18)&63];out+=table[(v>>12)&63];
        out+=i+1<bytes.size()?table[(v>>6)&63]:'=';out+=i+2<bytes.size()?table[v&63]:'=';
    }
    return out;
}
inline std::vector<uint8_t> decode(const std::string& text){
    if(text.empty() || text.size()%4)throw std::runtime_error("Invalid embedded kick data");
    auto value=[](char c)->int{
        if(c>='A'&&c<='Z')return c-'A';if(c>='a'&&c<='z')return c-'a'+26;
        if(c>='0'&&c<='9')return c-'0'+52;if(c=='+')return 62;if(c=='/')return 63;return -1;
    };
    std::vector<uint8_t> out;out.reserve(text.size()/4*3);
    for(size_t i=0;i<text.size();i+=4){
        int a=value(text[i]),b=value(text[i+1]);
        bool pad2=text[i+2]=='=',pad3=text[i+3]=='=';
        int c=pad2?0:value(text[i+2]),d=pad3?0:value(text[i+3]);
        if(a<0||b<0||c<0||d<0||(pad2&&!pad3)||((pad2||pad3)&&i+4!=text.size()))throw std::runtime_error("Invalid embedded kick data");
        uint32_t v=(a<<18)|(b<<12)|(c<<6)|d;
        out.push_back(v>>16);if(!pad2)out.push_back((v>>8)&255);if(!pad3)out.push_back(v&255);
    }
    return out;
}
inline void validateAudio(const std::vector<uint8_t>& bytes){
    auto tag=[&](size_t at,const char* s){return bytes.size()>=at+4 && std::equal(bytes.begin()+at,bytes.begin()+at+4,s);};
    bool wav=(tag(0,"RIFF")||tag(0,"RIFX")||tag(0,"RF64"))&&tag(8,"WAVE");
    bool aiff=tag(0,"FORM")&&(tag(8,"AIFF")||tag(8,"AIFC"));
    if(!wav&&!aiff)throw std::runtime_error("Choose a WAV or AIFF audio sample");
}
inline std::shared_ptr<const Asset> importFile(const std::filesystem::path& path){
    std::ifstream in(path,std::ios::binary);
    if(!in)throw std::runtime_error("Cannot read the selected sample");
    std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});
    validateAudio(bytes);
    auto asset=std::make_shared<Asset>();asset->name=path.filename().string();
    asset->id=fingerprint(bytes);asset->data=encode(bytes);return asset;
}
inline json serialize(const Asset& asset){return {{"name",asset.name},{"id",asset.id},{"encoding","base64"},{"data",asset.data}};}
inline std::shared_ptr<const Asset> read(const json& j){
    if(!j.is_object() || !j.contains("data"))return {};
    if(j.value("encoding",std::string())!="base64")throw std::runtime_error("Unknown embedded kick encoding");
    auto asset=std::make_shared<Asset>();asset->name=j.at("name").get<std::string>();
    asset->id=j.at("id").get<std::string>();asset->data=j.at("data").get<std::string>();return asset;
}
inline std::filesystem::path materialize(const Asset& asset,const std::filesystem::path& cache){
    auto bytes=decode(asset.data);validateAudio(bytes);
    if(fingerprint(bytes)!=asset.id)throw std::runtime_error("Embedded kick checksum mismatch");
    auto name=std::filesystem::path(asset.name).filename();
    if(name.empty()||name=="."||name=="..")throw std::runtime_error("Invalid embedded sample name");
    auto path=cache/asset.id/name;std::filesystem::create_directories(path.parent_path());
    auto tmp=path;tmp+=".tmp";
    {std::ofstream file(tmp,std::ios::binary);if(!file.write((const char*)bytes.data(),bytes.size()))throw std::runtime_error("Cannot restore embedded kick sample");}
    std::filesystem::rename(tmp,path);return path;
}
} // namespace formskick
