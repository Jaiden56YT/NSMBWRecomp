# deko3d backend for the Switch build: plan

## Why

On the Switch, aurora draws through Dawn's **OpenGL** backend in WebGPU compatibility mode,
on Mesa's nouveau GL driver: WebGPU validation → GL calls → Mesa's GL state tracker → Gallium
nvc0 → the GPU. deko3d (devkitPro, open source, installed: deko3d 0.5.0, uam 1.1.0) talks to
the GPU almost directly: command buffers, memory blocks and precompiled shaders, close to what
Nintendo's own API does, without using any of Nintendo's code.

What to expect (run 48 profile, levels, CPU at max clock):
- Game thread: Dawn/Mesa is only ~1–2% (mostly Dawn's device lock), so **level fps gains are
  small, maybe 1–3 fps**. The game thread is bound by translated guest code.
- Frame worker: idle ~40%; most of its busy time is Dawn + GL + Mesa. deko3d should cut that
  hard, which gives headroom and fewer late frames.
- Shader compiles: GLSL compiled by Mesa at first use (hitches). With deko3d, shaders compile
  once to DKSH binaries cached on the SD card.
- Memory: Dawn + Mesa use a lot of heap (the malloc arena is ~800 MiB); deko3d memory blocks
  are explicit, so this should drop.

## Approach: a WebGPU subset on deko3d ("dk-webgpu")

aurora uses about 45 WebGPU entry points (CreateTexture/View/Buffer/Sampler/BindGroup(Layout)/
PipelineLayout/ShaderModule/RenderPipeline/ComputePipeline/QuerySet, BeginRenderPass/
ComputePass, SetPipeline/BindGroup/Viewport/Scissor/BlendConstant/IndexBuffer, Draw/
DrawIndexed/DispatchWorkgroups, Copy{Buffer,Texture}To{Buffer,Texture}, Write{Buffer,Texture},
MapAsync/GetMappedRange/Unmap, Submit, WaitAny/ProcessEvents, surface Configure/
GetCurrentTexture/Present). Rather than rewriting aurora's ~20k lines of wgpu code, implement
**that subset of the webgpu.h C API on deko3d** and link it instead of Dawn on the Switch.
aurora stays untouched, and the Mac build keeps Dawn/Metal.

Pieces:
1. **Objects**: textures/buffers in `DkMemBlock`s (a simple sub-allocator), image
   descriptors + sampler descriptors in descriptor sets, bind groups → descriptor indices +
   uniform buffer bindings.
2. **Render pipelines** → precompiled vertex/fragment `DkShader`s + deko3d state objects
   (rasterizer, color, color-write, blend, depth-stencil), applied when bound.
3. **Command encoders** → one `dk::CmdBuf` per frame, recorded directly on the worker
   thread (no validation layer); queue submit + fences for MapAsync/WaitAny.
4. **Swapchain** → `dk::Swapchain` on the default `NWindow`, replacing EGL.
5. **Shaders**: aurora generates WGSL. Tint (already in our Dawn build) turns WGSL into GLSL,
   and **uam**'s compiler turns GLSL into DKSH. uam is a host tool, so it must be built as a
   library for the Switch (it is Mesa's GLSL front end + nouveau's Maxwell codegen, which our
   mesa-switch build already compiles). DKSH results go into a disk cache keyed by WGSL hash,
   pre-seeded like the current pipeline cache.

## Steps (each tested on hardware via the station)

1. **Proof of life**: a tiny deko3d NRO path inside the app (behind `NSMBW_DEKO3D=1`) that
   clears the screen and draws one triangle from a uam-compiled shader. Confirms deko3d
   coexists with our libnx/SDL setup.
2. **On-device shader compile**: build uam as a Switch library, compile one Tint-generated
   GLSL shader from aurora at runtime, draw with it.
3. **dk-webgpu core**: buffers, textures, samplers, bind groups, render pipelines, render
   passes, Draw/DrawIndexed, Submit, swapchain. Target: the title screen renders correctly.
4. **The rest**: copies (EFB copies), MapAsync readback (EFB peeks, depth peek), compute pass,
   query sets (timestamps; can be stubbed), texture uploads.
5. **Compare** against Dawn runs: fps, frame-worker busy %, shader hitches, memory. Keep Dawn
   as a runtime fallback until deko3d matches it everywhere.

## Risks
- uam on-device: it has never been shipped as a Switch library; may need trimming.
- WebGPU semantics aurora relies on (implicit barriers, texture usage transitions, mapped
  buffer timing) must be reproduced by hand with deko3d barriers.
- Effort: weeks, not days. Steps 1–2 decide whether it is worth continuing.

## Needs from outside the repo
- uam source (github.com/devkitPro/uam) to build it as a library: download needs the user's OK.
