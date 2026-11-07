// Forms GL Box's "Make thumbnails", headless: the engine builds one preview per instrument (as
// formsGLBox.h makeThumbnailsGL: one scene, one seed, the GL underlay under effects-only ones), the
// C++ port draws each; reports errors, empty images and time, and writes a contact sheet.
// Build: clang++ -std=c++17 -O2 -I tests/stub -I src/formsBox -I src/formsGL -I <OF>/libs/json/include
//        -DWITH_FONT -I <freetype2> tests/formsGL/test_thumbnails.cpp -lfreetype -lz
// Run:   ./a.out <renderer folder copy> <contact sheet .ppm>
#include "formsEngine.h"
#include "formsGLSketch.h"
#include "formsGLFont.h"
#include <chrono>
#include <cstdio>
using namespace forms;
int main(int, char** argv){
    Engine e; e.rendererFolder = argv[1]; e.loadAll(); e.newSession(7);
    formsgl::Schemas sc; if(!sc.loadEmbedded()){ printf("schemas\n"); return 1; }
    auto font = formsgl::loadGlyphFont((std::filesystem::path(argv[1]) / "FuturaStd-Bold.otf").string());
    std::string scene = e.scenes.begin()->first;
    e.itemBars = e.scenes.begin()->second.layers[0].bars;
    std::vector<std::pair<std::string, json>> variants;
    int under = 0;
    for(auto& [ref, pr] : e.presets){
        json items = json::array();
        if(e.onlyEffects(pr)){ for(auto& u : formsgl::glUnderlay()) items.push_back(u); under++; }
        for(auto& x : e.presetItemsSeeded(pr, 1234)) items.push_back(x);
        variants.push_back({ref, items});
    }
    auto paths = e.makePreviews(scene, variants);
    printf("%zu instruments (%d effects-only over the underlay), %zu requests, scene %s\n", variants.size(), under, paths.size(), scene.c_str());
    int errs = 0, empty = 0, n = 0; double ms = 0;
    const int TW = 96, TH = 54, COLS = 24;
    int rows = ((int)paths.size() + COLS - 1) / COLS;
    std::vector<uint8_t> sheet((size_t)COLS * TW * rows * TH * 3, 0);
    for(size_t i = 0; i < paths.size(); i++){
        std::ifstream in(paths[i]); json req; in >> req;
        formsgl::substituteUnderlay(req);
        formsgl::Sketch sk(sc); sk.font = font;
        auto t0 = std::chrono::steady_clock::now();
        auto r = sk.render(req);
        ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        n++;
        bool itemErr = false;
        for(auto& ir : r.itemResults) if(ir.contains("error")){ itemErr = true; if(errs < 8) printf("  %s: %s\n", variants[i].first.c_str(), ir["error"].get<std::string>().c_str()); }
        if(!r.error.empty() || itemErr){ errs++; continue; }
        long lit = 0;
        for(size_t k = 0; k < (size_t)r.w * r.h; k++) if(r.rgba[k * 4 + 3] > 8) lit++;
        if(lit == 0){ empty++; if(empty < 12) printf("  empty: %s\n", variants[i].first.c_str()); }
        int cx = (int)(i % COLS) * TW, cy = (int)(i / COLS) * TH;
        for(int y = 0; y < TH; y++) for(int x = 0; x < TW; x++){
            const uint8_t* s = &r.rgba[((size_t)(y * r.h / TH) * r.w + x * r.w / TW) * 4];
            for(int c = 0; c < 3; c++) sheet[((size_t)(cy + y) * COLS * TW + cx + x) * 3 + c] = (uint8_t)(s[c] * s[3] / 255);
        }
    }
    FILE* o = fopen(argv[2], "wb"); fprintf(o, "P6 %d %d 255\n", COLS * TW, rows * TH); fwrite(sheet.data(), 1, sheet.size(), o); fclose(o);
    printf("drawn %d, with errors %d, empty %d, %.0f ms in all (%.1f ms each)\n", n, errs, empty, ms, ms / std::max(1, n));
    return errs ? 1 : 0;
}
