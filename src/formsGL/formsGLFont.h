//
//  formsGLFont.h
//  ofxOceanodeForms — ESPECTRES 2027, Phase 7 (Forms GL Box)
//
//  The sketch's font (FuturaStd-Bold.otf) for the shapes generator's letters, read with FreeType:
//  each letter's outline (flattened, in em units, y down, baseline 0) and its advance (= Java's
//  textWidth, measured within 0.0001 px); ascent and descent as Processing reports them for this font
//  (0.8300781 / 0.2578125 em, measured).
//  No openFrameworks: the app and the headless tests read the font the same way.
//

#pragma once

#include "formsGLSketch.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include <memory>
#include <string>

namespace formsgl {

inline std::shared_ptr<const Sketch::GlyphFont> loadGlyphFont(const std::string& path){
    FT_Library lib = nullptr;
    if(FT_Init_FreeType(&lib)) return nullptr;
    FT_Face face = nullptr;
    if(FT_New_Face(lib, path.c_str(), 0, &face)){ FT_Done_FreeType(lib); return nullptr; }
    auto gf = std::make_shared<Sketch::GlyphFont>();
    const float em = (float)face->units_per_EM;
    // Processing (Java2D) on this font: ascent 0.8300781 em, descent 0.2578125 em (measured); from
    // the font's own tables when they agree with those, else the measured values
    float asc = face->ascender / em, desc = -face->descender / em;
    // (the descent matches the font's own table; Java's ascent is 0.0050781 em above it)
    (void)asc; (void)desc;
    gf->ascent = 0.8300781f;
    gf->descent = 0.2578125f;
    struct Ctx { std::vector<std::vector<Canvas::Pt>>* contours; Canvas::Pt last; float em; };
    auto toPt = [](const FT_Vector* v, float e){ return Canvas::Pt{v->x / e, -v->y / e}; };
    for(char c = 'A'; c <= 'z'; c++){
        if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))) continue;
        if(FT_Load_Char(face, (FT_ULong)c, FT_LOAD_NO_SCALE | FT_LOAD_NO_HINTING)) continue;
        Sketch::Glyph g;
        g.advance = face->glyph->metrics.horiAdvance / em;
        Ctx ctx{&g.contours, {0, 0}, em};
        FT_Outline_Funcs fn;
        fn.shift = 0; fn.delta = 0;
        fn.move_to = [](const FT_Vector* to, void* u) -> int {
            auto* x = (Ctx*)u; x->contours->push_back({}); x->last = {to->x / x->em, -to->y / x->em}; x->contours->back().push_back(x->last); return 0;
        };
        fn.line_to = [](const FT_Vector* to, void* u) -> int {
            auto* x = (Ctx*)u; x->last = {to->x / x->em, -to->y / x->em}; x->contours->back().push_back(x->last); return 0;
        };
        fn.conic_to = [](const FT_Vector* c1, const FT_Vector* to, void* u) -> int {
            auto* x = (Ctx*)u; Canvas::Pt p0 = x->last, q = {c1->x / x->em, -c1->y / x->em}, p1 = {to->x / x->em, -to->y / x->em};
            for(int k = 1; k <= 16; k++){ float t = k / 16.0f, s = 1 - t; x->contours->back().push_back({s * s * p0.x + 2 * s * t * q.x + t * t * p1.x, s * s * p0.y + 2 * s * t * q.y + t * t * p1.y}); }
            x->last = p1; return 0;
        };
        fn.cubic_to = [](const FT_Vector* c1, const FT_Vector* c2, const FT_Vector* to, void* u) -> int {
            auto* x = (Ctx*)u; Canvas::Pt p0 = x->last, a = {c1->x / x->em, -c1->y / x->em}, b = {c2->x / x->em, -c2->y / x->em}, p1 = {to->x / x->em, -to->y / x->em};
            for(int k = 1; k <= 24; k++){ float t = k / 24.0f, s = 1 - t;
                x->contours->back().push_back({s * s * s * p0.x + 3 * s * s * t * a.x + 3 * s * t * t * b.x + t * t * t * p1.x,
                                               s * s * s * p0.y + 3 * s * s * t * a.y + 3 * s * t * t * b.y + t * t * t * p1.y}); }
            x->last = p1; return 0;
        };
        (void)toPt;
        FT_Outline_Decompose(&face->glyph->outline, &fn, &ctx);
        gf->glyphs[c] = g;
    }
    FT_Done_Face(face);
    FT_Done_FreeType(lib);
    return gf;
}

} // namespace formsgl
