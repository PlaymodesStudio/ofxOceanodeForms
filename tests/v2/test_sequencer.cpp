// Headless tests for the v2 sequencer and harmony timeline (v2Sequencer.h, v2Harmony.h).
#include "v2Sequencer.h"
#include <cstdio>
#include <set>

using namespace forms2;

static int g_pass = 0, g_fail = 0;
#define CHECK(c) do{ if(c) g_pass++; else { g_fail++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)

static Instance inst(const std::string& id, const std::string& type, json seq){
    Instance in; in.id = id; in.type = type; in.seq = std::move(seq); return in;
}
static Phrase phr(int k, Beat start, Beat len, const Instance& in, uint64_t scene = 1){
    Phrase p; p.index = k; p.start = start; p.length = len; p.seed = basePhraseSeed(scene, in.id, k); return p;
}
static std::vector<int> stepsOf(const std::vector<SeqHit>& h){ std::vector<int> v; for(auto& x : h) v.push_back(x.step); return v; }
static bool sameEvents(const std::vector<Event>& a, const std::vector<Event>& b){
    if(a.size() != b.size()) return false;
    for(size_t i = 0; i < a.size(); i++)
        if(a[i].start != b[i].start || a[i].degree != b[i].degree || a[i].length != b[i].length || a[i].seed != b[i].seed || a[i].id != b[i].id) return false;
    return true;
}
static bool inChord(int deg, int root, ChordColour c, int n){
    for(int d : chordDegrees(c)) if((((deg - root - d) % n) + n) % n == 0) return true;
    return false;
}

