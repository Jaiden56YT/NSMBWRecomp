#pragma once
/* libnx has no dynamic loader: every dlopen fails, so Dawn falls back to the
   procedures the application hands it (RequestAdapterOptionsGetGLProc). */
#define RTLD_LAZY 1
#define RTLD_NOW 2
#define RTLD_NOLOAD 4
#define RTLD_LOCAL 0
#define RTLD_GLOBAL 8
#ifdef __cplusplus
extern "C" {
#endif
static inline void* dlopen(const char* f, int m) { (void)f; (void)m; return 0; }
static inline void* dlsym(void* h, const char* s) { (void)h; (void)s; return 0; }
static inline int dlclose(void* h) { (void)h; return 0; }
static inline char* dlerror(void) { return (char*)"no dynamic libraries on the Switch"; }
#ifdef __cplusplus
}
#endif
