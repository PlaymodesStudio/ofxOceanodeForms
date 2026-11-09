// test_modulation.cpp — headless tests for v2Modulation.h
#include "v2Modulation.h"
#include <array>
#include <cstdio>

using namespace forms2;

static int passed = 0, failed = 0;
#define CHECK(c) do{ if(c) passed++; else { failed++; std::printf("FAIL %s:%d  %s\n", __FILE__, __LINE__, #c); } }while(0)
static bool near(double a, double b, double e = 1e-6){ return std::fabs(a - b) < e; }

static std::map<std::string, ParamDef> defs;
static ParamDef mk(const std::string& k, double mn, double mx, unsigned lv, bool deg = false){
    ParamDef d; d.key = k; d.min = mn; d.max = mx; d.levels = lv; d.degrees = deg; return d;
}
static ModContext baseCtx(){
    ModContext c;
    c.paramDef = [](const std::string& k) -> const ParamDef* { auto f = defs.find(k); return f == defs.end() ? nullptr : &f->second; };
    c.sceneSeed = 77;
    return c;
}
static Routing R(const std::string& src, const std::string& dest, double amt, Level lv, json sp = json::object()){
    Routing r; r.source = src; r.dest = dest; r.amount = amt; r.level = lv; r.sourceParams = sp; return r;
}
static double src(const Routing& r, Beat t, const Phrase& ph, const ModContext& c, const Event* ev = nullptr){
    ModPoint p; p.t = t; p.phrase = &ph; p.event = ev; return sourceValue(r, p, c);
}

