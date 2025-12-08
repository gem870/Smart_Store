


cmake_minimum_required(VERSION 3.16)
project(ItemManagerProject LANGUAGES CXX)

# -----------------------------------
# Compiler and Build Configuration
# -----------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "-g -O1 -Wall -Wextra -pedantic")

# -----------------------------------
# External Dependencies
# -----------------------------------
include(FetchContent)

# nlohmann JSON
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(json)

# GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# -----------------------------------
# OpenCV (bundled in lib/opencv)
# -----------------------------------
set(OpenCV_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/include")

# Pick platform-specific lib/bin folders
if(MSVC)
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-msvc/lib")
    set(OpenCV_BIN_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-msvc/bin")
elseif(MINGW)
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-mingw/lib")
    set(OpenCV_BIN_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-mingw/bin")
elseif(UNIX AND NOT APPLE)
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/linux/lib")
elseif(APPLE)
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/macos/lib")
endif()

include_directories(${OpenCV_INCLUDE_DIR})
link_directories(${OpenCV_LIB_DIR})

# Only grab actual library files (.a for MinGW, .lib for MSVC)
file(GLOB OPENCV_LIBS "${OpenCV_LIB_DIR}/*.a" "${OpenCV_LIB_DIR}/*.lib")

# -----------------------------------
# ItemManager Library
# -----------------------------------
add_library(ItemManagerLib STATIC
    lib/tinyxml2/tinyxml2.cpp
    src/microservice/net_curl/network/network_agent.cpp
    src/versionForMigration/MigrationRegistry.cpp
    src/microservice/computer_vision/survelliance/FaceRecognition.cpp
    src/microservice/computer_vision/survelliance/FaceWatchlist.cpp
    src/microservice/computer_vision/survelliance/MotionDetection.cpp
)

target_include_directories(ItemManagerLib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/microservice_interface
        ${PROJECT_SOURCE_DIR}/src/microservice/net_curl
        ${PROJECT_SOURCE_DIR}/src/microservice/computer_vision/survelliance
        ${PROJECT_SOURCE_DIR}/src/microservice/net_curl/network
        ${PROJECT_SOURCE_DIR}/lib/tinyxml2
        ${json_SOURCE_DIR}/single_include
        ${PROJECT_SOURCE_DIR}/lib/asio/include
        ${OpenCV_INCLUDE_DIR}
)

target_link_libraries(ItemManagerLib
    PUBLIC
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
)

# -----------------------------------
# Test Executable
# -----------------------------------
add_executable(TestApp
    tests/test_ItemManager.cpp
)

target_include_directories(TestApp
    PRIVATE
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/lib/tinyxml2
        ${json_SOURCE_DIR}/single_include
        ${PROJECT_SOURCE_DIR}/lib/asio/include
        ${OpenCV_INCLUDE_DIR}
)

target_compile_definitions(TestApp PRIVATE -DASIO_STANDALONE)

target_link_libraries(TestApp PRIVATE
    ItemManagerLib
    gtest
    gtest_main
    ws2_32
)

# Copy DLLs automatically on Windows
if(WIN32 AND DEFINED OpenCV_BIN_DIR)
    add_custom_command(TARGET TestApp POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${OpenCV_BIN_DIR} $<TARGET_FILE_DIR:TestApp>)
endif()

# -----------------------------------
# Enable Test Discovery
# -----------------------------------
include(GoogleTest)
gtest_discover_tests(TestApp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
)

# -----------------------------------
# Output Summary
# -----------------------------------
message(STATUS "✅ Project Configured Successfully.")
message(STATUS "✅ OpenCV headers from ${OpenCV_INCLUDE_DIR}")
message(STATUS "✅ OpenCV libraries from ${OpenCV_LIB_DIR}")
message(STATUS "✅ Linked OpenCV libs: ${OPENCV_LIBS}")





