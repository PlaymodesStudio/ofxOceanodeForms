// Prints Forms Box's Root Out / Scale Out for a grid of harmonies (the real formsSoundModel.h code),
// as JSON lines, for the voice-tuning tests (test_voice_tuning.py)
#include "formsSoundModel.h"
#include <cstdio>
using namespace forms;
int main(){
    std::vector<std::vector<float>> scales = {
        {0, 2, 3, 5, 7, 8, 10},          // minor
        {0, 2, 4, 5, 7, 9, 11},          // major
        {0, 3, 7, 10, 14},               // the 1920 nebula's scale (14 = an octave + 2)
        {0, 2, 4, 7, 9},                 // pentatonic
        {0, 1, 4, 5, 7, 8, 11},          // double harmonic
        {0, 4, 7},                       // triad
    };
    for(int root : {24, 27, 31, 36}) for(int s = 0; s < (int)scales.size(); s++)
        for(int trans : {-5, 0, 2, 7}) for(int deg = 0; deg < (int)scales[s].size() + 2; deg++){
            int r = chordRootMidi(root, deg, trans, scales[s]);
            auto g = scaleGrid(root, trans, scales[s]);
            printf("{\"root\":%d,\"trans\":%d,\"degree\":%d,\"scale\":[", root, trans, deg);
            for(size_t i = 0; i < scales[s].size(); i++) printf("%s%g", i ? "," : "", scales[s][i]);
            printf("],\"rootOut\":%d,\"scaleOut\":[", r);
            for(size_t i = 0; i < g.size(); i++) printf("%s%d", i ? "," : "", g[i]);
            printf("]}\n");
        }
    return 0;
}
