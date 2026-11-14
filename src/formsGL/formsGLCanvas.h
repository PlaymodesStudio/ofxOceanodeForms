//
//  formsGLCanvas.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  The sketch's canvas on the CPU: an opaque RGB image (Processing's offscreen renders are RGB),
//  drawn the way Processing's Java2D renderer draws (measured, tests/formsGL/j2dpoint): a point
//  of stroke weight 1 is NOT anti-aliased (Java2D's stroke normalization): it fills the pixel
//  (floor(x + snap), floor(y + snap); snap ~0.001 px, none with ROUND), at the stroke alpha with the PROJECT cap, at 181/255 of it with ROUND, and
//  not at all with SQUARE.
//  16 bits per channel (2026-11-14): the pixels hold 16-bit colour and alpha, and a stamp's alpha
//  and coverage stay fractional until they are blended. With Java2D's 8-bit arithmetic a faint
//  stamp's alpha was a whole level (0, 1, 2 of 255) and its colour rounded per channel, so a row
//  of overlapping stamps fading out went down in steps (bands tens of pixels wide, and their hue
//  drifting: 255,192,0 at alpha 1 adds 1,1,0). Colours and alphas are still given on the 0..255
//  scale (alpha as a float); geometry and coverage are as measured from Java2D.
//  (FastDraw.pde's diamond model, used only by requests that ask for "fastDraw", is pointAA.)
//  No openFrameworks: it renders on any thread and in the headless tests.
//

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <thread>
#include <vector>
#include <unordered_map>
#include <stdexcept>

namespace formsgl {
struct RasterError : std::runtime_error { using std::runtime_error::runtime_error; };

class Canvas {
public:
    int w = 0, h = 0;
    typedef uint64_t Px;        // 0xAAAA'RRRR'GGGG'BBBB: 16 bits per channel
    std::vector<Px> px;
    static constexpr Px BLACK = 0xFFFF000000000000ull;   // opaque black
    static inline int pxA(Px p){ return (int)(p >> 48); }
    static inline int pxR(Px p){ return (int)((p >> 32) & 0xFFFF); }
    static inline int pxG(Px p){ return (int)((p >> 16) & 0xFFFF); }
    static inline int pxB(Px p){ return (int)(p & 0xFFFF); }
    static inline Px mkPx(int a, int r, int g, int b){ return ((Px)(uint16_t)a << 48) | ((Px)(uint16_t)r << 32) | ((Px)(uint16_t)g << 16) | (Px)(uint16_t)b; }
    static inline Px withA(Px p, int a){ return (p & 0x0000FFFFFFFFFFFFull) | ((Px)(uint16_t)a << 48); }
    // an 8-bit colour (0xRRGGBB, any alpha bits ignored) as an opaque pixel / a channel 0..255 as 16 bits
    static inline int c16(int c8){ return std::min(255, std::max(0, c8)) * 257; }
    static inline Px fromRGB8(uint32_t rgb){ return mkPx(65535, (int)((rgb >> 16) & 255) * 257, (int)((rgb >> 8) & 255) * 257, (int)(rgb & 255) * 257); }
    // an alpha on the 0..255 scale (fractional) as 16 bits
    static inline int a16(float a){ return a <= 0 ? 0 : a >= 255 ? 65535 : (int)(a * 257.0f + 0.5f); }

    bool layer = false;         // a transparent ARGB layer (straight alpha), as createGraphics() makes
    void allocate(int W, int H){ w = W; h = H; layer = false; px.assign((size_t)W * H, BLACK); }
    void allocateLayer(int W, int H){ w = W; h = H; layer = true; px.assign((size_t)W * H, (Px)0); }
    void background(uint32_t rgb){ std::fill(px.begin(), px.end(), fromRGB8(rgb)); }

    // a * b / 65535, rounded (a, b in 0..65535)
    static inline uint32_t mul16(uint32_t a, uint32_t b){ uint64_t t = (uint64_t)a * b + 32768u; return (uint32_t)((t + (t >> 16)) >> 16); }
    // (a * x + (65535 - a) * y) / 65535, rounded: one rounding for the mix
    static inline int mix16(uint32_t a, uint32_t x, uint32_t y){ uint64_t t = (uint64_t)a * x + (uint64_t)(65535u - a) * y + 32768u; return (int)((t + (t >> 16)) >> 16); }

