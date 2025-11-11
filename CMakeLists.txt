


cmake_minimum_required(VERSION 3.16)
project(ItemManagerProject LANGUAGES CXX)

# -----------------------------------
# Compiler and build configuration
# -----------------------------------

# Only run this logic if this is the top-level project
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SUPPORTED_CXX_STANDARDS 20 17 14)

    # Ensure a dummy source exists for try_compile checks
    set(_dummy_dir "${PROJECT_SOURCE_DIR}/cmake")
    set(_dummy_src "${_dummy_dir}/dummy.cpp")
    if(NOT EXISTS "${_dummy_src}")
        file(MAKE_DIRECTORY "${_dummy_dir}")
        file(WRITE "${_dummy_src}" "#include <iostream>\nint main(){std::cout<<\"ok\";return 0;}\n")
    endif()

    foreach(standard IN LISTS SUPPORTED_CXX_STANDARDS)
        set(CMAKE_CXX_STANDARD ${standard})
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)

        try_compile(COMPILER_SUPPORTS_STD
            ${CMAKE_BINARY_DIR}/std_check
            ${_dummy_src}
            CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=${standard}"
        )

        if(COMPILER_SUPPORTS_STD)
            message(STATUS "Using C++${standard}")
            break()
        endif()
    endforeach()

    if(NOT COMPILER_SUPPORTS_STD)
        message(FATAL_ERROR "No supported C++ standard found. Please use GCC 11+, Clang 13+, or MSVC 2019+.")
    endif()
endif()

# Default build type for single-config generators (respect multi-config like VS/Xcode)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Choose build type (Debug, Release, RelWithDebInfo, MinSizeRel)" FORCE)
endif()

# -----------------------------------
# Compiler detection and flags (portable)
# -----------------------------------

if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    message(STATUS "Detected Clang compiler")
    add_compile_options(-g -O1 -Wall -Wextra -Wpedantic -Wno-unused-parameter)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "AppleClang")
    message(STATUS "Detected AppleClang compiler (macOS)")
    add_compile_options(-g -O1 -Wall -Wextra -Wpedantic -Wno-unused-parameter)

elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    message(STATUS "Detected GCC compiler")
    add_compile_options(-g -O1 -Wall -Wextra -pedantic)

elseif(MSVC)
    message(STATUS "Detected MSVC compiler")
    add_compile_options(/W4 /EHsc /Zc:__cplusplus /std:c++20)
else()
    message(WARNING "Unknown compiler: ${CMAKE_CXX_COMPILER_ID}")
endif()

# Optional: allow static libstdc++/libgcc on Linux only (off by default)
option(ENABLE_STATIC_STDLIB "Link libstdc++/libgcc statically on Linux (may affect portability)" OFF)
if(ENABLE_STATIC_STDLIB AND CMAKE_SYSTEM_NAME STREQUAL "Linux" AND (CMAKE_CXX_COMPILER_ID STREQUAL "GNU" OR CMAKE_CXX_COMPILER_ID STREQUAL "Clang"))
    add_link_options(-static-libstdc++ -static-libgcc)
endif()

# -----------------------------------
# External dependencies
# -----------------------------------

include(FetchContent)

# nlohmann JSON
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(json)

# GoogleTest (do not use system-installed to keep cross-platform consistency)
# GoogleTest (use release tarball instead of git clone)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/release-1.12.1.zip
)
FetchContent_MakeAvailable(googletest)


# -----------------------------------
# ItemManager library
# -----------------------------------

add_library(ItemManagerLib STATIC
    lib/tinyxml2/tinyxml2.cpp
    src/versionForMigration/MigrationRegistry.cpp
    # src/utils/AtomicFileWriter.cpp  # Uncomment if needed
)

target_include_directories(ItemManagerLib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/lib/tinyxml2
        ${json_SOURCE_DIR}/single_include
)

target_link_libraries(ItemManagerLib
    PUBLIC
        nlohmann_json::nlohmann_json
)

# -----------------------------------
# Test executable
# -----------------------------------

add_executable(TestApp
    tests/test_ItemManager.cpp
)

target_include_directories(TestApp
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/lib/tinyxml2
        ${json_SOURCE_DIR}/single_include
)

target_link_libraries(TestApp
    PRIVATE
        ItemManagerLib
        gtest
        gtest_main
)

# Color output for MSVC GoogleTest (optional quality-of-life)
if(MSVC)
    target_compile_definitions(TestApp PRIVATE GTEST_COLOR=1)
endif()

# -----------------------------------
# Enable test discovery
# -----------------------------------

include(GoogleTest)
gtest_discover_tests(TestApp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
)

# -----------------------------------
# Output summary
# -----------------------------------
message(STATUS "Project configured successfully.")
