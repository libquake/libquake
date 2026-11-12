cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/tests)

project(
  quakelib
  VERSION 1.0.0
  LANGUAGES CXX
)

set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
option(QLIB_BUILD_VIEWER "Build qviewer tool" ON)
option(QLIB_BUILD_TESTS "Build tests" ON)
option(QLIB_BUILD_EXAMPLES "Build the examples used by the documentation" OFF)

add_subdirectory(src)

if(${QLIB_BUILD_VIEWER})
  add_subdirectory(tools/qviewer)
endif()

if(${QLIB_BUILD_TESTS})
  add_subdirectory(tests)
endif()

if(${QLIB_BUILD_EXAMPLES})
  add_subdirectory(examples)
endif()

set_property(TARGET ${PROJECT_NAME} PROPERTY VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")
