// Drive Transport against Oceanode's real transport (ofxOceanodeTransport.cpp, compiled in):
// Forms Box claims it as its external clock and reports TransportFeed's beat every frame (5 ms of
// real time per frame here), including a beat repeat (the header goes back, the feed does not) and
// a pause. The transport's beat must follow Forms Box's, never go back, and stop when it pauses.
// Build: clang++ -std=c++17 -I src/formsBox -I <OF>/addons/ofxOceanode/src/Managers tests/test_transport_drive.cpp <OF>/addons/ofxOceanode/src/Managers/ofxOceanodeTransport.cpp
#include "ofxOceanodeTransport.h"
#include "formsTransportFeed.h"
#include <chrono>
#include <cstdio>
#include <thread>
using namespace forms;

int main(){
    auto root = std::make_shared<ofxOceanodeTransport>();
    int owner = 0;
    root->claimExternalClock(&owner);
    root->setDriverMode(TransportDriverMode::External);
    TransportFeed feed;
    const int W = 1920; const double len = 32;   // an 8-bar image
    double formsBeat = 0, bpm = 95, worst = 0, lastT = -1; int back = 0, fails = 0;
    bool pausedSeen = false; double pausedAt = 0;
    uint64_t gen0 = 0; int seeks = 0;
    for(int f = 0; f < 800; f++){
        float dt = 0.005f;
        bool paused = f >= 450 && f < 520;
        // frames 600..680: negative Speed (Forms Box runs backwards), then forwards again
        double dir = (f >= 600 && f < 680) ? -1.0 : 1.0;
        if(!paused) formsBeat += dir * bpm / 60.0 * dt;
        // a beat repeat for frames 200..260: the header shows 1 beat earlier (repeatPx = 1 beat)
        int repeatPx = (f >= 200 && f < 260) ? (int)std::lround(W / len) : 0;
        double headerBeat = formsBeat - repeatPx / (double)W * len;
        feed.step(headerBeat, len, repeatPx, W, (float)bpm, paused, dt);
        ofxOceanodeTransport::ExternalClockUpdate u;
        u.beat = feed.beat; u.steadyTimeUs = ofxOceanodeTransport::steadyNowUs(); u.bpm = (float)feed.bpm;
        u.playing = feed.playing; u.jump = feed.jump; u.maxLeadBeats = 0.25;
        root->updateExternalClock(&owner, u);
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto st = root->getState();
        double tb = st.beatPosition;
        if(tb < lastT - 1e-9) back++;
        lastT = tb;
        if(f == 1) gen0 = st.generation;
        if(f > 1 && st.generation != gen0){ seeks++; gen0 = st.generation; }
        // while it is behind where it was (reverse), the transport holds; elsewhere it follows
        if(f < 600) worst = std::max(worst, std::fabs(tb - formsBeat));
        if(paused && f > 455){ if(!pausedSeen){ pausedSeen = true; pausedAt = tb; } else if(std::fabs(tb - pausedAt) > 1e-6 || st.isPlaying){ fails++; } }
    }
    auto st = root->getState();
    printf("transport beat %.3f, Forms Box %.3f; largest gap %.3f beats; went back %d; seeks after the start %d; external owner ok %d; paused held %s\n",
           st.beatPosition, formsBeat, worst, back, seeks, root->getExternalClockOwner() == &owner, fails ? "NO" : "yes");
    bool ok = worst < 0.3 && back == 0 && seeks == 0 && fails == 0 && root->getExternalClockOwner() == &owner;
    root->releaseExternalClock(&owner);
    ok = ok && !root->hasExternalClock();
    printf("%s\n", ok ? "ALL OK" : "FAILED");
    return ok ? 0 : 1;
}
