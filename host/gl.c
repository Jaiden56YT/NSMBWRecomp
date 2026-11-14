/* Loads the GL function pointers declared in gl.h (Switch only). */
#ifdef __SWITCH__
#include "gl.h"
#include <stdio.h>

#define GL_DEFINE(type, name) type p_##name;
GL_LIST(GL_DEFINE)

int gl_load(void* (*get_proc)(const char*)) {
    int missing = 0;
#define GL_FETCH(type, name) p_##name = (type)get_proc(#name); if (!p_##name) { printf("gl: missing %s\n", #name); missing++; }
    GL_LIST(GL_FETCH)
    return missing == 0;
}
#endif
