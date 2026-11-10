set(LIORA_EMBED_DIR ${CMAKE_CURRENT_BINARY_DIR}/embedded)
file(MAKE_DIRECTORY ${LIORA_EMBED_DIR})

function(liora_embed input symbol out_var)
  set(output ${LIORA_EMBED_DIR}/${symbol}.cpp)
  add_custom_command(
    OUTPUT ${output}
    COMMAND ${CMAKE_COMMAND} -DINPUT=${input} -DOUTPUT=${output} -DSYMBOL=${symbol}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    DEPENDS ${input} ${CMAKE_CURRENT_SOURCE_DIR}/cmake/embed.cmake
    VERBATIM)
  set(${out_var} ${output} PARENT_SCOPE)
endfunction()

liora_embed(${PROJECT_SOURCE_DIR}/data/dns-providers.json dns_providers EMBED_DNS)
liora_embed(${PROJECT_SOURCE_DIR}/data/rule-presets.json rule_presets EMBED_PRESETS)
liora_embed(${PROJECT_SOURCE_DIR}/data/search-engines.json search_engines EMBED_SEARCH)

add_library(liora_core STATIC
  src/util.cpp
  src/paths.cpp
  src/settings.cpp
  src/dns.cpp
  src/rules.cpp
  src/netplan.cpp
  src/storage.cpp
  src/backup.cpp
  src/plist.cpp
  src/migration/common.cpp
  src/migration/chromium.cpp
  src/migration/firefox.cpp
  src/migration/safari.cpp
  src/migration/html.cpp
  src/migration/migrator.cpp
  src/kernel.cpp
  src/components.cpp
  src/crypto.cpp
  ${PROJECT_SOURCE_DIR}/third_party/tweetnacl/tweetnacl.c
  src/rpc.cpp
  src/core.cpp
  src/core_data.cpp
  src/core_components.cpp
  ${EMBED_DNS}
  ${EMBED_PRESETS}
  ${EMBED_SEARCH}
)

if(MSVC)
  set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/tweetnacl/tweetnacl.c PROPERTIES COMPILE_OPTIONS "/w")
else()
  set_source_files_properties(${PROJECT_SOURCE_DIR}/third_party/tweetnacl/tweetnacl.c PROPERTIES COMPILE_OPTIONS "-w")
endif()

target_include_directories(liora_core
  PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src ${PROJECT_SOURCE_DIR}/third_party/tweetnacl)
target_link_libraries(liora_core
  PUBLIC nlohmann_json::nlohmann_json
  PRIVATE liora_sqlite)
target_compile_definitions(liora_core PUBLIC LIORA_VERSION="${PROJECT_VERSION}")
set_target_properties(liora_core PROPERTIES POSITION_INDEPENDENT_CODE ON)

if(MSVC)
  target_compile_options(liora_core PRIVATE /W4 /utf-8 /permissive-)
else()
  target_compile_options(liora_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()