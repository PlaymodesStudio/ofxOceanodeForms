//
//  formsGLCanvas.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  The sketch's canvas on the CPU: an opaque RGB image (Processing's offscreen renders are RGB),
//  drawn the way Processing's Java2D renderer draws (measured, tests/formsGL/j2dpoint): a point
//  of stroke weight 1 is NOT anti-aliased (Java2D's stroke normalization): it fills the pixel
//  (floor(x + snap), floor(y + snap); snap ~0.001 px, none with ROUND), at the stroke alpha with the PROJECT cap, at 181/255 of it with ROUND, and
//  not at all with SQUARE. Everything blends source-over with Java2D's 8-bit arithmetic.
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

namespace formsgl {

class Canvas {
public:
    int w = 0, h = 0;
    std::vector<uint32_t> px;   // 0xFFRRGGBB

    bool layer = false;         // a transparent ARGB layer (straight alpha), as createGraphics() makes
    void allocate(int W, int H){ w = W; h = H; layer = false; px.assign((size_t)W * H, 0xFF000000u); }
    void allocateLayer(int W, int H){ w = W; h = H; layer = true; px.assign((size_t)W * H, 0u); }
    void background(uint32_t rgb){ std::fill(px.begin(), px.end(), 0xFF000000u | (rgb & 0xFFFFFF)); }

    // Java2D's 8-bit multiply (a * b / 255, rounded)
    static inline int mul8(int a, int b){ int t = a * b + 128; return (t + (t >> 8)) >> 8; }

    // Java2D's AlphaMath tables (mul8 is the same as the formula above, checked for all pairs)
    static const uint8_t* div8row(int a){
        static const std::vector<uint8_t> t = []{
            std::vector<uint8_t> v(256 * 256, 0);
            for(unsigned i = 1; i < 256; i++){
                unsigned inc = ((0xffu << 24) + i / 2) / i, val = 1u << 23;
                for(unsigned j = 0; j < i; j++){ v[i * 256 + j] = (uint8_t)(val >> 24); val += inc; }
                for(unsigned j = i; j < 256; j++) v[i * 256 + j] = 255;
            }
            return v;
        }();
        return &t[(size_t)a * 256];
    }
    // Source-over onto a non-premultiplied ARGB pixel (Java2D's IntArgb loops; measured exact)
    inline void blendLayer(size_t i, int r, int g, int b, int a){
        int sR = mul8(a, r), sG = mul8(a, g), sB = mul8(a, b);
        int rA = a, rR = sR, rG = sG, rB = sB;
        int dF = 255 - a;
        if(dF){
            uint32_t d = px[i];
            int dFA = mul8(dF, (d >> 24) & 255);
            rA += dFA;
            if(dFA){
                int dR = (d >> 16) & 255, dG = (d >> 8) & 255, dB = d & 255;
                if(dFA != 255){ dR = mul8(dFA, dR); dG = mul8(dFA, dG); dB = mul8(dFA, dB); }
                rR += dR; rG += dG; rB += dB;
            }
        }
        if(rA && rA < 255){ const uint8_t* dv = div8row(rA); rR = dv[rR]; rG = dv[rG]; rB = dv[rB]; }
        px[i] = ((uint32_t)rA << 24) | ((uint32_t)rR << 16) | ((uint32_t)rG << 8) | (uint32_t)rB;
    }
    // rows split over the CPU cores (each pixel on its own: the same result as in order)
    template<class F> static void parallelFor(size_t n, F f){
        unsigned T = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
        if(n < 200000 || T == 1){ f((size_t)0, n); return; }
        std::vector<std::thread> th;
        size_t step = (n + T - 1) / T;
        for(unsigned t = 0; t < T; t++){ size_t a = t * step, b = std::min(n, a + step); if(a < b) th.emplace_back([=, &f]{ f(a, b); }); }
        for(auto& x : th) x.join();
    }
    // image(layer, 0, 0) onto this (opaque) canvas: Java2D's IntArgb -> IntRgb source-over (measured exact)
    void drawLayer(const Canvas& L){
        size_t n = std::min(px.size(), L.px.size());
        parallelFor(n, [&](size_t i0, size_t i1){ for(size_t i = i0; i < i1; i++){
            uint32_t s = L.px[i];
            int a = (s >> 24) & 255;
            if(a == 0) continue;
            uint32_t d = px[i];
            int nr = mul8(a, (s >> 16) & 255) + mul8(255 - a, (d >> 16) & 255);
            int ng = mul8(a, (s >> 8) & 255) + mul8(255 - a, (d >> 8) & 255);
            int nb = mul8(a, s & 255) + mul8(255 - a, d & 255);
            px[i] = 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
        } });
    }

