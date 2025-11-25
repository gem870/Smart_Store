


# cmake_minimum_required(VERSION 3.16)
# project(ItemManagerProject LANGUAGES CXX)

# # -----------------------------------
# # Compiler and Build Configuration
# # -----------------------------------

# if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
#     set(SUPPORTED_CXX_STANDARDS 20 17 14)

#     foreach(standard IN LISTS SUPPORTED_CXX_STANDARDS)
#         set(CMAKE_CXX_STANDARD ${standard})
#         set(CMAKE_CXX_STANDARD_REQUIRED ON)
#         set(CMAKE_CXX_EXTENSIONS OFF)

#         try_compile(COMPILER_SUPPORTS_STD
#             ${CMAKE_BINARY_DIR}/std_check
#             ${PROJECT_SOURCE_DIR}/cmake/dummy.cpp
#             CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=${standard}"
#         )

#         if(COMPILER_SUPPORTS_STD)
#             message(STATUS " Using C++${standard}")
#             break()
#         endif()
#     endforeach()

#     if(NOT COMPILER_SUPPORTS_STD)
#         message(FATAL_ERROR "❌ No supported C++ standard found! Please use GCC 11 or higher.")
#     endif()
# endif()

# # -----------------------------------
# # Build Settings
# # -----------------------------------
# set(CMAKE_BUILD_TYPE Debug)
# set(CMAKE_CXX_FLAGS "-g -O1 -Wall -Wextra -pedantic")
# set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

# # -----------------------------------
# # External Dependencies
# # -----------------------------------
# include(FetchContent)

# # nlohmann JSON
# FetchContent_Declare(
#     json
#     URL https://github.com/nlohmann/json/releases/download/v3.11.2/json.tar.xz
#     DOWNLOAD_EXTRACT_TIMESTAMP true
# )
# FetchContent_MakeAvailable(json)

# # GoogleTest
# FetchContent_Declare(
#     googletest
#     GIT_REPOSITORY https://github.com/google/googletest.git
#     GIT_TAG release-1.12.1
# )
# FetchContent_MakeAvailable(googletest)


# # -----------------------------------
# # ItemManager Library
# # -----------------------------------
# add_library(ItemManagerLib STATIC
#     lib/tinyxml2/tinyxml2.cpp
#     src/microservice/net_curl/network/network_agent.cpp
#     src/versionForMigration/MigrationRegistry.cpp
# )

# target_include_directories(ItemManagerLib
#     PUBLIC
#         ${PROJECT_SOURCE_DIR}/src
#         ${PROJECT_SOURCE_DIR}/src/microservice_interface
#         ${PROJECT_SOURCE_DIR}/src/microservice/net_curl
#         ${PROJECT_SOURCE_DIR}/src/microservice/net_curl/network
#         ${PROJECT_SOURCE_DIR}/lib/tinyxml2
#         ${json_SOURCE_DIR}/single_include
#         ${PROJECT_SOURCE_DIR}/lib/asio/include   # Asio headers
# )

# target_link_libraries(ItemManagerLib PUBLIC nlohmann_json::nlohmann_json)

# # -----------------------------------
# # Test Executable
# # -----------------------------------
# add_executable(TestApp
#     tests/test_ItemManager.cpp
# )

# target_include_directories(TestApp
#     PRIVATE
#         ${PROJECT_SOURCE_DIR}/src
#         ${PROJECT_SOURCE_DIR}/lib/tinyxml2
#         ${json_SOURCE_DIR}/single_include
#         ${PROJECT_SOURCE_DIR}/lib/asio/include   # Added Asio include path
# )
# target_compile_definitions(TestApp PRIVATE -DASIO_STANDALONE
# )

# target_link_libraries(TestApp PRIVATE
#     ItemManagerLib
#     gtest
#     gtest_main
#     ws2_32 # Windows sockets
# )

# # -----------------------------------
# # Enable Test Discovery
# # -----------------------------------
# include(GoogleTest)
# gtest_discover_tests(TestApp
#     WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
#     DISCOVERY_MODE PRE_TEST
# )

# # -----------------------------------
# # Output Summary
# # -----------------------------------
# message(STATUS "✅ Project Configured Successfully using Boost headers from lib/boost (manually bundled).")






cmake_minimum_required(VERSION 3.16)
project(ItemManagerProject LANGUAGES CXX)

# -----------------------------------
# Compiler and Build Configuration
# -----------------------------------

if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(SUPPORTED_CXX_STANDARDS 20 17 14)

    foreach(standard IN LISTS SUPPORTED_CXX_STANDARDS)
        set(CMAKE_CXX_STANDARD ${standard})
        set(CMAKE_CXX_STANDARD_REQUIRED ON)
        set(CMAKE_CXX_EXTENSIONS OFF)

        try_compile(COMPILER_SUPPORTS_STD
            ${CMAKE_BINARY_DIR}/std_check
            ${PROJECT_SOURCE_DIR}/cmake/dummy.cpp
            CMAKE_FLAGS "-DCMAKE_CXX_STANDARD=${standard}"
        )

        if(COMPILER_SUPPORTS_STD)
            message(STATUS " Using C++${standard}")
            break()
        endif()
    endforeach()

    if(NOT COMPILER_SUPPORTS_STD)
        message(FATAL_ERROR "❌ No supported C++ standard found! Please use GCC 11 or higher.")
    endif()
endif()

# -----------------------------------
# Build Settings
# -----------------------------------
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "-g -O1 -Wall -Wextra -pedantic")
set(CMAKE_EXE_LINKER_FLAGS "-static-libgcc -static-libstdc++")

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

# OpenSSL (always fetched to avoid manual setup)
FetchContent_Declare(
    openssl
    URL https://www.openssl.org/source/openssl-3.0.14.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP true
)
FetchContent_MakeAvailable(openssl)

# OpenSSL doesn’t provide CMake targets, so you must link manually:
set(OPENSSL_SSL_LIBRARY ${openssl_BINARY_DIR}/libssl.a)
set(OPENSSL_CRYPTO_LIBRARY ${openssl_BINARY_DIR}/libcrypto.a)

target_link_libraries(ItemManagerLib PUBLIC ${OPENSSL_SSL_LIBRARY} ${OPENSSL_CRYPTO_LIBRARY})

# -----------------------------------
# ItemManager Library
# -----------------------------------
add_library(ItemManagerLib STATIC
    lib/tinyxml2/tinyxml2.cpp
    src/microservice/net_curl/network/network_agent.cpp
    src/versionForMigration/MigrationRegistry.cpp
)

target_include_directories(ItemManagerLib
    PUBLIC
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/microservice_interface
        ${PROJECT_SOURCE_DIR}/src/microservice/net_curl
        ${PROJECT_SOURCE_DIR}/src/microservice/net_curl/network
        ${PROJECT_SOURCE_DIR}/lib/tinyxml2
        ${json_SOURCE_DIR}/single_include
        ${PROJECT_SOURCE_DIR}/lib/asio/include   # Asio headers
)

target_link_libraries(ItemManagerLib
    PUBLIC
        nlohmann_json::nlohmann_json
        OpenSSL::SSL
        OpenSSL::Crypto
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
        ${PROJECT_SOURCE_DIR}/lib/asio/include   # Added Asio include path
)

target_compile_definitions(TestApp PRIVATE -DASIO_STANDALONE)

target_link_libraries(TestApp PRIVATE
    ItemManagerLib
    gtest
    gtest_main
    ws2_32 # Windows sockets
)

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
message(STATUS "✅ Project Configured Successfully using Boost headers from lib/boost (manually bundled).")
message(STATUS "✅ OpenSSL fetched and linked successfully.")