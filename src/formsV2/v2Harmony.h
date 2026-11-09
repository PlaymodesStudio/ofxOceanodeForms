//
//  v2Harmony.h
//  ofxOceanodeForms — ESPECTRES v2 (Forms GL2 Box)
//
//  Resolves the authored Harmony (v2Core.h) into a concrete timeline for one run (ROADMAP 5.9,
//  5.10): sections laid end to end (the last loops), random step lengths and the rules source drawn
//  from the scene seed, timeline steps injected from outside, the live override applied on query.
//  Also the meter side: bar starts / indices, strong beats, section boundaries.
//

#pragma once

#include "v2Core.h"
#include <limits>

namespace forms2 {

// A step injected by the "timeline" source (the Oceanode chord track later). Beats are absolute.
struct TimelineStep {
    Beat startBeat = 0;
    int degree = 0;
    ChordColour colour = ChordColour::Triad;
    int keyShift = 0;
};

// A resolved section span and a resolved progression step
struct SectionSpan { int index = 0; Beat start = 0, end = 0; int pass = 0; double barBeats = 4; int firstBar = 0; };
struct StepSpan { Beat start = 0, end = 0; int degree = 0; ChordColour colour = ChordColour::Triad; int section = 0; int keyShift = 0; };

class HarmonyTimeline {
public:
    HarmonyTimeline(){ resolve(Harmony{}, 1); }
    HarmonyTimeline(const Harmony& h, uint64_t sceneSeed){ resolve(h, sceneSeed); }

    // Keeps its own copy of the harmony (HarmonyAt::scale points into it)
    void resolve(const Harmony& h, uint64_t sceneSeed){
        h_ = h;
        if(h_.sections.empty()) h_.sections.push_back(HarmonySection{});
        seed_ = mixSeed(sceneSeed, hashTag("harmony"));
        holdBeat_ = 0; jumpBeat_ = 0;
        rebuild();
    }

    // Timeline source: steps for the sections whose source is "timeline" (absolute beats)
    void setTimelineSteps(std::vector<TimelineStep> steps){
        std::stable_sort(steps.begin(), steps.end(), [](const TimelineStep& a, const TimelineStep& b){ return a.startBeat < b.startBeat; });
        timeline_ = std::move(steps);
        rebuild();
    }

    // Live override, applied on query from the beat it is engaged at
    void setOverride(const HarmonyOverride& o, Beat atBeat){
        if(o.hold && !h_.override_.hold) holdBeat_ = atBeat;
        if(o.jumpDegree != h_.override_.jumpDegree) jumpBeat_ = atBeat;
        h_.override_ = o;
    }
    void clearOverride(){ h_.override_ = HarmonyOverride{}; }

    const Harmony& harmony() const { return h_; }