int main(){
    unsigned all = LevelEvent | LevelPhrase | LevelLayer | LevelScene;
    defs["x"] = mk("x", 0, 1, all);
    defs["depth"] = mk("depth", 0.2, 3, LevelEvent | LevelPhrase | LevelLayer);
    defs["size"] = mk("size", 0, 10, all);
    defs["bright"] = mk("bright", 0, 1, all);
    defs["layer.zoom"] = mk("layer.zoom", 1, 64, LevelLayer | LevelPhrase);
    defs["pitch.degree"] = mk("pitch.degree", -14, 14, all, true);
    defs["pitch.fine"] = mk("pitch.fine", -2, 2, LevelLayer | LevelEvent);   // semitones, not degrees
    defs["level"] = mk("level", 0, 1, all);
    ModContext c = baseCtx();
    Phrase ph; ph.start = 16; ph.length = 8; ph.seed = 1234;

    // ---- LFO shapes (rate 4 beats, reset per phrase: phase 0 at beat 16)
    auto lfo = [&](const char* shape){ return R("lfo1", "x", 1, LevelLayer, {{"shape", shape}, {"rateBeats", 4}}); };
    CHECK(near(src(lfo("sine"), 16, ph, c), 0.5)); CHECK(near(src(lfo("sine"), 17, ph, c), 1)); CHECK(near(src(lfo("sine"), 19, ph, c), 0));
    CHECK(near(src(lfo("triangle"), 17, ph, c), 0.5)); CHECK(near(src(lfo("triangle"), 18, ph, c), 1));
    CHECK(near(src(lfo("saw"), 19, ph, c), 0.75));
    CHECK(near(src(lfo("square"), 17, ph, c), 1)); CHECK(near(src(lfo("square"), 19, ph, c), 0));
    { auto r = lfo("smoothRandom"); double a = src(r, 16, ph, c), b = src(r, 16, ph, c), m = src(r, 18, ph, c);
      CHECK(a == b); CHECK(a >= 0 && a <= 1 && m >= 0 && m <= 1); }
    // reset vs free: phrase starting at 17 shifts the reset LFO, not the free one
    { Phrase p2 = ph; p2.start = 17; auto rf = lfo("saw"); rf.sourceParams["reset"] = "free";
      CHECK(near(src(lfo("saw"), 18, p2, c), 0.25)); CHECK(near(src(rf, 18, p2, c), 0.5)); }
    { auto r = R("lfo2", "x", 1, LevelLayer, {{"shape", "saw"}, {"ratePhrases", 1}}); CHECK(near(src(r, 20, ph, c), 0.5)); }
    { auto r = lfo("square"); r.sourceParams["bipolar"] = true; CHECK(near(src(r, 19, ph, c), -1)); }
    { auto r = lfo("saw"); r.sourceParams["phase"] = 0.5; CHECK(near(src(r, 16, ph, c), 0.5)); }

    // ---- phrase envelopes
    auto env = [&](json sp){ return R("phraseEnv1", "x", 1, LevelLayer, sp); };
    CHECK(near(src(env({{"shape", "rise"}}), 18, ph, c), 0.25));
    CHECK(near(src(env({{"shape", "fall"}}), 18, ph, c), 0.75));
    CHECK(near(src(env({{"shape", "swell"}}), 20, ph, c), 1));
    CHECK(near(src(env({{"shape", "steps"}, {"steps", 4}}), 19, ph, c), 1.0 / 3));
    CHECK(near(src(env({{"shape", "steps"}, {"steps", 4}}), 23.9, ph, c), 1));
    { json pts = json::array({json::array({0, 0}), json::array({0.5, 1}), json::array({1, 0.2})});
      auto r = env({{"shape", "drawn"}, {"points", pts}});
      CHECK(near(src(r, 18, ph, c), 0.5)); CHECK(near(src(r, 20, ph, c), 1)); CHECK(near(src(r, 24, ph, c), 0.2)); }
    { auto r = env({{"shape", "drawn"}, {"points", {0.0, 1.0, 0.0}}}); CHECK(near(src(r, 20, ph, c), 1)); CHECK(near(src(r, 22, ph, c), 0.5)); }

    // ---- note envelopes
    { Event ev; ev.start = 10; ev.length = 2; auto r = R("noteEnv1", "x", 1, LevelLayer, {{"shape", "fall"}});
      CHECK(near(src(r, 10, ph, c, &ev), 1)); CHECK(near(src(r, 11, ph, c, &ev), 0.5)); CHECK(near(src(r, 13, ph, c, &ev), 0)); }

    // ---- random scopes
    {
        auto re = R("random1", "x", 1, LevelEvent, {{"scope", "event"}});
        Event e1, e2; e1.seed = 11; e2.seed = 12;
        double a = src(re, 0, ph, c, &e1), b = src(re, 0, ph, c, &e2);
        CHECK(a != b); CHECK(a == src(re, 5, ph, c, &e1));
        auto rp = R("random1", "x", 1, LevelPhrase, {{"scope", "phrase"}});
        Phrase q = ph; q.seed = 999;
        CHECK(src(rp, 16, ph, c) == src(rp, 23, ph, c)); CHECK(src(rp, 16, ph, c) != src(rp, 16, q, c));
        auto rb = R("random1", "x", 1, LevelLayer, {{"scope", "beats"}, {"everyBeats", 3}});
        CHECK(src(rb, 16, ph, c) == src(rb, 18.9, ph, c)); CHECK(src(rb, 16, ph, c) != src(rb, 19, ph, c));
        Phrase n = ph; n.start = 24;   // same seed (kept), new start: groups restart
        CHECK(src(rb, 16, ph, c) == src(rb, 24, n, c));
        auto rs = R("random1", "x", 1, LevelScene, {{"scope", "scene"}});
        CHECK(src(rs, 0, ph, c) == src(rs, 100, q, c));
        ModContext c2 = baseCtx(); c2.sceneSeed = 78; CHECK(src(rs, 0, ph, c) != src(rs, 0, ph, c2));
        auto rw = rb; rw.sourceParams["smooth"] = true;   // smooth walk: continuous at cell borders
        CHECK(near(src(rw, 18.999, ph, c), src(rw, 19.0, ph, c), 1e-3));
    }

    // ---- level semantics through the engine
    {
        Modulation m;
        m.add("a", 0, R("random1", "x", 1, LevelEvent, {{"scope", "event"}}));
        m.add("a", 0, R("lfo1", "size", 0.5, LevelPhrase, {{"shape", "saw"}, {"rateBeats", 4}}));
        m.add("a", 0, R("lfo1", "level", 1, LevelLayer, {{"shape", "saw"}, {"rateBeats", 4}}));
        m.add("", -1, R("random9", "bright", 1, LevelScene, {{"scope", "scene"}}));
        Event e1; e1.instance = "a"; e1.seed = 5; e1.start = 17;
        Event e2 = e1; e2.seed = 6;
        CHECK(m.evalEvent("x", 0, e1, ph, c) != m.evalEvent("x", 0, e2, ph, c));
        CHECK(m.evalEvent("x", 0, e1, ph, c) == m.evalEvent("x", 0, e1, ph, c));
        Event eb = e1; eb.instance = "b"; CHECK(m.evalEvent("x", 0, eb, ph, c) == 0);  // other instance
        CHECK(near(m.evalPhrase("size", 0, "a", ph, c), 0));      // saw at phrase start
        CHECK(m.evalPhrase("x", 0.3, "a", ph, c) == 0.3);           // event routing not read per phrase
        c.phrase = ph;
        CHECK(near(m.evalLayer("level", 0, 0, 17, c), 0.25)); CHECK(near(m.evalLayer("level", 0, 0, 18, c), 0.5));
        CHECK(near(m.evalLayer("level", 0, 1, 18, c), 0));          // other layer
        double s1 = m.evalScene("bright", 0, "a", c); CHECK(s1 > 0 && s1 == m.evalScene("bright", 0, "z", c));
        // hook
        auto h = m.hook("a", 0, c);
        CHECK(h("x", 0, LevelEvent, 5) == m.evalEvent("x", 0, e1, ph, c));
        CHECK(near(h("size", 0, LevelPhrase, 0), 0));
        // matrix
        auto rows = m.matrix("a", c); CHECK(rows.size() == 4); CHECK(rows[2].badge == "L");
        CHECK(m.matrix("", c).size() == 1);
    }

    // ---- amount, range, curve, invert, clamp, summing, degrees
    {
        Modulation m; c.phrase = ph;
        auto r = R("macro:a", "size", 0.5, LevelPhrase); m.add("i", 0, r);
        c.macros["a"] = 0.5;
        CHECK(near(m.evalPhrase("size", 2, "i", ph, c), 4.5));     // 2 + 0.5*0.5*10
        Modulation m2; auto rn = r; rn.amount = -1; m2.add("i", 0, rn);
        CHECK(near(m2.evalPhrase("size", 2, "i", ph, c), 0));      // clamped at min
        Modulation m3; auto rc = r; rc.amount = 1; rc.curve = 2; m3.add("i", 0, rc);
        CHECK(near(m3.evalPhrase("size", 0, "i", ph, c), 2.5));
        Modulation m4; auto ri = rc; ri.invert = true; ri.curve = 1; c.macros["a"] = 0.2; m4.add("i", 0, ri);
        CHECK(near(m4.evalPhrase("size", 0, "i", ph, c), 8));
        Modulation m5; auto rr = r; rr.amount = 1; rr.hasRange = true; rr.min = 2; rr.max = 4; c.macros["a"] = 0.5; m5.add("i", 0, rr);
        CHECK(near(m5.evalPhrase("size", 9, "i", ph, c), 3));
        Modulation m6; m6.add("i", 0, R("macro:a", "size", 0.2, LevelPhrase)); m6.add("i", 0, R("macro:b", "size", 0.4, LevelPhrase));
        c.macros["a"] = 1; c.macros["b"] = 0.5;
        CHECK(near(m6.evalPhrase("size", 1, "i", ph, c), 1 + 2 + 2));
        Modulation m7; m7.add("i", 0, R("macro:a", "pitch.degree", 0.1, LevelPhrase)); c.macros["a"] = 0.5;
        CHECK(m7.evalPhrase("pitch.degree", 0, "i", ph, c) == 1);  // 1.4 -> 1
        Modulation m8; m8.add("i", 0, R("macro:a", "size", 2.0, LevelPhrase)); c.macros["a"] = 1;
        CHECK(near(m8.evalPhrase("size", 5, "i", ph, c), 10));
    }

    // ---- disallowed level reported and ignored
    {
        Modulation m; m.add("i", 0, R("macro:a", "layer.zoom", 1, LevelEvent)); m.add("i", 0, R("nope", "size", 1, LevelPhrase));
        m.add("i", 0, R("macro:a", "missing", 1, LevelPhrase));
        auto is = m.check(c); CHECK(is.size() == 3);
        Event e; e.instance = "i"; CHECK(m.evalEvent("layer.zoom", 16, e, ph, c) == 16);
        auto rows = m.matrix("i", c); CHECK(!rows[0].valid);
    }

    // ---- smoothing glide
    {
        Modulation m; auto r = R("macro:a", "x", 1, LevelLayer); r.smooth = 1; m.add("i", 0, r);
        c.macros["a"] = 0; CHECK(near(m.evalLayer("x", 0, 0, 0, c), 0));
        c.macros["a"] = 1; double v1 = m.evalLayer("x", 0, 0, 1, c);
        CHECK(near(v1, 1 - std::exp(-1.0), 1e-9));
        double v2 = m.evalLayer("x", 0, 0, 10, c); CHECK(v2 > 0.999);
        Modulation n; auto r0 = r; r0.smooth = 0; n.add("i", 0, r0); CHECK(near(n.evalLayer("x", 0, 0, 1, c), 1));
    }

    // ---- followers, brightness, arc, lanes
    {
        ModContext k = baseCtx(); k.phrase = ph;
        k.follower = [](const std::string& id, const std::string& what, double win, Beat){ return id == "kick" && what == "density" && win == 8 ? 0.6 : 0.0; };
        k.brightness = [](Beat t){ return t > 5 ? 1.0 : 0.0; };
        k.arc = [](Beat t){ return t / 100; };
        k.lane = [](const std::string& n, Beat){ return n == "swell" ? 0.25 : 0.0; };
        Modulation m;
        m.add("", -1, R("follow:kick:density", "x", 1, LevelLayer, {{"window", 8}}));
        CHECK(near(m.evalLayer("x", 0, 0, 0, k), 0.6));
        Modulation b; b.add("", -1, R("brightness", "x", 1, LevelLayer)); CHECK(b.evalLayer("x", 0, 0, 6, k) == 1); CHECK(b.evalLayer("x", 0, 0, 1, k) == 0);
        Modulation a; a.add("", -1, R("arc", "x", 1, LevelLayer)); CHECK(near(a.evalLayer("x", 0, 0, 50, k), 0.5));
        Modulation l; l.add("", -1, R("lane:swell", "x", 1, LevelLayer)); CHECK(near(l.evalLayer("x", 0, 0, 0, k), 0.25));
    }

    // ---- per-event depth example: random per event -> depth 0.2..3, size and brightness agree
    {
        Modulation m;
        auto rd = R("random1", "depth", 1, LevelEvent, {{"scope", "event"}}); rd.hasRange = true; rd.min = 0.2; rd.max = 3;
        m.add("stars", 1, rd);
        m.add("stars", 1, R("random1", "size", 1, LevelEvent, {{"scope", "event"}}));
        m.add("stars", 1, R("random1", "bright", 1, LevelEvent, {{"scope", "event"}}));
        bool inRange = true, agree = true; double pd = -1, ps = -1, pb = -1;
        std::vector<std::array<double, 3>> v;
        for(int i = 0; i < 50; i++){
            Event e; e.instance = "stars"; e.seed = mixSeed(42, (uint64_t)i); e.start = i;
            double d = m.evalEvent("depth", 1, e, ph, c), s = m.evalEvent("size", 0, e, ph, c), b = m.evalEvent("bright", 0, e, ph, c);
            if(d < 0.2 || d > 3) inRange = false;
            v.push_back({d, s, b});
        }
        for(auto& x : v) for(auto& y : v) if((x[0] < y[0]) != (x[1] < y[1]) || (x[1] < y[1]) != (x[2] < y[2])) agree = false;
        (void)pd; (void)ps; (void)pb;
        CHECK(inRange); CHECK(agree);
    }

    // ---- render: drawn vs animated (vibrato on fine pitch, tremolo on level)
    {
        Modulation m;
        m.add("v", 0, R("lfo1", "pitch.fine", 0.25, LevelLayer, {{"shape", "sine"}, {"rateBeats", 1}, {"bipolar", true}, {"reset", "free"}}));
        m.add("v", 0, R("lfoT", "level", -1, LevelLayer, {{"shape", "square"}, {"rateBeats", 2}, {"reset", "free"}, {"render", "animated"}}));
        Event e; e.instance = "v"; e.start = 8; e.length = 2;
        // drawn vibrato: along the event's own time, not quantised (fine pitch is continuous)
        CHECK(near(m.evalDrawn("pitch.fine", 0, e, ph, 0.125, c), 1));     // beat 8.25: sine peak, 0.25*1*4
        CHECK(near(m.evalDrawn("pitch.fine", 0, e, ph, 0.375, c), -1));
        CHECK(near(m.evalDrawnBeats("pitch.fine", 0, e, ph, 0.25, c), 1));
        double half = m.evalDrawn("pitch.fine", 0, e, ph, 1.0 / 24, c); CHECK(half != std::round(half));
        // drawn ignores the animated tremolo; animated ignores the drawn vibrato
        CHECK(near(m.evalDrawn("level", 1, e, ph, 0.1, c), 1));
        CHECK(near(m.evalAnimated("pitch.fine", 0, e, ph, 8.25, c), 0));
        // animated tremolo depends on the clock only
        CHECK(near(m.evalAnimated("level", 1, e, ph, 4.5, c), 0)); CHECK(near(m.evalAnimated("level", 1, e, ph, 5.5, c), 1));
        CHECK(m.evalAnimated("level", 1, e, ph, 4.5, c) == m.evalAnimated("level", 1, e, ph, 6.5, c));
    }

    // ---- playhead distance (animated: scoop, swell, ring-out)
    {
        Modulation m;
        m.add("p", 0, R("playhead", "level", 1, LevelLayer, {{"shape", "peak"}, {"window", 2}, {"render", "animated"}}));
        m.add("p", 0, R("playheadA", "size", 1, LevelLayer, {{"type", "playhead"}, {"shape", "approach"}, {"window", 4}, {"render", "animated"}}));
        m.add("p", 0, R("playheadR", "x", 1, LevelLayer, {{"type", "playhead"}, {"shape", "ringout"}, {"window", 2}, {"render", "animated"}}));
        Event e; e.instance = "p"; e.start = 10; e.length = 1;
        CHECK(near(m.evalAnimated("level", 0, e, ph, 10, c), 1)); CHECK(near(m.evalAnimated("level", 0, e, ph, 9, c), 0.5));
        CHECK(near(m.evalAnimated("level", 0, e, ph, 11, c), 0.5)); CHECK(near(m.evalAnimated("level", 0, e, ph, 7, c), 0));
        CHECK(near(m.evalAnimated("size", 0, e, ph, 4, c), 0)); CHECK(near(m.evalAnimated("size", 0, e, ph, 8, c), 5));
        CHECK(near(m.evalAnimated("size", 0, e, ph, 12, c), 10));
        CHECK(near(m.evalAnimated("x", 0, e, ph, 9, c), 0)); CHECK(near(m.evalAnimated("x", 0, e, ph, 11, c), 0.5));
        CHECK(near(m.evalAnimated("x", 0, e, ph, 13, c), 0));
        // drawn: the position within the event against ctx.now
        Modulation d; d.add("p", 0, R("playhead", "level", 1, LevelLayer, {{"shape", "distance"}, {"window", 1}}));
        ModContext k = c; k.now = 10.5;
        CHECK(near(d.evalDrawn("level", 0, e, ph, 0.5, k), 0.5)); CHECK(near(d.evalDrawn("level", 0, e, ph, 1, k), 0.75));
    }

    // ---- destination forms: "<id>:<dest>", "layerN.<k>", "look.<k>" (and the engine's pre-normalised form)
    {
        Scene sc;
        Instance a; a.id = "a"; a.layer = 1; Instance b; b.id = "b"; b.layer = 2;
        a.routings.push_back(R("macro:m", "look.size", 1, LevelPhrase));
        sc.instances = {a, b};
        sc.routings.push_back(R("macro:m", "b:size", 0.5, LevelPhrase));
        sc.routings.push_back(R("macro:m", "layer2.zoom", 0.5, LevelLayer));
        sc.routings.push_back(R("macro:m", "a:x", 1, LevelLayer));
        ModContext k = baseCtx(); k.macros["m"] = 1;
        Modulation m; m.setScene(sc);
        CHECK(near(m.evalPhrase("size", 0, "b", ph, k), 5));          // scene routing applies to b only
        CHECK(near(m.evalPhrase("size", 0, "a", ph, k), 10));         // look.size == size
        CHECK(near(m.evalPhrase("look.size", 0, "a", ph, k), 10));
        CHECK(near(m.evalLayer("layer.zoom", 1, 2, 0, k), 32.5));     // layer2.zoom on layer 2
        CHECK(near(m.evalLayer("layer.zoom", 1, 1, 0, k), 1));
        CHECK(near(m.evalLayer("x", 0, 1, 0, k), 1)); CHECK(near(m.evalLayer("x", 0, 2, 0, k), 0));
        CHECK(m.check(k).empty());
        // pre-normalised (as the engine adds them) gives the same values
        Modulation n;
        n.add("a", 1, R("macro:m", "size", 1, LevelPhrase));
        n.add("b", 2, R("macro:m", "size", 0.5, LevelPhrase));
        n.add("@layer2", 2, R("macro:m", "layer.zoom", 0.5, LevelLayer));
        n.add("a", 1, R("macro:m", "x", 1, LevelLayer));
        CHECK(near(n.evalPhrase("size", 0, "b", ph, k), 5)); CHECK(near(n.evalPhrase("size", 0, "a", ph, k), 10));
        CHECK(near(n.evalLayer("layer.zoom", 1, 2, 0, k), 32.5)); CHECK(near(n.evalLayer("layer.zoom", 1, 1, 0, k), 1));
        CHECK(near(n.evalLayer("x", 0, 1, 0, k), 1));
        // add() with scene-form dests; "<id>:" before the instance is known learns its layer later
        Modulation q;
        q.add("", -1, R("macro:m", "a:x", 1, LevelLayer));
        q.add("", -1, R("macro:m", "layer3.zoom", 1, LevelLayer));
        q.add("a", 1, R("macro:m", "look.bright", 1, LevelLayer));
        CHECK(near(q.evalLayer("x", 0, 1, 0, k), 1)); CHECK(near(q.evalLayer("layer.zoom", 1, 3, 0, k), 64));
        CHECK(near(q.evalLayer("bright", 0, 1, 0, k), 1));
    }

    // ---- hook: event-level reads at the event's start
    {
        Modulation m; m.add("h", 0, R("phraseEnv", "x", 1, LevelEvent, {{"shape", "rise"}}));
        ModContext k = baseCtx(); k.phrase = ph; k.now = 16;
        auto old = m.hook("h", 0, k); auto at = m.hookAt("h", 0, k);
        CHECK(near(old("x", 0, LevelEvent, 5), 0));                    // read at ctx.now
        CHECK(near(at("x", 0, LevelEvent, 5, 20), 0.5));                // read at beat 20
        CHECK(near(at("x", 0, LevelEvent, 5, NAN), 0));
    }

    // ---- index / caches: fast path == reference path, unrouted dests give the clamped base
    {
        Modulation m; m.setCacheDefs(true);
        m.add("a", 0, R("lfo1", "look.size", 0.3, LevelLayer, {{"shape", "sine"}, {"rateBeats", 3}, {"bipolar", true}}));
        m.add("a", 0, R("lfo2", "size", 0.2, LevelLayer, {{"shape", "saw"}, {"rateBeats", 2}, {"render", "animated"}}));
        m.add("", -1, R("random1", "size", 0.1, LevelEvent, {{"scope", "event"}}));
        m.add("b", 1, R("noteEnv1", "pitch.fine", 1, LevelLayer, {{"shape", "fall"}}));
        CHECK(m.entriesFor("look.size") && m.entriesFor("size")->size() == 3 && !m.entriesFor("x"));
        CHECK(m.all()[1].animated && !m.all()[0].animated && m.all()[0].bipolar && m.all()[0].pure);
        CHECK(m.hasAnimatedFor("a") && !m.hasAnimatedFor("b") && m.drawnPure("a"));
        ModContext k = baseCtx(); k.phrase = ph;
        Event e; e.instance = "a"; e.seed = 9; e.start = 17; e.length = 2;
        bool same = true;
        for(double u : {0.0, 0.4, 1.0}) for(const char* d : {"size", "look.size", "x", "pitch.fine", "unknown"}) for(double base : {-1.0, 0.5, 20.0}){
            v2SlowPath() = false;
            double f1 = m.evalDrawn(d, base, e, ph, u, k), f2 = m.evalAnimated(d, base, e, ph, 17 + u, k), f3 = m.evalEvent(d, base, e, ph, k);
            v2SlowPath() = true;
            double s1 = m.evalDrawn(d, base, e, ph, u, k), s2 = m.evalAnimated(d, base, e, ph, 17 + u, k), s3 = m.evalEvent(d, base, e, ph, k);
            same = same && f1 == s1 && f2 == s2 && f3 == s3;
        }
        v2SlowPath() = false;
        CHECK(same);
        CHECK(m.evalAnimated("x", 5, e, ph, 17, k) == 1);          // no routing: base clamped
        CHECK(m.evalAnimated("unknown", 5, e, ph, 17, k) == 5);    // unknown dest: base
    }

    std::printf("test_modulation: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
