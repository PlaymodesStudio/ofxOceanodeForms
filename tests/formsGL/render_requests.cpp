// Renders request files with the C++ port (Forms GL Box's renderer), headless, to PNG next to a
// given folder: <out>/<request name>.png (straight alpha, as the sketch writes).
// Build: clang++ -std=c++17 -O2 -ffp-contract=off -I src/formsGL -I <OF>/libs/json/include tests/formsGL/render_requests.cpp -lz
//        (letters: -DWITH_FONT -I <freetype2 headers> -lfreetype, and FORMSGL_FONT=<FuturaStd-Bold.otf>)
// Run:   ./a.out <generators.json> <out dir> <request.json>...
#include "formsGLSketch.h"
#ifdef WITH_FONT
#include "formsGLFont.h"
#endif
#include <zlib.h>
#include <cstdio>
#include <filesystem>
using namespace formsgl;

static void put32(std::vector<uint8_t>& v, uint32_t x){ v.push_back(x >> 24); v.push_back(x >> 16); v.push_back(x >> 8); v.push_back(x); }
static void chunk(FILE* f, const char* type, const std::vector<uint8_t>& data){
    std::vector<uint8_t> c; put32(c, (uint32_t)data.size()); c.insert(c.end(), type, type + 4); c.insert(c.end(), data.begin(), data.end());
    uint32_t crc = crc32(0, c.data() + 4, (uInt)(c.size() - 4)); put32(c, crc);
    fwrite(c.data(), 1, c.size(), f);
}
static bool writePng(const std::string& path, const std::vector<uint8_t>& rgba, int w, int h){
    FILE* f = fopen(path.c_str(), "wb"); if(!f) return false;
    const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10}; fwrite(sig, 1, 8, f);
    std::vector<uint8_t> ih; put32(ih, w); put32(ih, h); ih.push_back(8); ih.push_back(6); ih.push_back(0); ih.push_back(0); ih.push_back(0);
    chunk(f, "IHDR", ih);
    std::vector<uint8_t> raw; raw.reserve((size_t)h * (w * 4 + 1));
    for(int y = 0; y < h; y++){ raw.push_back(0); raw.insert(raw.end(), rgba.begin() + (size_t)y * w * 4, rgba.begin() + (size_t)(y + 1) * w * 4); }
    uLongf n = compressBound(raw.size()); std::vector<uint8_t> z(n);
    compress2(z.data(), &n, raw.data(), raw.size(), 6); z.resize(n);
    chunk(f, "IDAT", z); chunk(f, "IEND", {});
    fclose(f); return true;
}

int main(int argc, char** argv){
    Schemas schemas;
    if(!schemas.load(argv[1])){ printf("cannot read %s\n", argv[1]); return 1; }
    std::filesystem::create_directories(argv[2]);
    int bad = 0;
    for(int i = 3; i < argc; i++){
        std::ifstream in(argv[i]); json req; in >> req;
        Sketch sk(schemas);
#ifdef WITH_FONT
        static auto font = getenv("FORMSGL_FONT") ? loadGlyphFont(getenv("FORMSGL_FONT")) : nullptr;
        sk.font = font;
#endif
        std::string why = sk.whyNot(req);
        if(!why.empty()){ printf("%s: not here (%s)\n", argv[i], why.c_str()); bad++; continue; }
        auto r = sk.render(req);
        if(!r.error.empty()){ printf("%s: error %s\n", argv[i], r.error.c_str()); bad++; continue; }
        std::string name = std::filesystem::path(argv[i]).stem().string();
        writePng(std::string(argv[2]) + "/" + name + ".png", r.rgba, r.w, r.h);
        printf("%s: %d ms\n", name.c_str(), r.renderMs);
        for(auto& ir : r.itemResults) if(ir.contains("error")) printf("   item error: %s\n", ir["error"].get<std::string>().c_str());
    }
    return bad ? 1 : 0;
}
