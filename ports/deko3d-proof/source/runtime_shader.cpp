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
