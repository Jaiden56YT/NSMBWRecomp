// Dawn-on-Switch probe: can Dawn's OpenGL backend (on Mesa/nouveau via EGL)
// create a device, a swap chain on the Switch's window, compile a WGSL shader
// through Tint's GLSL writer, and render? Every step is logged to
// sdmc:/switch/dawnprobe/probe.log. Press + to quit early.
#include <switch.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GL/gl.h>
#include <dawn/webgpu_cpp.h>
#include <dawn/native/OpenGLBackend.h>
#include <cstdio>
#include <cstdarg>
#include <sys/stat.h>
#include <cstring>
#include <string>
#include <chrono>

static FILE* g_log;
static void L(const char* fmt, ...) {
    va_list ap; va_start(ap, fmt);
    vfprintf(g_log, fmt, ap); va_end(ap);
    fputc('\n', g_log); fflush(g_log);
}
static std::string S(wgpu::StringView v) {
    if (!v.data) return "";
    return v.length == WGPU_STRLEN ? std::string(v.data) : std::string(v.data, v.length);
}

static void probe_gl_version(EGLDisplay dpy) {
    // Our own short-lived context, just to read what Mesa offers.
    eglBindAPI(EGL_OPENGL_API);
    EGLint cfgAttr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg; EGLint n = 0;
    if (!eglChooseConfig(dpy, cfgAttr, &cfg, 1, &n) || n == 0) { L("gl: no EGL config"); return; }
    const int versions[][2] = {{4, 6}, {4, 5}, {4, 4}, {4, 3}};
    for (auto& v : versions) {
        EGLint ctxAttr[] = {EGL_CONTEXT_MAJOR_VERSION, v[0], EGL_CONTEXT_MINOR_VERSION, v[1],
                            EGL_CONTEXT_OPENGL_PROFILE_MASK, EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT, EGL_NONE};
        EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctxAttr);
        if (ctx == EGL_NO_CONTEXT) { L("gl: core %d.%d context: no (0x%X)", v[0], v[1], eglGetError()); continue; }
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx);
        auto getString = reinterpret_cast<const GLubyte* (*)(GLenum)>(eglGetProcAddress("glGetString"));
        L("gl: core %d.%d context: yes; GL_VERSION=%s GLSL=%s RENDERER=%s", v[0], v[1],
          getString ? (const char*)getString(GL_VERSION) : "?", getString ? (const char*)getString(GL_SHADING_LANGUAGE_VERSION) : "?",
          getString ? (const char*)getString(GL_RENDERER) : "?");
        eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        eglDestroyContext(dpy, ctx);
        break;
    }
}

static const char* kWgsl = R"(
@vertex fn vs(@builtin(vertex_index) i : u32) -> @builtin(position) vec4f {
    var p = array<vec2f, 3>(vec2f(0.0, 0.6), vec2f(-0.6, -0.5), vec2f(0.6, -0.5));
    return vec4f(p[i], 0.0, 1.0);
}
@fragment fn fs() -> @location(0) vec4f { return vec4f(1.0, 0.85, 0.1, 1.0); }
)";