    // Source-over onto a non-premultiplied ARGB pixel (r, g, b, a: 16 bits)
    inline void blendLayer(size_t i, int r, int g, int b, int a){
        Px d = px[i];
        int dA = pxA(d);
        if(a >= 65535 || dA == 0){ px[i] = mkPx(a, r, g, b); return; }
        uint32_t dFA = mul16(65535u - (uint32_t)a, (uint32_t)dA);   // what is left of the destination
        uint32_t rA = (uint32_t)a + dFA;
        if(rA == 0){ px[i] = 0; return; }
        const double inv = 1.0 / rA;
        auto ch = [&](int s, int dc){ return (int)std::min(65535.0, ((double)s * a + (double)dc * dFA) * inv + 0.5); };
        px[i] = mkPx((int)std::min(65535u, rA), ch(r, pxR(d)), ch(g, pxG(d)), ch(b, pxB(d)));
    }
    // rows split over the CPU cores (each pixel on its own: the same result as in order)
    template<class F> static void parallelFor(size_t n, F f, size_t minWork = 200000){
        unsigned T = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        if(n < minWork || T == 1){ f((size_t)0, n); return; }
        std::vector<std::thread> th;
        size_t step = (n + T - 1) / T;
        for(unsigned t = 0; t < T; t++){ size_t a = t * step, b = std::min(n, a + step); if(a < b) th.emplace_back([=, &f]{ f(a, b); }); }
        for(auto& x : th) x.join();
    }
    // image(layer, 0, 0) onto this (opaque) canvas: straight-alpha source-over
    void drawLayer(const Canvas& L){
        size_t n = std::min(px.size(), L.px.size());
        parallelFor(n, [&](size_t i0, size_t i1){ for(size_t i = i0; i < i1; i++){
            Px s = L.px[i];
            int a = pxA(s);
            if(a == 0) continue;
            if(layer){ // Cover silhouettes retain geometric alpha, including black ink.
                blendLayer(i, pxR(s), pxG(s), pxB(s), a);
                continue;
            }
            Px d = px[i];
            px[i] = mkPx(65535, mix16(a, pxR(s), pxR(d)), mix16(a, pxG(s), pxG(d)), mix16(a, pxB(s), pxB(d)));
        } });
    }

    bool addMode = false;   // blendMode(ADD): the colour x alpha is added, pinned at white
    inline void blendAdd(size_t i, int r, int g, int b, int a){
        Px d = px[i];
        px[i] = mkPx(std::min(65535, pxA(d) + a), std::min(65535, pxR(d) + (int)mul16(r, a)), std::min(65535, pxG(d) + (int)mul16(g, a)), std::min(65535, pxB(d) + (int)mul16(b, a)));
    }
    // r, g, b: 0..255; a: 0..255, fractional (a stamp's alpha x its coverage, never rounded to a level)
    inline void blend(size_t i, int r, int g, int b, float a){
        const int A = a16(a);
        if(A <= 0) return;
        const int R = c16(r), G = c16(g), B = c16(b);
        if(addMode){ blendAdd(i, R, G, B, A); return; }
        if(layer){ blendLayer(i, R, G, B, A); return; }
        Px d = px[i];
        px[i] = mkPx(65535, mix16(A, R, pxR(d)), mix16(A, G, pxG(d)), mix16(A, B, pxB(d)));
    }

