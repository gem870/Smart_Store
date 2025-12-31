# -----------------------------------
# NetCurl Microservice
# -----------------------------------
add_library(net_curl_microservice STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/network/network_agent.cpp
)

target_include_directories(net_curl_microservice
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/network       # for network_agent.hpp
        ${CMAKE_CURRENT_SOURCE_DIR}               # for message.hpp
        ${PROJECT_SOURCE_DIR}/src       # for BaseMicroservice.hpp
        ${PROJECT_SOURCE_DIR}/src/microservice_interface
        ${PROJECT_SOURCE_DIR}/src           # for PathFinder.hpp, etc.
)

target_link_libraries(net_curl_microservice
    PUBLIC
        core_interfaces
        nlohmann_json::nlohmann_json
        ${OPENCV_LIBS}   # only if you use OpenCV here
)

target_compile_features(net_curl_microservice PUBLIC cxx_std_20)