int main(int, char**) {
    fsdevMountSdmc();
    mkdir("sdmc:/switch/dawnprobe", 0777);
    g_log = fopen("sdmc:/switch/dawnprobe/probe.log", "w");
    freopen("sdmc:/switch/dawnprobe/probe_stderr.log", "w", stderr);  // Dawn warnings
    setvbuf(stderr, nullptr, _IONBF, 0);
    if (!g_log) g_log = stdout;
    L("dawnprobe: start");

    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad; padInitializeDefault(&pad);

    // 1. EGL
    EGLDisplay dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    EGLint maj = 0, min = 0;
    if (!eglInitialize(dpy, &maj, &min)) { L("egl: eglInitialize failed 0x%X", eglGetError()); return 0; }
    L("egl: %d.%d vendor=%s", maj, min, eglQueryString(dpy, EGL_VENDOR));
    const char* ext = eglQueryString(dpy, EGL_EXTENSIONS);
    L("egl: extensions: %s", ext ? ext : "(none)");
    L("egl: create_context_robustness=%s fence_sync=%s reusable_sync=%s",
      ext && strstr(ext, "EGL_EXT_create_context_robustness") ? "yes" : "NO",
      ext && strstr(ext, "EGL_KHR_fence_sync") ? "yes" : "NO", ext && strstr(ext, "EGL_KHR_reusable_sync") ? "yes" : "NO");
    probe_gl_version(dpy);

    // 2. Dawn instance + adapter (OpenGL, compatibility mode, procs from EGL)
    wgpu::InstanceDescriptor idesc{};
    wgpu::Instance instance = wgpu::CreateInstance(&idesc);
    L("dawn: instance %s", instance ? "ok" : "FAILED");
    if (!instance) return 0;

    dawn::native::opengl::RequestAdapterOptionsGetGLProc glProc;
    glProc.getProc = reinterpret_cast<dawn::native::opengl::EGLGetProcProc>(eglGetProcAddress);
    glProc.display = dpy;
    wgpu::RequestAdapterOptions aopts{};
    aopts.nextInChain = &glProc;
    aopts.backendType = wgpu::BackendType::OpenGL;
    aopts.featureLevel = wgpu::FeatureLevel::Compatibility;
    wgpu::Adapter adapter;
    bool done = false;
    instance.RequestAdapter(&aopts, wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::RequestAdapterStatus st, wgpu::Adapter a, wgpu::StringView msg) {
            L("dawn: RequestAdapter status=%d msg=%s", (int)st, S(msg).c_str());
            adapter = std::move(a); done = true;
        });
    for (int i = 0; i < 1000 && !done; i++) { instance.ProcessEvents(); svcSleepThread(1000000); }
    if (!adapter) { L("dawn: no adapter - stop"); fclose(g_log); return 0; }
    wgpu::AdapterInfo info{};
    adapter.GetInfo(&info);
    L("dawn: adapter vendor=%s device=%s description=%s backend=%d", S(info.vendor).c_str(), S(info.device).c_str(), S(info.description).c_str(), (int)info.backendType);
    wgpu::Limits limits{};
    adapter.GetLimits(&limits);
    L("dawn: limits maxTextureDimension2D=%u maxBindGroups=%u maxSampledTexturesPerShaderStage=%u maxUniformBuffersPerShaderStage=%u maxStorageBuffersPerShaderStage=%u",
      limits.maxTextureDimension2D, limits.maxBindGroups, limits.maxSampledTexturesPerShaderStage, limits.maxUniformBuffersPerShaderStage, limits.maxStorageBuffersPerShaderStage);

    // 3. Device
    wgpu::DeviceDescriptor ddesc{};
    ddesc.SetUncapturedErrorCallback([](const wgpu::Device&, wgpu::ErrorType t, wgpu::StringView m) {
        L("dawn: UNCAPTURED ERROR type=%d: %s", (int)t, S(m).c_str());
    });
    ddesc.SetDeviceLostCallback(wgpu::CallbackMode::AllowProcessEvents, [](const wgpu::Device&, wgpu::DeviceLostReason r, wgpu::StringView m) {
        L("dawn: DEVICE LOST reason=%d: %s", (int)r, S(m).c_str());
    });
    wgpu::Device device; done = false;
    adapter.RequestDevice(&ddesc, wgpu::CallbackMode::AllowProcessEvents,
        [&](wgpu::RequestDeviceStatus st, wgpu::Device d, wgpu::StringView msg) {
            L("dawn: RequestDevice status=%d msg=%s", (int)st, S(msg).c_str());
            device = std::move(d); done = true;
        });
    for (int i = 0; i < 1000 && !done; i++) { instance.ProcessEvents(); svcSleepThread(1000000); }
    if (!device) { L("dawn: no device - stop"); fclose(g_log); return 0; }

    // 4. Surface on the Switch's window
    wgpu::SurfaceSourceAndroidNativeWindow src{};
    src.window = nwindowGetDefault();
    wgpu::SurfaceDescriptor sdesc{};
    sdesc.nextInChain = &src;
    wgpu::Surface surface = instance.CreateSurface(&sdesc);
    L("dawn: surface %s", surface ? "ok" : "FAILED");
    wgpu::SurfaceCapabilities caps{};
    surface.GetCapabilities(adapter, &caps);
    L("dawn: surface formats %zu, first %d", caps.formatCount, caps.formatCount ? (int)caps.formats[0] : -1);
    if (!caps.formatCount) { fclose(g_log); return 0; }
    wgpu::SurfaceConfiguration cfg{};
    cfg.device = device; cfg.format = caps.formats[0]; cfg.usage = wgpu::TextureUsage::RenderAttachment;
    cfg.width = 1280; cfg.height = 720; cfg.presentMode = wgpu::PresentMode::Fifo;
    surface.Configure(&cfg);
    L("dawn: surface configured 1280x720");

    // 5. WGSL pipeline (Tint -> GLSL)
    wgpu::ShaderSourceWGSL wgsl{}; wgsl.code = kWgsl;
    wgpu::ShaderModuleDescriptor smd{}; smd.nextInChain = &wgsl;
    wgpu::ShaderModule sm = device.CreateShaderModule(&smd);
    wgpu::ColorTargetState ct{}; ct.format = cfg.format;
    wgpu::FragmentState fst{}; fst.module = sm; fst.entryPoint = "fs"; fst.targetCount = 1; fst.targets = &ct;
    wgpu::RenderPipelineDescriptor rpd{};
    rpd.vertex.module = sm; rpd.vertex.entryPoint = "vs"; rpd.fragment = &fst;
    auto t0 = std::chrono::steady_clock::now();
    wgpu::RenderPipeline pipe = device.CreateRenderPipeline(&rpd);
    auto t1 = std::chrono::steady_clock::now();
    L("dawn: pipeline %s (%.1f ms)", pipe ? "ok" : "FAILED", std::chrono::duration<double, std::milli>(t1 - t0).count());

    // 6. Render frames
    wgpu::Queue queue = device.GetQueue();
    auto start = std::chrono::steady_clock::now();
    int frames = 0;
    for (; frames < 600 && appletMainLoop(); frames++) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        wgpu::SurfaceTexture st{};
        surface.GetCurrentTexture(&st);
        if (!st.texture) { L("dawn: frame %d: no surface texture (status %d)", frames, (int)st.status); break; }
        wgpu::TextureView view = st.texture.CreateView();
        float c = (frames % 120) / 120.0f;
        wgpu::RenderPassColorAttachment ca{};
        ca.view = view; ca.loadOp = wgpu::LoadOp::Clear; ca.storeOp = wgpu::StoreOp::Store;
        ca.clearValue = {0.1, 0.2 + 0.5 * c, 0.6, 1.0};
        wgpu::RenderPassDescriptor rp{}; rp.colorAttachmentCount = 1; rp.colorAttachments = &ca;
        wgpu::CommandEncoder enc = device.CreateCommandEncoder();
        wgpu::RenderPassEncoder pass = enc.BeginRenderPass(&rp);
        pass.SetPipeline(pipe); pass.Draw(3); pass.End();
        wgpu::CommandBuffer cb = enc.Finish();
        queue.Submit(1, &cb);
        surface.Present();
        instance.ProcessEvents();
        if (frames == 0 || frames == 59 || frames == 299) {
            double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
            L("dawn: frame %d presented, %.1f fps so far", frames + 1, (frames + 1) / s);
        }
    }
    double s = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
    L("dawn: done, %d frames in %.1f s (%.1f fps)", frames, s, frames / s);
    fclose(g_log);
    return 0;
}
