# -----------------------------------
# Alarm Microservice
# -----------------------------------
add_library(alarm_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/AlarmSystem.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AlarmSystem.hpp
)

target_include_directories(alarm_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}        # so AlarmSystem.hpp is found
        ${PROJECT_SOURCE_DIR}/src
)

# -----------------------------------
# SDL2 + SDL2_mixer (Windows bundled, Linux/macOS via system)
# -----------------------------------
if(MINGW)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(SDL2_ARCH_DIR "x86_64-w64-mingw32")
    else()
        set(SDL2_ARCH_DIR "i686-w64-mingw32")
    endif()
endif()

if(WIN32 AND MINGW)
    set(SDL2_ROOT "${PROJECT_SOURCE_DIR}/lib/SDL2-2.32.4")
    set(SDL2_MIXER_ROOT "${PROJECT_SOURCE_DIR}/lib/SDL2_mixer-2.8.1")

    set(SDL2_INCLUDE_DIR "${SDL2_ROOT}/${SDL2_ARCH_DIR}/include")
    set(SDL2_LIB_DIR     "${SDL2_ROOT}/${SDL2_ARCH_DIR}/lib")
    set(SDL2_BIN_DIR     "${SDL2_ROOT}/${SDL2_ARCH_DIR}/bin")

    set(SDL2_MIXER_INCLUDE_DIR "${SDL2_MIXER_ROOT}/${SDL2_ARCH_DIR}/include")
    set(SDL2_MIXER_LIB_DIR     "${SDL2_MIXER_ROOT}/${SDL2_ARCH_DIR}/lib")
    set(SDL2_MIXER_BIN_DIR     "${SDL2_MIXER_ROOT}/${SDL2_ARCH_DIR}/bin")

    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR} "${SDL2_INCLUDE_DIR}/SDL2")
    set(SDL2_MIXER_INCLUDE_DIRS ${SDL2_MIXER_INCLUDE_DIR} "${SDL2_MIXER_INCLUDE_DIR}/SDL2")

    include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})
    link_directories(${SDL2_LIB_DIR} ${SDL2_MIXER_LIB_DIR})

    file(GLOB SDL2_LIBS "${SDL2_LIB_DIR}/*.a" "${SDL2_LIB_DIR}/*.lib")
    file(GLOB SDL2_MIXER_LIBS "${SDL2_MIXER_LIB_DIR}/*.a" "${SDL2_MIXER_LIB_DIR}/*.lib")

elseif(WIN32 AND MSVC)
    set(SDL2_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/SDL2/win-msvc/include")
    set(SDL2_LIB_DIR     "${PROJECT_SOURCE_DIR}/lib/SDL2/win-msvc/lib")
    set(SDL2_BIN_DIR     "${PROJECT_SOURCE_DIR}/lib/SDL2/win-msvc/bin")

    set(SDL2_MIXER_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/lib/SDL2_mixer/win-msvc/include")
    set(SDL2_MIXER_LIB_DIR     "${PROJECT_SOURCE_DIR}/lib/SDL2_mixer/win-msvc/lib")
    set(SDL2_MIXER_BIN_DIR     "${PROJECT_SOURCE_DIR}/lib/SDL2_mixer/win-msvc/bin")

    set(SDL2_INCLUDE_DIRS ${SDL2_INCLUDE_DIR} "${SDL2_INCLUDE_DIR}/SDL2")
    set(SDL2_MIXER_INCLUDE_DIRS ${SDL2_MIXER_INCLUDE_DIR} "${SDL2_MIXER_INCLUDE_DIR}/SDL2")

    include_directories(${SDL2_INCLUDE_DIRS} ${SDL2_MIXER_INCLUDE_DIRS})
    link_directories(${SDL2_LIB_DIR} ${SDL2_MIXER_LIB_DIR})

    file(GLOB SDL2_LIBS "${SDL2_LIB_DIR}/*.lib")
    file(GLOB SDL2_MIXER_LIBS "${SDL2_MIXER_LIB_DIR}/*.lib")

elseif(UNIX OR APPLE)
    find_package(SDL2 REQUIRED)
    find_package(SDL2_mixer REQUIRED)
endif()

# -----------------------------------
# Link dependencies
# -----------------------------------
target_link_libraries(alarm_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}
        $<$<PLATFORM_ID:Windows>:${SDL2_LIBS} ${SDL2_MIXER_LIBS} ws2_32 setupapi imm32 version winmm>
        $<$<NOT:$<PLATFORM_ID:Windows>>:SDL2::SDL2 SDL2_mixer::SDL2_mixer>
)

target_compile_features(alarm_microservice PUBLIC cxx_std_20)

# -----------------------------------
# Unit Test section removed (no test executable needed)
# -----------------------------------
