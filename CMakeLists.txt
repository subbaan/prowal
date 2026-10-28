cmake_minimum_required(VERSION 3.21)

# Single source of truth for the version: written into version.h, read by -V and the UI.
project(Prowal VERSION 1.0.4 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets Test)
find_package(PkgConfig REQUIRED)
pkg_check_modules(XCB REQUIRED IMPORTED_TARGET xcb xcb-shape)

configure_file(src/version.h.in ${CMAKE_BINARY_DIR}/generated/version.h @ONLY)

add_compile_options(-Wall -Wextra -Wpedantic)

# Address + undefined-behaviour sanitizers, for `make sanitize`.
option(PROWAL_SANITIZE "Build with ASan and UBSan" OFF)
if(PROWAL_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=undefined)
  add_link_options(-fsanitize=address,undefined)
endif()
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-Werror)
endif()

# Toolkit-light core: config model, import, command splitting, layout maths.
# Unit tests link against this without needing a display.
add_library(prowal_core STATIC
  src/config/Model.h
  src/config/Config.h src/config/Config.cpp
  src/config/ApwalImport.h src/config/ApwalImport.cpp
  src/config/Paths.h src/config/Paths.cpp
  src/system/CommandLine.h src/system/CommandLine.cpp
  src/system/Terminal.h src/system/Terminal.cpp
  src/system/Launch.h src/system/Launch.cpp
  src/config/GridOps.h src/config/GridOps.cpp
  src/popup/Layout.h src/popup/Layout.cpp
  src/editor/Document.h src/editor/Document.cpp
  src/desktop/DesktopEntry.h src/desktop/DesktopEntry.cpp
  src/icons/IconIndex.h src/icons/IconIndex.cpp
)
target_include_directories(prowal_core PUBLIC src ${CMAKE_BINARY_DIR}/generated)
target_link_libraries(prowal_core PUBLIC Qt6::Core Qt6::Gui)

add_executable(prowal
  src/app/main.cpp
  src/popup/PopupWindow.h src/popup/PopupWindow.cpp
  src/editor/EditorWindow.h src/editor/EditorWindow.cpp
  src/editor/GridCanvas.h src/editor/GridCanvas.cpp
  src/editor/PropertiesPanel.h src/editor/PropertiesPanel.cpp
  src/editor/PreferencesDialog.h src/editor/PreferencesDialog.cpp
  src/editor/AppPicker.h src/editor/AppPicker.cpp
  src/editor/IconPicker.h src/editor/IconPicker.cpp
  src/icons/IconLoader.h src/icons/IconLoader.cpp
  src/icons/IconTheme.h src/icons/IconTheme.cpp
  src/platform/X11.h src/platform/X11.cpp
  src/platform/SingleInstance.h src/platform/SingleInstance.cpp
)
target_link_libraries(prowal PRIVATE prowal_core Qt6::Widgets PkgConfig::XCB)

# Keep a project-root symlink to the built binary (the main build only, so a
# debug build in another directory doesn't steal it).
option(PROWAL_LINK_ROOT "Point ./prowal at this build's binary" ON)
if(PROWAL_LINK_ROOT)
  file(RELATIVE_PATH PROWAL_LINK_TARGET ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/prowal)
  add_custom_command(TARGET prowal POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f ${CMAKE_SOURCE_DIR}/prowal
    COMMAND ${CMAKE_COMMAND} -E create_symlink ${PROWAL_LINK_TARGET} ${CMAKE_SOURCE_DIR}/prowal
    COMMENT "Linking ./prowal -> ${PROWAL_LINK_TARGET}")
endif()

# Man page, with the version filled in.
configure_file(docs/prowal.1.in ${CMAKE_BINARY_DIR}/prowal.1 @ONLY)

# For packagers (`cmake --install`); the project itself is used from ./prowal.
include(GNUInstallDirs)
install(TARGETS prowal RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES ${CMAKE_BINARY_DIR}/prowal.1 DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
install(FILES data/prowal-editor.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)

# Release tarball: `make dist` -> build/dist/prowal-<version>-linux-<arch>.tar.gz
set(PROWAL_DIST_NAME prowal-${PROJECT_VERSION}-linux-${CMAKE_SYSTEM_PROCESSOR})
set(PROWAL_DIST_DIR ${CMAKE_BINARY_DIR}/dist/${PROWAL_DIST_NAME})
add_custom_target(dist
  COMMAND ${CMAKE_COMMAND} -E rm -rf ${PROWAL_DIST_DIR}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${PROWAL_DIST_DIR}
  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:prowal> ${PROWAL_DIST_DIR}/prowal
  COMMAND ${CMAKE_STRIP} ${PROWAL_DIST_DIR}/prowal
  COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/prowal.1 ${CMAKE_SOURCE_DIR}/data/prowal-editor.desktop
          ${CMAKE_SOURCE_DIR}/README.md ${CMAKE_SOURCE_DIR}/CHANGELOG.md ${CMAKE_SOURCE_DIR}/LICENSE
          ${PROWAL_DIST_DIR}/
  COMMAND ${CMAKE_COMMAND} -E chdir ${CMAKE_BINARY_DIR}/dist
          ${CMAKE_COMMAND} -E tar czf ${PROWAL_DIST_NAME}.tar.gz ${PROWAL_DIST_NAME}
  COMMAND ${CMAKE_COMMAND} -E echo "Release tarball: ${CMAKE_BINARY_DIR}/dist/${PROWAL_DIST_NAME}.tar.gz"
  DEPENDS prowal
  VERBATIM)

enable_testing()
function(prowal_test name)
  add_executable(${name} tests/${name}.cpp)
  target_link_libraries(${name} PRIVATE prowal_core Qt6::Test)
  target_compile_definitions(${name} PRIVATE PROWAL_TEST_DATA="${CMAKE_SOURCE_DIR}/tests/data")
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
prowal_test(tst_commandline)
prowal_test(tst_config)
prowal_test(tst_apwalimport)
prowal_test(tst_layout)
prowal_test(tst_terminal)
prowal_test(tst_document)
prowal_test(tst_desktop)
prowal_test(tst_iconindex)
