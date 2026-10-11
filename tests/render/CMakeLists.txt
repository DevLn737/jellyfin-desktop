cmake_minimum_required(VERSION 3.31.6)
project(DesktopRendererTests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 17)
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/../../build-manifest.json" build_manifest)
string(JSON locked_qt_version GET "${build_manifest}" qt version)
find_package(Qt6 ${locked_qt_version} EXACT REQUIRED COMPONENTS Core Gui Quick Qml OpenGL)
find_package(Threads REQUIRED)
if(NOT MPV_INCLUDE_DIR OR NOT MPV_LIBRARY)
  message(FATAL_ERROR "Provide the locked MPV_INCLUDE_DIR and MPV_LIBRARY")
endif()
add_executable(renderer-tests RendererTest.cpp
  ../../src/player/PlayerQuickItem.cpp ../../src/player/PlayerQuickItem.h)
target_include_directories(renderer-tests PRIVATE ../../src ../../src/player ${MPV_INCLUDE_DIR})
target_link_libraries(renderer-tests PRIVATE Qt6::Core Qt6::Gui Qt6::Quick Qt6::Qml
  Qt6::OpenGL Threads::Threads ${MPV_LIBRARY})
if(WIN32)
  target_link_libraries(renderer-tests PRIVATE dwmapi avrt)
endif()
if(MSVC)
  target_compile_options(renderer-tests PRIVATE /utf-8)
endif()
enable_testing()
add_test(NAME production-renderer-offscreen COMMAND renderer-tests
  "${CMAKE_CURRENT_SOURCE_DIR}/../playback/fixtures/hls-video-30/index.m3u8"
  "${CMAKE_CURRENT_BINARY_DIR}/renderer-report.json")
set_tests_properties(production-renderer-offscreen PROPERTIES TIMEOUT 60
  ENVIRONMENT "QT_OPENGL=software;QSG_RHI_BACKEND=opengl;QT_SCALE_FACTOR=1")
