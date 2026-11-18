add_executable(liora_tests
  main.cpp
  test_util.cpp
  test_settings.cpp
  test_dns_rules.cpp
  test_storage.cpp
  test_backup_migration.cpp
  test_components_rpc.cpp
  test_watches.cpp
  test_userscripts.cpp
)

target_include_directories(liora_tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/core/src
  ${PROJECT_SOURCE_DIR}/third_party/tweetnacl)

target_link_libraries(liora_tests PRIVATE liora_core liora_sqlite)

if(MSVC)
  target_compile_options(liora_tests PRIVATE /W4 /utf-8)
else()
  target_compile_options(liora_tests PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

add_test(NAME liora_tests COMMAND liora_tests)
