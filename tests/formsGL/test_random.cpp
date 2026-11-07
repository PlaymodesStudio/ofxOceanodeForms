// The C++ random streams against Processing's (reference values printed by a Processing sketch:
// tests/formsGL/rngcheck/rngcheck.pde). Usage: ./a.out rng_java.txt
#include "formsGLRandom.h"
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
using namespace formsgl;
static int32_t bits(float f){ int32_t b; std::memcpy(&b, &f, 4); return b; }
int main(int, char** argv){
    std::ifstream in(argv[1]);
    std::map<std::string, std::pair<int,int>> stat;   // kind -> (ok, total)
    int seeds[] = {0, 1, 12345, 839201, 2147483000};
    std::string line;
    std::map<int, PRandom> pr; std::map<int, SketchPRNG> sp; std::map<int, bool> inited;
    std::map<std::string, int> firstBad;
    PRandom* cur = nullptr; SketchPRNG* curp = nullptr; int curSeed = -1; std::string lastKind;
    while(std::getline(in, line)){
        std::istringstream ss(line); std::string k; long long s; long long v; ss >> k >> s >> v;
        if((int)s != curSeed){ curSeed = (int)s; pr[curSeed] = PRandom(); cur = &pr[curSeed]; cur->randomSeed(s); }
        long long got = 0;
        if(k == "r1") got = bits(cur->random(1));
        else if(k == "r2") got = bits(cur->random(-3.5f, 17.25f));
        else if(k == "ri") got = (int)cur->random(7);
        else if(k == "rg") got = bits(cur->randomGaussian());
        else if(k.rfind("n", 0) == 0){
            static int ni = 0; static std::string lk;
            if(k != lk){ ni = 0; if(k == "n1"){ cur->noiseSeed(s); cur->noiseDetail(4, 0.5f); } if(k == "n3") cur->noiseDetail(3, 0.65f); lk = k; }
            int i = ni++;
            if(k == "n1") got = bits(cur->noise(i * 0.137f));
            else if(k == "n2") got = bits(cur->noise(i * 0.071f, i * 0.33f + 5.5f));
            else got = bits(cur->noise(i * 0.21f, 3.3f, i * 0.05f));
        }
        else if(k == "pr" || k == "pp" || k == "pm"){
            static int pi = 0; static std::string lk2; static long long ls = -1;
            if(k == "pr" && (lk2 != "pr" || ls != s)){ sp[curSeed] = SketchPRNG(s); curp = &sp[curSeed]; }
            if(k != lk2 || ls != s){ pi = 0; lk2 = k; ls = s; }
            int i = pi++;
            if(k == "pr") got = bits(curp->random(0, 1));
            else if(k == "pp") got = bits(curp->perlin(i * 0.173f, i * 0.029f + 1.7f));
            else got = bits(curp->mixPerlin(i * 0.1f, 2.2f, 4, 2, 0.5f));
        }
        else if(k == "jm") got = JavaRandom(s).nextInt(1 << 30);
        auto& st = stat[k]; st.second++;
        if(got == v) st.first++; else if(!firstBad.count(k)) firstBad[k] = st.second;
    }
    int bad = 0;
    for(auto& [k, st] : stat){ printf("%s %d/%d exact%s\n", k.c_str(), st.first, st.second, st.first == st.second ? "" : ("  first mismatch at #" + std::to_string(firstBad[k])).c_str()); bad += st.second - st.first; }
    printf("%s\n", bad ? "MISMATCHES" : "ALL EXACT");
    return bad ? 1 : 0;
}
