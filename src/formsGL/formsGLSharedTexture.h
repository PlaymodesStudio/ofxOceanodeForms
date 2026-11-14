#pragma once
#include <memory>
#include <mutex>
#include <vector>
#include <stdexcept>

namespace formsgl {

// GL names cross the worker/UI boundary only after a fence signals. Retirement is deferred to
// the UI; the private cleanup context also handles the last reference being released at shutdown.
class SharedTexturePool {
public:
    struct Retired { GLuint texture; GLsync fence; };
    static std::shared_ptr<SharedTexturePool> create(CGLContextObj shared, GLenum target = GL_TEXTURE_2D){
        if(!shared) return {};
        auto p = std::shared_ptr<SharedTexturePool>(new SharedTexturePool);
        p->textureTarget = target;
        if(CGLCreateContext(CGLGetPixelFormat(shared), shared, &p->cleanup) != kCGLNoError) return {};
        CGLRetainContext(shared); p->shared = shared;
        return p;
    }
    ~SharedTexturePool(){
        if(cleanup){
            CGLContextObj previous = CGLGetCurrentContext();
            CGLSetCurrentContext(cleanup); drain();
            CGLSetCurrentContext(previous == cleanup ? nullptr : previous);
            CGLDestroyContext(cleanup);
        }
        if(shared) CGLReleaseContext(shared);
    }
    CGLContextObj context() const { return shared; }
    GLenum target() const { return textureTarget; }
    void retire(GLuint texture, GLsync fence){
        std::lock_guard<std::mutex> lock(mutex);
        retired.push_back({texture, fence});
    }
    // Call with a context in the share group current (the app's update or this pool's destructor).
    void drain(){
        std::vector<Retired> pending;
        { std::lock_guard<std::mutex> lock(mutex); pending.swap(retired); }
        for(auto& r : pending){
            if(r.fence) glDeleteSync(r.fence);
            if(r.texture) glDeleteTextures(1, &r.texture);
        }
    }
private:
    CGLContextObj shared = nullptr, cleanup = nullptr;
    GLenum textureTarget = GL_TEXTURE_2D;
    std::mutex mutex;
    std::vector<Retired> retired;
};

struct SharedTexture {
    GLuint id = 0;
    GLenum target = GL_TEXTURE_2D;
    GLenum internalFormat = GL_RGBA16;
    int w = 0, h = 0;
    GLsync fence = nullptr;
    std::shared_ptr<SharedTexturePool> pool;
    ~SharedTexture(){ if(pool) pool->retire(id, fence); }
    bool ready() const {
        if(!fence) return true;
        GLenum r = glClientWaitSync(fence, 0, 0); // zero timeout: never pause the scrolling thread
        if(r == GL_WAIT_FAILED) throw std::runtime_error("shared texture fence failed");
        return r == GL_ALREADY_SIGNALED || r == GL_CONDITION_SATISFIED;
    }
};
} // namespace formsgl
