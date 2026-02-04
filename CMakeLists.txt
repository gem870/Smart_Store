

cmake_minimum_required(VERSION 3.16)
project(ItemManagerProject LANGUAGES CXX)

# -----------------------------------
# Compiler and build configuration
# -----------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O1 -Wall -Wextra -pedantic")

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

# GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
FetchContent_MakeAvailable(googletest)

# -----------------------------------
# Core interfaces (shared headers)
# -----------------------------------
add_library(core_interfaces INTERFACE)
target_include_directories(core_interfaces INTERFACE
    ${PROJECT_SOURCE_DIR}/src/microservice_interface
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/src/interface
    ${PROJECT_SOURCE_DIR}/lib/asio/include
    ${json_SOURCE_DIR}/single_include
)

# -----------------------------------
# Local TinyXML2
# -----------------------------------
add_library(tinyxml2_static STATIC
    ${PROJECT_SOURCE_DIR}/lib/tinyxml2/tinyxml2.cpp
)
target_include_directories(tinyxml2_static PUBLIC
    ${PROJECT_SOURCE_DIR}/lib/tinyxml2
)

# -----------------------------------
# Microservices
# -----------------------------------
add_subdirectory(src/microservice/alarm_system)
add_subdirectory(src/microservice/computer_vision)
add_subdirectory(src/microservice/net_curl)

# -----------------------------------
# t_manager module (ItemManager headers + MigrationRegistry implementation)
# -----------------------------------
add_library(t_manager STATIC
    src/versionForMigration/MigrationRegistry.cpp
    src/gpu_manager/gpu_manager.cpp
)

target_include_directories(t_manager
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src/t_manager
        ${PROJECT_SOURCE_DIR}/src/interface
        ${PROJECT_SOURCE_DIR}/src/versionForMigration
        ${PROJECT_SOURCE_DIR}/lib/opencv/include
)

target_link_libraries(t_manager
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        tinyxml2_static
)

target_compile_features(t_manager PUBLIC cxx_std_20)

# -----------------------------------
# Top-level test executable
# -----------------------------------
add_executable(TestApp tests/test_ItemManager.cpp)

# Make sure TestApp sees OpenCV headers
target_include_directories(TestApp PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${PROJECT_SOURCE_DIR}/lib/asio/include
    ${json_SOURCE_DIR}/single_include
    ${PROJECT_SOURCE_DIR}/lib/tinyxml2
    ${PROJECT_SOURCE_DIR}/lib/opencv/include
)

target_compile_definitions(TestApp PRIVATE -DASIO_STANDALONE)

# Link order: project libs -> third-party libs -> system libs (system libs last!)
target_link_libraries(TestApp PRIVATE
    alarm_microservice
    scanner_microservice
    face_monitor_microservice
    face_recognition_microservice
    motion_detection_microservice
    net_curl_microservice
    t_manager
    tinyxml2_static
    gtest
    gtest_main
    ${OPENCV_LIBS}
    ${SDL2_LIBS}
    ${SDL2_MIXER_LIBS}
    $<$<PLATFORM_ID:Windows>:ws2_32 setupapi imm32 version winmm>
)

# Copy DLLs automatically on Windows (MinGW/MSVC)
if(WIN32)
    if(DEFINED OpenCV_BIN_DIR AND EXISTS ${OpenCV_BIN_DIR})
        add_custom_command(TARGET TestApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${OpenCV_BIN_DIR} $<TARGET_FILE_DIR:TestApp>)
    endif()
    if(DEFINED SDL2_BIN_DIR AND EXISTS ${SDL2_BIN_DIR})
        add_custom_command(TARGET TestApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SDL2_BIN_DIR} $<TARGET_FILE_DIR:TestApp>)
    endif()
    if(DEFINED SDL2_MIXER_BIN_DIR AND EXISTS ${SDL2_MIXER_BIN_DIR})
        add_custom_command(TARGET TestApp POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${SDL2_MIXER_BIN_DIR} $<TARGET_FILE_DIR:TestApp>)
    endif()
endif()


# -----------------------------------
# Enable test discovery
# -----------------------------------
enable_testing()
include(GoogleTest)
gtest_discover_tests(TestApp
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    DISCOVERY_MODE PRE_TEST
)

# -----------------------------------
# Output summary
# -----------------------------------
message(STATUS "✅ Project Configured Successfully.")


