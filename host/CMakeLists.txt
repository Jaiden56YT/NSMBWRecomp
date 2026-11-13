cmake_minimum_required(VERSION 3.16)
project(nsmbw_host C)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(DOLRECOMP ${ROOT}/third_party/DolRecomp)
set(GEN_DOL ${ROOT}/generated/dol/SMNE01_generated)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

file(GLOB DOL_CHUNKS ${GEN_DOL}/chunks/*.c)
add_library(gen_dol STATIC ${DOL_CHUNKS} dispatch_dol.c)
target_include_directories(gen_dol PUBLIC ${DOLRECOMP}/src ${GEN_DOL})
# The generated code is huge and machine-made: optimise lightly, keep it quiet.
target_compile_options(gen_dol PRIVATE -O1 -w)

# REL modules, recompiled at fixed addresses from 0x81800000 (beyond the
# 24 MB of MEM1 the game knows about; the host gives MEM1 32 MB).
set(GEN_RELS ${ROOT}/generated/rels/SMNE01_generated/rels)
foreach(MOD d_profileNP_1 d_basesNP_2 d_enemiesNP_3 d_en_bossNP_4)
  string(REGEX REPLACE ".*_([0-9])$" "\\1" ID ${MOD})
  file(GLOB CH ${GEN_RELS}/${MOD}/chunks/*.c)
  add_library(gen_rel${ID} STATIC ${CH} dispatch_rel${ID}.c)
  target_include_directories(gen_rel${ID} PRIVATE ${DOLRECOMP}/src ${GEN_RELS}/${MOD})
  target_compile_options(gen_rel${ID} PRIVATE -O1 -w)
  list(APPEND REL_LIBS gen_rel${ID})
endforeach()

# The CPU helpers (FP, paired singles, memory) from DolRecomp itself.
add_library(dr_cpu STATIC ${DOLRECOMP}/src/cpu/cpu.c)
target_include_directories(dr_cpu PUBLIC ${DOLRECOMP}/src)

find_package(SDL2 REQUIRED)
add_executable(nsmbw main.c boot.c mmio.c cpu_hooks.c hle.c ios.c gx.c render.c)
target_include_directories(nsmbw PRIVATE ${SDL2_INCLUDE_DIRS} /opt/homebrew/include)
target_compile_definitions(nsmbw PRIVATE GL_SILENCE_DEPRECATION)
target_compile_definitions(nsmbw PRIVATE DOLRECOMP_RUN_SLICE=4096)
target_link_libraries(nsmbw gen_dol ${REL_LIBS} dr_cpu m ${SDL2_LIBRARIES} "-framework OpenGL" z)
