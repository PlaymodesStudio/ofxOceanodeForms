//
//  formsRenderers.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  Extra renderers ("workers"): more copies of the Processing renderer, each listening on its own
//  port, so the extra layers (2..4) and editor previews render in parallel with layer 1.
//  Layer 1 always stays on the main renderer, in order: only images placed by beat (extra
//  layers) or shown by tag (previews) go to workers, because their completion order doesn't
//  matter. A worker announces itself with /worker <port> <codeHash>; one whose request is
//  overdue is marked dead and its requests are sent again elsewhere (same request, same image).
//  No openFrameworks here, so it can be tested headless.
//

#pragma once

#include <chrono>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace forms {

class RendererPool {
public:
    using Clock = std::chrono::steady_clock;
    struct Worker {
        int port = 0;
        bool alive = false;            // announced itself, or answered, and not overdue
        std::string codeHash;          // what it was compiled from
        std::map<std::string, Clock::time_point> inFlight;   // request id -> sent at
        std::map<std::string, std::string> paths;              // request id -> request path
        int answered = 0, resent = 0;
        double lastMs = 0, maxMs = 0;  // render times seen (ms), for the overdue bound
        Clock::time_point lastSeen;
    };
    std::vector<Worker> workers;
    int perWorker = 2;                 // requests in flight per worker
    double minOverdueS = 10;           // a request is overdue after max(this, 3 x the longest render)
    // send(port, path): how a request reaches a worker (set by the node: an OSC /render)
    std::function<void(int, const std::string&)> send;
    std::function<void(const std::string&)> note;

    void configure(int basePort, int count){
        if((int)workers.size() == count && (count == 0 || workers[0].port == basePort)) return;
        std::vector<Worker> w(count);
        for(int i = 0; i < count; i++){
            w[i].port = basePort + i;
            for(auto& o : workers) if(o.port == w[i].port) w[i] = o;   // keep state of ports that stay
        }
        workers = w;
    }

    int aliveCount() const { int n = 0; for(auto& w : workers) if(w.alive) n++; return n; }
    int capacity() const { return aliveCount() * perWorker; }
    int inFlightCount() const { int n = 0; for(auto& w : workers) n += (int)w.inFlight.size(); return n; }

    // A worker said hello
    void hello(int port, const std::string& hash){
        for(auto& w : workers) if(w.port == port){
            if(!w.alive && note) note("renderer on port " + std::to_string(port) + " is up");
            w.alive = true; w.codeHash = hash; w.lastSeen = Clock::now();
        }
    }

    // Send to the alive worker with the fewest in flight; false if none has room
    bool dispatch(const std::string& id, const std::string& path){
        Worker* best = nullptr;
        for(auto& w : workers)
            if(w.alive && (int)w.inFlight.size() < perWorker && (!best || w.inFlight.size() < best->inFlight.size())) best = &w;
        if(!best) return false;
        best->inFlight[id] = Clock::now();
        best->paths[id] = path;
        if(send) send(best->port, path);
        return true;
    }

    bool owns(const std::string& id) const { for(auto& w : workers) if(w.inFlight.count(id)) return true; return false; }

    // An answer (/ready or /error) for id
    void answered(const std::string& id){
        for(auto& w : workers){
            auto it = w.inFlight.find(id);
            if(it == w.inFlight.end()) continue;
            double ms = std::chrono::duration<double, std::milli>(Clock::now() - it->second).count();
            w.lastMs = ms; w.maxMs = std::max(w.maxMs * 0.98, ms);
            w.inFlight.erase(it); w.paths.erase(id);
            w.answered++; w.alive = true; w.lastSeen = Clock::now();
        }
    }

    // Overdue requests: their worker is marked dead and they go to another worker (or are given
    // back, so the caller can send them to the main renderer)
    std::vector<std::pair<std::string, std::string>> expire(){
        std::vector<std::pair<std::string, std::string>> orphans;
        auto now = Clock::now();
        for(auto& w : workers){
            double bound = std::max(minOverdueS, 3 * w.maxMs / 1000.0);
            bool overdue = false;
            for(auto& [id, t] : w.inFlight) if(std::chrono::duration<double>(now - t).count() > bound) overdue = true;
            if(!overdue) continue;
            if(note) note("renderer on port " + std::to_string(w.port) + " is not answering: its requests go elsewhere");
            w.alive = false;
            for(auto& [id, p] : w.paths) orphans.push_back({id, p});
            w.inFlight.clear(); w.paths.clear();
        }
        std::vector<std::pair<std::string, std::string>> left;
        for(auto& [id, p] : orphans){
            if(dispatch(id, p)){ for(auto& w : workers) if(w.inFlight.count(id)) w.resent++; }
            else left.push_back({id, p});
        }
        return left;
    }
};

} // namespace forms
