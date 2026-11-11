cmake_minimum_required(VERSION 3.21)
project(gswavetable VERSION 0.1.0 LANGUAGES CXX)

# The CLI and the tests by default only when this is the top-level project, so that a project
# pulling the library in with FetchContent or add_subdirectory builds just the library.
option(GSWAVETABLE_BUILD_TOOLS "Build the gswavetable-render CLI" ${PROJECT_IS_TOP_LEVEL})
option(GSWAVETABLE_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})

# Warnings, and no floating-point contraction: the tables and the articulation conversions use
# pow, log10 and sin, and a fused multiply-add could flip a rounding (the tables test catches it).
set(gswavetable_options
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-ffp-contract=off>
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/fp:precise>)

add_library(gswavetable src/engine.cpp src/sequencer.cpp src/player.cpp)
add_library(gswavetable::gswavetable ALIAS gswavetable)
target_include_directories(gswavetable PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(gswavetable PUBLIC cxx_std_20)
set_target_properties(gswavetable PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(gswavetable PRIVATE ${gswavetable_options})

if(GSWAVETABLE_BUILD_TOOLS)
  add_executable(gswavetable-render tools/render.cpp)
  target_link_libraries(gswavetable-render PRIVATE gswavetable)
  set_target_properties(gswavetable-render PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(gswavetable-render PRIVATE ${gswavetable_options})
  if(EMSCRIPTEN)
    target_link_options(gswavetable-render PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
  endif()
endif()

