

cmake_minimum_required(VERSION 3.10)

# -----------------------------------
# Project setup
# -----------------------------------
project(SmartSurveillance VERSION 1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# AUTOMATION: Enable vcpkg to automatically copy DLLs to your output folder.
set(X_VCPKG_APPLOCAL_COPY_ON_INSTALL ON CACHE BOOL "")

# -----------------------------------
# Library Path Discovery (Portable)
# -----------------------------------
# This allows you to set the library location via command line:
# -DSMART_STORE_LIB_DIR="C:/Your/Path"
# If not set, it defaults to a 'lib' folder two levels up from the source.
if(NOT SMART_STORE_LIB_DIR)
    get_filename_component(SMART_STORE_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../../lib" ABSOLUTE)
endif()

message(STATUS "Using Library Root: ${SMART_STORE_LIB_DIR}")

# -----------------------------------
# vcpkg Dependencies
# -----------------------------------
find_package(faiss CONFIG REQUIRED)
find_package(OpenCV REQUIRED)
find_package(OpenSSL REQUIRED)

# Distributed Messaging Dependencies (Kafka multi-station clustering, RabbitMQ)
find_package(RdKafka CONFIG REQUIRED)
find_package(rabbitmq-c CONFIG REQUIRED)

# CloudClient's HTTP sync/alarm/stream/registration/chat threads
find_package(CURL REQUIRED)

# LocalAdminServer's local LAN admin UI
find_package(httplib CONFIG REQUIRED)

# gSOAP's stdsoap2.cpp calls zlib directly for gzip-compressed SOAP transport
find_package(ZLIB REQUIRED)

# Best-effort discovery for Tesseract naming quirks
find_package(Tesseract CONFIG QUIET)
if(NOT Tesseract_FOUND)
    find_package(tesseract CONFIG REQUIRED)
endif()

find_package(SDL2 CONFIG REQUIRED)

# SQLCipher's own package needs tcl (for amalgamation generation) -> minizip
# -> a zlib with a CMake CONFIG export, which the shared x64-windows tree's
# zlib doesn't have; upgrading it there would force a rebuild of curl/
# opencv4/tesseract/etc. also installed against it. Installed instead into
# an isolated root (`vcpkg install sqlcipher:x64-windows --classic
# --x-install-root=lib/vcpkg/installed-sqlcipher`) that doesn't touch the
# shared tree at all.
list(APPEND CMAKE_PREFIX_PATH "${SMART_STORE_LIB_DIR}/vcpkg/installed-sqlcipher/x64-windows")
find_package(sqlcipher CONFIG REQUIRED)

# Paths for manual/legacy dependencies (Relative to SMART_STORE_LIB_DIR)
set(FFMPEG_DIR "${SMART_STORE_LIB_DIR}/ffmpeg")

# -----------------------------------
# Define Manual Library Variables
# -----------------------------------

# -----------------------------------
# Source files
# -----------------------------------
set(SOURCES
    src/config_file/config.cpp
    src/camera_capabilities/camera_capabilities.cpp
    src/night_vision/night_vision.cpp
    src/face_recognition/face_recognition.cpp
    src/motion_detection/motion_detection.cpp
    src/object_detection/object_detection.cpp
    src/database/database.cpp
    src/database/key_provider/db_key_provider.cpp
    src/alarm/dispatch/AlarmDispatcher.cpp
    src/alarm/voice/voice_alarm_channel.cpp
    src/alarm/voice/offline_tts_provider.cpp
    src/alarm/beep/beep_alarm_channel.cpp
    src/cloud/cloud_client.cpp
    src/cloud/hardware_vitals.cpp
    src/surveillance/surveillance_controller.cpp
    src/main.cpp
    src/record_event/event_recorder.cpp
    src/action_detection/action_detection.cpp
    src/cpu/action_processor/action_event_processor.cpp
    src/cpu/recording_processor/recording_event_processor.cpp
    src/vehicle_detector/vehicle_monitor.cpp
    src/util/helperFunctions.cpp
    src/database/storage/asset_engine.cpp
    src/util/BoundingBoxAnnotator.cpp
    src/util/cv_ui.cpp
    src/cpu/global_detection_processor/global_detection_processor.tpp
    src/local_admin/local_admin_server.cpp
    "src/system interface/system_interface.cpp"
    src/command_queue/command_queue_consumer.cpp
    src/command_queue/surveillance_lifecycle_watcher.cpp
)

# Organize Output
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# -----------------------------------
# Executable
# -----------------------------------
add_executable(SmartSurveillance ${SOURCES})

# -----------------------------------
# Include directories
# -----------------------------------
target_include_directories(SmartSurveillance PRIVATE
    # sqlcipher::sqlcipher's own INTERFACE_INCLUDE_DIRECTORIES points at
    # the parent include/ dir, not include/sqlcipher/ where its headers
    # actually land (a port quirk). That parent dir also had a flat,
    # ambiguous sqlite3.h from the plain `sqlite3` port (pulled in only as
    # tcl's own build-time dependency for amalgamation generation, deleted
    # from the isolated install root since nothing else needs it) -- listed
    # explicitly here so `#include <sqlite3.h>` resolves to the right one.
    "${SMART_STORE_LIB_DIR}/vcpkg/installed-sqlcipher/x64-windows/include/sqlcipher"
    ${OpenCV_INCLUDE_DIRS}
    ${Tesseract_INCLUDE_DIRS}
    ${SDL2_INCLUDE_DIRS}
    ${FFMPEG_INCLUDE_DIRS}
    "${SMART_STORE_LIB_DIR}"
    "${PROJECT_SOURCE_DIR}/include"
    "${PROJECT_SOURCE_DIR}/src"
    "${PROJECT_SOURCE_DIR}"
)

# sqlcipher's own CMakeLists.txt defines SQLITE_HAS_CODEC as PRIVATE (only
# for compiling its own sqlite3.c), not PUBLIC/INTERFACE, so it never
# reaches consumers -- but sqlite3.h itself guards sqlite3_key()/
# sqlite3_key_v2()'s declarations behind #ifdef SQLITE_HAS_CODEC, so
# Database (database.cpp) needs this defined too or those calls won't
# compile at all (confirmed: this was missing and broke the build).
target_compile_definitions(SmartSurveillance PRIVATE SQLITE_HAS_CODEC)

# -----------------------------------
# gSOAP + ONVIF Setup
# -----------------------------------
set(GSOAP_ROOT "${SMART_STORE_LIB_DIR}/gsoap-2.8/gsoap")
set(ONVIF_DIR  "${PROJECT_SOURCE_DIR}/src/onvif")

target_include_directories(SmartSurveillance PRIVATE
    "${GSOAP_ROOT}" "${ONVIF_DIR}"
    "${GSOAP_ROOT}/plugin" "${GSOAP_ROOT}/custom"
)

file(GLOB ONVIF_SOURCES "${ONVIF_DIR}/*.cpp")

target_sources(SmartSurveillance PRIVATE
    ${ONVIF_SOURCES}
    "${GSOAP_ROOT}/stdsoap2.cpp"
    # WS-Security (WS-UsernameToken) -- ONVIF's auth model; no dedicated
    # login operation exists in any of the ONVIF WSDLs, only a WS-Security
    # digest header attached per-call. dom.cpp/threads.c are needed but NOT
    # listed in gSOAP's own import/wsse.h doc-comment requirements (verified
    # by reading wsseapi.cpp directly: it requires -DWITH_DOM, and
    # unconditionally #includes threads.h for MT mutex support without
    # defining emulate_pthread_* itself).
    "${GSOAP_ROOT}/dom.cpp"
    "${GSOAP_ROOT}/plugin/wsseapi.cpp"
    # wsseapi.cpp's fault-reporting paths (soap_wsse_receiver_fault etc.)
    # call soap_wsa_sender_fault_subcode/soap_wsa_receiver_fault_subcode
    # even without the WS-Addressing plugin itself being registered --
    # confirmed via linker (LNK2019) after wsseapi.cpp alone compiled clean.
    "${GSOAP_ROOT}/plugin/wsaapi.c"
    "${GSOAP_ROOT}/plugin/threads.c"
    "${GSOAP_ROOT}/plugin/smdevp.c"
    "${GSOAP_ROOT}/plugin/mecevp.c"
    "${GSOAP_ROOT}/custom/struct_timeval.c"
)

# wsseapi.cpp's own #error guards demand stdsoap2/dom/smdevp/mecevp/
# wsseapi/soapC all compiled with matching WITH_OPENSSL/WITH_DOM defines --
# target-level (not per-file) so no translation unit risks a silent
# struct-layout mismatch against another. No new libraries needed --
# OpenSSL::SSL/OpenSSL::Crypto are already linked below (for SQLCipher).
target_compile_definitions(SmartSurveillance PRIVATE WITH_OPENSSL WITH_DOM)

# soapC.cpp's serialization code for the full generated ONVIF type universe
# (Device/Media2/PTZ combined) exceeds MSVC's default object-file section
# limit (C1128) -- /bigobj raises it. Scoped to just the ONVIF sources
# rather than the whole target, since nothing else here approaches that size.
# struct_timeval.c and wsaapi.c both #include soapH.h (the generated ONVIF
# serialization header, C++-only content) despite being .c files -- MSVC
# compiles .c as plain C by default, which chokes on C++ constructs with
# STL1003 ("expected C++ compiler"). Force these to compile as C++
# regardless of extension; threads.c/smdevp.c/mecevp.c don't include
# soapH.h and compile fine as plain C.
set_source_files_properties(
    "${GSOAP_ROOT}/custom/struct_timeval.c"
    "${GSOAP_ROOT}/plugin/wsaapi.c"
    PROPERTIES LANGUAGE CXX
)

if(MSVC)
    set_source_files_properties(${ONVIF_SOURCES} PROPERTIES COMPILE_OPTIONS "/bigobj")
endif()

# -----------------------------------
# Final Linking
# -----------------------------------
target_link_libraries(SmartSurveillance PRIVATE
    faiss
    # Link against whichever Tesseract target was found
    $<$<TARGET_EXISTS:Tesseract::libtesseract>:Tesseract::libtesseract>
    $<$<TARGET_EXISTS:tesseract::libtesseract>:tesseract::libtesseract>
    ${OpenCV_LIBS}
    sqlcipher::sqlcipher
    ${FFMPEG_LIBRARIES}
    $<TARGET_NAME_IF_EXISTS:SDL2::SDL2main>
    $<IF:$<TARGET_EXISTS:SDL2::SDL2>,SDL2::SDL2,SDL2::SDL2-static>

    # OpenSSL target mapping (cross-platform cryptographic backend, also
    # SQLCipher's own crypto backend)
    OpenSSL::SSL
    OpenSSL::Crypto

    # Messaging Queue Targets (Kafka multi-station clustering)
    RdKafka::rdkafka
    RdKafka::rdkafka++
    rabbitmq::rabbitmq

    # CloudClient's HTTP client
    CURL::libcurl

    # LocalAdminServer's local LAN admin UI (header-only)
    httplib::httplib

    # gSOAP's gzip-compressed SOAP transport
    ZLIB::ZLIB

    # ole32/oleaut32/wbemuuid: WMI-based camera capability detection
    # (camera_capabilities.cpp). Crypt32: Windows crypto API, alongside bcrypt.
    $<$<PLATFORM_ID:Windows>:ws2_32 setupapi imm32 version winmm bcrypt ole32 oleaut32 wbemuuid Crypt32>
)

# -----------------------------------
# Post-Build Copy Commands (Windows)
# -----------------------------------
if(WIN32)
    # Calculate the Smart Store root
    get_filename_component(SMART_STORE_ROOT "${SMART_STORE_LIB_DIR}/.." ABSOLUTE)
    set(LOCAL_AI_MODELS_SRC "${SMART_STORE_ROOT}/ai_models")
    set(MODELS_DEST "$<TARGET_FILE_DIR:SmartSurveillance>/ai_models")

    set(GLOBAL_MODELS_SRC "${SMART_STORE_LIB_DIR}/opencv/models")

    message(STATUS "LOCAL_AI_MODELS_SRC: ${LOCAL_AI_MODELS_SRC}")
    message(STATUS "EXISTS: ${LOCAL_AI_MODELS_SRC} -> $<EXISTS:${LOCAL_AI_MODELS_SRC}>")

    if(EXISTS "${LOCAL_AI_MODELS_SRC}")
        message(STATUS "AI models source exists, adding copy target")
        add_custom_target(copy_ai_models ALL
            COMMAND ${CMAKE_COMMAND} -E echo "Copying AI models from ${LOCAL_AI_MODELS_SRC} to ${MODELS_DEST}"
            COMMAND ${CMAKE_COMMAND} -E remove_directory "${MODELS_DEST}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${LOCAL_AI_MODELS_SRC}" "${MODELS_DEST}"
            COMMENT "Syncing AI Models from: ${LOCAL_AI_MODELS_SRC}"
            VERBATIM)

        add_dependencies(copy_ai_models SmartSurveillance)
    else()
        message(STATUS "AI models source does not exist, skipping AI model copy target")
    endif()

    # 2. Copy SDL2 DLLs
    if(EXISTS "${SDL2_BIN_DIR}")
        add_custom_command(TARGET SmartSurveillance POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${SDL2_BIN_DIR}" "$<TARGET_FILE_DIR:SmartSurveillance>"
            COMMENT "Copying SDL2 binaries..."
            VERBATIM)
    endif()

    # 3. Copy FFmpeg DLLs
    if(EXISTS "${FFMPEG_DIR}/bin")
        add_custom_command(TARGET SmartSurveillance POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${FFMPEG_DIR}/bin" "$<TARGET_FILE_DIR:SmartSurveillance>"
            COMMENT "Copying FFmpeg binaries..."
            VERBATIM)
    endif()

    # 4. Copy legacy OpenCV models (Only if the folder exists)
    if(EXISTS "${GLOBAL_MODELS_SRC}")
        add_custom_command(TARGET SmartSurveillance POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory "${GLOBAL_MODELS_SRC}" "${MODELS_DEST}"
            COMMENT "Syncing legacy OpenCV models..."
            VERBATIM)
    endif()

    # 5. Copy sqlcipher.dll -- it lives in the isolated install root (see
    # find_package(sqlcipher) above), so vcpkg's own applocal DLL
    # deployment doesn't know to find it; every other DLL it depends on
    # (OpenSSL) is already deployed via the main tree's own OpenSSL usage.
    set(SQLCIPHER_DLL "${SMART_STORE_LIB_DIR}/vcpkg/installed-sqlcipher/x64-windows/bin/sqlcipher.dll")
    if(EXISTS "${SQLCIPHER_DLL}")
        add_custom_command(TARGET SmartSurveillance POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy
            "${SQLCIPHER_DLL}" "$<TARGET_FILE_DIR:SmartSurveillance>"
            COMMENT "Copying sqlcipher.dll..."
            VERBATIM)
    endif()
endif()