    // Coverage (0..255) of the 45-degree unit square centred at (fx, fy) in [0, 1) on the 3 x 3
    // pixels around it, per 1/64 offset (FastDraw.pde fdCoverage; its decimal literals are floats,
    // as the Processing preprocessor makes them). Computed once for all offsets: thread safe.
    static const std::array<int, 9>& coverage(float fx, float fy){
        int qx = std::min(63, std::max(0, (int)(fx * 64))), qy = std::min(63, std::max(0, (int)(fy * 64)));
        return covTable()[qy * 64 + qx];
    }
    enum Cap { ROUND = 0, PROJECT = 1, SQUARE = 2 };
    // The columns points and axis lines may touch, [penX0, penX1): a note is kept inside its own
    // time span. A stamp wider than a pixel is centred on its column, so without this a thick note
    // spills before its start and past its end (by up to its thickness), and whatever cuts on the
    // beat (a blank, a gate, a repeat) leaves that spill as a stray column one or two pixels wide.
    int penX0 = -(1 << 30), penX1 = 1 << 30;
    static float snapOf(int cap){ return cap == ROUND ? 0.0f : 0.00105f; }
    // One Java2D point of weight 1
    void point(float x, float y, int r, int g, int b, float a, int cap){
        if(a <= 0 || cap == SQUARE) return;
        // Java2D snaps a coordinate less than ~0.001 px below a whole pixel up to it with the PROJECT
        // and SQUARE caps (not with ROUND): measured, the same in x and y
        const float SNAP = snapOf(cap);
        int ix = (int)std::floor(x + SNAP), iy = (int)std::floor(y + SNAP);
        if(ix < 0 || iy < 0 || ix >= w || iy >= h || ix < penX0 || ix >= penX1) return;
        blend((size_t)iy * w + ix, r, g, b, cap == ROUND ? a * (181.0f / 255.0f) : a);
    }

    // A horizontal or vertical line of weight 1 (measured): its ends snap to pixel centres; the
    // pixels between them are filled; the end pixels get the cap: PROJECT whole, SQUARE half,
    // ROUND 218/255. A zero-length line is a point. Other directions are not used by the ported
    // generators (returns false).
    bool lineAxis(float x0, float y0, float x1, float y1, int r, int g, int b, float a, int cap){
        const float SNAP = snapOf(cap);
        if(a <= 0) return true;
        bool vertical = x0 == x1, horizontal = y0 == y1;
        if(!vertical && !horizontal) return false;
        const float endA = a * ((cap == PROJECT ? 255 : cap == SQUARE ? 128 : 218) / 255.0f);
        if(vertical){
            int cx = (int)std::floor(x0 + SNAP), p = (int)std::floor(y0 + SNAP), q = (int)std::floor(y1 + SNAP);
            if(p > q) std::swap(p, q);
            if(p == q && horizontal){ point(x0, y0, r, g, b, a, cap); return true; }
            if(cx < 0 || cx >= w || cx < penX0 || cx >= penX1) return true;
            for(int k = std::max(0, p); k <= std::min(h - 1, q); k++)
                blend((size_t)k * w + cx, r, g, b, (k == p || k == q) ? endA : a);
        }else{
            int cy = (int)std::floor(y0 + SNAP), p = (int)std::floor(x0 + SNAP), q = (int)std::floor(x1 + SNAP);
            if(p > q) std::swap(p, q);
            if(cy < 0 || cy >= h) return true;
            size_t row = (size_t)cy * w;
            for(int k = std::max(std::max(0, penX0), p); k <= std::min(std::min(w, penX1) - 1, q); k++)
                blend(row + k, r, g, b, (k == p || k == q) ? endA : a);
        }
        return true;
    }

    // A point thicker than 1 (measured): centred on the pixel centre it snaps to; PROJECT is an
    // axis-aligned square with box coverage (within 1 level of Java2D), ROUND a disc (coverage by
    // 16 x 16 samples: close to Java2D, not exact), SQUARE draws nothing
    void thickPoint(float x, float y, float wgt, int r, int g, int b, float a, int cap){
        const float SNAP = snapOf(cap);
        if(a <= 0 || cap == SQUARE) return;
        float cx = std::floor(x + SNAP) + 0.5f, cy = std::floor(y + SNAP) + 0.5f, hw = wgt / 2;
        int x0 = std::max(std::max(0, penX0), (int)std::floor(cx - hw)), x1 = std::min(std::min(w, penX1) - 1, (int)std::ceil(cx + hw));
        int y0 = std::max(0, (int)std::floor(cy - hw)), y1 = std::min(h - 1, (int)std::ceil(cy + hw));
        for(int yy = y0; yy <= y1; yy++) for(int xx = x0; xx <= x1; xx++){
            float cov;   // 0..1
            if(cap == PROJECT){
                float ox = std::max(0.0f, std::min(xx + 1.0f, cx + hw) - std::max((float)xx, cx - hw));
                float oy = std::max(0.0f, std::min(yy + 1.0f, cy + hw) - std::max((float)yy, cy - hw));
                cov = ox * oy;
            }else{
                // the 16 x 16 samples depend only on the pixel's offset from the (pixel-centred)
                // point and on hw: counted once per width and kept (the same arithmetic, exact at
                // these magnitudes, so the same coverage)
                const RoundKernel& K = roundKernel(hw);
                int ky = yy - (int)(cy - 0.5f) - K.o, kx = xx - (int)(cx - 0.5f) - K.o;
                cov = (ky >= 0 && ky < K.n && kx >= 0 && kx < K.n) ? K.cov[(size_t)ky * K.n + kx] / 256.0f : 0.0f;
            }
            if(cov > 0) blend((size_t)yy * w + xx, r, g, b, cov * a);
        }
    }

