cmake_minimum_required(VERSION 3.24)

if(NOT APPLE)
  message(STATUS "The macOS shell is only built on Apple platforms")
  return()
endif()

enable_language(OBJCXX)

add_executable(liora
  macos/main.mm
  macos/SystemKernel.mm
)

set_source_files_properties(macos/main.mm macos/SystemKernel.mm
  PROPERTIES COMPILE_FLAGS "-fobjc-arc")

target_include_directories(liora PRIVATE
  ${PROJECT_SOURCE_DIR}/core/include)

target_link_libraries(liora PRIVATE
  liora_core
  liora_sqlite
  "-framework Cocoa"
  "-framework WebKit"
  "-framework UniformTypeIdentifiers")

target_compile_definitions(liora PRIVATE
  LIORA_UI_DIR="${PROJECT_SOURCE_DIR}/ui/dist")

set_target_properties(liora PROPERTIES
  MACOSX_BUNDLE TRUE
  MACOSX_BUNDLE_BUNDLE_NAME "Liora"
  MACOSX_BUNDLE_GUI_IDENTIFIER "io.liora.browser"
  MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
  MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
  XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-")

# The interface is loaded from disk during development. A release build copies
# it into the bundle so the app is self-contained. The target always runs so a
# rebuilt interface reaches the bundle without a full relink.
add_custom_target(liora-bundle-ui ALL
  COMMAND ${CMAKE_COMMAND} -E rm -rf $<TARGET_BUNDLE_CONTENT_DIR:liora>/Resources/ui
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${PROJECT_SOURCE_DIR}/ui/dist
          $<TARGET_BUNDLE_CONTENT_DIR:liora>/Resources/ui
  COMMENT "Bundling the interface")
add_dependencies(liora liora-bundle-ui)
