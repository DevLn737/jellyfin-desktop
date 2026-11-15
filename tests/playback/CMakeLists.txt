cmake_minimum_required(VERSION 3.16)
project(PlaybackValueTests LANGUAGES CXX)
enable_testing()
add_executable(playback-value-tests PlaybackValueTest.cpp)
target_compile_features(playback-value-tests PRIVATE cxx_std_17)
target_include_directories(playback-value-tests PRIVATE ../../src/player ../../src)
add_test(NAME playback-values COMMAND playback-value-tests)

find_package(Python3 REQUIRED COMPONENTS Interpreter)
set(player_source "${CMAKE_CURRENT_SOURCE_DIR}/../../src/player/PlayerComponent.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${player_source}")
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-video-ui-method.py"
  "${player_source}" "${CMAKE_CURRENT_BINARY_DIR}/VideoUiReadyMethod.inc"
  RESULT_VARIABLE extraction_result)
if(NOT extraction_result EQUAL 0)
  message(FATAL_ERROR "Could not extract the production videoUiReady method")
endif()
add_executable(video-ui-ready-tests VideoUiReadyTest.cpp)
target_compile_features(video-ui-ready-tests PRIVATE cxx_std_17)
target_include_directories(video-ui-ready-tests PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
add_test(NAME video-ui-ready COMMAND video-ui-ready-tests)

# Use Qt's real event loop/context cancellation with the actual production helper.
find_package(Qt6 REQUIRED COMPONENTS Core)
execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-load-hook-method.py"
  "${player_source}" "${CMAKE_CURRENT_BINARY_DIR}/LoadHookMethod.inc"
  RESULT_VARIABLE load_hook_extraction_result)
if(NOT load_hook_extraction_result EQUAL 0)
  message(FATAL_ERROR "Could not extract the production load-hook continuation")
endif()
add_executable(load-hook-lifetime-tests LoadHookLifetimeTest.cpp)
target_compile_features(load-hook-lifetime-tests PRIVATE cxx_std_17)
target_include_directories(load-hook-lifetime-tests PRIVATE ../../src/player "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(load-hook-lifetime-tests PRIVATE Qt6::Core)
add_test(NAME load-hook-lifetime COMMAND load-hook-lifetime-tests)

execute_process(COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/extract-stream-methods.py"
  "${player_source}" "${CMAKE_CURRENT_BINARY_DIR}/StreamSelectionMethods.inc"
  RESULT_VARIABLE stream_extraction_result)
if(NOT stream_extraction_result EQUAL 0)
  message(FATAL_ERROR "Could not extract the production external-stream methods")
endif()
add_executable(external-stream-selection-tests ExternalStreamSelectionTest.cpp)
target_compile_features(external-stream-selection-tests PRIVATE cxx_std_17)
target_include_directories(external-stream-selection-tests PRIVATE ../../src/player "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(external-stream-selection-tests PRIVATE Qt6::Core)
add_test(NAME external-stream-selection COMMAND external-stream-selection-tests)
