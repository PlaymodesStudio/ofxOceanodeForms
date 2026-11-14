// Offline sonification uses the same Forms SynthDefs and column/pitch model as playback.
#pragma once
#include "formsSoundModel.h"
#include "formsCover.h"
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <stdexcept>
#include <future>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <numeric>
#include <signal.h>
#include <pthread.h>

namespace forms {
namespace exportmedia {
using json = nlohmann::json;
inline std::string quote(const std::string& s){
    std::string q = "'"; for(char c : s) q += c == '\'' ? "'\\''" : std::string(1, c); return q + "'";
}
inline std::string executable(const std::vector<std::string>& paths){
    for(auto& p : paths) if(std::filesystem::exists(p)) return p;
    return {};
}
inline std::pair<int,int> videoSize(int w, int h){
    double k = std::min({1.0, 1920.0 / w, 1080.0 / h});
    return {std::max(2, (int)std::lround(w * k / 2) * 2), std::max(2, (int)std::lround(h * k / 2) * 2)};
}
// The common loop of the layers, in bars: the least common multiple of their lengths, counted in
// 1/16 bars (layer lengths may be fractions of a bar: 0.25, 1.5 ...)
inline double loopBars(const std::vector<double>& bars){
    int64_t loop = 1;
    for(double b : bars){
        int64_t t = (int64_t)std::llround(b * 16);
        if(t < 1) throw std::runtime_error("Layer bars must be positive");
        int64_t next = loop / std::gcd(loop, t) * t;
        if(next > 16000000) throw std::runtime_error("Combined layer loop exceeds one million bars");
        loop = next;
    } return loop / 16.0;
}
// Bounded encoder queue: rendering uses frame/sample time, never the application's wall clock.
// A slow encoder pauses export production without dropping or duplicating frames.
class VideoWriter {
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::vector<uint8_t>> queue;
    bool closing = false;
    std::string error;
    std::future<std::string> worker;
public:
    VideoWriter(const std::string& base, int w, int h, double duration){
        auto ffmpeg = executable({"/opt/homebrew/bin/ffmpeg", "/usr/local/bin/ffmpeg", "/usr/bin/ffmpeg"});
        if(ffmpeg.empty()) throw std::runtime_error("PNG and WAV saved; install FFmpeg to export MP4.");
        std::ostringstream dur; dur << std::setprecision(12) << duration;
        std::string cmd = quote(ffmpeg) + " -nostdin -y -f rawvideo -pixel_format rgba -video_size " + std::to_string(w) + "x" + std::to_string(h)
            + " -framerate 60 -i pipe:0 -map 0:v:0 -an -t " + dur.str()
            + " -c:v libx264 -preset slow -crf 14 -profile:v high -pix_fmt yuv420p -r 60 -fps_mode cfr -movflags +faststart " + quote(base + ".video.tmp.mp4") + " >> " + quote(base + ".media.log") + " 2>&1";
        worker = std::async(std::launch::async, [this, cmd]{
            sigset_t blocked; sigemptyset(&blocked); sigaddset(&blocked, SIGPIPE); pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
            FILE* pipe = popen(cmd.c_str(), "w");
            if(!pipe){ std::lock_guard<std::mutex> lock(mutex); error = "Cannot start FFmpeg"; return error; }
            bool failed = false;
            for(;;){
                std::vector<uint8_t> frame;
                { std::unique_lock<std::mutex> lock(mutex); cv.wait(lock, [this]{ return closing || !queue.empty(); });
                  if(queue.empty()) break;
                  frame = std::move(queue.front()); queue.pop_front(); }
                if(fwrite(frame.data(), 1, frame.size(), pipe) != frame.size()){ failed = true; break; }
            }
            int status = pclose(pipe);
            std::lock_guard<std::mutex> lock(mutex);
            if(failed || status != 0) error = "PNG and WAV saved; video encoding failed. See the .media.log file.";
            return error;
        });
    }
    ~VideoWriter(){ finish(); if(worker.valid()) worker.wait(); }
    bool available(){ std::lock_guard<std::mutex> lock(mutex); if(!error.empty()) throw std::runtime_error(error); return queue.size() < 3; }
    void push(std::vector<uint8_t> frame){ { std::lock_guard<std::mutex> lock(mutex); queue.push_back(std::move(frame)); } cv.notify_one(); }
    void finish(){ { std::lock_guard<std::mutex> lock(mutex); closing = true; } cv.notify_all(); }
    bool done(){ return worker.wait_for(std::chrono::seconds(0)) == std::future_status::ready; }
    std::string result(){ return worker.get(); }
};
inline void u32(std::vector<uint8_t>& b, uint32_t v){ for(int s = 24; s >= 0; s -= 8) b.push_back((v >> s) & 255); }
inline void str(std::vector<uint8_t>& b, const std::string& s){ b.insert(b.end(), s.begin(), s.end()); b.push_back(0); while(b.size() % 4) b.push_back(0); }
// SuperCollider non-real-time score: length-prefixed OSC bundles; time tags are seconds from zero.
struct Score {
    std::ofstream file;
    explicit Score(const std::string& path) : file(path, std::ios::binary) {}
    void add(double time, const json& args){
        std::vector<uint8_t> m, b;
        str(m, args.at(0).get<std::string>());
        std::string tags = ",";
        for(size_t i = 1; i < args.size(); ++i) tags += args[i].is_string() ? 's' : args[i].is_number_integer() ? 'i' : 'f';
        str(m, tags);
        for(size_t i = 1; i < args.size(); ++i){
            auto& a = args[i];
            if(a.is_string()) str(m, a.get<std::string>());
            else if(a.is_number_integer()) u32(m, a.get<int32_t>());
            else { float f = a.get<float>(); uint32_t v; std::memcpy(&v, &f, 4); u32(m, v); }
        }
        str(b, "#bundle"); u32(b, (uint32_t)time); u32(b, (uint32_t)((time - std::floor(time)) * 4294967296.0));
        u32(b, (uint32_t)m.size()); b.insert(b.end(), m.begin(), m.end());
        std::vector<uint8_t> len; u32(len, (uint32_t)b.size());
        file.write((char*)len.data(), len.size()); file.write((char*)b.data(), b.size());
    }
    void array(double t, const char* command, int id, const json& key, const std::vector<float>& v){
        json a = json::array({command, id, key, (int)v.size()});
        for(float f : v) a.push_back(f); add(t, a);
    }
};
struct Layer { std::vector<formscover::Mask> covers; json recipe; std::vector<uint8_t> rgba; int w = 0, h = 0, index = 0, root = 24; float gain = 1; std::vector<float> scanAmps, kickLevels; };
inline int sampleChannels(const std::string& path){
    int n=wavChannels(path); if(n) return n;
    std::ifstream in(path,std::ios::binary); char header[12]; in.read(header,12);
    if(!in || std::memcmp(header,"FORM",4)) return 0;
    while(in){
        unsigned char h[8]; in.read((char*)h,8); if(!in) break;
        uint32_t length=((uint32_t)h[4]<<24)|((uint32_t)h[5]<<16)|((uint32_t)h[6]<<8)|h[7];
        if(!std::memcmp(h,"COMM",4)){ unsigned char ch[2]; in.read((char*)ch,2); return in ? (ch[0]<<8)|ch[1] : 0; }
        in.seekg((std::streamoff)(length+(length&1)),std::ios::cur);
    } return 0;
}
// Trim the synth's fixed lookahead and write an exact sample-count float WAV.
inline void trimFloatWav(const std::string& source,const std::string& target,uint32_t skip,uint32_t frames){
    std::ifstream in(source,std::ios::binary); char header[12]; in.read(header,12);
    if(!in || std::memcmp(header,"RIFF",4) || std::memcmp(header+8,"WAVE",4)) throw std::runtime_error("Invalid NRT WAV");
    auto read32=[&](){ unsigned char b[4]; in.read((char*)b,4); return (uint32_t)b[0]|((uint32_t)b[1]<<8)|((uint32_t)b[2]<<16)|((uint32_t)b[3]<<24); };
    uint64_t dataOffset=0; uint32_t dataBytes=0;
    while(in){ char id[4]; in.read(id,4); if(!in) break; auto length=read32(); auto offset=in.tellg();
        if(!std::memcmp(id,"data",4)){ dataOffset=(uint64_t)offset; dataBytes=length; break; }
        in.seekg(offset+(std::streamoff)(length+(length&1)));
    }
    uint64_t bytes=(uint64_t)frames*8;
    if(!dataOffset || ((uint64_t)skip+frames)*8>dataBytes || bytes>0xffffffffULL-48) throw std::runtime_error("NRT WAV is incomplete or exceeds the WAV size limit");
    std::ofstream out(target,std::ios::binary);
    auto w16=[&](uint16_t v){ out.put(v&255); out.put((v>>8)&255); };
    auto w32=[&](uint32_t v){ for(int i=0;i<4;++i) out.put((v>>(i*8))&255); };
    out.write("RIFF",4); w32((uint32_t)bytes+48); out.write("WAVEfmt ",8); w32(16); w16(3); w16(2); w32(48000); w32(48000*8); w16(8); w16(32);
    out.write("fact",4); w32(4); w32(frames); out.write("data",4); w32((uint32_t)bytes);
    in.seekg(dataOffset+(uint64_t)skip*8); char buffer[65536];
    while(bytes){ auto n=(std::streamsize)std::min<uint64_t>(bytes,sizeof(buffer)); in.read(buffer,n); out.write(buffer,n); bytes-=n; }
    if(!in || !out) throw std::runtime_error("Cannot finalize WAV");
}
inline std::string render(const json& manifest, const std::vector<Layer>& layers, const SoundSettings& snd,
                          const std::string& base, const std::string& defs, const std::string& kickFile,
                          bool audio, bool video){
    const double duration = manifest.at("duration").get<double>();
    size_t layerCount=snd.layers.size();
    for(auto& l:layers) layerCount=std::max(layerCount,(size_t)l.index+1);
    const int duckBus=(int)layerCount*SOUND_PARTIALS;
    const auto perf = manifest.value("performance", json::object());
    auto f = [&](const char* key, float fallback){ return perf.value(key, fallback); };
    const double latency = snd.limit > 0 ? .02 : 0.0; // FormsMaster: Limiter duration .01, two buffers of lookahead.
    const std::string rawWav=base+".nrt.wav";
    const std::string wav = base + ".wav", osc = base + ".osc", log = base + ".media.log";
    std::string synth = executable({manifest.value("scsynth",std::string()),"/Applications/SuperCollider/SuperCollider.app/Contents/MacOS/scsynth", "/Applications/SuperCollider.app/Contents/MacOS/scsynth", "/Applications/SuperCollider.app/Contents/Resources/scsynth", "/Applications/SuperCollider/SuperCollider.app/Contents/Resources/scsynth", "/opt/homebrew/bin/scsynth", "/usr/local/bin/scsynth"});
    if(synth.empty() || !std::filesystem::exists(std::filesystem::path(defs) / "FormsBank512.scsyndef"))
        throw std::runtime_error("PNG saved; audio needs SuperCollider and the compiled Forms SynthDefs.");
    {
        Score score(osc);
        score.add(0, json::array({"/d_loadDir", defs}));
        score.add(0, json::array({"/c_set", duckBus, 0.0}));
        bool synthetic=f("kickSynth",0)>.5f;
        bool kick = snd.kickOn;
        if(kick){
            if(!kickFile.empty()){
                score.add(0, json::array({"/b_allocReadChannel", 0, kickFile, 0, 0, 0}));
                score.add(0, json::array({"/b_allocReadChannel", 1, kickFile, 0, 0, sampleChannels(kickFile) > 1 ? 1 : 0}));
            }else{
                score.add(0,json::array({"/b_alloc",0,1,1}));score.add(0,json::array({"/b_alloc",1,1,1}));
            }
            score.add(0, json::array({"/s_new", "FormsKick", 900, 1, 0, "out", 104, "bufl", 0, "bufr", 1, "duckbus", duckBus,
                "duckdecay", snd.duckBeats * f("duckScale",1.f) * 60.0 / manifest.at("bpm").get<double>(), "duckcurve", snd.duckCurve, "amp", (synthetic||!kickFile.empty())?f("kickVol",.16f):0.f,
                "source",synthetic?1:0,"synthstart",f("kickPitchStart",160),"synthend",f("kickPitchEnd",48),
                "synthdecay",f("kickDecay",.45f),"synthclick",f("kickClick",.3f),"synthnoise",f("kickNoise",0),"synthdrive",f("kickDrive",.1f),
                "gate",manifest.value("kickGate",1.f)}));
        }
        for(auto& l : layers){
            int id = 1000 + l.index;
            score.add(0, json::array({"/s_new", "FormsBank512", id, 1, 0, "ampbus", l.index * 512, "out", 100, "wetout", 106,
                "duckbus", duckBus, "gain", snd.bankGain, "level", snd.layers[l.index].on ? snd.layers[l.index].level : 0.0f,
                "lag", snd.lag, "detune", snd.detune, "bend", -snd.detune * .5f,
                "send", std::clamp(f("reverbAmt", .1119f) * manifest.value("reverbMult", 1.0f) * snd.layers[l.index].reverb, 0.f, 1.f)}));
        }
        json fx = json::array({"/s_new", "FormsFX", 950, 1, 0, "in", 100, "wetin", 106, "out", 102, "comp", f("Compressor", 0), "gain", f("SpectrumGain", 2)});
        auto fxSettings=manifest.value("fx",json::object());
        for(auto& kv : fxSettings.items()){ fx.push_back(kv.key()); fx.push_back(kv.value()); } score.add(0, fx);
        json master = json::array({"/s_new", "FormsMaster", 960, 1, 0, "in", 102, "kickin", 104, "out", 0, "volume", snd.volume, "limit", snd.limit > 0 ? 1 : 0, "kickgain", snd.kickGain,
            "duckbus",duckBus,"duckdepth",snd.duckFullBand?std::clamp(snd.duckDepth*f("sideChain",1),0.f,1.f):0.f});
        auto masterSettings=manifest.value("master",json::object());
        for(auto& kv : masterSettings.items()){ master.push_back(kv.key()); master.push_back(kv.value()); } score.add(0, master);
        std::vector<int> lastBars(layers.size(), -1);
        std::vector<PartialLayout> layouts;
        for(auto& l : layers) layouts.push_back(partialLayout(l.h, l.recipe.value("notegrid", 100), l.recipe.at("size").at(1).get<int>()));
        bool wasKick = false; double lastKick = -1e9; LRBalance lr;
        const bool captured = !layers.empty() && !layers.front().scanAmps.empty();
        const double rate = captured ? 60.0 : 120.0;
        const int ticks = (int)std::ceil(duration * rate);
        if(captured) for(auto& l:layers) if(l.scanAmps.size()!=(size_t)ticks*SOUND_PARTIALS || l.kickLevels.size()!=(size_t)ticks)
            throw std::runtime_error("Incomplete synchronized audio capture");
        for(int tick = 0; tick < ticks; ++tick){
            double t = tick / rate;
            for(size_t li = 0; li < layers.size(); ++li){
                auto& l = layers[li]; auto& layout = layouts[li];
                double layerPhase = std::fmod(.25 + t * manifest.at("bpm").get<double>() / (240.0 * l.recipe.value("bars", 8.0)), 1.0);
                int x = std::min(l.w - 1, (int)(layerPhase * l.w));
                std::vector<uint8_t> col(l.h * 4);
                if(!captured) for(int y = 0; y < l.h; ++y) std::memcpy(col.data() + y * 4, &l.rgba[((size_t)y * l.w + x) * 4], 4);
                int bar = std::min(std::max(1, (int)std::ceil(l.recipe.value("bars", 8.0) - 1e-9)) - 1, (int)(layerPhase * l.recipe.value("bars", 8.0)));
                if(lastBars[li] != bar){
                lastBars[li] = bar;
                auto trans = l.recipe.value("trans", std::vector<int>{0});
                int tr = trans.empty() ? 0 : trans[bar % trans.size()];
                auto pitch = partialPitches(l.root, layout, tr, l.recipe.value("scaleNotes", std::vector<float>{}), f("scaleQuant", 0));
                std::vector<float> gl, gr; partialGains(pitch, l.index, snd, gl, gr);
                SoundSettings ds = snd; ds.duckDepth *= f("sideChain", 1);
                score.array(t, "/n_setn", 1000 + l.index, "pitch", pitch);
                score.array(t, "/n_setn", 1000 + l.index, "gl", gl); score.array(t, "/n_setn", 1000 + l.index, "gr", gr);
                score.array(t, "/n_setn", 1000 + l.index, "duckw", imageDuckWeights(pitch, ds));
                }
                auto trans = l.recipe.value("trans", std::vector<int>{0});
                int tr = trans.empty() ? 0 : trans[bar % trans.size()];
                std::vector<float> amps;
                if(captured) amps.assign(l.scanAmps.begin() + (size_t)tick * SOUND_PARTIALS, l.scanAmps.begin() + (size_t)(tick + 1) * SOUND_PARTIALS);
                else columnAmps(col.data(), l.h, manifest.value("imageMul", 1.0f), layout, amps);
                json a = json::array({"/c_setn", l.index * 512, (int)amps.size()}); for(float v : amps) a.push_back(v); score.add(t, a);
                if(kick && l.index == manifest.value("kickLayer", 0)){
                    int strip = std::clamp((int)std::lround(l.h * f("kickStrip", snd.kickStrip)), 1, l.h); double sum = 0;
                    for(int y = l.h - strip; y < l.h; ++y) for(int c = 0; c < 3; ++c) sum += col[y * 4 + c] / (3.0 * 255 * strip);
                    if(captured) sum = l.kickLevels.at(tick);
                    bool on = f("kDetectOn", 1) > .5f && sum > f("kickThreshold", snd.kickThreshold);
                    if(on && !wasKick && t - lastKick >= f("kickMinGap", snd.kickMinGap)){
                        score.add(t, json::array({"/n_set", 900, "rate", std::pow(2.f, (tr + f("kickTune", snd.kickTune)) / 12.f), "t_trig", 1})); lastKick = t;
                    } wasKick = on;
                }
            }
            double headerPhase=std::fmod(.25+t*manifest.at("bpm").get<double>()/(240.0*manifest.value("bars",8.0)),1.0);
            lr.step((float)headerPhase, manifest.value("bars",8.0), manifest.at("bpm").get<float>(), (float)(1.0 / rate), perf, snd.lrBalance);
            score.add(t, json::array({"/n_set", 960, "lgain", lr.l, "rgain", lr.r}));
        }
        score.add(duration+latency, json::array({"/c_set", duckBus, 0.0}));
        // Explicitly free the graph before quitting (the same offline server teardown as a live graph).
        score.add(duration+latency, json::array({"/g_freeAll", 0}));
        score.add(duration+latency, json::array({"/quit"}));
        if(!score.file) throw std::runtime_error("Cannot write offline audio score");
    }
    // plugin folders: the app's Extensions (FormsSparseBank lives there) and the ones next to scsynth;
    // without the first, scsynth's own defaults apply (its plugins and the user's Extensions)
    std::string ugens;
    {
        std::error_code ec;
        const std::string ext = manifest.value("ugens", std::string());
        if(!ext.empty() && std::filesystem::is_directory(ext, ec)){
            ugens = ext;
            for(const char* sub : {"plugins", "../Resources/plugins"}){
                auto p = std::filesystem::path(synth).parent_path() / sub;
                if(std::filesystem::is_directory(p, ec)){ ugens += ":" + p.lexically_normal().string(); break; }
            }
        }
    }
    std::string cmd = quote(synth) + " -N " + quote(osc) + " _ " + quote(rawWav) + " 48000 WAV float -o 2 -i 0 -a 256 -c " + std::to_string(std::max(8192,duckBus+16)) + " -m 65536 -w " + std::to_string(std::max(4096,(int)layerCount+1024)) + " -D 0" + (ugens.empty() ? std::string() : " -U " + quote(ugens)) + " > " + quote(log) + " 2>&1";
    if(std::system(cmd.c_str()) != 0 || !std::filesystem::exists(rawWav)) throw std::runtime_error("PNG saved; audio render failed. See " + log);
    { std::ifstream messages(log); std::string line;
      while(std::getline(messages,line)) if(line.find("FAILURE")!=std::string::npos || line.find("*** ERROR")!=std::string::npos)
          throw std::runtime_error("Offline audio server reported an error. See "+log); }
    trimFloatWav(rawWav,wav,(uint32_t)std::lround(latency*48000),(uint32_t)std::lround(duration*48000));
    { std::error_code ec; std::filesystem::remove(rawWav,ec); }
    if(video){
        auto ffmpeg=executable({"/opt/homebrew/bin/ffmpeg","/usr/local/bin/ffmpeg","/usr/bin/ffmpeg"});
        if(ffmpeg.empty()) throw std::runtime_error("FFmpeg missing for audio embedding");
        std::ostringstream dur; dur << std::setprecision(12) << duration;
        cmd=quote(ffmpeg)+" -nostdin -y -i "+quote(base+".video.tmp.mp4")+" -i "+quote(wav)
            +" -map 0:v:0 -map 1:a:0 -c:v copy -c:a aac -b:a 320k -t "+dur.str()+" -movflags +faststart "+quote(base+".mp4")+" >> "+quote(log)+" 2>&1";
        if(std::system(cmd.c_str())!=0) throw std::runtime_error("WAV and silent video saved; audio embedding failed. See "+log);
        std::error_code ec; std::filesystem::remove(base+".video.tmp.mp4",ec);
    }
    (void)audio;
    return video ? "Saved PNG, 16-bit TIFF, recipe, WAV and MP4 with embedded audio." : "Saved PNG, recipe and WAV.";
}
} // exportmedia
} // forms
