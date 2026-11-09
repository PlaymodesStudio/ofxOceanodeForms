// Exact visual regression: CPU vs integer GPU raster, shared textures and complete recipes.
// Build: clang++ -std=c++17 -O2 -DGL_SILENCE_DEPRECATION -I src/formsGL -I <OF>/libs/json/include
//   tests/formsGL/test_gpu_exact.cpp -framework OpenGL -o <scratch>/gpu_exact
// Run: gpu_exact [--16k] [sparse_euclid_kicks.json]. No application data is written.
#include "formsGLGpu.h"
#include <cstdio>
#include <random>
#include <thread>
#include <atomic>
using namespace formsgl;
static void check(bool ok, const std::string& message){ if(!ok) throw std::runtime_error(message); }
static double ms(std::chrono::steady_clock::time_point start){
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}
static void reference(Canvas& c, float x, float top, float w, float power, int cap){
    float h = c.h - top;
    for(float i = 0; i < w; i++){
        float ph = pjPow(i / w, 0.25f), ph2 = pjPow(1 - i / w, power * 2);
        float y2 = ph * h + top, h2 = c.h - y2;
        for(float j = 0; j < h2; j++){
            float af = ph2 * 255 * pjPow(j / h2, power / 2);
            int a = (int)(pjConstrain(af / 255.0f, 0.0f, 1.0f) * 255);
            c.point(i + x, j + y2, 255, 255, 255, a, cap);
        }
    }
}
static json normalize(json v){
    if(v.is_object() && v.contains("options")) return normalize(v["options"][0]);
    if(v.is_object()) for(auto& i : v.items()) i.value() = normalize(i.value());
    if(v.is_array()) for(auto& i : v) i = normalize(i);
    return v;
}
static void sharedUploads(bool large){
    CGLContextObj main = CGLGetCurrentContext();
    for(GLenum target : {GLenum(GL_TEXTURE_2D), GLenum(GL_TEXTURE_RECTANGLE)}){
        auto pool = SharedTexturePool::create(main, target); check(bool(pool), "shared context pool");
        const int w = large ? 15360 : 4093, h = large ? 8640 : 2077;
        std::vector<uint8_t> pixels((size_t)w * h * 4), back(pixels.size());
        for(size_t i = 0; i < pixels.size(); i++) pixels[i] = (i * 17 + i / 293) & 255;
        std::shared_ptr<SharedTexture> texture;
        GLuint fbo = 0, colour = 0;
        glGenFramebuffers(1, &fbo); glGenTextures(1, &colour);
        glBindTexture(GL_TEXTURE_2D, colour); glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 16, 16, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo); glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, colour, 0);
        check(glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE, "playback check framebuffer");
        std::atomic<bool> complete{false};
        std::exception_ptr workerError;
        std::thread worker([&]{
            try { GpuEffectsCGL renderer(pool->context()); texture = renderer.upload(pixels, w, h, pool); }
            catch(...){ workerError = std::current_exception(); }
            complete = true;
        });
        int frames = 0; double longest = 0;
        while(!complete){
            auto frame = std::chrono::steady_clock::now();
            glClearColor(0.2f, 0.4f, 0.6f, 1); glClear(GL_COLOR_BUFFER_BIT); glFinish();
            longest = std::max(longest, ms(frame)); frames++;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        worker.join(); if(workerError) std::rethrow_exception(workerError);
        check(bool(texture), "shared upload failed");
        glBindFramebuffer(GL_FRAMEBUFFER, 0); glDeleteFramebuffers(1, &fbo); glDeleteTextures(1, &colour);
        printf("shared %dx%d upload: %d concurrent GL frames, slowest %.2f ms\n", w, h, frames, longest);
        CGLSetCurrentContext(main);
        auto start = std::chrono::steady_clock::now();
        while(!texture->ready()){ check(ms(start) < 3000, "fence timeout"); std::this_thread::sleep_for(std::chrono::milliseconds(1)); }
        glBindTexture(target, texture->id); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
        glGetTexImage(target, 0, GL_RGBA, GL_UNSIGNED_BYTE, back.data());
        check(pixels == back, "shared texture changed bytes or row order");
        GLuint id = texture->id; texture.reset(); pool->drain(); check(!glIsTexture(id), "retirement failed");
        // Covers are single-channel, often with odd row widths. They must take the
        // same worker/fence path as the image, without a UI-thread texture upload.
        std::vector<uint8_t> alpha((size_t)w*17),alphaBack(alpha.size());
        for(size_t i=0;i<alpha.size();++i) alpha[i]=(i*23+i/79)&255;
        std::thread maskWorker([&]{
            GpuEffectsCGL renderer(pool->context()); texture=renderer.upload(alpha,w,17,pool,GL_RED);
        });
        maskWorker.join(); check(bool(texture),"shared cover upload failed");
        check(texture->internalFormat==GL_R8,"cover format must remain single-channel");
        start=std::chrono::steady_clock::now();
        while(!texture->ready()){check(ms(start)<3000,"cover fence timeout");std::this_thread::sleep_for(std::chrono::milliseconds(1));}
        glBindTexture(target,texture->id);glPixelStorei(GL_PACK_ALIGNMENT,1);
        glGetTexImage(target,0,GL_RED,GL_UNSIGNED_BYTE,alphaBack.data());glPixelStorei(GL_PACK_ALIGNMENT,4);
        check(alpha==alphaBack,"cover pixels or row alignment changed");
        id=texture->id;texture.reset();pool->drain();check(!glIsTexture(id),"cover retirement failed");
    }
    printf("worker upload, both texture targets, RGBA/row order, fence and retirement: exact\n");
}
int main(int argc, char** argv){
    try {
        bool large = false; std::string preset;
        for(int i = 1; i < argc; i++) if(std::string(argv[i]) == "--16k") large = true; else preset = argv[i];
        GpuEffectsCGL gpu; check(gpu.ok(), "GPU initialization failed");
        {
            Canvas fallback; fallback.allocate(256, 128); auto before = fallback.px;
            gpu.enableExactRaster = false;
            check(!gpu.kickSweep(fallback, 0, 0, 256, 4, Canvas::ROUND), "disabled raster did not fall back");
            check(fallback.px == before, "fallback altered the canvas");
            gpu.enableExactRaster = true;
            check(!gpu.kickSweep(fallback, 0, 0, 256, -1, Canvas::ROUND), "unsupported envelope did not fall back");
            check(fallback.px == before, "unsupported path altered the canvas");
        }
        std::mt19937 rng(42);
        for(int t = 0; t < 180; t++){
            Canvas a; a.allocate(97 + rng() % 700, 81 + rng() % 600);
            a.layer = t % 3 == 1; a.addMode = t % 3 == 2;
            for(auto& p : a.px) p = rng();
            Canvas b = a;
            for(int pass = 0; pass < 3; pass++){
                float x = (int(rng() % 2000) - 900) / 7.f, y = (int(rng() % 3000) - 1600) / 7.f;
                if(t % 7 == 0){ x = -0.00105f; y = 127.99999f; }
                float w = (1 + rng() % 2500) / 3.f, power = (1 + rng() % 1000) / 67.f;
                int cap = (t / 3) % 3;
                reference(a, x, y, w, power, cap);
                check(gpu.kickSweep(b, x, y, w, power, cap), "GPU fallback: " + gpu.error);
                check(a.px == b.px, "kick differs in case " + std::to_string(t));
            }
        }
        printf("540 kick comparisons: exact (caps, layers, additive, overlap, fractional clipping)\n");
        sharedUploads(large);
        Schemas schema; check(schema.loadEmbedded(), "schemas");
        json items = json::array();
        if(!preset.empty()){
            std::ifstream f(preset); json p; f >> p;
            for(auto& layer : p["layers"]) items.push_back(normalize(layer));
        }else items.push_back({{"gen", "hits"}, {"params", {{"cell", "kick sweep"}, {"fills", {3}}, {"length", {6, 6}}, {"kickPower", 4}, {"bandHigh", 1.093}}}});
        std::vector<std::pair<int,int>> sizes{{1920,1080},{3840,2160}};
        if(large){ sizes.push_back({15360,8640}); sizes.push_back({16384,1024}); }
        if(std::getenv("GPU_EXACT_SQUARE")) sizes.push_back({16384,16384});
        for(auto size : sizes) for(int variant = 0; variant < (size.first > 4000 ? 1 : 3); variant++){
            json q = {{"size", {size.first,size.second}}, {"bars",4}, {"seed",719+variant}, {"items",items}};
            if(variant == 1){
                q["items"][0]["process"] = {{"fadeTop",0.2}, {"fadeBottom",0.3}};
                q["items"][0]["gain"] = 0.63; q["items"][0]["clipBars"] = {0.2,3.7};
            }
            if(variant == 2) q["items"].push_back({{"gen","effects"}, {"params", {{"maskAmount",0.6}, {"maskScale",3}}}});
            RenderResult a,b;
            auto start = std::chrono::steady_clock::now();
            { Sketch sk(schema); a = sk.render(q); } double cpu = ms(start);
            start = std::chrono::steady_clock::now();
            double prep = gpu.exactPrepareMs(), draw = gpu.exactRasterMs();
            { Sketch sk(schema); sk.gpu = &gpu; sk.gpuMinPixels = 1; b = sk.render(q); } double gm = ms(start);
            check(a.error.empty() && b.error.empty(), "recipe failed");
            for(auto& i : a.itemResults) check(!i.contains("error"), i.dump());
            for(auto& i : b.itemResults) check(!i.contains("error"), i.dump());
            check(a.rgba == b.rgba, "recipe differs at " + std::to_string(size.first));
            printf("recipe %dx%d variant %d: exact; parallel CPU %.1f ms, GPU %.1f ms\n",size.first,size.second,variant,cpu,gm);
            printf("  GPU preparation %.1f ms, raster/transfers %.1f ms\n", gpu.exactPrepareMs()-prep, gpu.exactRasterMs()-draw);
        }
        check(gpu.exactDraws() > 0, "GPU path never exercised");
        printf("ALL EXACT (%llu GPU draws)\n", (unsigned long long)gpu.exactDraws());
    }catch(const std::exception& e){ fprintf(stderr,"FAIL: %s\n",e.what()); return 1; }
}
