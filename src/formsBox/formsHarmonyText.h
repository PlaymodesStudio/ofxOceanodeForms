//
//  formsHarmonyText.h
//  ofxOceanodeForms — ESPECTRES 2027 (v1 backport: the typed harmony editor)
//
//  "I vi IV V" (or with lengths in bars, "I:2 vi IV:1 V*2") into v1's harmony: per bar a degree
//  index (roots: I = 0 ... VII = 6) and transpose 0. Case and quality letters are ignored
//  ("vi", "VI", "vi7", "V7" all mean degree 6 / 5); digits 1-7 work too. The bars repeat the
//  progression to fill an image. Where it goes is the Scenes tab's choice (formsEditorScenes.h).
//  Plain C++ (tests/test_v1_backports.cpp).
//

#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <cctype>

namespace forms {

struct HarmonyText {
    std::vector<int> degrees;           // per step: degree index
    std::vector<int> lengths;           // per step: bars
    std::string error;
    int totalBars() const { int n = 0; for(int l : lengths) n += l; return n; }
    // roots per bar for an image of `bars` bars (the progression repeats; empty if it failed)
    std::vector<int> rootsFor(int bars) const {
        std::vector<int> out;
        if(degrees.empty() || bars <= 0) return out;
        while((int)out.size() < bars)
            for(size_t i = 0; i < degrees.size() && (int)out.size() < bars; i++)
                for(int k = 0; k < lengths[i] && (int)out.size() < bars; k++) out.push_back(degrees[i]);
        return out;
    }
};

inline int romanDegree(const std::string& t, size_t& used){
    static const char* names[7] = {"vii", "iii", "vi", "iv", "ii", "v", "i"};   // longest first
    static const int deg[7] = {6, 2, 5, 3, 1, 4, 0};
    std::string low;
    for(char c : t) low += (char)std::tolower((unsigned char)c);
    if(!low.empty() && low[0] >= '1' && low[0] <= '7'){ used = 1; return low[0] - '1'; }
    for(int i = 0; i < 7; i++){
        std::string n = names[i];
        if(low.compare(0, n.size(), n) == 0){ used = n.size(); return deg[i]; }
    }
    used = 0;
    return -1;
}

inline HarmonyText parseHarmonyText(const std::string& text){
    HarmonyText h;
    std::istringstream in(text);
    std::string tok;
    while(in >> tok){
        if(tok == "|" || tok == "-" || tok == ",") continue;
        size_t used = 0;
        int d = romanDegree(tok, used);
        if(d < 0){ h.error = "not a degree: \"" + tok + "\" (use I..VII or 1..7)"; h.degrees.clear(); h.lengths.clear(); return h; }
        int len = 1;
        size_t sep = tok.find_first_of(":*x", used);
        if(sep != std::string::npos){
            try { len = std::stoi(tok.substr(sep + 1)); } catch(...) { len = 0; }
            if(len < 1 || len > 64){ h.error = "bad length in \"" + tok + "\" (1..64 bars)"; h.degrees.clear(); h.lengths.clear(); return h; }
        }
        h.degrees.push_back(d);
        h.lengths.push_back(len);
    }
    if(h.degrees.empty()) h.error = "type degrees, e.g. I vi IV V or I:2 V:2";
    return h;
}

inline std::string harmonyToText(const std::vector<int>& roots){
    static const char* names[7] = {"I", "ii", "iii", "IV", "V", "vi", "vii"};
    std::string s;
    for(size_t i = 0; i < roots.size();){
        size_t j = i;
        while(j < roots.size() && roots[j] == roots[i]) j++;
        int r = roots[i];
        std::string n = r >= 0 && r < 7 ? names[r] : std::to_string(r + 1);
        if(!s.empty()) s += " ";
        s += n;
        if(j - i > 1) s += ":" + std::to_string(j - i);
        i = j;
    }
    return s;
}

} // namespace forms
