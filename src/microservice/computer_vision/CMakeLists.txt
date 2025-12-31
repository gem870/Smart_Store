# -----------------------------------
# Computer Vision Microservices
# -----------------------------------

# Shared include path for BaseMicroservice.hpp
include_directories(${PROJECT_SOURCE_DIR}/src/interface)

# -----------------------------------
# OpenCV (bundled)
# -----------------------------------
set(OpenCV_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/opencv/include")

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
file(GLOB OPENCV_LIBS "${OpenCV_LIB_DIR}/*.a" "${OpenCV_LIB_DIR}/*.lib")

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
# Face Monitor Microservice
# -----------------------------------
add_library(face_monitor_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/face_monitor/FaceWatchlist.cpp
)

target_include_directories(face_monitor_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/face_monitor
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/utils
)

target_link_libraries(face_monitor_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
)

target_compile_features(face_monitor_microservice PUBLIC cxx_std_20)

# -----------------------------------
# Face Recognition Microservice
# -----------------------------------
add_library(face_recognition_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/face_recognition/FaceRecognition.cpp
)

target_include_directories(face_recognition_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/face_recognition
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/utils
)

target_link_libraries(face_recognition_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
)

target_compile_features(face_recognition_microservice PUBLIC cxx_std_20)

# -----------------------------------
# Motion Detection Microservice
# -----------------------------------
add_library(motion_detection_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/motion_detection/MotionDetection.cpp
)

target_include_directories(motion_detection_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/motion_detection
        ${PROJECT_SOURCE_DIR}/src
        ${PROJECT_SOURCE_DIR}/src/utils
)

target_link_libraries(motion_detection_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
)

target_compile_features(motion_detection_microservice PUBLIC cxx_std_20)
