#pragma once
#include <SDL.h>
#include <SDL_opengl.h>
#include <stdexcept>
#include <string>
namespace ember {
// Per-context dispatch: Windows GL pointers cannot be shared across contexts.
struct GlFunctions {
    PFNGLACTIVETEXTUREPROC glActiveTexture=nullptr;
    PFNGLATTACHSHADERPROC glAttachShader=nullptr;
    PFNGLBINDBUFFERPROC glBindBuffer=nullptr;
    PFNGLBINDBUFFERBASEPROC glBindBufferBase=nullptr;
    PFNGLBINDFRAMEBUFFERPROC glBindFramebuffer=nullptr;
    PFNGLBLITFRAMEBUFFERPROC glBlitFramebuffer=nullptr;
    PFNGLBINDRENDERBUFFERPROC glBindRenderbuffer=nullptr;
    PFNGLBINDVERTEXARRAYPROC glBindVertexArray=nullptr;
    PFNGLBUFFERDATAPROC glBufferData=nullptr;
    PFNGLBUFFERSUBDATAPROC glBufferSubData=nullptr;
    PFNGLCHECKFRAMEBUFFERSTATUSPROC glCheckFramebufferStatus=nullptr;
    PFNGLCOMPILESHADERPROC glCompileShader=nullptr;
    PFNGLCREATEPROGRAMPROC glCreateProgram=nullptr;
    PFNGLCREATESHADERPROC glCreateShader=nullptr;
    PFNGLDELETEBUFFERSPROC glDeleteBuffers=nullptr;
    PFNGLDELETEFRAMEBUFFERSPROC glDeleteFramebuffers=nullptr;
    PFNGLDELETEPROGRAMPROC glDeleteProgram=nullptr;
    PFNGLDELETERENDERBUFFERSPROC glDeleteRenderbuffers=nullptr;
    PFNGLDELETESHADERPROC glDeleteShader=nullptr;
    PFNGLDELETEVERTEXARRAYSPROC glDeleteVertexArrays=nullptr;
    PFNGLENABLEVERTEXATTRIBARRAYPROC glEnableVertexAttribArray=nullptr;
    PFNGLFRAMEBUFFERRENDERBUFFERPROC glFramebufferRenderbuffer=nullptr;
    PFNGLFRAMEBUFFERTEXTURE2DPROC glFramebufferTexture2D=nullptr;
    PFNGLGENBUFFERSPROC glGenBuffers=nullptr;
    PFNGLGENFRAMEBUFFERSPROC glGenFramebuffers=nullptr;
    PFNGLGENRENDERBUFFERSPROC glGenRenderbuffers=nullptr;
    PFNGLGENVERTEXARRAYSPROC glGenVertexArrays=nullptr;
    PFNGLGETPROGRAMIVPROC glGetProgramiv=nullptr;
    PFNGLGETSHADERINFOLOGPROC glGetShaderInfoLog=nullptr;
    PFNGLGETSHADERIVPROC glGetShaderiv=nullptr;
    PFNGLGETUNIFORMBLOCKINDEXPROC glGetUniformBlockIndex=nullptr;
    PFNGLGETUNIFORMLOCATIONPROC glGetUniformLocation=nullptr;
    PFNGLLINKPROGRAMPROC glLinkProgram=nullptr;
    PFNGLRENDERBUFFERSTORAGEPROC glRenderbufferStorage=nullptr;
    PFNGLSHADERSOURCEPROC glShaderSource=nullptr;
    PFNGLTEXBUFFERPROC glTexBuffer=nullptr;
    PFNGLUNIFORM1IPROC glUniform1i=nullptr;
    PFNGLUNIFORMBLOCKBINDINGPROC glUniformBlockBinding=nullptr;
    PFNGLUSEPROGRAMPROC glUseProgram=nullptr;
    PFNGLVERTEXATTRIBPOINTERPROC glVertexAttribPointer=nullptr;
    void load() {
        glBlitFramebuffer=reinterpret_cast<PFNGLBLITFRAMEBUFFERPROC>(SDL_GL_GetProcAddress("glBlitFramebuffer"));
        if(!glBlitFramebuffer)throw std::runtime_error("OpenGL function unavailable: glBlitFramebuffer");
        glActiveTexture=reinterpret_cast<PFNGLACTIVETEXTUREPROC>(SDL_GL_GetProcAddress("glActiveTexture")); if(!glActiveTexture)throw std::runtime_error("OpenGL function unavailable: glActiveTexture");
        glAttachShader=reinterpret_cast<PFNGLATTACHSHADERPROC>(SDL_GL_GetProcAddress("glAttachShader")); if(!glAttachShader)throw std::runtime_error("OpenGL function unavailable: glAttachShader");
        glBindBuffer=reinterpret_cast<PFNGLBINDBUFFERPROC>(SDL_GL_GetProcAddress("glBindBuffer")); if(!glBindBuffer)throw std::runtime_error("OpenGL function unavailable: glBindBuffer");
        glBindBufferBase=reinterpret_cast<PFNGLBINDBUFFERBASEPROC>(SDL_GL_GetProcAddress("glBindBufferBase")); if(!glBindBufferBase)throw std::runtime_error("OpenGL function unavailable: glBindBufferBase");
        glBindFramebuffer=reinterpret_cast<PFNGLBINDFRAMEBUFFERPROC>(SDL_GL_GetProcAddress("glBindFramebuffer")); if(!glBindFramebuffer)throw std::runtime_error("OpenGL function unavailable: glBindFramebuffer");
        glBindRenderbuffer=reinterpret_cast<PFNGLBINDRENDERBUFFERPROC>(SDL_GL_GetProcAddress("glBindRenderbuffer")); if(!glBindRenderbuffer)throw std::runtime_error("OpenGL function unavailable: glBindRenderbuffer");
        glBindVertexArray=reinterpret_cast<PFNGLBINDVERTEXARRAYPROC>(SDL_GL_GetProcAddress("glBindVertexArray")); if(!glBindVertexArray)throw std::runtime_error("OpenGL function unavailable: glBindVertexArray");
        glBufferData=reinterpret_cast<PFNGLBUFFERDATAPROC>(SDL_GL_GetProcAddress("glBufferData")); if(!glBufferData)throw std::runtime_error("OpenGL function unavailable: glBufferData");
        glBufferSubData=reinterpret_cast<PFNGLBUFFERSUBDATAPROC>(SDL_GL_GetProcAddress("glBufferSubData")); if(!glBufferSubData)throw std::runtime_error("OpenGL function unavailable: glBufferSubData");
        glCheckFramebufferStatus=reinterpret_cast<PFNGLCHECKFRAMEBUFFERSTATUSPROC>(SDL_GL_GetProcAddress("glCheckFramebufferStatus")); if(!glCheckFramebufferStatus)throw std::runtime_error("OpenGL function unavailable: glCheckFramebufferStatus");
        glCompileShader=reinterpret_cast<PFNGLCOMPILESHADERPROC>(SDL_GL_GetProcAddress("glCompileShader")); if(!glCompileShader)throw std::runtime_error("OpenGL function unavailable: glCompileShader");
        glCreateProgram=reinterpret_cast<PFNGLCREATEPROGRAMPROC>(SDL_GL_GetProcAddress("glCreateProgram")); if(!glCreateProgram)throw std::runtime_error("OpenGL function unavailable: glCreateProgram");
        glCreateShader=reinterpret_cast<PFNGLCREATESHADERPROC>(SDL_GL_GetProcAddress("glCreateShader")); if(!glCreateShader)throw std::runtime_error("OpenGL function unavailable: glCreateShader");
        glDeleteBuffers=reinterpret_cast<PFNGLDELETEBUFFERSPROC>(SDL_GL_GetProcAddress("glDeleteBuffers")); if(!glDeleteBuffers)throw std::runtime_error("OpenGL function unavailable: glDeleteBuffers");
        glDeleteFramebuffers=reinterpret_cast<PFNGLDELETEFRAMEBUFFERSPROC>(SDL_GL_GetProcAddress("glDeleteFramebuffers")); if(!glDeleteFramebuffers)throw std::runtime_error("OpenGL function unavailable: glDeleteFramebuffers");
        glDeleteProgram=reinterpret_cast<PFNGLDELETEPROGRAMPROC>(SDL_GL_GetProcAddress("glDeleteProgram")); if(!glDeleteProgram)throw std::runtime_error("OpenGL function unavailable: glDeleteProgram");
        glDeleteRenderbuffers=reinterpret_cast<PFNGLDELETERENDERBUFFERSPROC>(SDL_GL_GetProcAddress("glDeleteRenderbuffers")); if(!glDeleteRenderbuffers)throw std::runtime_error("OpenGL function unavailable: glDeleteRenderbuffers");
        glDeleteShader=reinterpret_cast<PFNGLDELETESHADERPROC>(SDL_GL_GetProcAddress("glDeleteShader")); if(!glDeleteShader)throw std::runtime_error("OpenGL function unavailable: glDeleteShader");
        glDeleteVertexArrays=reinterpret_cast<PFNGLDELETEVERTEXARRAYSPROC>(SDL_GL_GetProcAddress("glDeleteVertexArrays")); if(!glDeleteVertexArrays)throw std::runtime_error("OpenGL function unavailable: glDeleteVertexArrays");
        glEnableVertexAttribArray=reinterpret_cast<PFNGLENABLEVERTEXATTRIBARRAYPROC>(SDL_GL_GetProcAddress("glEnableVertexAttribArray")); if(!glEnableVertexAttribArray)throw std::runtime_error("OpenGL function unavailable: glEnableVertexAttribArray");
        glFramebufferRenderbuffer=reinterpret_cast<PFNGLFRAMEBUFFERRENDERBUFFERPROC>(SDL_GL_GetProcAddress("glFramebufferRenderbuffer")); if(!glFramebufferRenderbuffer)throw std::runtime_error("OpenGL function unavailable: glFramebufferRenderbuffer");
        glFramebufferTexture2D=reinterpret_cast<PFNGLFRAMEBUFFERTEXTURE2DPROC>(SDL_GL_GetProcAddress("glFramebufferTexture2D")); if(!glFramebufferTexture2D)throw std::runtime_error("OpenGL function unavailable: glFramebufferTexture2D");
        glGenBuffers=reinterpret_cast<PFNGLGENBUFFERSPROC>(SDL_GL_GetProcAddress("glGenBuffers")); if(!glGenBuffers)throw std::runtime_error("OpenGL function unavailable: glGenBuffers");
        glGenFramebuffers=reinterpret_cast<PFNGLGENFRAMEBUFFERSPROC>(SDL_GL_GetProcAddress("glGenFramebuffers")); if(!glGenFramebuffers)throw std::runtime_error("OpenGL function unavailable: glGenFramebuffers");
        glGenRenderbuffers=reinterpret_cast<PFNGLGENRENDERBUFFERSPROC>(SDL_GL_GetProcAddress("glGenRenderbuffers")); if(!glGenRenderbuffers)throw std::runtime_error("OpenGL function unavailable: glGenRenderbuffers");
        glGenVertexArrays=reinterpret_cast<PFNGLGENVERTEXARRAYSPROC>(SDL_GL_GetProcAddress("glGenVertexArrays")); if(!glGenVertexArrays)throw std::runtime_error("OpenGL function unavailable: glGenVertexArrays");
        glGetProgramiv=reinterpret_cast<PFNGLGETPROGRAMIVPROC>(SDL_GL_GetProcAddress("glGetProgramiv")); if(!glGetProgramiv)throw std::runtime_error("OpenGL function unavailable: glGetProgramiv");
        glGetShaderInfoLog=reinterpret_cast<PFNGLGETSHADERINFOLOGPROC>(SDL_GL_GetProcAddress("glGetShaderInfoLog")); if(!glGetShaderInfoLog)throw std::runtime_error("OpenGL function unavailable: glGetShaderInfoLog");
        glGetShaderiv=reinterpret_cast<PFNGLGETSHADERIVPROC>(SDL_GL_GetProcAddress("glGetShaderiv")); if(!glGetShaderiv)throw std::runtime_error("OpenGL function unavailable: glGetShaderiv");
        glGetUniformBlockIndex=reinterpret_cast<PFNGLGETUNIFORMBLOCKINDEXPROC>(SDL_GL_GetProcAddress("glGetUniformBlockIndex")); if(!glGetUniformBlockIndex)throw std::runtime_error("OpenGL function unavailable: glGetUniformBlockIndex");
        glGetUniformLocation=reinterpret_cast<PFNGLGETUNIFORMLOCATIONPROC>(SDL_GL_GetProcAddress("glGetUniformLocation")); if(!glGetUniformLocation)throw std::runtime_error("OpenGL function unavailable: glGetUniformLocation");
        glLinkProgram=reinterpret_cast<PFNGLLINKPROGRAMPROC>(SDL_GL_GetProcAddress("glLinkProgram")); if(!glLinkProgram)throw std::runtime_error("OpenGL function unavailable: glLinkProgram");
        glRenderbufferStorage=reinterpret_cast<PFNGLRENDERBUFFERSTORAGEPROC>(SDL_GL_GetProcAddress("glRenderbufferStorage")); if(!glRenderbufferStorage)throw std::runtime_error("OpenGL function unavailable: glRenderbufferStorage");
        glShaderSource=reinterpret_cast<PFNGLSHADERSOURCEPROC>(SDL_GL_GetProcAddress("glShaderSource")); if(!glShaderSource)throw std::runtime_error("OpenGL function unavailable: glShaderSource");
        glTexBuffer=reinterpret_cast<PFNGLTEXBUFFERPROC>(SDL_GL_GetProcAddress("glTexBuffer")); if(!glTexBuffer)throw std::runtime_error("OpenGL function unavailable: glTexBuffer");
        glUniform1i=reinterpret_cast<PFNGLUNIFORM1IPROC>(SDL_GL_GetProcAddress("glUniform1i")); if(!glUniform1i)throw std::runtime_error("OpenGL function unavailable: glUniform1i");
        glUniformBlockBinding=reinterpret_cast<PFNGLUNIFORMBLOCKBINDINGPROC>(SDL_GL_GetProcAddress("glUniformBlockBinding")); if(!glUniformBlockBinding)throw std::runtime_error("OpenGL function unavailable: glUniformBlockBinding");
        glUseProgram=reinterpret_cast<PFNGLUSEPROGRAMPROC>(SDL_GL_GetProcAddress("glUseProgram")); if(!glUseProgram)throw std::runtime_error("OpenGL function unavailable: glUseProgram");
        glVertexAttribPointer=reinterpret_cast<PFNGLVERTEXATTRIBPOINTERPROC>(SDL_GL_GetProcAddress("glVertexAttribPointer")); if(!glVertexAttribPointer)throw std::runtime_error("OpenGL function unavailable: glVertexAttribPointer");
    }
};
}
