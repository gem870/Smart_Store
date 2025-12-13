#pragma once
#include <iostream>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

#include "FaceWatchlist.hpp"
#include "interface/BaseMicroservice.hpp"
#include "microservice/net_curl/network/network_agent.hpp"
#include "microservice/computer_vision/survelliance/FaceRecognition.hpp"
#include "microservice/computer_vision/survelliance/MotionDetection.hpp"

class MicroserviceManager {
public:
    static std::unique_ptr<BaseMicroservice> createMicroObjects(const std::string& productType) {
        static const std::unordered_map<std::string, std::function<BaseMicroservice*()>> factoryMap = {
            {"Network Agent", []() { return new NetworkAgent(); }},
            {"Face Recognition", []() { return new FaceRecognition(); }},
            {"Face Watchlist", []() { return new FaceWatchlist(); }},
            {"Motion Detection", []() { return new MotionDetection(); }}
        };

        auto it = factoryMap.find(productType);
        return (it != factoryMap.end())
            ? std::unique_ptr<BaseMicroservice>(it->second())
            : nullptr;
    }
};
