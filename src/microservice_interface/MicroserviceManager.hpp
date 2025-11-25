#pragma once
#include <iostream>
#include <string> 
#include "microservice/net_curl/network/network_agent.hpp" 
#include <memory>
#include <unordered_map>
#include <functional>
#include "interface/BaseMicroservice.hpp"




class MicroserviceManager {
public:

    template <typename T>
    static std::unique_ptr<T> createMicroObjects(const std::string& productType) {
        if (productType.empty()) return nullptr;
        
        static const std::unordered_map<std::string, std::function<T*()>> factoryMap = {
            {"Network Agent", []() { return new NetworkAgent(); }}
        };
        
        auto it = factoryMap.find(productType);
        return (it != factoryMap.end()) ? std::unique_ptr<T>(it->second()) : nullptr;
    }
};