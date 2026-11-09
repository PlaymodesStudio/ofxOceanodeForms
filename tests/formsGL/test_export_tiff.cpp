// Verify the export backend retains >8-bit samples through float GL readback and TIFF storage.
#define GL_SILENCE_DEPRECATION
#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <FreeImage.h>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <vector>
int main(int argc,char** argv){
    assert(argc==2);
    CGLPixelFormatAttribute attrs[]={kCGLPFAOpenGLProfile,(CGLPixelFormatAttribute)kCGLOGLPVersion_GL4_Core,kCGLPFAAccelerated,(CGLPixelFormatAttribute)0};
    CGLPixelFormatObj pf=nullptr; GLint count=0; CGLContextObj ctx=nullptr;
    assert(CGLChoosePixelFormat(attrs,&pf,&count)==kCGLNoError);
    assert(CGLCreateContext(pf,nullptr,&ctx)==kCGLNoError); CGLSetCurrentContext(ctx);
    const int w=1024,h=2; std::vector<float> source(w*h*4);
    for(int y=0;y<h;++y) for(int x=0;x<w;++x){ size_t i=(y*w+x)*4;
        source[i]=x/(float)(w-1); source[i+1]=y*.4f+x/(float)(w*2); source[i+2]=.123456f; source[i+3]=1;
    }
    GLuint tex,fbo; glGenTextures(1,&tex); glBindTexture(GL_TEXTURE_2D,tex);
    glTexImage2D(GL_TEXTURE_2D,0,GL_RGBA32F,w,h,0,GL_RGBA,GL_FLOAT,source.data());
    glGenFramebuffers(1,&fbo); glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,tex,0);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    std::vector<uint16_t> samples(w*h*4); glReadPixels(0,0,w,h,GL_RGBA,GL_UNSIGNED_SHORT,samples.data());
    assert(glGetError()==GL_NO_ERROR);
    int unique=1; for(int x=1;x<w;++x) unique+=samples[x*4]!=samples[(x-1)*4]; assert(unique>256);
    FreeImage_Initialise();
    assert(FreeImage_FIFSupportsExportType(FIF_TIFF,FIT_RGBA16));
    FIBITMAP* image=FreeImage_AllocateT(FIT_RGBA16,w,h,64); assert(image);
    for(int y=0;y<h;++y) std::memcpy(FreeImage_GetScanLine(image,h-y-1),samples.data()+y*w*4,w*8);
    assert(FreeImage_Save(FIF_TIFF,image,argv[1])); FreeImage_Unload(image);
    image=FreeImage_Load(FIF_TIFF,argv[1]); assert(image);
    assert(FreeImage_GetImageType(image)==FIT_RGBA16 && FreeImage_GetBPP(image)==64);
    for(int y=0;y<h;++y) assert(std::memcmp(FreeImage_GetScanLine(image,h-y-1),samples.data()+y*w*4,w*8)==0);
    FreeImage_Unload(image); FreeImage_DeInitialise();
    glDeleteFramebuffers(1,&fbo); glDeleteTextures(1,&tex); CGLSetCurrentContext(nullptr); CGLDestroyContext(ctx); CGLDestroyPixelFormat(pf);
    printf("PASS: RGBA32F -> 16-bit RGBA TIFF, %d distinct gradient levels, exact channel/row round trip\n",unique);
}