    // A thick point that is one stamp of a row of stamps `step` pixels apart (a note drawn at more
    // than 1080 lines: weight and step both grow with the image). Separate squares or discs overlap
    // unevenly there (stripes every `step` pixels, scalloped edges). This draws the point swept
    // across its own step instead: each pixel gets the share `len / step` of the sweep that covers
    // it, and the alpha that share of a stamp gives (1 - (1 - a)^share), so a row of stamps adds up
    // to the same opacity everywhere and the envelope glides from one stamp to the next. The cells
    // of consecutive stamps meet on whole pixels.
    // span (0 = the whole step): this call draws only a piece of the step, `span` pixels wide and
    // centred on x, at its own height: a note whose pitch moves along its length (vibrato, a slide)
    // is drawn as several pieces per step, each where the curve is there, instead of one flat
    // block per step (stair-steps as wide as the step at large sizes). The shares are still those
    // of the whole step, so the pieces of one step add up to the same opacity.
    void sweptPoint(float x, float y, float wgt, float step, int r, int g, int b, float a, int cap, float span = 0){
        if(a <= 0 || cap == SQUARE || step <= 0) return;
        const float SNAP = snapOf(cap);
        float cy = std::floor(y + SNAP) + 0.5f, hw = wgt / 2;
        const float reach = span > 0 ? span : step;
        int X0 = (int)std::floor(x - reach / 2 + 0.5f), X1 = (int)std::floor(x + reach / 2 + 0.5f);
        if(X1 <= X0) X1 = X0 + 1;
        // a stamp narrower than its step (a thin line) still fills the step along x: only its height is thin
        float widen = hw < step / 2 ? step / (2 * hw) : 1;
        float wmax = std::max(1e-6f, std::min((float)(X1 - X0), 2 * hw * widen) / step);
        // the alpha a share of the sweep gives, for a stamp of alpha `af` (0..1): a table per alpha
        float lut[65], rowLut[65], lutFor = -1;
        auto fill = [&](float* t, float af){
            for(int k = 0; k <= 64; k++){
                float wk = wmax * k / 64;
                t[k] = addMode ? std::min(1.0f, af * wk) : (af >= 0.998f ? (k ? 1.0f : 0.0f) : 1 - std::pow(1 - af, wk));
            }
        };
        fill(lut, a / 255.0f);
        int y0 = std::max(0, (int)std::floor(cy - hw)), y1 = std::min(h - 1, (int)std::ceil(cy + hw));
        for(int yy = y0; yy <= y1; yy++){
            float c, covy;
            if(cap == PROJECT){
                c = hw;
                covy = std::max(0.0f, std::min(yy + 1.0f, cy + hw) - std::max((float)yy, cy - hw));
            }else{
                float dy = yy + 0.5f - cy;
                c = std::sqrt(std::max(0.0f, hw * hw - dy * dy));
                covy = std::max(0.0f, std::min(1.0f, hw - std::fabs(dy) + 0.5f));
            }
            if(covy <= 0 || c <= 0) continue;
            c *= widen;
            // a partly covered row (the stamp's top and bottom edge) is a fainter stamp: its own table
            const float* t = lut;
            if(covy < 0.999f){ float af = a / 255.0f * covy; if(af != lutFor){ fill(rowLut, af); lutFor = af; } t = rowLut; }
            int xs = std::max(std::max(0, penX0), (int)std::floor(X0 - c)), xe = std::min(std::min(w, penX1) - 1, (int)std::ceil(X1 + c));
            size_t row = (size_t)yy * w;
            for(int xx = xs; xx <= xe; xx++){
                float pc = xx + 0.5f;
                float len = std::min((float)X1, pc + c) - std::max((float)X0, pc - c);
                if(len <= 0) continue;
                float covx = std::max(0.0f, std::min(1.0f, std::min(xx + 1.0f, X1 + c) - std::max((float)xx, X0 - c)));
                int k = std::min(64, (int)(len / step / wmax * 64 + 0.5f));
                if(k == 0 && a >= 255 && covy >= 0.999f) k = 1;   // opaque: any share covers
                float A = t[k] * covx * 255;
                if(A > 0) blend(row + xx, r, g, b, A);
            }
        }
    }