    // ---------------------------------------------------------------- queries
    HarmonyAt at(Beat b) const {
        const auto& ov = h_.override_;
        Beat q = (ov.hold && b >= holdBeat_) ? holdBeat_ : b;
        const StepSpan& s = stepAt(q);
        HarmonyAt r;
        r.section = s.section;
        r.scale = &h_.sections[(size_t)s.section].scale;
        r.keyShift = s.keyShift;
        r.root = s.degree;
        r.colour = s.colour;
        r.stepStart = s.start;
        r.stepEnd = s.end;
        if(ov.hold && b >= holdBeat_) r.stepEnd = std::numeric_limits<double>::infinity();
        if(ov.jumpDegree > -1000 && b >= jumpBeat_) r.root = ov.jumpDegree;
        return r;
    }
    const SectionSpan& sectionAt(Beat b) const {
        ensure(b);
        for(size_t i = spans_.size(); i-- > 0;) if(b >= spans_[i].start) return spans_[i];
        return spans_.front();
    }
    const HarmonySection& sectionDef(Beat b) const { return h_.sections[(size_t)sectionAt(b).index]; }
    const Meter& meterAt(Beat b) const { return sectionDef(b).meter; }
    const Scale& scaleAt(Beat b) const { return sectionDef(b).scale; }   // section scale (no step key shift)
    int keyShiftAt(Beat b) const { return stepAt(b).keyShift; }
    double barBeatsAt(Beat b) const { return sectionAt(b).barBeats; }
    Beat barStart(Beat b) const {
        const auto& s = sectionAt(b);
        return s.start + std::floor((b - s.start) / s.barBeats + 1e-9) * s.barBeats;
    }
    int barIndex(Beat b) const {
        const auto& s = sectionAt(b);
        return s.firstBar + (int)std::floor((b - s.start) / s.barBeats + 1e-9);
    }
    // the meter's strong beats (first beat of each group); off-beat positions are never strong
    bool isStrongBeat(Beat b) const {
        Beat bs = barStart(b);
        double off = b - bs, r = std::round(off);
        if(std::fabs(off - r) > 1e-6) return false;
        return meterAt(b).strong((int)r);
    }
    // section starts inside [a, b)
    std::vector<Beat> sectionBoundaries(Beat a, Beat b) const {
        ensure(b);
        std::vector<Beat> out;
        for(auto& s : spans_) if(s.start >= a && s.start < b) out.push_back(s.start);
        return out;
    }
    // bar starts inside [a, b)
    std::vector<Beat> barStarts(Beat a, Beat b) const {
        std::vector<Beat> out;
        Beat x = barStart(a);
        while(x < b){ if(x >= a - 1e-9) out.push_back(x); x += barBeatsAt(x + 1e-9); if(sectionAt(x).start > x) x = sectionAt(x).start; }
        return out;
    }
    // resolved steps overlapping [a, b)
    std::vector<StepSpan> steps(Beat a, Beat b) const {
        ensure(b);
        std::vector<StepSpan> out;
        for(auto& s : steps_) if(s.end > a && s.start < b) out.push_back(s);
        return out;
    }
    // degree -> height through the scale's tuning
    double heightOf(int degree, Beat b, const PitchSpace& ps) const { return ps.heightOf(scaleAt(b).semitonesOf(degree) + keyShiftAt(b)); }

private:
    Harmony h_;
    uint64_t seed_ = 0;
    std::vector<TimelineStep> timeline_;
    Beat holdBeat_ = 0, jumpBeat_ = 0;
    mutable std::vector<SectionSpan> spans_;
    mutable std::vector<StepSpan> steps_;
    mutable int nextSection_ = 0, lastPass_ = 0;
    static constexpr double kOpenEnd = 1e9;

    void rebuild(){
        spans_.clear(); steps_.clear(); nextSection_ = 0; lastPass_ = 0;
        ensure(64);
    }
    const StepSpan& stepAt(Beat b) const {
        ensure(b);
        if(b < steps_.front().start) return steps_.front();
        auto it = std::upper_bound(steps_.begin(), steps_.end(), b, [](Beat x, const StepSpan& s){ return x < s.start; });
        return *(it - 1);
    }
    static double roundUpBars(double len, double bb){ return std::max(bb, std::ceil(len / bb - 1e-9) * bb); }

