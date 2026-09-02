#pragma once
#ifndef MICROSERVICEMANAGER_HPP
#define MICROSERVICEMANAGER_HPP



#include <iostream>
#include <string>
#include <memory>
#include <unordered_map>
#include <functional>

#include "interface/BaseMicroservice.hpp"
#include "microservice/alarm_system/AlarmSystem.hpp"
#include "microservice/computer_vision/scanner/document/Scanner.hpp"
#include "microservice/computer_vision/scanner/qr_code/QRCodeScanner.hpp"
#include "microservice/computer_vision/surveillance_interface/Surveillance.hpp"




class MicroserviceManager {
public:

    /**
    * @brief Factory method to create microservice objects based on product type.
    * 
    * @param productType The type of microservice to create.
    * @return A unique pointer to the created BaseMicroservice object.
    */
    static std::unique_ptr<BaseMicroservice> createMicroObjects(const std::string& productType) {
        static const std::unordered_map<std::string, std::function<BaseMicroservice*()>> factoryMap = {
            {"Document Scanner", []() { return new Scanner(); }},
            {"QR Code Scanner", []() { return new QRCodeScanner(); }},
            {"Alarm System", []() { return new AlarmSystem(); }},
            {"Surveillance", []() { return new Surveillance(); }}
        };

        auto it = factoryMap.find(productType);
        return (it != factoryMap.end())
            ? std::unique_ptr<BaseMicroservice>(it->second())
            : nullptr;
    }
};

#endif // MICROSERVICEMANAGER_HPP