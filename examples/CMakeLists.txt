# The examples. Inside this repository's build the library target already exists; on their own
# (cmake -S examples -B build-examples -DCMAKE_PREFIX_PATH=<install prefix>) they find the
# installed package, as a program using the library would.
cmake_minimum_required(VERSION 3.21)
project(gswavetable_examples LANGUAGES CXX)

if(NOT TARGET gswavetable::gswavetable)
  find_package(gswavetable 0.1 REQUIRED)
endif()

foreach(example render_wav audio_callback)
  add_executable(${example} ${example}.cpp)
  target_link_libraries(${example} PRIVATE gswavetable::gswavetable)
  set_target_properties(${example} PROPERTIES CXX_EXTENSIONS OFF)
  if(DEFINED gswavetable_options) # this repository's warnings
    target_compile_options(${example} PRIVATE ${gswavetable_options})
  endif()
  if(EMSCRIPTEN)
    target_link_options(${example} PRIVATE -sNODERAWFS=1 -sALLOW_MEMORY_GROWTH=1 -sEXIT_RUNTIME=1)
  endif()
endforeach()