    bool addMode = false;   // blendMode(ADD): Processing's blend_add_pin
    inline void blendAdd(size_t i, int r, int g, int b, int a){
        uint32_t d = px[i];
        int sa = a + (a >= 0x7F ? 1 : 0);
        int nr = std::min(255, (int)((d >> 16) & 255) + ((r * sa) >> 8));
        int ng = std::min(255, (int)((d >> 8) & 255) + ((g * sa) >> 8));
        int nb = std::min(255, (int)(d & 255) + ((b * sa) >> 8));
        int na = std::min(255, (int)((d >> 24) & 255) + a);
        px[i] = ((uint32_t)na << 24) | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    }
    inline void blend(size_t i, int r, int g, int b, int a){
        if(a <= 0) return;
        if(addMode){ blendAdd(i, r, g, b, a); return; }
        if(layer){ blendLayer(i, r, g, b, a); return; }
        uint32_t d = px[i];
        int dr = (d >> 16) & 255, dg = (d >> 8) & 255, db = d & 255, ia = 255 - a;
        int nr = mul8(a, r) + mul8(ia, dr), ng = mul8(a, g) + mul8(ia, dg), nb = mul8(a, b) + mul8(ia, db);
        px[i] = 0xFF000000u | ((uint32_t)nr << 16) | ((uint32_t)ng << 8) | (uint32_t)nb;
    }

    // Coverage (0..255) of the 45-degree unit square centred at (fx, fy) in [0, 1) on the 3 x 3
    // pixels around it, per 1/64 offset (FastDraw.pde fdCoverage; its decimal literals are floats,
    // as the Processing preprocessor makes them). Computed once for all offsets: thread safe.
    static const std::array<int, 9>& coverage(float fx, float fy){
        int qx = std::min(63, std::max(0, (int)(fx * 64))), qy = std::min(63, std::max(0, (int)(fy * 64)));
        return covTable()[qy * 64 + qx];
    }
    enum Cap { ROUND = 0, PROJECT = 1, SQUARE = 2 };
    static float snapOf(int cap){ return cap == ROUND ? 0.0f : 0.00105f; }
    // One Java2D point of weight 1
    void point(float x, float y, int r, int g, int b, int a, int cap){
        if(a <= 0 || cap == SQUARE) return;
        // Java2D snaps a coordinate less than ~0.001 px below a whole pixel up to it with the PROJECT
        // and SQUARE caps (not with ROUND): measured, the same in x and y
        const float SNAP = snapOf(cap);
        int ix = (int)std::floor(x + SNAP), iy = (int)std::floor(y + SNAP);
        if(ix < 0 || iy < 0 || ix >= w || iy >= h) return;
        blend((size_t)iy * w + ix, r, g, b, cap == ROUND ? mul8(181, a) : a);
    }

    // A horizontal or vertical line of weight 1 (measured): its ends snap to pixel centres; the
    // pixels between them are filled; the end pixels get the cap: PROJECT whole, SQUARE half,
    // ROUND 218/255. A zero-length line is a point. Other directions are not used by the ported
    // generators (returns false).
    bool lineAxis(float x0, float y0, float x1, float y1, int r, int g, int b, int a, int cap){
        const float SNAP = snapOf(cap);
        if(a <= 0) return true;
        bool vertical = x0 == x1, horizontal = y0 == y1;
        if(!vertical && !horizontal) return false;
        int endCov = cap == PROJECT ? 255 : cap == SQUARE ? 128 : 218;
        if(vertical){
            int cx = (int)std::floor(x0 + SNAP), p = (int)std::floor(y0 + SNAP), q = (int)std::floor(y1 + SNAP);
            if(p > q) std::swap(p, q);
            if(p == q && horizontal){ point(x0, y0, r, g, b, a, cap); return true; }
            if(cx < 0 || cx >= w) return true;
            for(int k = std::max(0, p); k <= std::min(h - 1, q); k++)
                blend((size_t)k * w + cx, r, g, b, (k == p || k == q) ? mul8(endCov, a) : a);
        }else{
            int cy = (int)std::floor(y0 + SNAP), p = (int)std::floor(x0 + SNAP), q = (int)std::floor(x1 + SNAP);
            if(p > q) std::swap(p, q);
            if(cy < 0 || cy >= h) return true;
            size_t row = (size_t)cy * w;
            for(int k = std::max(0, p); k <= std::min(w - 1, q); k++)
                blend(row + k, r, g, b, (k == p || k == q) ? mul8(endCov, a) : a);
        }
        return true;
    }