    // lays out sections until the timeline passes beat b
    void ensure(Beat b) const {
        while(spans_.empty() || (spans_.back().end <= b && spans_.back().end < kOpenEnd)) appendSection();
    }
    void appendSection() const {
        int n = (int)h_.sections.size();
        int idx = std::min(nextSection_, n - 1);
        int pass = nextSection_ >= n - 1 ? lastPass_++ : 0;
        nextSection_++;
        const HarmonySection& sec = h_.sections[(size_t)idx];
        double bb = sec.meter.barBeats();
        SectionSpan sp; sp.index = idx; sp.pass = pass; sp.barBeats = bb;
        sp.start = spans_.empty() ? 0 : spans_.back().end;
        sp.firstBar = spans_.empty() ? 0 : spans_.back().firstBar + (int)std::llround((spans_.back().end - spans_.back().start) / spans_.back().barBeats);
        uint64_t sseed = mixSeed(seed_, (uint64_t)idx + 1, (uint64_t)pass + 1);
        std::vector<StepSpan> st;
        auto push = [&](double len, int deg, ChordColour c, int ks = 0){
            StepSpan s; s.start = st.empty() ? sp.start : st.back().end; s.end = s.start + len;
            s.degree = deg; s.colour = c; s.section = idx; s.keyShift = ks; st.push_back(s);
        };
        if(sec.source == "rules"){
            const auto& R = sec.rules;
            double stepLen = std::max(1e-3, R.stepBeats);
            double L = roundUpBars(sec.lengthBeats > 0 ? sec.lengthBeats : stepLen * 4, bb);
            int count = std::max(1, (int)std::ceil(L / stepLen - 1e-9));
            int sz = sec.scale.size(), cur = 0;
            Rng rng(streamSeed(sseed, Stream::Pitch));
            for(int i = 0; i < count; i++){
                int deg = cur;
                if(i > 0) deg = rulesNext(cur, sz, R, rng);
                if(i == count - 1 && R.cadenceDegree >= 0) deg = R.cadenceDegree;
                double len = std::min(stepLen, L - stepLen * i);
                push(len, deg, ChordColour::Triad);
                cur = deg;
            }
            sp.end = sp.start + L;
        } else if(sec.source == "timeline"){
            bool last = idx == n - 1;
            double L = sec.lengthBeats > 0 ? roundUpBars(sec.lengthBeats, bb) : (last ? kOpenEnd : bb * 4);
            sp.end = sp.start + L;
            StepSpan s0; s0.start = sp.start; s0.end = sp.end; s0.section = idx; st.push_back(s0);
            for(auto& t : timeline_){
                if(t.startBeat < sp.start || t.startBeat >= sp.end) continue;
                if(t.startBeat <= st.back().start + 1e-9){ st.back().degree = t.degree; st.back().colour = t.colour; st.back().keyShift = t.keyShift; continue; }
                st.back().end = t.startBeat;
                StepSpan s; s.start = t.startBeat; s.end = sp.end; s.degree = t.degree; s.colour = t.colour; s.keyShift = t.keyShift; s.section = idx;
                st.push_back(s);
            }
        } else {   // typed
            std::vector<ProgressionStep> P = sec.steps.empty() ? std::vector<ProgressionStep>{ProgressionStep{}} : sec.steps;
            auto stepLen = [&](size_t i, int cycle){
                const auto& p = P[i];
                double len = std::max(1e-3, p.length);
                if(p.lengthMax > p.length){
                    Rng r(mixSeed(sseed, 0x57E9 + (uint64_t)cycle, i));
                    double lo = roundUpBars(p.length, bb), hi = std::max(lo, std::floor(p.lengthMax / bb + 1e-9) * bb);
                    len = std::clamp(std::round(r.uniform(lo, hi + bb) / bb - 0.5) * bb, lo, hi);   // whole bars, uniform
                }
                return len;
            };
            if(sec.lengthBeats > 0){
                double L = roundUpBars(sec.lengthBeats, bb), used = 0;
                for(int cycle = 0; used < L - 1e-9; cycle++)
                    for(size_t i = 0; i < P.size() && used < L - 1e-9; i++){
                        double len = std::min(stepLen(i, cycle), L - used);
                        push(len, P[i].degree, P[i].colour, P[i].keyShift); used += len;
                    }
                sp.end = sp.start + L;
            } else {
                double used = 0;
                for(size_t i = 0; i < P.size(); i++){ double len = stepLen(i, 0); push(len, P[i].degree, P[i].colour, P[i].keyShift); used += len; }
                double L = roundUpBars(used, bb);
                st.back().end += L - used;
                sp.end = sp.start + L;
            }
        }
        spans_.push_back(sp);
        steps_.insert(steps_.end(), st.begin(), st.end());
    }

    // Markov step with tension: low tension prefers near / stable degrees, high far / unstable ones
    static int rulesNext(int from, int n, const HarmonyRules& R, Rng& rng){
        std::vector<double> w((size_t)n, 1.0);
        double total = 0;
        for(int to = 0; to < n; to++){
            double base = 1;
            if(!R.table.empty() && (size_t)from < R.table.size() && (size_t)to < R.table[(size_t)from].size()) base = R.table[(size_t)from][(size_t)to];
            int d = std::abs(to - from); d = std::min(d, n - d);
            bool stable = (to == 0 || to == 4 % n || to == 3 % n);
            double t = std::clamp(R.tension, 0.0, 1.0);
            double f = std::pow(1.0 + d, 2 * t - 1) * (stable ? (1.5 - t) : (0.5 + t));
            if(to == from) f *= 0.15;
            w[(size_t)to] = std::max(0.0, base) * f;
            total += w[(size_t)to];
        }
        if(total <= 0) return from;
        double x = rng.uniform() * total;
        for(int to = 0; to < n; to++){ x -= w[(size_t)to]; if(x < 0) return to; }
        return n - 1;
    }
};

} // namespace forms2
