#pragma once
#ifdef __EMSCRIPTEN__
// WebGL 2 through Emscripten: every entry point links directly.
#include <GLES3/gl3.h>
inline bool loadGL() { return true; }
#else
// Minimal OpenGL 3.3 loader: the entry points opengl32.dll doesn't export.
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <GL/wglext.h>

#define GL_FUNCTIONS(X)                                                    \
    X(PFNGLACTIVETEXTUREPROC, glActiveTexture)                             \
    X(PFNGLGENBUFFERSPROC, glGenBuffers)                                   \
    X(PFNGLDELETEBUFFERSPROC, glDeleteBuffers)                             \
    X(PFNGLBINDBUFFERPROC, glBindBuffer)                                   \
    X(PFNGLBUFFERDATAPROC, glBufferData)                                   \
    X(PFNGLBUFFERSUBDATAPROC, glBufferSubData)                             \
    X(PFNGLGENVERTEXARRAYSPROC, glGenVertexArrays)                         \
    X(PFNGLBINDVERTEXARRAYPROC, glBindVertexArray)                         \
    X(PFNGLENABLEVERTEXATTRIBARRAYPROC, glEnableVertexAttribArray)         \
    X(PFNGLVERTEXATTRIBPOINTERPROC, glVertexAttribPointer)                 \
    X(PFNGLCREATESHADERPROC, glCreateShader)                               \
    X(PFNGLSHADERSOURCEPROC, glShaderSource)                               \
    X(PFNGLCOMPILESHADERPROC, glCompileShader)                             \
    X(PFNGLGETSHADERIVPROC, glGetShaderiv)                                 \
    X(PFNGLGETSHADERINFOLOGPROC, glGetShaderInfoLog)                       \
    X(PFNGLCREATEPROGRAMPROC, glCreateProgram)                             \
    X(PFNGLATTACHSHADERPROC, glAttachShader)                               \
    X(PFNGLLINKPROGRAMPROC, glLinkProgram)                                 \
    X(PFNGLGETPROGRAMIVPROC, glGetProgramiv)                               \
    X(PFNGLGETPROGRAMINFOLOGPROC, glGetProgramInfoLog)                     \
    X(PFNGLUSEPROGRAMPROC, glUseProgram)                                   \
    X(PFNGLGETUNIFORMLOCATIONPROC, glGetUniformLocation)                   \
    X(PFNGLUNIFORM1IPROC, glUniform1i)                                     \
    X(PFNGLUNIFORM1FPROC, glUniform1f)                                     \
    X(PFNGLUNIFORM2FPROC, glUniform2f)                                     \
    X(PFNGLUNIFORM3FPROC, glUniform3f)                                     \
    X(PFNGLUNIFORM4FPROC, glUniform4f)                                     \
    X(PFNGLUNIFORM3FVPROC, glUniform3fv)                                   \
    X(PFNGLUNIFORM4FVPROC, glUniform4fv)                                   \
    X(PFNGLUNIFORMMATRIX4FVPROC, glUniformMatrix4fv)                       \
    X(PFNGLGENFRAMEBUFFERSPROC, glGenFramebuffers)                         \
    X(PFNGLBINDFRAMEBUFFERPROC, glBindFramebuffer)                         \
    X(PFNGLFRAMEBUFFERTEXTURE2DPROC, glFramebufferTexture2D)               \
    X(PFNGLFRAMEBUFFERRENDERBUFFERPROC, glFramebufferRenderbuffer)         \
    X(PFNGLGENRENDERBUFFERSPROC, glGenRenderbuffers)                       \
    X(PFNGLBINDRENDERBUFFERPROC, glBindRenderbuffer)                       \
    X(PFNGLRENDERBUFFERSTORAGEPROC, glRenderbufferStorage)                 \
    X(PFNGLBLITFRAMEBUFFERPROC, glBlitFramebuffer)                         \
    X(PFNWGLSWAPINTERVALEXTPROC, wglSwapIntervalEXT)

#define X(type, name) extern type name;
GL_FUNCTIONS(X)
#undef X

bool loadGL();
#endif
