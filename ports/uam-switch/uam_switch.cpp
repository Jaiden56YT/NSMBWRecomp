// C entry point for compiling GLSL to a DKSH image at runtime on the Switch (deko3d backend).
// Linked together with uam's objects into one relocatable object whose other symbols are made
// local (build.sh), so uam's Mesa copy cannot clash with the Mesa the game links.
#include "compiler_iface.h"
#include <cstdlib>
#include <cstring>
#include <string>

extern "C" int uam_compile_dksh(int stage, const char* glsl, void** outData, size_t* outSize) {
    static const pipeline_stage kStages[] = {pipeline_stage_vertex, pipeline_stage_fragment, pipeline_stage_compute};
    *outData = nullptr;
    *outSize = 0;
    if (stage < 0 || stage > 2) return 0;
    DekoCompiler compiler{kStages[stage]};
    if (!compiler.CompileGlsl(glsl)) return 0;
    std::string image;
    compiler.OutputDkshMemory(image);
    void* p = std::malloc(image.size());
    if (!p) return 0;
    std::memcpy(p, image.data(), image.size());
    *outData = p;
    *outSize = image.size();
    return 1;
}
