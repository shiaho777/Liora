cmake_minimum_required(VERSION 3.24)

project(Liora VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(LIORA_BUILD_TESTS "Build Liora Core tests" ON)
option(LIORA_BUILD_SHELL "Build the platform shell" ON)

include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  sqlite3
  URL https://www.sqlite.org/2026/sqlite-amalgamation-3540100.zip
  DOWNLOAD_EXTRACT_TIMESTAMP ON
)
FetchContent_MakeAvailable(sqlite3)

add_library(liora_sqlite STATIC ${sqlite3_SOURCE_DIR}/sqlite3.c)
target_include_directories(liora_sqlite PUBLIC ${sqlite3_SOURCE_DIR})
target_compile_definitions(liora_sqlite PRIVATE
  SQLITE_DQS=0
  SQLITE_THREADSAFE=1
  SQLITE_DEFAULT_WAL_SYNCHRONOUS=1
  SQLITE_OMIT_DEPRECATED
)
set_target_properties(liora_sqlite PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(APPLE)
  enable_language(OBJCXX)
endif()

add_subdirectory(core)

if(APPLE AND LIORA_CEF_ROOT)
  add_subdirectory(kernels/cef)
endif()

if(LIORA_BUILD_TESTS)
  enable_testing()
  add_subdirectory(core/tests)
endif()

if(LIORA_BUILD_SHELL)
  add_subdirectory(shell)
endif()