int main(){
    HarmonyTimeline H0;   // C major, 4/4, degree 0
    PlanContext ctx; ctx.harmony = &H0;

    // ---- euclidean: k hits over n steps, rotation
    {
        auto in = inst("e", "kick", {{"mode", "euclidean"}, {"steps", 8}, {"hits", 3}});
        auto h = planRhythm(in, phr(0, 0, 4, in), ctx);
        CHECK(h.size() == 3);
        CHECK(stepsOf(h) == (std::vector<int>{0, 3, 6}) || stepsOf(h) == (std::vector<int>{0, 2, 5}) || h.front().step == 0);
        for(int k = 0; k <= 16; k++){
            auto in2 = inst("e", "kick", {{"steps", 16}, {"hits", k}});
            CHECK((int)planRhythm(in2, phr(0, 0, 4, in2), ctx).size() == k);
        }
        auto inR = inst("e", "kick", {{"steps", 8}, {"hits", 3}, {"rotation", 2}});
        auto hr = planRhythm(inR, phr(0, 0, 4, inR), ctx);
        CHECK(hr.size() == 3 && hr[1].step == 2);
        std::set<int> a, b; for(auto& x : h) a.insert((x.step + 2) % 8); for(auto& x : hr) b.insert(x.step);
        CHECK(a == b);
        CHECK(h[1].start == 0.5 * h[1].step);   // 8 steps over a 4-beat bar
    }
    // ---- probability statistics
    {
        auto in = inst("p", "hats", {{"mode", "probability"}, {"steps", 16}, {"density", 0.3}});
        auto h = planRhythm(in, phr(0, 0, 400, in), ctx);   // 1600 steps
        double f = h.size() / 1600.0;
        CHECK(f > 0.26 && f < 0.34);
        auto in2 = inst("p", "hats", {{"mode", "probability"}, {"steps", 4}, {"stepChances", {100, 0, 50, 0}}});
        auto h2 = planRhythm(in2, phr(0, 0, 400, in2), ctx);
        int c0 = 0, c1 = 0, c2 = 0; for(auto& x : h2){ int i = x.step % 4; c0 += i == 0; c1 += i == 1 || i == 3; c2 += i == 2; }
        CHECK(c0 == 100 && c1 == 0 && c2 > 35 && c2 < 65);
    }
    // ---- typed patterns and alternatives
    {
        auto in = inst("t", "snare", {{"mode", "pattern"}, {"steps", 8}, {"pattern", "x..x..x."}});
        auto h = planRhythm(in, phr(0, 0, 4, in), ctx);
        CHECK(stepsOf(h) == (std::vector<int>{0, 3, 6}));
        auto in2 = inst("t", "snare", {{"mode", "pattern"}, {"steps", 4}, {"pattern", "x...|.x.."}});
        std::set<int> firsts;
        for(int k = 0; k < 20; k++){ auto hh = planRhythm(in2, phr(k, 0, 4, in2), ctx); CHECK(hh.size() == 1); firsts.insert(hh[0].step); }
        CHECK(firsts.size() == 2);
    }
    // ---- follow modes
    {
        auto kick = inst("kick", "kick", {{"steps", 8}, {"hits", 3}});
        auto ke = planPhrase(kick, phr(0, 0, 4, kick), ctx);
        auto fh = inst("s", "snare", {{"mode", "follow"}, {"followId", "kick"}, {"followMode", "hits"}, {"steps", 8}});
        auto fs = inst("s", "snare", {{"mode", "follow"}, {"followId", "kick"}, {"followMode", "skipped"}, {"steps", 8}});
        auto fb = inst("s", "snare", {{"mode", "follow"}, {"followId", "kick"}, {"followMode", "both"}, {"steps", 8}, {"hits", 4}});
        auto a = planRhythm(fh, phr(0, 0, 4, fh), ctx, &ke), b = planRhythm(fs, phr(0, 0, 4, fs), ctx, &ke), c = planRhythm(fb, phr(0, 0, 4, fb), ctx, &ke);
        CHECK(a.size() == 3 && b.size() == 5);
        for(size_t i = 0; i < a.size(); i++) CHECK(a[i].start == ke[i].start);
        CHECK(c.size() == 2 && c[0].step == 0 && c[1].step == 6);   // kick {0,3,6} and euclid 4/8 {0,2,4,6}
    }
    // ---- noRepeat
    {
        auto in = inst("n", "hats", {{"mode", "probability"}, {"steps", 16}, {"density", 0.9}, {"noRepeat", true}});
        auto h = planRhythm(in, phr(0, 0, 64, in), ctx);
        bool ok = !h.empty(); for(size_t i = 1; i < h.size(); i++) if(h[i].step == h[i - 1].step + 1) ok = false;
        CHECK(ok);
    }
    // ---- rolls
    {
        for(int strokes : {2, 5, 64}) for(const char* shape : {"even", "accel", "ritard", "bunch"}){
            auto in = inst("r", "snare", {{"steps", 4}, {"hits", 4}, {"rollChance", 100}, {"rollStrokes", strokes}, {"rollShape", shape}});
            auto h = planRhythm(in, phr(0, 0, 4, in), ctx);
            CHECK((int)h.size() == 4 * strokes);
            bool inside = true; for(auto& x : h) if(x.start < x.step * 1.0 - 1e-9 || x.start >= x.step + 1.0) inside = false;
            CHECK(inside);
        }
        auto in = inst("r", "snare", {{"steps", 1}, {"hits", 1}, {"rollChance", 100}, {"rollStrokes", 4}, {"rollShape", "accel"}});
        auto h = planRhythm(in, phr(0, 0, 4, in), ctx);
        CHECK(h.size() == 4 && (h[1].start - h[0].start) > (h[3].start - h[2].start));
    }
    // ---- polymetre: pattern of 3 steps over 4-step bars
    {
        auto in = inst("pm", "hats", {{"mode", "pattern"}, {"steps", 4}, {"pattern", "x.."}, {"patternLength", 3}});
        auto h = planRhythm(in, phr(0, 0, 12, in), ctx);
        CHECK(stepsOf(h) == (std::vector<int>{0, 3, 6, 9}));
        auto in2 = inst("pm", "hats", {{"mode", "pattern"}, {"steps", 4}, {"pattern", "x.."}});   // restarts each bar
        CHECK(stepsOf(planRhythm(in2, phr(0, 0, 12, in2), ctx)) == (std::vector<int>{0, 3, 4, 7, 8, 11}));
    }
    // ---- meters: 4/4 then 7/8 (2+2+3)
    Harmony hm;
    {
        HarmonySection a; a.lengthBeats = 4;
        HarmonySection b; b.meter.num = 7; b.meter.den = 8; b.meter.grouping = {2, 2, 3}; b.lengthBeats = 14; b.steps[0].length = 7;
        hm.sections = {a, b};
    }
    HarmonyTimeline HM(hm, 1);
    {
        CHECK(HM.barStart(5) == 4 && HM.barStart(12) == 11 && HM.barIndex(12) == 2 && HM.barBeatsAt(10) == 7);
        CHECK(HM.isStrongBeat(4) && !HM.isStrongBeat(5) && HM.isStrongBeat(6) && HM.isStrongBeat(8) && !HM.isStrongBeat(9) && HM.isStrongBeat(11));
        CHECK(HM.isStrongBeat(0) && !HM.isStrongBeat(2));
        PlanContext c2; c2.harmony = &HM;
        auto in = inst("m", "hats", {{"hits", 64}});   // every step
        auto h = planRhythm(in, phr(0, 0, 18, in), c2);
        CHECK(h.size() == 8 + 14);   // 4/4: 8 eighth steps; 7/8: 7 steps per bar
        int strong = 0; for(auto& x : h) if(x.start >= 4 && x.start < 11 && x.strong) strong++;
        CHECK(strong == 3);
        CHECK(HM.sectionBoundaries(0, 40).size() >= 3 && HM.sectionBoundaries(0, 40)[1] == 4);
        auto ina = inst("m", "hats", {{"hits", 64}, {"accent", 1.0}});
        auto ha = planRhythm(ina, phr(0, 0, 18, ina), c2);
        bool ok = true; for(auto& x : ha) if((x.level == 1) != x.strong) ok = false;
        CHECK(ok);
    }
    // ---- swing
    {
        auto in = inst("sw", "hats", {{"steps", 8}, {"hits", 8}, {"swing", 0.5}});
        auto h = planRhythm(in, phr(0, 0, 4, in), ctx);
        CHECK(h.size() == 8 && h[0].start == 0 && std::fabs(h[1].start - (0.5 + 0.125)) < 1e-9 && h[2].start == 1.0);
        Harmony hs; hs.sections[0].meter.swing = 1.0; HarmonyTimeline HS(hs, 1); PlanContext c3; c3.harmony = &HS;
        auto in2 = inst("sw", "hats", {{"steps", 8}, {"hits", 8}});
        CHECK(std::fabs(planRhythm(in2, phr(0, 0, 4, in2), c3)[1].start - 0.75) < 1e-9);
    }
    // ---- every pitched event on a scale degree (several scales, a microtonal tuning)
    {
        std::vector<Scale> scales(4);
        scales[1].semis = {0, 2, 3, 5, 7, 8, 10}; scales[1].tonic = 3;
        scales[2].semis = {0, 2, 4, 7, 9};
        scales[3].tuning.cents = {0, 240, 480, 720, 960};   // 5-TET
        for(auto& sc : scales){
            Harmony hh; hh.sections[0].scale = sc;
            hh.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}, ProgressionStep{4, 8}};
            HarmonyTimeline HT(hh, 3); PlanContext c4; c4.harmony = &HT;
            for(const char* src : {"root", "scale", "chord"}){
                auto in = inst("pp", "melody", {{"mode", "probability"}, {"density", 0.6}, {"pitchSource", src}, {"range", 12}, {"octave", -1}, {"scaleMode", "walk"}});
                auto ev = planPhrase(in, phr(0, 0, 32, in), c4);
                bool ok = !ev.empty();
                for(auto& e : ev){
                    double semis = c4.pitch.semisOf(e.height);
                    double cents = (semis - sc.tonic) * 100, per = sc.tuning.cents.empty() ? 1200 : sc.tuning.periodCents;
                    double m = std::fmod(std::fmod(cents, per) + per, per);
                    bool found = false;
                    for(int d = 0; d < sc.size(); d++){ double c = sc.tuning.cents.empty() ? sc.semis[(size_t)d] * 100.0 : sc.tuning.cents[(size_t)d]; if(std::fabs(c - m) < 1e-4 || std::fabs(c - m + per) < 1e-4) found = true; }
                    if(!found || !e.pitched) ok = false;
                    if(std::string(src) == "chord"){ HarmonyAt a = HT.at(e.start); if(!inChord(e.degree, a.root, a.colour, sc.size())) ok = false; }
                }
                CHECK(ok);
            }
        }
    }
    // ---- follow modes for harmony: chord / scale-only / pedal / off
    {
        Harmony hh; hh.sections[0].steps = {ProgressionStep{3, 8}};
        HarmonyTimeline HT(hh, 1); PlanContext c5; c5.harmony = &HT;
        auto in = inst("f", "bass", {{"pitchSource", "root"}, {"steps", 4}, {"hits", 4}});
        auto e1 = planPhrase(in, phr(0, 0, 4, in), c5);
        in.follow = Follow::ScaleOnly; auto e2 = planPhrase(in, phr(0, 0, 4, in), c5);
        in.follow = Follow::Pedal; in.pedalDegree = 4; auto e3 = planPhrase(in, phr(0, 0, 4, in), c5);
        in.follow = Follow::Off; auto e4 = planPhrase(in, phr(0, 0, 4, in), c5);
        CHECK(e1.size() == 4 && e1[0].degree == 3 && e2[0].degree == 0 && e3[0].degree == 4);
        CHECK(e4.size() == 4 && !e4[0].pitched);
        auto inS = inst("f", "melody", {{"pitchSource", "scale"}, {"steps", 4}, {"hits", 4}, {"range", 3}});
        inS.follow = Follow::Pedal; inS.pedalDegree = 2;
        bool ok = true; for(auto& e : planPhrase(inS, phr(0, 0, 4, inS), c5)) if(e.degree != 2) ok = false;
        CHECK(ok);
        auto kick = inst("k", "kick", {{"steps", 4}, {"hits", 4}});
        auto ek = planPhrase(kick, phr(0, 0, 4, kick), c5);
        CHECK(!ek[0].pitched && ek[0].transient && ek[0].kind == EventKind::Hit && ek[0].height < 0.2);
    }
    // ---- chord / arp
    {
        auto arp = inst("a", "arpeggio", {{"pitchSource", "chord"}, {"voices", 4}, {"notesPerStep", 1}, {"order", "up"}, {"steps", 8}, {"hits", 8}});
        auto e = planPhrase(arp, phr(0, 0, 4, arp), ctx);
        std::vector<int> d; for(auto& x : e) d.push_back(x.degree);
        CHECK(d == (std::vector<int>{0, 2, 4, 7, 0, 2, 4, 7}));
        arp.seq["order"] = "updown"; d.clear(); for(auto& x : planPhrase(arp, phr(0, 0, 4, arp), ctx)) d.push_back(x.degree);
        CHECK(d == (std::vector<int>{0, 2, 4, 7, 4, 2, 0, 2}));
        arp.seq["order"] = "typed"; arp.seq["typedOrder"] = "1 3 2 4"; d.clear(); for(auto& x : planPhrase(arp, phr(0, 0, 4, arp), ctx)) d.push_back(x.degree);
        CHECK(d == (std::vector<int>{0, 4, 2, 7, 0, 4, 2, 7}));
        auto dy = inst("a", "arpeggio", {{"pitchSource", "chord"}, {"voices", 4}, {"notesPerStep", 2}, {"advance", 1}, {"steps", 4}, {"hits", 4}});
        auto ed = planPhrase(dy, phr(0, 0, 4, dy), ctx);
        CHECK(ed.size() == 8 && ed[0].degree == 0 && ed[1].degree == 2 && ed[2].degree == 2 && ed[3].degree == 4 && ed[6].degree == 7 && ed[7].degree == 0);
        auto blk = inst("c", "chords", {{"pitchSource", "chord"}, {"voices", 3}, {"steps", 1}, {"hits", 1}, {"strum", 0.1}});
        auto eb = planPhrase(blk, phr(0, 0, 4, blk), ctx);
        CHECK(eb.size() == 3 && std::fabs(eb[1].start - 0.1) < 1e-9 && std::fabs(eb[2].start - 0.2) < 1e-9);
        Harmony hh; hh.sections[0].steps = {ProgressionStep{0, 8}};
        HarmonyTimeline HT(hh, 1); PlanContext c6; c6.harmony = &HT;
        auto hold = inst("h", "arpeggio", {{"pitchSource", "chord"}, {"notesPerStep", 1}, {"steps", 4}, {"hits", 4}, {"hold", true}, {"noteLength", 0.5}});
        auto eh = planPhrase(hold, phr(0, 0, 8, hold), c6);
        CHECK(eh.size() == 8 && std::fabs(eh[0].start + eh[0].length - 8) < 1e-9 && eh[0].start + eh[0].length > eh[1].start);
        hh.sections[0].steps[0].colour = ChordColour::Add7;
        HarmonyTimeline H7(hh, 1); PlanContext c7; c7.harmony = &H7;
        auto b7 = inst("c", "chords", {{"pitchSource", "chord"}, {"voices", 4}, {"steps", 1}, {"hits", 1}});
        auto e7 = planPhrase(b7, phr(0, 0, 4, b7), c7);
        CHECK(e7.size() == 4 && e7[3].degree == 6);
    }
    // ---- step key shift: the timeline reports it, pitches move by semitones, degrees stay
    {
        Harmony hh; hh.sections[0].scale.tonic = 2; hh.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4, -1, ChordColour::Triad, 1}};
        HarmonyTimeline HT(hh, 1); PlanContext ck; ck.harmony = &HT;
        CHECK(HT.at(1).keyShift == 0 && HT.at(5).keyShift == 1 && HT.at(5).root == 3 && HT.at(5).scale->tonic == 2);
        CHECK(std::fabs(HT.at(5).semitonesOf(0) - 3) < 1e-12 && std::fabs(HT.heightOf(0, 5, ck.pitch) - ck.pitch.heightOf(3)) < 1e-12);
        auto b = inst("kb", "melody", {{"pitchSource", "root"}, {"steps", 1}, {"hits", 1}});
        auto e = planPhrase(b, phr(0, 0, 8, b), ck);
        CHECK(e.size() == 2 && e[1].degree == 3 && !e[0].p.count("glideToSemis"));
        CHECK(e.size() == 2 && std::fabs(e[1].param("glideToSemis", 0) - 8) < 1e-12 && std::fabs(e[1].param("glideFromSemis", 0) - 8) < 1e-12);   // D + F(5) + 1
        CHECK(e.size() == 2 && std::fabs(e[1].height - ck.pitch.heightOf(8)) < 1e-12);
    }
    // ---- voice leading: held chord crossing a harmony change bends to the nearest new tone
    {
        Harmony hh; hh.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
        HarmonyTimeline HT(hh, 1); PlanContext c8; c8.harmony = &HT;
        auto pad = inst("pad", "chords", {{"pitchSource", "chord"}, {"voices", 3}, {"steps", 1}, {"hits", 1}, {"hold", true}, {"voiceLeading", true}, {"voiceLeadGlide", 1.0}});
        auto e = planPhrase(pad, phr(0, 0, 8, pad), c8);
        // bar 1: C E G (0 2 4) held into F A C (3 5 0/7): nearest 0->0, 2->3, 4->5
        CHECK(e.size() == 6 && std::fabs(e[0].length - 8) < 1e-9);
        CHECK(e[0].param("glideTo", -1) == 0 && e[1].param("glideTo", -1) == 3 && e[2].param("glideTo", -1) == 5);
        CHECK(e[1].param("glideStart", -1) == 4 && e[1].param("glideBeats", -1) == 1.0);
        CHECK(e[1].height2 > e[1].height && std::fabs(e[1].height2 - c8.pitch.heightOf(HT.scaleAt(5).semitonesOf(3))) < 1e-12);
        pad.seq["voiceLeading"] = false;
        auto e2 = planPhrase(pad, phr(0, 0, 8, pad), c8);
        CHECK(e2[0].p.count("glideTo") == 0 && std::fabs(e2[0].length - 4) < 1e-9);
    }
    // ---- determinism, ids
    {
        auto in = inst("d", "melody", {{"mode", "probability"}, {"density", 0.5}, {"pitchSource", "scale"}});
        auto a = planPhrase(in, phr(3, 12, 16, in), ctx), b = planPhrase(in, phr(3, 12, 16, in), ctx);
        CHECK(sameEvents(a, b) && !a.empty());
        auto c = planPhrase(in, phr(4, 12, 16, in), ctx);
        CHECK(!sameEvents(a, c));
        std::set<uint64_t> ids; for(auto& e : a) ids.insert(e.id); for(auto& e : c) ids.insert(e.id);
        CHECK(ids.size() == a.size() + c.size());
    }
    // ---- keep / mutate / new
    {
        auto base = json{{"mode", "probability"}, {"density", 0.5}, {"steps", 16}, {"pitchSource", "scale"}, {"range", 14}};
        auto keep = inst("v", "melody", base); keep.seq["varRhythm"] = "keep"; keep.seq["varPitch"] = "new";
        auto a = planRhythm(keep, phr(0, 0, 16, keep), ctx), b = planRhythm(keep, phr(5, 0, 16, keep), ctx);
        CHECK(stepsOf(a) == stepsOf(b));
        auto pa = planPhrase(keep, phr(0, 0, 16, keep), ctx), pb = planPhrase(keep, phr(5, 0, 16, keep), ctx);
        int diff = 0; for(size_t i = 0; i < pa.size() && i < pb.size(); i++) diff += pa[i].degree != pb[i].degree;
        CHECK(diff > 0);
        auto mut = inst("v", "melody", base); mut.seq["varRhythm"] = "mutate"; mut.seq["mutateAmount"] = 0.2;
        int changed = 0, total = 0;
        for(int k = 1; k < 40; k++){
            auto x = planRhythm(mut, phr(k - 1, 0, 64, mut), ctx), y = planRhythm(mut, phr(k, 0, 64, mut), ctx);
            std::vector<int> hx(256, 0), hy(256, 0); for(auto& s : x) hx[(size_t)s.step] = 1; for(auto& s : y) hy[(size_t)s.step] = 1;
            for(int i = 0; i < 256; i++){ total++; changed += hx[(size_t)i] != hy[(size_t)i]; }
        }
        double f = (double)changed / total;   // a redrawn step changes with p = 0.5 -> ~0.1
        CHECK(f > 0.07 && f < 0.13);
        auto nw = inst("v", "melody", base);
        CHECK(stepsOf(planRhythm(nw, phr(0, 0, 16, nw), ctx)) != stepsOf(planRhythm(nw, phr(1, 0, 16, nw), ctx)));
        Instance pv = keep; pv.phrase = {{"variation", {{"rhythm", "new"}}}};
        CHECK(variationOf(pv).rhythm == VarMode::New && variationOf(keep).rhythm == VarMode::Keep);
    }
    // ---- chance scopes
    {
        auto in = inst("c", "hats", {{"steps", 8}, {"hits", 8}, {"chance", 50}, {"chanceScope", "phrase"}});
        int on = 0;
        for(int k = 0; k < 200; k++){ auto h = planRhythm(in, phr(k, 0, 8, in), ctx); CHECK(h.empty() || h.size() == 16); on += !h.empty(); }
        CHECK(on > 70 && on < 130);
        auto gb = inst("g", "hats", {{"steps", 8}, {"hits", 8}, {"chance", 50}, {"chanceScope", "beats"}, {"chanceBeats", 2}});
        bool constGroups = true, restart = true;
        for(int k = 0; k < 20; k++){
            auto a = planRhythm(gb, phr(k, 0, 16, gb), ctx);
            auto b = planRhythm(gb, phr(k, 1.5, 16, gb), ctx);   // starts mid-beat, same seeds
            std::vector<int> ga(8, 0), gbv(8, 0);
            for(auto& x : a) ga[(size_t)(x.start / 2)]++;
            for(auto& x : b) gbv[(size_t)((x.start - 1.5) / 2 + 1e-9)]++;
            for(int g = 0; g < 8; g++){ if(ga[(size_t)g] != 0 && ga[(size_t)g] != 4) constGroups = false; if((ga[(size_t)g] > 0) != (gbv[(size_t)g] > 0)) restart = false; }
        }
        CHECK(constGroups && restart);
        auto sc = inst("s", "hats", {{"steps", 8}, {"hits", 8}, {"chance", 50}, {"chanceScope", "scene"}});
        bool same = true; bool first = !planRhythm(sc, phr(0, 0, 4, sc), ctx).empty();
        for(int k = 1; k < 20; k++) if((!planRhythm(sc, phr(k, 0, 4, sc), ctx).empty()) != first) same = false;
        CHECK(same);
        auto g1 = inst("x1", "hats", {{"steps", 8}, {"hits", 8}, {"chance", 50}, {"chanceScope", "phrase"}, {"chanceGroup", "drums"}});
        auto g2 = inst("x2", "hats", {{"steps", 8}, {"hits", 8}, {"chance", 50}, {"chanceScope", "phrase"}, {"chanceGroup", "drums"}});
        bool shared = true; for(int k = 0; k < 30; k++) if(planRhythm(g1, phr(k, 0, 4, g1), ctx).empty() != planRhythm(g2, phr(k, 0, 4, g2), ctx).empty()) shared = false;
        CHECK(shared);
    }
    // ---- harmony timeline
    {
        Harmony h; h.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{5, 8, -1, ChordColour::Add7}, ProgressionStep{3, 4}, ProgressionStep{4, 4}};
        HarmonyTimeline T(h, 9);
        CHECK(T.at(1).root == 0 && T.at(5).root == 5 && T.at(5).colour == ChordColour::Add7 && T.at(13).root == 3 && T.at(17).root == 4 && T.at(21).root == 0);
        CHECK(T.at(5).stepStart == 4 && T.at(5).stepEnd == 12 && T.at(5).scale != nullptr);
        Harmony r; r.sections[0].steps = {ProgressionStep{0, 4, 16}, ProgressionStep{4, 4, 12}};
        HarmonyTimeline R1(r, 7), R2(r, 7), R3(r, 8);
        bool det = true, quant = true, differs = false;
        for(auto& s : R1.steps(0, 400)){ if(std::fmod(s.end - s.start, 4) > 1e-9) quant = false; }
        auto s1 = R1.steps(0, 400), s2 = R2.steps(0, 400), s3 = R3.steps(0, 400);
        for(size_t i = 0; i < s1.size() && i < s2.size(); i++) if(s1[i].end != s2[i].end) det = false;
        for(size_t i = 0; i < s1.size() && i < s3.size(); i++) if(s1[i].end != s3[i].end) differs = true;
        CHECK(det && quant && differs);
        Harmony ru; ru.sections[0].source = "rules"; ru.sections[0].lengthBeats = 32; ru.sections[0].rules.cadenceDegree = 0; ru.sections[0].rules.tension = 0.8;
        HarmonySection b; b.steps = {ProgressionStep{2, 4}}; ru.sections.push_back(ru.sections[0]);
        HarmonyTimeline RT(ru, 5);
        bool cad = true; std::set<int> used;
        for(Beat sb : {32.0, 64.0, 96.0}){ if(RT.at(sb - 0.5).root != 0) cad = false; }
        for(auto& s : RT.steps(0, 96)) used.insert(s.degree);
        CHECK(cad && used.size() >= 3 && RT.steps(0, 32).size() == 8);
        HarmonyTimeline OT(h, 9);
        HarmonyOverride o; o.hold = true; OT.setOverride(o, 5);
        CHECK(OT.at(14).root == 5 && OT.at(2).root == 0 && std::isinf(OT.at(14).stepEnd));
        HarmonyOverride j; j.jumpDegree = 6; OT.setOverride(j, 20);
        CHECK(OT.at(21).root == 6 && OT.at(14).root == 3);
        Harmony tl; tl.sections[0].source = "timeline";
        HarmonyTimeline TT(tl, 1);
        TT.setTimelineSteps({{8, 3, ChordColour::Sus4}, {0, 1, ChordColour::Triad}, {16, 4, ChordColour::Triad}});
        CHECK(TT.at(2).root == 1 && TT.at(9).root == 3 && TT.at(9).colour == ChordColour::Sus4 && TT.at(500).root == 4);
        CHECK(std::fabs(T.heightOf(7, 0, PitchSpace{}) - PitchSpace{}.heightOf(12)) < 1e-12);
    }
    // ---- phrase lengths, quantisation, restart at sections, look-ahead
    {
        Instance in = inst("ph", "kick", json::object());
        in.phrase = {{"length", json::array({4, 16})}, {"quant", "bars"}};
        PhraseClock pc(in, H0, 1);
        std::set<double> lens; bool ok = true;
        for(int k = 0; k < 100; k++){ const Phrase& p = pc.phrase(k); lens.insert(p.length); if(std::fmod(p.length, 4) > 1e-9 || p.length < 4 || p.length > 16) ok = false; if(k > 0 && p.start != pc.phrase(k - 1).end()) ok = false; }
        CHECK(ok && lens.size() == 4);
        PhraseClock pc2(in, H0, 1); CHECK(pc2.phrase(37).start == pc.phrase(37).start);
        in.phrase = {{"length", json::array({3, 9})}, {"quant", 1.5}};
        PhraseClock pc3(in, H0, 2); bool q = true; for(int k = 0; k < 50; k++){ double l = pc3.phrase(k).length; if(std::fmod(l, 1.5) > 1e-9 || l < 3 || l > 9) q = false; }
        CHECK(q);
        in.phrase = {{"length", 6}, {"quant", "halfBars"}, {"restartAtSections", true}};
        PhraseClock pc4(in, HM, 1);   // sections at 0, 4, 18, 32 ...
        CHECK(pc4.phrase(0).length == 4 && pc4.phrase(1).start == 4);
        in.phrase = {{"length", 8}};
        PhraseClock pc5(in, H0, 1);
        auto la = phrasesToPlan(pc5, 3, 2);
        CHECK(la.size() == 2 && la[0].index == 0 && la[1].index == 1);
        auto lb = phrasesToPlan(pc5, 3, 30);
        CHECK(lb.size() == 5 && lb.back().start < 33);
        LayerView v; CHECK(std::fabs(lookAheadBeats(v, 0.2) - 80) < 1e-9);
    }
    // ---- modulation hook, continuous types, schema
    {
        PlanContext cm = ctx;
        cm.mod = [](const std::string& k, double b, Level, uint64_t){ return k == "seq.hits" ? 5.0 : k == "depth" ? 2.0 : b; };
        auto in = inst("mh", "kick", {{"steps", 8}, {"hits", 1}});
        auto e = planPhrase(in, phr(0, 0, 4, in), cm);
        CHECK(e.size() == 5 && e[0].depth == 2.0);
        auto ms = inst("ms", "mass", {{"hits", 4}, {"steps", 4}});
        auto em = planPhrase(ms, phr(0, 0, 4, ms), ctx);
        CHECK(em.size() == 4 && em[0].kind == EventKind::Mass && !em[0].pitched && em[0].height >= 0.2 && em[0].height <= 0.8);
        int mains = 0; std::set<std::string> keys; bool lv = true;
        for(auto& p : sequencerParams()){ mains += p.main; keys.insert(p.key); if(!(p.levels & p.defaultLevel)) lv = false; }
        CHECK(mains >= 8 && mains <= 12 && keys.size() == sequencerParams().size() && lv);
        CHECK(sequencerParam("octave")->degrees && !(sequencerParam("mode")->levels & LevelEvent));
    }

    // ---- external rhythm: steps given from outside
    {
        ExternalSteps xs;
        xs.set("x", -1, {ExternalStep{0, 1}, ExternalStep{1.5, 0.5, true, 2}, ExternalStep{3, 1, false, 0, 0.25}, ExternalStep{9, 1}});
        xs.set("x", 1, {ExternalStep{2, 1}});
        PlanContext cx = ctx; cx.externalSteps = xs.fn();
        auto mel = inst("x", "melody", {{"mode", "external"}, {"pitchSource", "root"}, {"noteLength", 1.0}});
        auto e = planPhrase(mel, phr(0, 0, 4, mel), cx);
        CHECK(e.size() == 3 && e[0].start == 0 && e[1].start == 1.5 && e[2].start == 3);   // beat 9 is outside
        CHECK(std::fabs(e[0].length - 1.5) < 1e-9 && std::fabs(e[2].length - 0.25) < 1e-9);
        CHECK(std::fabs(e[1].level - 0.4) < 1e-9 && e[1].degree == 2 && e[0].degree == 0);
        auto e1 = planPhrase(mel, phr(1, 4, 4, mel), cx);
        CHECK(e1.size() == 1 && e1[0].start == 6);
        auto kick = inst("x", "kick", {{"mode", "external"}});
        CHECK(planRhythm(kick, phr(0, 0, 4, kick), cx).size() == 3);
        CHECK(planRhythm(kick, phr(0, 0, 4, kick), ctx).empty());   // no source: silent
    }
    // ---- phrase-position slots (effects)
    {
        auto fx = inst("fx", "timefx", {{"mode", "position"}, {"positions", "firstBeat lastBar beforeEnd barLastBeat"}, {"positionBeats", 1}});
        auto e = planPhrase(fx, phr(0, 0, 16, fx), ctx);
        std::vector<std::pair<double, double>> got; for(auto& x : e) got.push_back({x.start, x.length});
        std::vector<std::pair<double, double>> want{{0, 1}, {3, 1}, {7, 1}, {11, 1}, {12, 4}, {15, 1}, {15, 1}};
        CHECK(got == want);
        CHECK(!e.empty() && e[0].kind == EventKind::Effect && e[0].param("slot", -1) == (int)Slot::FirstBeat && e[4].param("slot", -1) == (int)Slot::LastBar);
        CHECK(e[4].param("slotBeats", 0) == 4);
        // before each hit of a followed instance (pre-snare reverse)
        auto pre = inst("rv", "timefx", {{"mode", "position"}, {"positions", "beforeFollowed"}, {"positionBeats", 0.5}, {"followId", "sn"}});
        std::vector<Event> fol(3); fol[0].start = 2; fol[1].start = 6; fol[2].start = 6;
        auto ep = planPhrase(pre, phr(0, 0, 8, pre), ctx, &fol);
        CHECK(ep.size() == 2 && ep[0].start == 1.5 && ep[0].length == 0.5 && ep[1].start == 5.5 && ep[1].param("slot", -1) == (int)Slot::BeforeFollowed);
        CHECK(planPhrase(pre, phr(0, 0, 8, pre), ctx).empty());
    }
    // ---- glide bends
    {
        auto mel = inst("g", "melody", {{"pitchSource", "scale"}, {"steps", 4}, {"hits", 4}, {"glide", 0.5}, {"glideCurve", "sCurve"}, {"range", 12}});
        auto e = planPhrase(mel, phr(0, 0, 4, mel), ctx);
        CHECK(e.size() == 4 && e[0].p.count("glideFrom") == 0);
        CHECK(e[1].param("glideFrom", -99) == e[0].degree && e[1].param("glideTo", -99) == e[1].degree);
        CHECK(e[1].param("glideStart", -1) == e[1].start && e[1].param("glideBeats", -1) == std::min(0.5, e[1].length) && e[1].param("glideCurve", -1) == 3);
        Harmony hh; hh.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
        HarmonyTimeline HT(hh, 1); PlanContext c8; c8.harmony = &HT;
        auto pad = inst("pad", "chords", {{"pitchSource", "chord"}, {"voices", 3}, {"steps", 1}, {"hits", 1}, {"hold", true}, {"voiceLeading", true}});
        auto ev = planPhrase(pad, phr(0, 0, 8, pad), c8);
        CHECK(ev[1].param("glideFrom", -1) == ev[1].degree && ev[1].param("glideCurve", -1) == 1);   // default ease
    }
    // ---- arp pointer reset at chord changes
    {
        Harmony hh; hh.sections[0].steps = {ProgressionStep{0, 4}, ProgressionStep{3, 4}};
        HarmonyTimeline HT(hh, 1); PlanContext c9; c9.harmony = &HT;
        auto arp = inst("ar", "arpeggio", {{"pitchSource", "chord"}, {"voices", 4}, {"notesPerStep", 1}, {"steps", 3}, {"hits", 3}});
        auto e = planPhrase(arp, phr(0, 0, 8, arp), c9);
        CHECK(e.size() == 6 && e[0].degree == 0 && e[3].degree == 3);         // restarts on the new root
        arp.seq["arpReset"] = false;
        auto f = planPhrase(arp, phr(0, 0, 8, arp), c9);
        CHECK(f.size() == 6 && f[3].degree == 3 + 7);                          // carries on (4th voice)
    }
    // ---- event-level reads at the step's beat (modAt)
    {
        PlanContext ca = ctx;
        ca.modAt = [](const std::string& k, double base, Level lv, uint64_t, Beat at){ return k == "seq.level" && lv == LevelEvent ? (at < 2 ? 0.2 : 1.0) : base; };
        auto in = inst("ma", "kick", {{"steps", 4}, {"hits", 4}});
        auto e = planPhrase(in, phr(0, 0, 4, in), ca);
        CHECK(e.size() == 4 && std::fabs(e[0].level - 0.2) < 1e-9 && std::fabs(e[3].level - 1) < 1e-9);
    }

    // ---- rests, bar masks, repetition masks
    {
        auto in = inst("r", "kick", {{"steps", 4}, {"hits", 4}, {"rests", {1, 3}}});
        auto h = planRhythm(in, phr(0, 0, 8, in), ctx);
        CHECK(h.size() == 4 && h[0].step == 0 && h[1].step == 2 && h[2].step == 4);
        auto off = inst("r", "kick", {{"steps", 4}, {"hits", 4}});
        CHECK(planRhythm(off, phr(0, 0, 8, off), ctx).size() == 8);           // defaults unchanged
        auto bm = inst("b", "kick", {{"steps", 4}, {"hits", 4}, {"barMask", "1 0 1 1"}});
        auto hb = planRhythm(bm, phr(0, 0, 32, bm), ctx);                     // 8 bars: 1 0 1 1 1 0 1 1
        CHECK(hb.size() == 24);
        for(auto& x : hb) CHECK(((int)(x.start / 4)) % 4 != 1);
        auto ro = inst("o", "kick", {{"steps", 4}, {"hits", 4}, {"repMask", "odd"}});
        auto ho = planRhythm(ro, phr(0, 0, 16, ro), ctx);
        CHECK(ho.size() == 8 && ho[0].start == 0 && ho[4].start == 8);         // bars 1 and 3
        auto re = inst("o", "kick", {{"steps", 4}, {"hits", 4}, {"repMask", "even"}, {"patternLength", 3}});
        auto he = planRhythm(re, phr(0, 0, 4, re), ctx);                       // 4 steps, reps of 3: only step 3
        CHECK(he.size() == 1 && he[0].step == 3);
        auto pr = inst("pr", "hats", {{"mode", "pattern"}, {"pattern", "xxxx"}, {"steps", 4}, {"rests", {0}}});
        CHECK(planRhythm(pr, phr(0, 0, 4, pr), ctx).size() == 3);
    }
    // ---- phase shift: onsets move later, a lead-in step stays at the phrase start
    {
        auto ps = inst("ps", "mass", {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 1}, {"noteLength", 1.0}, {"phaseShift", 2.0}});
        auto ev = planPhrase(ps, phr(0, 0, 8, ps), ctx);
        CHECK(ev.size() == 3);
        if(ev.size() == 3){ CHECK(ev[0].start == 0 && std::fabs(ev[0].length - 2) < 1e-9); CHECK(ev[1].start == 2 && ev[2].start == 6); }
        auto p0 = inst("p0", "mass", {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 1}, {"noteLength", 1.0}});
        CHECK(planPhrase(p0, phr(0, 0, 8, p0), ctx).size() == 2);   // default unchanged
    }
    // ---- degree offset stays in the scale
    {
        auto in = inst("d", "bass", {{"pitchSource", "root"}, {"steps", 1}, {"hits", 1}});
        auto a = planPhrase(in, phr(0, 0, 4, in), ctx);
        in.seq["degreeOffset"] = 2;
        auto b = planPhrase(in, phr(0, 0, 4, in), ctx);
        CHECK(a.size() == 1 && b.size() == 1 && b[0].degree == a[0].degree + 2);
        CHECK(std::fabs(H0.scaleAt(0).semitonesOf(b[0].degree) - 4) < 1e-9);  // C major: E
        auto ch = inst("dc", "chords", {{"pitchSource", "chord"}, {"voices", 3}, {"steps", 1}, {"hits", 1}, {"degreeOffset", -1}});
        auto c = planPhrase(ch, phr(0, 0, 4, ch), ctx);
        CHECK(c.size() == 3 && c[0].degree == -1 && c[1].degree == 1);
    }
    // ---- mass harmonics of the root
    {
        PlanContext cm = ctx; cm.pitch.lowSemi = -24; cm.pitch.highSemi = 72;
        auto in = inst("m", "mass", {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 1}, {"massPitch", "harmonics"}, {"harmonics", 4}, {"harmonicFade", 1}});
        in.follow = Follow::Chord;
        auto e = planPhrase(in, phr(0, 0, 4, in), cm);
        CHECK(e.size() == 4);
        if(e.size() == 4){
            CHECK(std::fabs(e[0].height - cm.pitch.heightOf(0)) < 1e-9);
            CHECK(std::fabs(e[1].height - cm.pitch.heightOf(12)) < 1e-9);      // 2nd harmonic: octave
            CHECK(std::fabs(e[2].height - cm.pitch.heightOf(12 * std::log2(3.0))) < 1e-9);
            CHECK(e[1].level < e[0].level && e[3].level < e[2].level && e[3].param("harmonic", 0) == 4);
        }
        in.seq["harmonicStep"] = 2;
        auto o = planPhrase(in, phr(0, 0, 4, in), cm);
        CHECK(o.size() == 4 && o[1].param("harmonic", 0) == 3);
        auto band = inst("m", "mass", {{"mode", "pattern"}, {"pattern", "x"}, {"steps", 1}});
        CHECK(planPhrase(band, phr(0, 0, 4, band), cm).size() == 1);
    }

    std::printf("test_sequencer: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
