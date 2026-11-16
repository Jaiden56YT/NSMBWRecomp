# switch-mesa with the shader cache on

devkitPro's switch-mesa (20.1.0-5) is built with Mesa's GLSL shader cache disabled, so every GL
program Dawn creates is compiled from GLSL on every launch (about 0.15–0.2 s each on the Switch;
run 23's world map entry spent ~10 s on ~55 of them).

`shader-cache.patch` (on top of devkitPro's patches in `devkitpro/`):
- turns `ENABLE_SHADER_CACHE` on for horizon, but with no cache directory: `disk_cache_create`
  returns a callback-only cache (no mmap, getpwuid, flock, dirfd on newlib);
- gives nouveau a cache id from its compile time (no dladdr/build-id on the Switch);
- exposes `EGL_ANDROID_blob_cache` in the Switch EGL driver, so the application stores entries;
- raises the blob read buffer to 1 MB and rejects oversize answers.

aurora (`lib/webgpu/gpu.cpp`, Switch only) installs `eglSetBlobCacheFuncsANDROID` callbacks that
keep the entries in its SQLite graphics cache (`nsmbw_data/Cache/dawn_cache.db`, keys prefixed
with `mesa`). A cache hit skips GLSL compile, link and GLSL-to-TGSI; nouveau's code generation
still runs (about a third of the original cost). `AURORA_NO_MESA_SHADER_CACHE=1` in env.txt turns
it off. Dawn's own program-binary cache is switched off while this is on.

Build: `ports/mesa-switch/build.sh`, then re-run cmake in `third_party/NSMBWiicompiled/build_switch`.
