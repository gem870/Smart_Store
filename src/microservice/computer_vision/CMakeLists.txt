# -----------------------------------
# Computer Vision Microservices
# -----------------------------------

# Shared include path for BaseMicroservice.hpp
include_directories(${PROJECT_SOURCE_DIR}/src/interface)

# -----------------------------------
# OpenCV
# -----------------------------------
if(MSVC)
    set(OpenCV_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/include")
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-msvc/lib")
    set(OpenCV_BIN_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/win-msvc/bin")
elseif(MINGW)
    # vcpkg's default x64-windows triplet builds with MSVC and can't link
    # into this MinGW-compiled project (same reasoning as SDL2's own fix in
    # alarm_system/CMakeLists.txt) -- built instead via the community
    # x64-mingw-dynamic triplet (`vcpkg install
    # "opencv4[calib3d,contrib,dnn,dshow,ffmpeg,fs,gapi,highgui,intrinsics,
    # jpeg,png,quirc,thread,tiff,webp,win32ui]:x64-mingw-dynamic" --classic`
    # from lib/vcpkg), matching the standalone surveillance app's already-
    # proven x64-windows opencv4 feature set (see its own CMakeLists.txt),
    # minus directml/msmf which that port marks as MinGW-unsupported.
    set(OpenCV_VCPKG_ROOT "${PROJECT_SOURCE_DIR}/lib/vcpkg/installed/x64-mingw-dynamic")
    set(OpenCV_INCLUDE_DIR "${OpenCV_VCPKG_ROOT}/include/opencv4")
    set(OpenCV_LIB_DIR "${OpenCV_VCPKG_ROOT}/lib")
    set(OpenCV_BIN_DIR "${OpenCV_VCPKG_ROOT}/bin")
elseif(UNIX AND NOT APPLE)
    set(OpenCV_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/include")
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/linux/lib")
elseif(APPLE)
    set(OpenCV_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/include")
    set(OpenCV_LIB_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/macos/lib")
endif()

include_directories(${OpenCV_INCLUDE_DIR})
link_directories(${OpenCV_LIB_DIR})

# Named by prefix rather than a blind glob -- on MINGW, OpenCV_LIB_DIR is
# the shared x64-mingw-dynamic triplet tree, which also holds SDL2, ffmpeg,
# zlib, libpng, protobuf, etc.; every OpenCV module .dll.a consistently
# starts with "libopencv_", so this pulls in exactly OpenCV's own libraries
# and nothing else installed alongside them.
if(MINGW)
    file(GLOB OPENCV_LIBS "${OpenCV_LIB_DIR}/libopencv_*.a")
else()
    file(GLOB OPENCV_LIBS "${OpenCV_LIB_DIR}/*.a" "${OpenCV_LIB_DIR}/*.lib")
endif()

# add_subdirectory() gives this file its own variable scope -- without
# PARENT_SCOPE, OPENCV_LIBS/OpenCV_INCLUDE_DIR/OpenCV_BIN_DIR never reach
# the root CMakeLists.txt (t_manager, TestApp, SurveillanceTestApp),
# silently linking nothing / falling back to stale/missing headers.
set(OPENCV_LIBS ${OPENCV_LIBS} PARENT_SCOPE)
set(OpenCV_INCLUDE_DIR ${OpenCV_INCLUDE_DIR} PARENT_SCOPE)
set(OpenCV_BIN_DIR ${OpenCV_BIN_DIR} PARENT_SCOPE)

# -----------------------------------
# Scanner Microservice
# -----------------------------------
add_library(scanner_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/scanner/document/Scanner.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/scanner/qr_code/QRCodeScanner.cpp
)

target_include_directories(scanner_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/scanner/document
        ${CMAKE_CURRENT_SOURCE_DIR}/scanner/qr_code
        ${CMAKE_CURRENT_SOURCE_DIR}/scanner/include
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/utils
)

target_link_libraries(scanner_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
)

target_compile_features(scanner_microservice PUBLIC cxx_std_20)

# -----------------------------------
# Surveillance Interface Microservice
# -----------------------------------
# Deliberately lean -- this is a thin ItemManager-facing handle (start/stop/
# status via a sentinel file), not the standalone surveillance app itself,
# so it needs neither OpenCV nor any of that app's heavier dependencies.
# See Surveillance.hpp.
add_library(surveillance_interface_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/surveillance_interface/Surveillance.cpp
)

target_include_directories(surveillance_interface_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/surveillance_interface
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/utils
)

target_link_libraries(surveillance_interface_microservice
    PUBLIC
        core_interfaces
)

target_compile_features(surveillance_interface_microservice PUBLIC cxx_std_20)
