// Proof step 2: compile the triangle's GLSL on the Switch with uam (ports/uam-switch) instead of
// loading DKSH files made at build time. Writes timings to /switch/nsmbw2/nsmbw.log, which the
// station collects.
#include <switch.h>
#include <deko3d.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" int uam_compile_dksh(int stage, const char* glsl, void** outData, size_t* outSize);

static const char* kVertex = "#version 460\n"
    "\n"
    "// Hardcoded array of vertex positions for our triangle\n"
    "const vec4 positions[3] = vec4[](\n"
    "    vec4( 0.0, +1.0, 0.0, 1.0),\n"
    "    vec4(-1.0, -1.0, 0.0, 1.0),\n"
    "    vec4(+1.0, -1.0, 0.0, 1.0)\n"
    ");\n"
    "\n"
    "// Hardcoded array of vertex colors for our triangle\n"
    "const vec4 colors[3] = vec4[](\n"
    "    vec4(1.0, 0.0, 0.0, 1.0),\n"
    "    vec4(0.0, 1.0, 0.0, 1.0),\n"
    "    vec4(0.0, 0.0, 1.0, 1.0)\n"
    ");\n"
    "\n"
    "layout (location = 0) out vec4 outColor;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position = positions[gl_VertexID];\n"
    "    outColor = colors[gl_VertexID];\n"
    "}\n"
    "";
static const char* kFragment = "#version 460\n"
    "\n"
    "layout (location = 0) in vec4 inColor;\n"
    "layout (location = 0) out vec4 outColor;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    outColor = inColor;\n"
    "}\n"
    "";

// Compiles stage 0 (vertex) or 1 (fragment), copies the DKSH into code memory at *offset.
extern "C" bool compileShaderAtRuntime(DkShader* shader, int stage, DkMemBlock codeMem, uint32_t* offset) {
    const uint64_t t0 = armGetSystemTick();
    void* image = nullptr;
    size_t size = 0;
    const bool ok = uam_compile_dksh(stage, stage == 0 ? kVertex : kFragment, &image, &size) != 0;
    const double ms = armTicksToNs(armGetSystemTick() - t0) / 1e6;
    if (FILE* f = std::fopen("/switch/nsmbw2/nsmbw.log", "a")) {
        std::fprintf(f, "[deko3d-proof] uam %s shader: %s, %zu bytes, %.2f ms\n", stage == 0 ? "vertex" : "fragment",
                     ok ? "ok" : "FAILED", size, ms);
        std::fclose(f);
    }
    if (!ok) return false;
    const uint32_t at = *offset;
    std::memcpy(static_cast<uint8_t*>(dkMemBlockGetCpuAddr(codeMem)) + at, image, size);
    *offset += (size + DK_SHADER_CODE_ALIGNMENT - 1) & ~(DK_SHADER_CODE_ALIGNMENT - 1);
    std::free(image);
    DkShaderMaker maker;
    dkShaderMakerDefaults(&maker, codeMem, at);
    dkShaderInitialize(shader, &maker);
    return true;
}

// Step 3: compile every converted aurora shader in romfs:/aurora (Tint GLSL, sampler bindings
// added) on the device and log each result and time.
#include <dirent.h>
#include <string>
extern "C" void compileAuroraShaders(void) {
    FILE* log = std::fopen("/switch/nsmbw2/nsmbw.log", "a");
    DIR* d = opendir("romfs:/aurora");
    double total = 0, worst = 0;
    int ok = 0, failed = 0;
    while (struct dirent* e = d ? readdir(d) : nullptr) {
        const std::string name = e->d_name;
        const bool vert = name.size() > 5 && name.compare(name.size() - 5, 5, ".vert") == 0;
        const bool frag = name.size() > 5 && name.compare(name.size() - 5, 5, ".frag") == 0;
        if (!vert && !frag) continue;
        FILE* f = std::fopen(("romfs:/aurora/" + name).c_str(), "rb");
        if (!f) continue;
        std::string src;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) src.append(buf, n);
        std::fclose(f);
        const uint64_t t0 = armGetSystemTick();
        void* image = nullptr;
        size_t size = 0;
        const bool good = uam_compile_dksh(vert ? 0 : 1, src.c_str(), &image, &size) != 0;
        const double ms = armTicksToNs(armGetSystemTick() - t0) / 1e6;
        std::free(image);
        good ? ++ok : ++failed;
        total += ms;
        if (ms > worst) worst = ms;
        if (log) std::fprintf(log, "[deko3d-proof] aurora %s: %s, %zu bytes, %.2f ms\n", name.c_str(), good ? "ok" : "FAILED", size, ms);
    }
    if (d) closedir(d);
    if (log) {
        std::fprintf(log, "[deko3d-proof] aurora shaders: %d ok, %d failed, total %.1f ms, worst %.1f ms\n", ok, failed, total, worst);
        std::fclose(log);
    }
}
