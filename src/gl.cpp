#include "gl.h"

#define X(type, name) type name = nullptr;
GL_FUNCTIONS(X)
#undef X

bool loadGL() {
    bool ok = true;
#define X(type, name)                                                  \
    name = reinterpret_cast<type>(reinterpret_cast<void*>(wglGetProcAddress(#name))); \
    ok = ok && name;
    GL_FUNCTIONS(X)
#undef X
    return ok;
}
