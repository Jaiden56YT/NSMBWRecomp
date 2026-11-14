# Dawn (WebGPU) on the Switch - probe

aurora (NSMBWiicompiled's renderer) needs Dawn; this builds Dawn's OpenGL backend for libnx.

- Dawn commit 13abc3bc8ea2d3c2050f9e77a12d012108ceee24 (aurora's v20260603.191052), cloned into
  third_party/dawn-switch/dawn, deps via `python3 tools/fetch_dawn_dependencies.py -s`.
- `dawn.patch`: Switch platform (DAWN_PLATFORM_IS(SWITCH), POSIX), no executable/module path,
  the native-window surface path enabled for the Switch (NWindow* via SurfaceSourceAndroidNativeWindow
  -> eglCreateWindowSurface).
- `abseil.patch`: no ELF mem image, UTC-only tm_gmtoff/tm_zone, no signal masking, GetTID from pthread_self.
- `dlfcn.h`: stub loader (Dawn gets EGL/GL procs from the app via RequestAdapterOptionsGetGLProc).
- Configure (flags must include the toolchain's own; CMAKE_*_FLAGS_INIT gets overridden):
      F="-march=armv8-a+crc+crypto -mtune=cortex-a57 -mtp=soft -ftls-model=local-exec -ffunction-sections -fdata-sections -D__SWITCH__ -D_GNU_SOURCE -DEGL_NO_PLATFORM_SPECIFIC_TYPES -I<switch_compat>"
      cmake -S dawn -B build -G Ninja -DCMAKE_TOOLCHAIN_FILE=/opt/devkitpro/cmake/Switch.cmake -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="$F" -DCMAKE_C_FLAGS="$F" -DDAWN_ENABLE_DESKTOP_GL=ON -DDAWN_ENABLE_OPENGLES=OFF -DDAWN_ENABLE_VULKAN=OFF \
        -DDAWN_ENABLE_METAL=OFF -DDAWN_ENABLE_NULL=ON -DDAWN_BUILD_MONOLITHIC_LIBRARY=STATIC -DDAWN_FETCH_DEPENDENCIES=OFF \
        -DDAWN_BUILD_SAMPLES=OFF -DDAWN_BUILD_TESTS=OFF -DDAWN_USE_X11=OFF -DDAWN_USE_WAYLAND=OFF -DDAWN_USE_GLFW=OFF \
        -DTINT_BUILD_TESTS=OFF -DTINT_BUILD_CMD_TOOLS=OFF -DTINT_BUILD_SPV_READER=OFF -DTINT_BUILD_SPV_WRITER=OFF \
        -DTINT_BUILD_MSL_WRITER=OFF -DTINT_BUILD_HLSL_WRITER=OFF -DTINT_BUILD_GLSL_WRITER=ON -DTINT_BUILD_GLSL_VALIDATOR=OFF
      ninja -C build webgpu_dawn       # ~3 min, libwebgpu_dawn.a
- `probe.cpp`: EGL + GL version report, Dawn adapter/device on GL (compatibility level), swap chain on
  nwindowGetDefault(), WGSL triangle, 600 frames; logs to sdmc:/switch/dawnprobe/probe.log.
Status 2026-11-14: Dawn and the probe build; not yet run on hardware.