    // coverage of a ROUND point of half-width hw, per pixel offset from its pixel (kernel n x n,
    // offsets o .. o + n - 1), made with the same sampling as the loop it replaces
    struct RoundKernel { int o = 0, n = 0; std::vector<int> cov; };
    std::vector<std::pair<float, RoundKernel>> roundKernels;   // per canvas (one thread)
    std::unordered_map<float, size_t> roundIndex;
    const RoundKernel& roundKernel(float hw){
        auto f = roundIndex.find(hw);
        if(f != roundIndex.end() && f->second < roundKernels.size()) return roundKernels[f->second].second;
        if(roundKernels.size() >= 4096){ roundKernels.clear(); roundIndex.clear(); }
        RoundKernel K;
        float cx = 0.5f, cy = 0.5f;   // a point in pixel 0
        K.o = (int)std::floor(cx - hw);
        int last = (int)std::ceil(cx + hw);
        K.n = last - K.o + 1;
        K.cov.assign((size_t)K.n * K.n, 0);
        const float r2 = hw * hw;
        for(int yy = K.o; yy <= last; yy++) for(int xx = K.o; xx <= last; xx++){
            // the samples' squared distances grow with |dx| and |dy| (float rounding is monotone):
            // if the farthest is inside, all 256 are; if the nearest is outside, none is
            float ax = xx + 0.5f / 16 - cx, bx = xx + 15.5f / 16 - cx, ay = yy + 0.5f / 16 - cy, by = yy + 15.5f / 16 - cy;
            float fx = std::max(ax * ax, bx * bx), fy = std::max(ay * ay, by * by);
            float nx = (ax <= 0 && bx >= 0) ? 0.0f : std::min(ax * ax, bx * bx), ny = (ay <= 0 && by >= 0) ? 0.0f : std::min(ay * ay, by * by);
            int n = 0;
            if(fx + fy <= r2) n = 256;
            else if(!(nx + ny > r2) || (ax <= 0 && bx >= 0) || (ay <= 0 && by >= 0)){
                for(int sy = 0; sy < 16; sy++) for(int sx = 0; sx < 16; sx++){
                    float dx = xx + (sx + 0.5f) / 16 - cx, dy = yy + (sy + 0.5f) / 16 - cy;
                    if(dx * dx + dy * dy <= r2) n++;
                }
            }
            K.cov[(size_t)(yy - K.o) * K.n + (xx - K.o)] = n;   // samples inside, of 256
        }
        roundIndex[hw] = roundKernels.size();
        roundKernels.push_back({hw, std::move(K)});
        return roundKernels.back().second;
    }

