#pragma once

#include "ItemWrapper.h"
#include <sstream>
#include <typeinfo>
#include <memory>
#include <iostream>
#include <type_traits>

// Helper trait to check if a type is streamable (C++17-compatible)
template<typename T, typename = void>
struct is_streamable : std::false_type {};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>> : std::true_type {};

// Template function implementations for ItemWrapper
template<typename T>
void ItemWrapper<T>::display() const {
    std::cout << "Type: " << getTypeName();
    if (!tag.empty()) {
        std::cout << " | Tag: " << tag;

        if (!data) {
            std::cout << " | Value: [null data]\n";
            return;
        }

        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            std::cout << " | Value: " << *data << "\n";
        } else {
            std::cout << " | Value: [non-streamable type]\n";
        }
    } else {
        std::cout << " :::| --> { No type found } <--\n";
    }
}

template<typename T>
std::string ItemWrapper<T>::getTypeName() const {
    return typeid(T).name();
}

template<typename T>
json ItemWrapper<T>::serialize() const {

    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot serialize null data." + Logger::getColorCode(LogColor::RESET));
    }

    json j;
    j["id"] = id_;
    j["tag"] = tag;
    j["type"] = getTypeName();
    

    // Direct arithmetic values like int, float, etc.
    if constexpr (std::is_arithmetic_v<T>) {
        j["data"] = *data;
        return j;
    }

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    // If T defines a custom .serialize() method returning json
    else if constexpr (requires(const T& obj) { { obj.serialize() } -> std::same_as<json>; }) {
        j["data"] = data->serialize();
        return j;
    }
    // If T is streamable to ostream
    else if constexpr (requires(std::ostream& os, const T& obj) { os << obj; }) {
        std::ostringstream oss;
        oss << *data;
        j["data"] = oss.str();
        return j;
    }
#else
    // Legacy fallback if not using concepts
    else if constexpr (has_serialize<T>::value) {
        j["data"] = data->serialize();
        return j;
    }
    else if constexpr (is_streamable<T>::value) {
        std::ostringstream oss;
        oss << *data;
        j["data"] = oss.str();
        return j;
    }
#endif

    // Catch-all: try converting to nlohmann::json
    else {
        try {
            j["data"] = nlohmann::json(*data);
            return j;
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Serialization failed for type '" + getTypeName() + "': " + e.what());
            return j;
        }
    }
}

template<typename T>
std::shared_ptr<BaseItem> ItemWrapper<T>::clone() const {
    return std::make_shared<ItemWrapper<T>>(std::make_shared<T>(*data), tag);
}

template<typename T>
std::string ItemWrapper<T>::getTag() const {
    return tag;
}

template<typename T>
T& ItemWrapper<T>::getData() {
    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot access null data." + Logger::getColorCode(LogColor::RESET));
    }
    return *data;
}

template<typename T>
const T& ItemWrapper<T>::getData() const {
    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot access null data." + Logger::getColorCode(LogColor::RESET));
    }
    return *data;
}

template<typename T>
T& ItemWrapper<T>::getMutableData() {
    return *data;
}

template <typename T>
nlohmann::json ItemWrapper<T>::toJson() const {
    return *data;  
}

