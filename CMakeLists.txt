cmake_minimum_required(VERSION 3.20)
project(RE2HeadTracking VERSION 0.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(WIN32)
    add_definitions(-DWIN32_LEAN_AND_MEAN -DNOMINMAX -D_CRT_SECURE_NO_WARNINGS)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# cameraunlock-core shared library (static) with REFramework utilities
set(CAMERAUNLOCK_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_BUILD_REFRAMEWORK ON CACHE BOOL "" FORCE)
set(CAMERAUNLOCK_REFRAMEWORK_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/extern CACHE PATH "" FORCE)
add_subdirectory(cameraunlock-core/cpp ${CMAKE_BINARY_DIR}/cameraunlock-core)

# Plugin sources
add_library(RE2HeadTracking SHARED
    src/pch.cpp
    src/plugin_main.cpp
    src/core/mod.cpp
    src/core/config.cpp
    src/core/logger.cpp
    src/camera/camera_hook.cpp
    src/camera/game_state_detector.cpp
)

target_precompile_headers(RE2HeadTracking PRIVATE src/pch.h)

set_target_properties(RE2HeadTracking PROPERTIES
    OUTPUT_NAME "RE2HeadTracking"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/bin"
)

target_link_libraries(RE2HeadTracking PRIVATE cameraunlock cameraunlock_reframework ws2_32)

target_include_directories(RE2HeadTracking PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
    ${CMAKE_CURRENT_SOURCE_DIR}/extern
)

# Unit tests (off by default; enable with -DRE2HT_BUILD_TESTS=ON)
option(RE2HT_BUILD_TESTS "Build RE2 Head Tracking unit tests" OFF)
if(RE2HT_BUILD_TESTS)
    enable_testing()
    add_executable(re2ht_tests
        tests/test_main.cpp
        tests/config_tests.cpp
        src/core/config.cpp
        src/core/logger.cpp
    )
    target_include_directories(re2ht_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/extern
    )
    target_link_libraries(re2ht_tests PRIVATE cameraunlock ws2_32)
    add_test(NAME re2ht_tests COMMAND re2ht_tests)
endif()
