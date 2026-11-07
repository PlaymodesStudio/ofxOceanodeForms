// The GPU noise mask (formsGLGpu.h) against the CPU path, on request files at a bigger size:
// time per image both ways and how many pixels differ. Run: ./a.out <scale> <request.json>...
#include "formsGLGpu.h"
#include <chrono>
#include <cstdio>
using namespace formsgl;
int main(int argc, char** argv){
    Schemas sc; sc.loadEmbedded();
    int scale = atoi(argv[1]);
    GpuEffectsCGL gpu;
    if(!gpu.ok()){ printf("no GPU: %s\n", gpu.error.c_str()); return 1; }
    double tc = 0, tg = 0; long diff = 0, diff2 = 0, total = 0; int maxd = 0, n = 0;
    for(int i = 2; i < argc; i++){
        std::ifstream in(argv[i]); json r; in >> r;
        r["refSize"] = {1920, 1080}; r["size"] = {1920 * scale, 1080 * scale};
        Sketch a(sc), b(sc);
        b.gpu = &gpu; b.gpuMinPixels = getenv("GPU_OFF") ? (size_t)-1 : 1;
        auto t0 = std::chrono::steady_clock::now(); auto ra = a.render(r);
        auto t1 = std::chrono::steady_clock::now(); auto rb = b.render(r);
        auto t2 = std::chrono::steady_clock::now();
        tc += std::chrono::duration<double, std::milli>(t1 - t0).count(); tg += std::chrono::duration<double, std::milli>(t2 - t1).count(); n++;
        for(size_t k = 0; k < ra.rgba.size(); k += 4){
            int d = 0; for(int c = 0; c < 4; c++) d = std::max(d, std::abs((int)ra.rgba[k + c] - (int)rb.rgba[k + c]));
            if(d) diff++; if(d > 2) diff2++; maxd = std::max(maxd, d); total++;
        }
    }
    printf("x%d, %d images: CPU %.0f ms, GPU %.0f ms per image; pixels differing %.4f%% (> 2 levels %.4f%%), max %d%s\n",
           scale, n, tc / n, tg / n, 100.0 * diff / total, 100.0 * diff2 / total, maxd, gpu.error.empty() ? "" : (" GPU error: " + gpu.error).c_str());
}
