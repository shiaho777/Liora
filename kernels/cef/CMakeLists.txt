cmake_minimum_required(VERSION 3.24)

# The CEF kernel is optional: it needs a CEF binary distribution, which is
# downloaded rather than vendored. Point LIORA_CEF_ROOT at an extracted
# distribution, or set the CEF_ROOT environment variable.
#
#   cmake -S . -B build -DLIORA_CEF_ROOT=/path/to/cef_binary_...
#
# This builds and links the adapter. Running it still needs the Chromium helper
# app bundles, which are not produced yet; see README.md in this directory.

if(NOT APPLE)
  message(STATUS "CEF: only wired up for macOS so far")
  return()
endif()

if(NOT LIORA_CEF_ROOT)
  message(STATUS "CEF: LIORA_CEF_ROOT not set, skipping the Chromium kernel")
  return()
endif()

if(NOT EXISTS "${LIORA_CEF_ROOT}/include/cef_app.h")
  message(FATAL_ERROR "CEF: ${LIORA_CEF_ROOT} does not look like a CEF distribution")
endif()

set(CEF_ROOT "${LIORA_CEF_ROOT}" CACHE PATH "CEF binary distribution" FORCE)
list(APPEND CMAKE_MODULE_PATH "${LIORA_CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory("${CEF_LIBCEF_DLL_WRAPPER_PATH}" "${CMAKE_BINARY_DIR}/cef_wrapper")

add_library(liora_cef_kernel STATIC cef_kernel.mm)
set_source_files_properties(cef_kernel.mm PROPERTIES
  COMPILE_FLAGS "-fobjc-arc -Wno-missing-field-initializers")
target_include_directories(liora_cef_kernel
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CEF_INCLUDE_PATH})
target_link_libraries(liora_cef_kernel
  PUBLIC liora_core
  PRIVATE libcef_dll_wrapper "-framework Cocoa")

message(STATUS "CEF: building the Chromium kernel adapter against ${CEF_VERSION}")
