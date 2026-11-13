cmake_minimum_required(VERSION 3.21)
project(gswavetable VERSION 0.1.0 LANGUAGES CXX)

# The CLI, the examples, the tests and the install rules by default only when this is the top-level
# project, so that a project pulling the library in with FetchContent or add_subdirectory builds
# just the library.
option(GSWAVETABLE_BUILD_TOOLS "Build the gswavetable-render CLI" ${PROJECT_IS_TOP_LEVEL})
option(GSWAVETABLE_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(GSWAVETABLE_BUILD_TESTS "Build the tests" ${PROJECT_IS_TOP_LEVEL})
option(GSWAVETABLE_INSTALL "Generate the install rules (find_package(gswavetable))" ${PROJECT_IS_TOP_LEVEL})

include(GNUInstallDirs)

# Warnings, and no floating-point contraction: the tables and the articulation conversions use
# pow, log10 and sin, and a fused multiply-add could flip a rounding (the tables test catches it).
set(gswavetable_options
  $<$<CXX_COMPILER_ID:Clang,AppleClang,GNU>:-Wall;-Wextra;-Wpedantic;-Wconversion;-ffp-contract=off>
  $<$<CXX_COMPILER_ID:MSVC>:/W4;/fp:precise>)

add_library(gswavetable src/engine.cpp src/sequencer.cpp src/player.cpp)
add_library(gswavetable::gswavetable ALIAS gswavetable)
target_include_directories(gswavetable PUBLIC $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(gswavetable PUBLIC cxx_std_20)
set_target_properties(gswavetable PROPERTIES CXX_EXTENSIONS OFF)
target_compile_options(gswavetable PRIVATE ${gswavetable_options})
if(EMSCRIPTEN)
  # load_dls and Sequencer report bad input with exceptions, and Emscripten catches none unless
  # the code is built with exception handling: WebAssembly's own, which costs nothing until a
  # throw. Public, so that the code calling the library is built the same way.
  target_compile_options(gswavetable PUBLIC -fwasm-exceptions)
  target_link_options(gswavetable PUBLIC -fwasm-exceptions)
endif()

if(GSWAVETABLE_BUILD_TOOLS)
  add_executable(gswavetable-render tools/render.cpp)
  target_link_libraries(gswavetable-render PRIVATE gswavetable)
  set_target_properties(gswavetable-render PROPERTIES CXX_EXTENSIONS OFF)
  target_compile_options(gswavetable-render PRIVATE ${gswavetable_options})
  if(EMSCRIPTEN)
    target_link_options(gswavetable-render PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
  endif()
endif()

if(GSWAVETABLE_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

if(GSWAVETABLE_BUILD_TESTS)
  enable_testing()
  foreach(test tables sequencer resampler golden)
    add_executable(${test}_test tests/${test}_test.cpp)
    target_link_libraries(${test}_test PRIVATE gswavetable)
    set_target_properties(${test}_test PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_options(${test}_test PRIVATE ${gswavetable_options})
    target_compile_definitions(${test}_test PRIVATE $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>) # getenv
    if(EMSCRIPTEN)
      target_link_options(${test}_test PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
    endif()
  endforeach()
  set(data "${CMAKE_CURRENT_SOURCE_DIR}/tests/data")
  add_test(NAME tables COMMAND tables_test)
  add_test(NAME sequencer COMMAND sequencer_test)
  add_test(NAME resampler COMMAND resampler_test)
  add_test(NAME golden COMMAND golden_test "${data}")
  # The same on gm.dls when GSWAVETABLE_DLS names it; skipped otherwise.
  add_test(NAME golden_gm COMMAND golden_test "${data}" --gm)
  set_tests_properties(golden_gm PROPERTIES SKIP_RETURN_CODE 77)
  add_test(NAME readme_example COMMAND ${CMAKE_COMMAND} "-DREADME=${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "-DEXAMPLE=${CMAKE_CURRENT_SOURCE_DIR}/examples/render_wav.cpp" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/readme_example.cmake")
  if(GSWAVETABLE_BUILD_EXAMPLES)
    set(run_example ${CMAKE_COMMAND} "-DEMULATOR=${CMAKE_CROSSCOMPILING_EMULATOR}" "-DDATA=${data}"
      "-DOUT=${CMAKE_CURRENT_BINARY_DIR}/example-output")
    add_test(NAME example_render_wav COMMAND ${run_example} -DMODE=render_wav "-DEXAMPLE=$<TARGET_FILE:render_wav>"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/example_output.cmake")
    if(GSWAVETABLE_BUILD_TOOLS)
      add_test(NAME example_audio_callback COMMAND ${run_example} -DMODE=audio_callback
        "-DEXAMPLE=$<TARGET_FILE:audio_callback>" "-DRENDER=$<TARGET_FILE:gswavetable-render>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/example_output.cmake")
    endif()
  endif()
endif()

if(GSWAVETABLE_INSTALL)
  include(CMakePackageConfigHelpers)
  set(config_dir "${CMAKE_INSTALL_LIBDIR}/cmake/gswavetable")
  install(TARGETS gswavetable EXPORT gswavetableTargets)
  install(DIRECTORY include/gswavetable TYPE INCLUDE)
  install(EXPORT gswavetableTargets NAMESPACE gswavetable:: DESTINATION "${config_dir}")
  file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/gswavetableConfig.cmake"
    "include(\"\${CMAKE_CURRENT_LIST_DIR}/gswavetableTargets.cmake\")\n")
  write_basic_package_version_file("${CMAKE_CURRENT_BINARY_DIR}/gswavetableConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion)
  install(FILES "${CMAKE_CURRENT_BINARY_DIR}/gswavetableConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/gswavetableConfigVersion.cmake" DESTINATION "${config_dir}")
  install(FILES LICENSE README.md DESTINATION "${CMAKE_INSTALL_DOCDIR}")
  if(GSWAVETABLE_BUILD_TOOLS)
    install(TARGETS gswavetable-render)
    if(EMSCRIPTEN) # the .js loads the .wasm beside it
      install(FILES "$<TARGET_FILE_DIR:gswavetable-render>/gswavetable-render.wasm" TYPE BIN)
    endif()
  endif()
endif()
