//
//  formsTransportFeed.h
//  ofxOceanodeForms — ESPECTRES 2027
//
//  What Forms Box tells the transport each frame when it drives it (Drive Transport): its beat
//  under the header with the beat repeats left out (the transport never runs back for a repeat),
//  the tempo it really moves at, whether it plays, and whether this frame is a jump (a seek: the
//  first frame, a header moved by hand, a new session). Plain C++ (tests/test_layers_live.cpp).
//

#pragma once

#include <algorithm>
#include <cmath>

namespace forms {

struct TransportFeed {
    // out
    double beat = 0, bpm = 120;
    bool playing = false, jump = true;

    // headerBeat / headerLen: the scroller's beat under the header and the length (beats) of the
    // image there; repeatPx: the header's beat-repeat offset (px of viewW); clockBpm: Forms Box's
    // tempo (the first frame's); paused: Pause
    void step(double headerBeat, double headerLen, float repeatPx, int viewW, float clockBpm, bool paused, float dt){
        double tb = headerBeat + repeatPx / std::max(1.0, (double)viewW) * std::max(1.0, headerLen);
        // running backwards (negative Speed): the transport holds where it was and goes on once
        // Forms Box passes that point again (a seek every frame would reset everything synced)
        double drop = last - tb;
        if(!first && drop > 1e-6 && drop < 1.0){ jump = false; playing = false; beat = std::max(0.0, last); return; }
        double expected = last + bpm / 60.0 * dt;
        jump = first || tb < last - 1e-6 || std::fabs(tb - expected) > 1.0;
        bool moving = tb > last + 1e-9;
        if(first) bpm = std::max(1.0f, clockBpm);
        else if(!jump && moving && dt > 0){
            double inst = (tb - last) / dt * 60.0;
            bpm = 0.9 * bpm + 0.1 * std::max(1.0, std::min(999.0, inst));
        }
        // plays while the beat moves (not before the first image, not at speed 0 or paused)
        playing = !paused && (moving || jump);
        beat = std::max(0.0, tb);
        last = tb;
        first = false;
    }
    void restart(){ first = true; }

private:
    double last = 0;
    bool first = true;
};

} // namespace forms
