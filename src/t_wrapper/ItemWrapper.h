#pragma once
#ifndef ITEM_MANAGER_H
#define ITEM_MANAGER_H

#include "interface/BaseItem.h"
#include <iostream>
#include <memory>
#include <atomic>
#include <string>
#include <type_traits> 
#include <nlohmann/json.hpp>
#include <cxxabi.h>
#include <random>
#include <chrono>
#include <iomanip>
#include "versionForMigration/MigrationRegistry.h"
#include "err_log/Logger.hpp"

using json = nlohmann::json;




//    ==========================================================
//   |-- This ItemWrapper class  wraps every single item        |
//   |-- that are stored in the ItemManagert class.             |
//    ==========================================================

// namespace IdProvider {
//     inline std::atomic<size_t> counter{0};

//     inline std::string generateId() {
//         return "obj_no_" + std::to_string(++counter);
//     }
// }

namespace IdProvider {

    inline std::string generateId() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);

        std::stringstream ss;
        ss << "obj_";
        for (int i = 0; i < 8; ++i) ss << std::hex << dis(gen);
        ss << '-';
        for (int i = 0; i < 4; ++i) ss << std::hex << dis(gen);
        ss << "-4"; // UUID version 4
        for (int i = 0; i < 3; ++i) ss << std::hex << dis(gen);
        ss << '-';
        ss << std::hex << ((dis(gen) & 0x3) | 0x8); // UUID variant (8, 9, A, B)
        for (int i = 0; i < 3; ++i) ss << std::hex << dis(gen);
        ss << '-';
        for (int i = 0; i < 12; ++i) ss << std::hex << dis(gen);
        return ss.str();
    }

}


template<typename T>
class ItemWrapper : public BaseItem {
private:
    std::shared_ptr<T> data;
    std::string tag;

protected:
mutable std::string id_; // Unique ID for each item, mutable to allow modification in const methods

    

public:
public:
    ItemWrapper(std::shared_ptr<T> obj, const std::string& tag = "")
         : data(std::move(obj)), tag(tag),id_(IdProvider::generateId()) {} 


    ItemWrapper(const json& j)
    : data(std::make_shared<T>(j.at("data").get<T>())),
      tag(j.value("tag", ""))
    {
        std::string resolvedId = 
            j.contains("id") && !j.at("id").get<std::string>().empty()
                ? j.at("id").get<std::string>()
                : IdProvider::generateId();

        id_ = resolvedId;
    }

    std::string getId() const override {
        return id_;
    }

    void logId() const override {
            std::cout <<Logger::getColorCode(LogColor::WHITE) + "::: [ItemWrapper] Tag: " << tag << " | ID: " << id_ << Logger::getColorCode(LogColor::RESET) << std::endl;
    }


    void display() const override;

    std::string getTypeName() const override;

    json serialize() const override;

    std::shared_ptr<BaseItem> clone() const override;

    std::string getTag() const override;

     T& getData();
     
    const T& getData() const;

    T& getMutableData();

    nlohmann::json toJson() const override;
    
    static std::string friendlyName;
};

#include "ItemWrapper.tpp"  // Template definitions should be included at the end of the header


#endif // ITEM_MANAGER_H