    // ---- stroked paths (polylines, flattened curves, diagonal lines): Java2D draws a stroke as one
    // anti-aliased shape (its outline: the segments, MITER joins, the caps) composited once. Here:
    // vertices snapped to pixel centres (Java2D's stroke normalization), the outline polygons
    // filled together (nonzero winding = their union) with 8 x 8 samples per pixel, then blended.
    struct Pt { float x, y; };
    static float snapPC(float v, int cap){ return std::floor(v + snapOf(cap)) + 0.5f; }
    void strokePath(std::vector<Pt> pts, float width, int cap, int r, int g, int b, float a, bool normalize = true, bool closed = false){
        if(a <= 0 || pts.empty() || width <= 0) return;
        if(normalize) for(auto& p : pts){ p.x = snapPC(p.x, cap); p.y = snapPC(p.y, cap); }
        // drop repeated points
        std::vector<Pt> P;
        for(auto& p : pts) if(P.empty() || std::fabs(p.x - P.back().x) > 1e-6f || std::fabs(p.y - P.back().y) > 1e-6f) P.push_back(p);
        if(closed && P.size() > 2){
            if(std::fabs(P.front().x - P.back().x) > 1e-6f || std::fabs(P.front().y - P.back().y) > 1e-6f) P.push_back(P.front());
            P.push_back(P[1]);   // so the last vertex gets its join; no caps on a closed path
            cap = SQUARE;
        }
        float hw = width / 2;
        std::vector<std::vector<Pt>> polys;
        auto addPoly = [&](std::vector<Pt> q){
            // counter-clockwise, so overlapping pieces add up (union under nonzero)
            float area = 0;
            for(size_t i = 0; i < q.size(); i++){ auto& u = q[i]; auto& v = q[(i + 1) % q.size()]; area += u.x * v.y - v.x * u.y; }
            if(area < 0) std::reverse(q.begin(), q.end());
            if(std::fabs(area) > 1e-9f) polys.push_back(std::move(q));
        };
        auto disc = [&](Pt c){
            std::vector<Pt> q;
            for(int k = 0; k < 24; k++){ float t = k * 6.2831853f / 24; q.push_back({c.x + hw * std::cos(t), c.y + hw * std::sin(t)}); }
            addPoly(q);
        };
        if(P.size() == 1){
            if(cap == ROUND) disc(P[0]);
            else if(cap == PROJECT) addPoly({{P[0].x - hw, P[0].y - hw}, {P[0].x + hw, P[0].y - hw}, {P[0].x + hw, P[0].y + hw}, {P[0].x - hw, P[0].y + hw}});
        }
        for(size_t i = 0; i + 1 < P.size(); i++){
            Pt p0 = P[i], p1 = P[i + 1];
            float dx = p1.x - p0.x, dy = p1.y - p0.y, len = std::sqrt(dx * dx + dy * dy);
            if(len < 1e-6f) continue;
            float ux = dx / len, uy = dy / len, nx = -uy * hw, ny = ux * hw;
            // the caps: PROJECT extends the first and last segment by half the width
            if(cap == PROJECT){
                if(i == 0){ p0.x -= ux * hw; p0.y -= uy * hw; }
                if(i + 2 == P.size()){ p1.x += ux * hw; p1.y += uy * hw; }
            }
            if(!(closed && i + 2 == P.size()))   // closed: the extra segment only gives the join
                addPoly({{p0.x + nx, p0.y + ny}, {p1.x + nx, p1.y + ny}, {p1.x - nx, p1.y - ny}, {p0.x - nx, p0.y - ny}});
            // MITER join with the next segment (limit 10, else bevel)
            if(i + 2 < P.size()){
                Pt q = P[i + 2];
                float ex = q.x - P[i + 1].x, ey = q.y - P[i + 1].y, el = std::sqrt(ex * ex + ey * ey);
                if(el > 1e-6f){
                    float vx = ex / el, vy = ey / el;
                    float cross = ux * vy - uy * vx;
                    if(std::fabs(cross) > 1e-6f){
                        float s = cross > 0 ? -1.0f : 1.0f;   // the outer side
                        Pt c = P[i + 1];
                        Pt a1 = {c.x + s * -uy * hw, c.y + s * ux * hw}, a2 = {c.x + s * -vy * hw, c.y + s * vx * hw};
                        float cosT = ux * vx + uy * vy;
                        float miter = 1.0f / std::sqrt(std::max(1e-6f, (1 + cosT) / 2));
                        if(miter <= 10){
                            float mx = (-uy + -vy), my = (ux + vx), ml = std::sqrt(mx * mx + my * my);
                            Pt m = {c.x + s * mx / ml * hw * miter, c.y + s * my / ml * hw * miter};
                            addPoly({c, a1, m, a2});
                        }else addPoly({c, a1, a2});
                    }
                }
            }
        }
        if(cap == ROUND && P.size() > 1 && !closed){ disc(P.front()); disc(P.back()); }
        fillUnion(polys, r, g, b, a);
    }
    // A cubic Bezier as a stroke: the end points snapped like the vertices, the control points moved
    // with their end point, flattened finely
    void strokeBezier(Pt a0, Pt c0, Pt c1, Pt a1, float width, int cap, int r, int g, int b, float a){
        Pt s0 = {snapPC(a0.x, cap), snapPC(a0.y, cap)}, s1 = {snapPC(a1.x, cap), snapPC(a1.y, cap)};
        c0.x += s0.x - a0.x; c0.y += s0.y - a0.y; c1.x += s1.x - a1.x; c1.y += s1.y - a1.y;
        float est = std::fabs(c0.x - s0.x) + std::fabs(c0.y - s0.y) + std::fabs(c1.x - c0.x) + std::fabs(c1.y - c0.y) + std::fabs(s1.x - c1.x) + std::fabs(s1.y - c1.y);
        int n = std::max(8, std::min(4096, (int)(est / 2)));
        std::vector<Pt> pts;
        for(int k = 0; k <= n; k++){
            float t = k / (float)n, u = 1 - t;
            pts.push_back({u * u * u * s0.x + 3 * u * u * t * c0.x + 3 * u * t * t * c1.x + t * t * t * s1.x,
                           u * u * u * s0.y + 3 * u * u * t * c0.y + 3 * u * t * t * c1.y + t * t * t * s1.y});
        }
        strokePath(pts, width, cap, r, g, b, a, false);
    }
    // a filled shape (no stroke normalization for fills, as Java2D)
    void fillPolygon(const std::vector<Pt>& poly, int r, int g, int b, float a){ fillUnion({poly}, r, g, b, a); }
    void fillPolygons(const std::vector<std::vector<Pt>>& polys, int r, int g, int b, float a){ fillUnion(polys, r, g, b, a); }
    // the union of polygons, 8 x 8 samples per pixel, composited once
    void fillUnion(const std::vector<std::vector<Pt>>& polys, int r, int g, int b, float a){
        if(polys.empty()) return;
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        for(auto& q : polys) for(auto& p : q){ x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
        int px0 = std::max(0, (int)std::floor(x0)), px1 = std::min(w - 1, (int)std::ceil(x1));
        int py0 = std::max(0, (int)std::floor(y0)), py1 = std::min(h - 1, (int)std::ceil(y1));
        if(px0 > px1 || py0 > py1) return;
        const int S = 8;
        int bw = px1 - px0 + 1, bh = py1 - py0 + 1, rows = bh * S;
        struct X { float x; int d; };
        std::vector<std::vector<X>> cross(rows);
        for(auto& q : polys){
            for(size_t i = 0; i < q.size(); i++){
                Pt u = q[i], v = q[(i + 1) % q.size()];
                if(u.y == v.y) continue;
                int d = v.y > u.y ? 1 : -1;
                float ya = std::min(u.y, v.y), yb = std::max(u.y, v.y);
                int r0 = std::max(0, (int)std::ceil((ya - py0) * S - 0.5f)), r1 = std::min(rows - 1, (int)std::ceil((yb - py0) * S - 0.5f) - 1);
                for(int rr = r0; rr <= r1; rr++){
                    float sy = py0 + (rr + 0.5f) / S;
                    float t = (sy - u.y) / (v.y - u.y);
                    cross[rr].push_back({u.x + t * (v.x - u.x), d});
                }
            }
        }
        std::vector<uint16_t> cov((size_t)bw * bh, 0);
        for(int rr = 0; rr < rows; rr++){
            auto& c = cross[rr];
            if(c.size() < 2) continue;
            std::sort(c.begin(), c.end(), [](const X& p, const X& q){ return p.x < q.x; });
            int wind = 0;
            uint16_t* row = &cov[(size_t)(rr / S) * bw];
            for(size_t k = 0; k + 1 < c.size(); k++){
                wind += c[k].d;
                if(wind == 0) continue;
                // samples at px0 + (s + 0.5) / S inside [c[k].x, c[k+1].x)
                int s0 = std::max(0, (int)std::ceil((c[k].x - px0) * S - 0.5f));
                int s1 = std::min(bw * S, (int)std::ceil((c[k + 1].x - px0) * S - 0.5f));
                for(int s = s0; s < s1; s++) row[s / S]++;
            }
        }
        for(int yy = 0; yy < bh; yy++) for(int xx = 0; xx < bw; xx++){
            int n = cov[(size_t)yy * bw + xx];
            if(!n) continue;
            blend((size_t)(py0 + yy) * w + px0 + xx, r, g, b, std::min(1.0f, n / (float)(S * S)) * a);
        }
    }

    // One point (FastDraw fdPoint: the anti-aliased diamond)
    void pointAA(float x, float y, int r, int g, int b, float a){
        if(a <= 0) return;
        const auto& cov = coverage(x - std::floor(x), y - std::floor(y));
        int ix = (int)std::floor(x) - 1, iy = (int)std::floor(y) - 1;
        for(int py = 0; py < 3; py++){
            int yy = iy + py;
            if(yy < 0 || yy >= h) continue;
            size_t row = (size_t)yy * w;
            for(int pxx = 0; pxx < 3; pxx++){
                int c = cov[py * 3 + pxx];
                if(c == 0) continue;
                int xx = ix + pxx;
                if(xx < 0 || xx >= w) continue;
                blend(row + xx, r, g, b, c * a / 255.0f);
            }
        }
    }

    // A filled axis-aligned rectangle, anti-aliased on fractional edges (box coverage)
    void fillRect(float x, float y, float rw, float rh, int r, int g, int b, float a){
        if(a <= 0 || rw <= 0 || rh <= 0) return;
        float x1 = x + rw, y1 = y + rh;
        int ix0 = std::max(0, (int)std::floor(x)), ix1 = std::min(w, (int)std::ceil(x1));
        int iy0 = std::max(0, (int)std::floor(y)), iy1 = std::min(h, (int)std::ceil(y1));
        for(int yy = iy0; yy < iy1; yy++){
            float cy = std::min((float)yy + 1, y1) - std::max((float)yy, y);
            for(int xx = ix0; xx < ix1; xx++){
                float cx = std::min((float)xx + 1, x1) - std::max((float)xx, x);
                blend((size_t)yy * w + xx, r, g, b, cx * cy * a);
            }
        }
    }

    // Straight alpha, as the sketch writes its PNGs (Render.pde toStraightAlpha): a = max(R,G,B),
    // colour = round(c x 65535 / a); a = 0 -> transparent. RGBA, 16 bits per channel.
    std::vector<uint16_t> straightAlpha() const {
        std::vector<uint16_t> out((size_t)w * h * 4);
        parallelFor(px.size(), [&](size_t i0, size_t i1){ for(size_t i = i0; i < i1; i++){
            int r = pxR(px[i]), g = pxG(px[i]), b = pxB(px[i]);
            int a = std::max(r, std::max(g, b));
            uint16_t* o = &out[i * 4];
            if(a == 0){ o[0] = o[1] = o[2] = o[3] = 0; continue; }
            if(a == 65535){ o[0] = (uint16_t)r; o[1] = (uint16_t)g; o[2] = (uint16_t)b; o[3] = 65535; continue; }
            const double k = 65535.0 / a;
            o[0] = (uint16_t)std::min(65535, (int)(r * k + 0.5)); o[1] = (uint16_t)std::min(65535, (int)(g * k + 0.5)); o[2] = (uint16_t)std::min(65535, (int)(b * k + 0.5));
            o[3] = (uint16_t)a;
        } });
        return out;
    }

private:
    static const std::vector<std::array<int, 9>>& covTable(){
        static const std::vector<std::array<int, 9>> t = []{
            std::vector<std::array<int, 9>> v(64 * 64);
            float r = std::sqrt(2.0f) / 2;
            const int S = 16;
            for(int qy = 0; qy < 64; qy++) for(int qx = 0; qx < 64; qx++){
                float cx = 1 + (qx + 0.5f) / 64.0f, cy = 1 + (qy + 0.5f) / 64.0f;
                auto& c = v[qy * 64 + qx];
                for(int py = 0; py < 3; py++) for(int pxx = 0; pxx < 3; pxx++){
                    int n = 0;
                    for(int sy = 0; sy < S; sy++) for(int sx = 0; sx < S; sx++){
                        float x = pxx + (sx + 0.5f) / S, y = py + (sy + 0.5f) / S;
                        if(std::fabs(x - cx) + std::fabs(y - cy) <= r) n++;
                    }
                    c[py * 3 + pxx] = (int)std::floor(n * 255.0f / (S * S) + 0.5f);
                }
            }
            return v;
        }();
        return t;
    }
};

} // namespace formsgl
