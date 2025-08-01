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
#include "utils/Json_traits.hpp"
#include <cstdlib>    // for free()


using json = nlohmann::json;

//    ==========================================================
//   |-- This ItemWrapper class  wraps every single item        |
//   |-- that are stored in the ItemManagert class.             |
//    ==========================================================




// :: Forward declaration of IdProvider to generate unique IDs
// ***********************************************************

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

    std::string demangleType(const std::string& mangledName) const{
        #if defined(__GNUC__) || defined(__clang__)
        int status;
        char* demangled = abi::__cxa_demangle(mangledName.c_str(), nullptr, nullptr, &status);

        std::string result;
        if (status == 0 && demangled) {
            result = std::string(demangled);  // Store safely in std::string
            free(demangled);  // Ensure valid memory cleanup
            demangled = nullptr;  // Prevent accidental reuse
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Demangling failed for: " + mangledName, {});
            result = mangledName.c_str();
        }

        return result;
        #elif defined(_MSC_VER)
            return mangledName.c_str();
        #else
            return "Unknown compiler";
        #endif
   }

protected:
mutable std::string id_; // Unique ID for each item, mutable to allow modification in const methods

public:
public:
    ItemWrapper(std::shared_ptr<T> obj, const std::string& tag = "")
         : data(std::move(obj)), tag(tag),id_(IdProvider::generateId()) {} 

    ItemWrapper(const nlohmann::json& j) {
        data = std::make_shared<T>();

        // Assign id
        id_ = j.contains("id") && !j.at("id").get<std::string>().empty()
            ? j.at("id").get<std::string>()
            : IdProvider::generateId();

        // Assign tag
        tag = j.value("tag", "");

        // Assign data
        if (j.contains("data")) {
            if constexpr (has_from_json<T>::value) {
                from_json(j.at("data"), *data);
            } else if constexpr (std::is_same_v<T, std::string>) {
                // Handle both formats
                if (j.at("data").is_object() && j.at("data").contains("value")) {
                    *data = j.at("data").at("value").get<std::string>();
                } else if (j.at("data").is_string()) {
                    *data = j.at("data").get<std::string>();
                } else {
                    *data = ""; // fallback
                }
            } else {
                try {
                    j.at("data").get_to(*data);
                } catch (...) {
                    // leave default-constructed
                }
            }
        }
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