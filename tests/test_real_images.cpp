// Real rendered images: the centre of every bright line (a run of rows > half the column's
// maximum, 2..8 rows thick), its pitch by the renderer's formula and by the 1920 mapping, and
// the pitch Forms Box's partials give it (the brightness-weighted centre of the lit partials)
#include "formsSoundModel.h"
#include <fstream>
#include <cstdio>
#include <dirent.h>
using namespace forms;
int main(int argc, char** argv){
    std::string dir = argv[1];
    DIR* D = opendir(dir.c_str());
    std::vector<double> hNew(20, 0), hOld(20, 0), hForms(20, 0);
    long n = 0, inScaleNew = 0, inScaleOld = 0, inScaleForms = 0;
    while(auto* e = readdir(D)){
        std::string nm = e->d_name; if(nm.size() < 5 || nm.substr(nm.size() - 4) != ".bmp") continue;
        std::string base = dir + "/" + nm.substr(0, nm.size() - 4);
        std::ifstream f(base + ".bmp", std::ios::binary);
        std::vector<unsigned char> b((std::istreambuf_iterator<char>(f)), {});
        auto u32 = [&](int o){ return (unsigned)(b[o] | b[o+1] << 8 | b[o+2] << 16 | b[o+3] << 24); };
        unsigned off = u32(10); int W = u32(18), hh = (int)u32(22), bpp = b[28] | b[29] << 8, H = std::abs(hh);
        int bytes = bpp / 8, stride = (W * bytes + 3) & ~3;
        auto j = nlohmann::json::parse(std::ifstream(base + ".json"));
        int ng = j["notegrid"], bars = j.value("bars", 8);
        auto sc = j["scaleNotes"].get<std::vector<float>>(); auto tr = j["trans"].get<std::vector<int>>();
        auto L = partialLayout(H, ng, H);
        double noteh = (double)H / ng, safe = H / ng;
        for(int k = 0; k < 96; k++){
            int x = (int)((k + 0.5) * W / 96), bar = std::min(bars - 1, x * bars / W);
            auto grid = scaleGrid(0, tr[bar], sc);
            std::vector<unsigned char> col(H * 4);
            for(int y = 0; y < H; y++){ int sy = hh > 0 ? H - 1 - y : y; const unsigned char* s = &b[off + sy * stride + x * bytes]; col[y*4] = s[2]; col[y*4+1] = s[1]; col[y*4+2] = s[0]; }
            auto v = [&](int y){ return std::max(col[y*4], std::max(col[y*4+1], col[y*4+2])) / 255.0f; };
            float mx = 0; for(int y = 0; y < H; y++) mx = std::max(mx, v(y));
            if(mx < 0.3f) continue;
            std::vector<float> am; columnAmps(col.data(), H, 1, L, am);
            auto pitches = partialPitches(0, L, 0, {}, 0);
            auto onScale = [&](double p){ double rr = std::round(p); if(std::abs(p - rr) > 0.15) return false; int pc = (((int)rr % 12) + 12) % 12; for(int g : grid) if(g == pc) return true; return false; };
            auto bin = [](double p){ return std::min(19, (int)((p - std::floor(p + 0.5) + 0.5) * 20)); };
            for(int y = 0; y < H; ){
                if(v(y) <= 0.5f * mx){ y++; continue; }
                int a = y; while(y < H && v(y) > 0.5f * mx) y++;
                int len = y - a;
                if(len < 2 || len > 8) continue;           // lines, not clouds
                double c = (a + y) / 2.0;                   // the run's centre (rows a..y-1)
                double pNew = (H - safe - c) / noteh, pOld = (ng + 1) * (H - c) / H - 1.33;
                // Forms Box: the partials lit by this run, brightness-weighted
                double sw = 0, swp = 0;
                for(int i = 0; i < L.n; i++) if(am[i] > 0 && L.rowB[i] > a && L.rowA[i] < y){ sw += am[i]; swp += am[i] * pitches[i]; }
                double pForms = sw > 0 ? swp / sw : pNew;
                hNew[bin(pNew)]++; hOld[bin(pOld)]++; hForms[bin(pForms)]++; n++;
                if(onScale(pNew)) inScaleNew++; if(onScale(pOld)) inScaleOld++; if(onScale(pForms)) inScaleForms++;
            }
        }
    }
    auto show = [&](const char* t, std::vector<double>& h){ printf("%s", t); for(int i = 0; i < 20; i++) printf("%5.1f", 100 * h[i] / n); printf("\n"); };
    printf("%ld lines (2-8 rows thick) in 20 images; where they fall within a semitone, in 5-cent bins (-50 .. +50 cents; the middle two = on a note):\n", n);
    show("renderer formula ", hNew); show("Forms Box sound  ", hForms); show("1920 mapping     ", hOld);
    printf("on a scale note of their bar (within 15 cents): renderer formula %.1f%%, Forms Box sound %.1f%%, 1920 mapping %.1f%%\n",
           100.0 * inScaleNew / n, 100.0 * inScaleForms / n, 100.0 * inScaleOld / n);
}