    // A point thicker than 1 (measured): centred on the pixel centre it snaps to; PROJECT is an
    // axis-aligned square with box coverage (within 1 level of Java2D), ROUND a disc (coverage by
    // 16 x 16 samples: close to Java2D, not exact), SQUARE draws nothing
    void thickPoint(float x, float y, float wgt, int r, int g, int b, int a, int cap){
        const float SNAP = snapOf(cap);
        if(a <= 0 || cap == SQUARE) return;
        float cx = std::floor(x + SNAP) + 0.5f, cy = std::floor(y + SNAP) + 0.5f, hw = wgt / 2;
        int x0 = std::max(0, (int)std::floor(cx - hw)), x1 = std::min(w - 1, (int)std::ceil(cx + hw));
        int y0 = std::max(0, (int)std::floor(cy - hw)), y1 = std::min(h - 1, (int)std::ceil(cy + hw));
        for(int yy = y0; yy <= y1; yy++) for(int xx = x0; xx <= x1; xx++){
            int cov;
            if(cap == PROJECT){
                float ox = std::max(0.0f, std::min(xx + 1.0f, cx + hw) - std::max((float)xx, cx - hw));
                float oy = std::max(0.0f, std::min(yy + 1.0f, cy + hw) - std::max((float)yy, cy - hw));
                cov = (int)(ox * oy * 255 + 0.5f);
            }else{
                // the 16 x 16 samples depend only on the pixel's offset from the (pixel-centred)
                // point and on hw: counted once per width and kept (the same arithmetic, exact at
                // these magnitudes, so the same coverage)
                const RoundKernel& K = roundKernel(hw);
                int ky = yy - (int)(cy - 0.5f) - K.o, kx = xx - (int)(cx - 0.5f) - K.o;
                cov = (ky >= 0 && ky < K.n && kx >= 0 && kx < K.n) ? K.cov[(size_t)ky * K.n + kx] : 0;
            }
            if(cov > 0) blend((size_t)yy * w + xx, r, g, b, mul8(cov, a));
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
            K.cov[(size_t)(yy - K.o) * K.n + (xx - K.o)] = (int)(n * 255.0f / 256 + 0.5f);
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
    void strokePath(std::vector<Pt> pts, float width, int cap, int r, int g, int b, int a, bool normalize = true, bool closed = false){
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
    void strokeBezier(Pt a0, Pt c0, Pt c1, Pt a1, float width, int cap, int r, int g, int b, int a){
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
    void fillPolygon(const std::vector<Pt>& poly, int r, int g, int b, int a){ fillUnion({poly}, r, g, b, a); }
    void fillPolygons(const std::vector<std::vector<Pt>>& polys, int r, int g, int b, int a){ fillUnion(polys, r, g, b, a); }
    // the union of polygons, 8 x 8 samples per pixel, composited once
    void fillUnion(const std::vector<std::vector<Pt>>& polys, int r, int g, int b, int a){
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
            int cv = (int)(n * 255.0f / (S * S) + 0.5f);
            blend((size_t)(py0 + yy) * w + px0 + xx, r, g, b, mul8(std::min(255, cv), a));
        }
    }

    // One point (FastDraw fdPoint: the anti-aliased diamond)
    void pointAA(float x, float y, int r, int g, int b, int a){
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
                blend(row + xx, r, g, b, mul8(c, a));
            }
        }
    }

    // A filled axis-aligned rectangle, anti-aliased on fractional edges (box coverage)
    void fillRect(float x, float y, float rw, float rh, int r, int g, int b, int a){
        if(a <= 0 || rw <= 0 || rh <= 0) return;
        float x1 = x + rw, y1 = y + rh;
        int ix0 = std::max(0, (int)std::floor(x)), ix1 = std::min(w, (int)std::ceil(x1));
        int iy0 = std::max(0, (int)std::floor(y)), iy1 = std::min(h, (int)std::ceil(y1));
        for(int yy = iy0; yy < iy1; yy++){
            float cy = std::min((float)yy + 1, y1) - std::max((float)yy, y);
            for(int xx = ix0; xx < ix1; xx++){
                float cx = std::min((float)xx + 1, x1) - std::max((float)xx, x);
                int cov = (int)(cx * cy * 255 + 0.5f);
                blend((size_t)yy * w + xx, r, g, b, mul8(cov, a));
            }
        }
    }

    // Straight alpha, as the sketch writes its PNGs (Render.pde toStraightAlpha): a = max(R,G,B),
    // colour = round(c x 255 / a); a = 0 -> transparent. RGBA bytes.
    std::vector<uint8_t> straightAlpha() const {
        std::vector<uint8_t> out((size_t)w * h * 4);
        parallelFor(px.size(), [&](size_t i0, size_t i1){ for(size_t i = i0; i < i1; i++){
            int r = (px[i] >> 16) & 255, g = (px[i] >> 8) & 255, b = px[i] & 255;
            int a = std::max(r, std::max(g, b));
            uint8_t* o = &out[i * 4];
            if(a == 0){ o[0] = o[1] = o[2] = o[3] = 0; continue; }
            const uint8_t* t = &straightTable()[(size_t)a << 8];
            o[0] = t[r]; o[1] = t[g]; o[2] = t[b];
            o[3] = (uint8_t)a;
        } });
        return out;
    }

private:
    // round(c x 255 / a), clamped, for every a, c (the same float formula, computed once)
    static const std::vector<uint8_t>& straightTable(){
        static const std::vector<uint8_t> t = []{
            std::vector<uint8_t> v(256 * 256, 0);
            for(int a = 1; a < 256; a++) for(int c = 0; c < 256; c++)
                v[(size_t)a * 256 + c] = (uint8_t)std::min(255, (int)std::floor(c * 255.0f / a + 0.5f));
            return v;
        }();
        return t;
    }
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
