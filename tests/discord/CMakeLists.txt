cmake_minimum_required(VERSION 3.16)
project(DiscordPresenceTests LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
find_package(Qt5 5.15 REQUIRED COMPONENTS Core Network Test)
enable_testing()
add_executable(discord-presence-tests
  DiscordPresenceTest.cpp
  ../../src/discord/DiscordActivity.cpp
  ../../src/discord/DiscordIpc.cpp
  ../../src/discord/DiscordIpc.h)
target_include_directories(discord-presence-tests PRIVATE ../../src/discord)
target_link_libraries(discord-presence-tests PRIVATE Qt5::Core Qt5::Network Qt5::Test)
if(MSVC)
  target_compile_options(discord-presence-tests PRIVATE /utf-8)
endif()
add_test(NAME discord-presence COMMAND discord-presence-tests)
