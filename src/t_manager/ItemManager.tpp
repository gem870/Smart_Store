#include "ItemManager.h"
#include "nlohmann/json.hpp"
#include "err_log/Logger.hpp"
#include "utils/AtomicFileWriter .hpp"
#include "utils/Json_traits.hpp"
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <type_traits>
#include "../lib/tinyxml2/tinyxml2.h"
#include <string>
#include <typeinfo>
#include <thread>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h> // For abi::__cxa_demangle
#endif

using json = nlohmann::json;



//::::: PRIVATE FUNCTIONS ::::::
//******************************

template<typename T>
std::string ItemManager::getCompilerTypeName() {
 return typeid(T).name(); // Return the mangled name directly for simplicity
}

ItemManager::State ItemManager::cloneCurrentState() const {
    State clone;
    clone.reserve(items.size()); // Reserve memory upfront to avoid repeated allocations
    for (const auto& [tag, item] : items) {
        clone.emplace(tag, item->clone()); // Use emplace for in-place construction
    }
    return clone;
}

void ItemManager::saveState() {
    // Trim oldest undo if exceeding max history
    if (undoHistory.size() > MAX_UNDO_HISTORY) {
        undoHistory.pop_front();
    }

    while (redoQueue.size() > MAX_REDO_HISTORY) {
        redoQueue.pop(); // Drop oldest redo
    }
}

template<typename T>
std::shared_ptr<BaseItem> ItemManager::deserializeItemById(const json& j) {
    std::string id = j.at("id");
    if (idMap.count(id)) {
        return idMap[id];
    }
    // Only call deserialization for supported types
    std::shared_ptr<ItemWrapper<T>> item;
    if constexpr (has_from_json<T>::value) {
        item = std::make_shared<ItemWrapper<T>>(j);
    } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
        item = std::make_shared<ItemWrapper<T>>(j);
    } else {
        // fallback: construct with default data only
        item = std::make_shared<ItemWrapper<T>>(std::make_shared<T>(), j.value("tag", ""));
    }
    idMap[id] = item;
    // Recursively deserialize children, using idMap
    return item;
}

template<typename T>
void ItemManager::registerType() {
    std::string typeName = getCompilerTypeName<T>();

    if (deserializers.find(typeName) == deserializers.end()) {
        // Use a lambda that calls deserializeItemById<T>
        deserializers[typeName] = [this](const json& j, const std::string&) {
            return this->deserializeItemById<T>(j);
        };

        registeredTypes.emplace(typeName, std::type_index(typeid(T)));

        if constexpr (has_schema<T>::value) {
            schemaRegistry[typeName] = []() { return T::schema(); };
            std::cout << Logger::getColorCode(LogColor::WHITE) + "::: Registered schema for type: " << typeName << Logger::getColorCode(LogColor::RESET) <<"\n";
        }

        std::cout << Logger::getColorCode(LogColor::MAGENTA) + "\n:::| Automatically registered type (without adding item): " << demangleType(typeName) << Logger::getColorCode(LogColor::RESET) + "\n";
    }
}

json ItemManager::getSchemaForType(std::string type) const {
    auto it = schemaRegistry.find(type);
    return (it != schemaRegistry.end()) ? it->second() : json{};
}


// ::::: MAIN API USER CALLS OR PUBLIC FUNCTIONS ::::::
// ****************************************************

void ItemManager::displayRegisteredDeserializers() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << Logger::getColorCode(LogColor::MAGENTA) << "\n:::| Registered Deserializers in ItemManager |:::\n" << Logger::getColorCode(LogColor::RESET);
    
    if (deserializers.empty()) {
        Logger::log(LogLevel::INFO, "No deserializers registered");
        return;
    }

    for (const auto& entry : deserializers) {
        Logger::log(LogLevel::DEBUG, "Type: " + demangleType(entry.first) + " -> Deserialization Function Exists");
    }

    std::cout << "\n::::::::::::::::::::::::::::::::::::::::::::::::\n";

    std::cout << Logger::getColorCode(LogColor::MAGENTA) << "\n:::| Registered types in ItemManager |:::\n" << Logger::getColorCode(LogColor::RESET);
    if (registeredTypes.empty()) {
        Logger::log(LogLevel::INFO, "No types registered");
        return;
    }
    for (const auto& entry : registeredTypes) {
        Logger::log(LogLevel::DEBUG, "Type: " + demangleType(entry.first) + " -> Type Index: " + demangleType(entry.second.name()));
    }
    std::cout << "\n::::::::::::::::::::::::::::::::::::::::::::::::\n";
}

bool ItemManager::hasItem(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items.find(tag) != items.end();
}

std::string ItemManager::demangleType(const std::string& mangledName) const{
    #if defined(__GNUC__) || defined(__clang__)
    int status;
    char* demangled = abi::__cxa_demangle(mangledName.c_str(), nullptr, nullptr, &status);

    std::string result;
    if (status == 0 && demangled) {
        result = std::string(demangled);  // Store safely in std::string
        free(demangled);  // Ensure valid memory cleanup
        demangled = nullptr;  // Prevent accidental reuse
    } else {
        Logger::log(LogLevel::WARNING, "Demangling failed for: " + mangledName);
        result = mangledName.c_str();
    }

    return result;
    #elif defined(_MSC_VER)
        return mangledName.c_str();
    #else
        return "Unknown compiler";
    #endif
}

template<typename T>
void ItemManager::addItem(std::shared_ptr<T> obj, const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);
    if (!obj) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + "\n:::| WARNING: Cannot add null object with tag '" + tag + Logger::getColorCode(LogColor::RESET));
    }
    std::cout <<Logger::getColorCode(LogColor::GREEN) + "\nAn item added with tag: " << tag << Logger::getColorCode(LogColor::RESET) << std::endl;

    saveState();
    undoHistory.push_back(cloneCurrentState());
    redoQueue = {};

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    std::cout << "Using C++20 Concepts for Type Registration.\n";
#else
    std::cout << "Using SFINAE-based Registration (C++17 or older).\n";
#endif

    // Automatic Type Registration
    migrationRegistry.registerVersion("User", 3);
    migrationRegistry.registerMigration("User", 1, [](const json& j) {
        json upgraded = j;
        upgraded["age"] = 0;
        return upgraded;
    });
    migrationRegistry.registerMigration("User", 2, [](const json& j) {
        json upgraded = j;
        upgraded["email"] = "unknown@example.com";
        return upgraded;
    });
    registerType<T>();  // Ensures type is registered separately for imports

    items[tag] = std::make_shared<ItemWrapper<T>>(std::move(obj), tag);

    for (const auto& [key, value] : items) {
        Logger::log(LogLevel::DEBUG, "Item with tag '" + key + "' registered with type: " + demangleType(value->getTypeName()));
    }

    Logger::log(LogLevel::INFO, "Item with tag '" + tag + "' added successfully. Type: " + demangleType(getCompilerTypeName<T>()));
}

template<typename T>
bool ItemManager::modifyItem(const std::string& tag, const std::function<void(T&)>& modifier) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = items.find(tag);
    if (it != items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            undoHistory.push_back(cloneCurrentState());
            redoQueue = {};
            modifier(wrapper->getMutableData());
            return true;
        }
    }
    return false;
}

template<typename T>
std::optional<T> ItemManager::getItem(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = items.find(tag);
    if (it != items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getData();
        } else {
            Logger::log(LogLevel::WARNING, "Type mismatch for tag '" + tag + "'. Requested type: " + demangleType(typeid(T).name())+ ", Actual type: " + demangleType(it->second->getTypeName()));
        }
    } else {
        Logger::log(LogLevel::WARNING, "No item found with tag '" + tag + "'");
    }
    return std::nullopt;
}

template<typename T>
T& ItemManager::getItemRaw(const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = items.find(tag);
    if (it != items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getMutableData();
        } else {
            Logger::log(LogLevel::WARNING, "Type mismatch for item with tag '" + tag + "'. Requested type: " + demangleType(typeid(T).name()) + ", Actual type: " + demangleType(it->second->getTypeName()));
            throw std::runtime_error("\n:::| Type mismatch for item with tag '" + tag + "'.\n");
        }
    } else {
        Logger::log(LogLevel::WARNING, "Item with tag '" + tag + "' not found.");
        throw std::runtime_error("\n:::| Item with tag '" + tag + "' not found.\n");
    }
}

template<typename T>
const T& ItemManager::getItemRaw(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = items.find(tag);
    if (it != items.end()) {
        auto wrapper = dynamic_cast<const ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getData();
        } else {
            Logger::log(LogLevel::WARNING, "Type mismatch for item with tag '" + tag + "'. Requested type: " + demangleType(typeid(T).name()) + ", Actual type: " + demangleType(it->second->getTypeName()));
            throw std::runtime_error("\n:::| Type mismatch for item with tag '" + tag + "'.\n");
        }
    } else {
        Logger::log(LogLevel::WARNING, "Item with tag '" + tag + "' not found.");
        throw std::runtime_error("\n:::| Item with tag '" + tag + "' not found.\n");
    }
}

void ItemManager::displayAll() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout << Logger::getColorCode(LogColor::WHITE) + ":::::: Types Stored ::::::" + Logger::getColorCode(LogColor::RESET);
    if (!items.empty()) {
        for (const auto& [_, item] : items) item->display();
    }else{
        Logger::log(LogLevel::INFO, "No items found to display.");
    }
}

void ItemManager::displayByTag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);

    auto it = items.find(tag);
    if (it != items.end()) {
        Logger::log(LogLevel::DEBUG, "Displaying item with tag '" + tag + "'");
        it->second->display();
        return;
    }

    throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Item with tag '" + tag + "' —> not found." + Logger::getColorCode(LogColor::RESET));
}

void ItemManager::removeByTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (tag.empty()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Cannot remove item with empty tag." + Logger::getColorCode(LogColor::RESET));
    }
    auto it = items.find(tag);
    if (it != items.end()) {
        undoHistory.push_back(cloneCurrentState());

        std::queue<State> empty;
        std::swap(redoQueue, empty);

        std::string typeName = it->second->getTypeName();
        std::string id = it->second->getId(); // Extract ID before erasing

        if (--typeUsage[typeName] == 0) {
            typeUsage.erase(typeName);
            registeredTypes.erase(typeName);
            deserializers.erase(typeName);
            schemaRegistry.erase(typeName);  //  Clean up schema too
            Logger::log(LogLevel::DEBUG, "Removed type: " + demangleType(typeName) + " from registry");
        }

        items.erase(it);
        idMap.erase(id); // Now erase from idMap as well

        Logger::log(LogLevel::DEBUG, "Removed item with tag '" + tag + "' and id '" + id + "'");
    } else {
        Logger::log(LogLevel::WARNING, "No item found with tag '" + tag + "' to be removed.");
    }
}

void ItemManager::undo() {
    std::lock_guard<std::mutex> lock(mutex_);  // Thread guard

    if (!undoHistory.empty()) {
        auto current = cloneCurrentState();            // Save current state
        auto prev = std::move(undoHistory.back());     // Last undo state
        undoHistory.pop_back();

        redoQueue.push(std::move(current));            // Push current into redo
        items = std::move(prev);                       // Restore previous state

        Logger::log(LogLevel::DEBUG, "Undo successful. Restored to previous state.");
    } else {
        Logger::log(LogLevel::INFO, "Nothing to undo.");
    }
}

void ItemManager::redo() {
    std::lock_guard<std::mutex> lock(mutex_);
  
    if (!redoQueue.empty()) {
        undoHistory.push_back(cloneCurrentState());   // Save current state
        items = std::move(redoQueue.front());         // Restore redo state
        redoQueue.pop();

        Logger::log(LogLevel::DEBUG, "Redo successful. Restored to next state.");
    } else {
        Logger::log(LogLevel::INFO, "Nothing to redo.");
    }
}

void ItemManager::exportToFile_Json(const std::string& filename) const {
    if (items.empty()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + 
            ":::| WARNING: Cannot export to file '" + filename + "' — no items found." + 
            Logger::getColorCode(LogColor::RESET));
    }

    nlohmann::json jArray = nlohmann::json::array();

    for (const auto& [tag, item] : items) {
        if (!item) {
            Logger::log(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.");
            continue;
        }


        nlohmann::json entry;
        entry["id"] = item->getId();
        entry["tag"] = tag;
        entry["type"] = item->getTypeName();

        try {
            entry["data"] = item->serialize();
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Serialization failed for item '" + tag + "': " + e.what());
            continue;
        }

        auto schema = getSchemaForType(item->getTypeName());
        if (!schema.is_null()) {
            entry["schema"] = schema;
            Logger::log(LogLevel::DEBUG, "Attached schema for type: " + demangleType(item->getTypeName()));
        }

        jArray.push_back(entry);
        
        Logger::log(LogLevel::INFO, "Exporting item with tag: " + tag + " of type: " + demangleType(item->getTypeName()));
        std::cout << Logger::getColorCode(LogColor::CYAN)
                  << entry.dump(4) 
                  << Logger::getColorCode(LogColor::RESET) + "\n";

        Logger::log(LogLevel::INFO, "Added entry for tag: " + tag);
    }

    std::string jsonContent = jArray.dump(4);

    if (!AtomicFileWriter::writeAtomically(filename, jsonContent)) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + 
            ":::| ERROR: Failed atomic write to file: " + filename + 
            Logger::getColorCode(LogColor::RESET));
    }

    Logger::log(LogLevel::INFO, "Exported " + std::to_string(jArray.size()) + " items to file (atomically): " + filename);
}

void ItemManager::asyncExportToFile_Json(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            this->exportToFile_Json(filename);  // Thread-safe at its core
        } catch (const std::exception& e) {

            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Exception in asyncExportToFile_Json: " + std::string(e.what()) + Logger::getColorCode(LogColor::RESET));
        }
    }).detach();  // Fire-and-forget
}

void ItemManager::importFromFile_Json(const std::string& filename) {
    Logger::log(LogLevel::INFO, "Attempting JSON import from file: " + filename);

    std::ifstream in(filename);
    if (!in) {
        throw std::runtime_error(Logger::getColorCode(LogColor::WHITE) + "\n:::| WARNING: Cannot open file for reading: " + filename + Logger::getColorCode(LogColor::RESET) + "\n");
    }

    json parsedJson;
    in >> parsedJson;
    Logger::log(LogLevel::DEBUG, "JSON file loaded successfully: " + filename);

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::| Loaded JSON content from file:\n" << Logger::getColorCode(LogColor::RESET) << parsedJson.dump(2) << "\n";

    if (parsedJson.is_array()) {
        Logger::log(LogLevel::DEBUG, "Processing JSON array format.");
    } else if (parsedJson.contains("items") && parsedJson["items"].is_array()) {
        parsedJson = parsedJson["items"];
        Logger::log(LogLevel::DEBUG, "Processing JSON with 'items' key.");
    } else {
        Logger::log(LogLevel::ERR, "Invalid JSON format in: " + filename);
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + "\n:::| WARNING: Invalid JSON format: Expected an array or 'items' key." + Logger::getColorCode(LogColor::RESET) + "\n");
    }

    undoHistory.push_back(cloneCurrentState());
    redoQueue = {};
    items.clear();

    int importCount = 0;

    for (const auto& entry : parsedJson) {
        if (!entry.contains("tag") || !entry.contains("type") || !entry.contains("data")) {
            Logger::log(LogLevel::WARNING, "Skipping entry due to missing keys: 'tag', 'type', or 'data'.");
            continue;
        }

        std::string tag = entry["tag"].get<std::string>();
        std::string typeName = entry["type"].get<std::string>();
        int version = entry.value("version", 1);
        json rawData = entry["data"];

        Logger::log(LogLevel::INFO, "Importing item: '" + tag + "' of type: '" + demangleType(typeName) + "'");

        if (!rawData.contains("id") && entry.contains("id")) {
            rawData["id"] = entry["id"];
        }

        if (entry.contains("schema")) {
            Logger::log(LogLevel::DEBUG, "Schema detected for type: " + demangleType(typeName));
            schemaRegistry[typeName] = [schema = entry["schema"]]() {
                return schema;
            };
        }

        json upgraded = migrationRegistry.upgradeToLatest(typeName, version, rawData);
        Logger::log(LogLevel::DEBUG, "Schema migration applied (if needed) for '" + tag + "' to latest version.");

        auto typeIt = registeredTypes.find(typeName);
        if (typeIt == registeredTypes.end()) {
            Logger::log(LogLevel::WARNING, "Unknown type: " + demangleType(typeName) + " — skipping.");
            continue;
        }

        auto desIt = deserializers.find(typeName);
        if (desIt == deserializers.end()) {
            Logger::log(LogLevel::WARNING, "No deserializer registered for type: " + demangleType(typeName) + " — skipping.");
            continue;
        }

        Logger::log(LogLevel::INFO, "Attempting to deserialize item with tag '" + tag + "' and type '" + demangleType(typeName) + "'.");
        std::cout << Logger::getColorCode(LogColor::CYAN)
                  << entry.dump(4) 
                  << Logger::getColorCode(LogColor::RESET) + "\n";

        try {
            auto newItem = desIt->second(upgraded, tag);
            if (newItem) {
                items[tag] = std::move(newItem);
                Logger::log(LogLevel::INFO, "Item '" + tag + "' imported successfully.");
                ++importCount;
            } else {
                Logger::log(LogLevel::ERR, "Deserializer returned null for tag: " + tag);
            }
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + e.what());
        }
    }

    Logger::log(LogLevel::INFO, "Completed import of " + std::to_string(importCount) + " item(s) from JSON file: " + filename);
}

void ItemManager::asyncImportFromFile_Json(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            this->importFromFile_Json(filename);  // Thread-safe core
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "asyncImportFromFile_Json error: " + std::string(e.what()));
        }
    }).detach();  // Fire-and-forget style
}

std::shared_ptr<BaseItem> ItemManager::importSingleObject_Json(const std::string& filename, const std::string& typeName, const std::string& tag) {
    Logger::log(LogLevel::INFO, "Attempting to import single JSON object from file: " + filename);

    std::ifstream in(filename);
    if (!in) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Cannot open file '" + filename + "' for reading." + Logger::getColorCode(LogColor::RESET));
    }

    json array;
    try {
        in >> array;
        Logger::log(LogLevel::DEBUG, "JSON file parsed successfully.");
    } catch (const std::exception& e) {
        Logger::log(LogLevel::ERR, "Failed to parse JSON from '" + filename + "': " + e.what());
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Failed to parse JSON from file '" + filename + "': " + e.what() + Logger::getColorCode(LogColor::RESET));
    }

    for (const auto& entry : array) {
        if (entry.value("tag", "") == tag && entry.value("type", "") == typeName) {
            Logger::log(LogLevel::INFO, "Found matching object with tag '" + tag + "' and type '" + demangleType(typeName) + "'.");

            int version = entry.value("version", 1);
            json rawData = entry["data"];

            std::cout << Logger::getColorCode(LogColor::YELLOW)
                      << entry.dump(4)
                      << Logger::getColorCode(LogColor::RESET) + "\n";

            if (!rawData.contains("id") && entry.contains("id")) {
                rawData["id"] = entry["id"];
            }

            if (entry.contains("schema")) {
                Logger::log(LogLevel::DEBUG, "Embedded schema detected for tag: " + tag);
                schemaRegistry[typeName] = [schema = entry["schema"]]() {
                    return schema;
                };
            }

            json upgraded = migrationRegistry.upgradeToLatest(typeName, version, rawData);
            Logger::log(LogLevel::DEBUG, "Schema migration applied (if needed) to latest version.");

            auto typeIt = registeredTypes.find(typeName);
            if (typeIt == registeredTypes.end()) {
                Logger::log(LogLevel::WARNING, "Unknown type: " + demangleType(typeName) + " — skipping.");
                return nullptr;
            }

            auto desIt = deserializers.find(typeName);
            if (desIt == deserializers.end()) {
                Logger::log(LogLevel::WARNING, "No deserializer registered for type: " + demangleType(typeName) + " — skipping.");
                return nullptr;
            }

            try {
                Logger::log(LogLevel::INFO, "Attempting to deserialize item with tag '" + tag + "' and type '" + demangleType(typeName) + "'.");
                auto item = desIt->second(upgraded, tag);
                if (item) {
                    Logger::log(LogLevel::INFO, "Deserialization successful for tag '" + tag + "'.");
                } else {
                    Logger::log(LogLevel::ERR, "Deserializer returned null for tag: " + tag);
                }

                undoHistory.push_back(cloneCurrentState());
                redoQueue = {};
                items[tag] = item;  // safely inserts into store

                return item;
            } catch (const std::exception& e) {
                  throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Exception during deserialization of '" + tag + "': " + e.what() + Logger::getColorCode(LogColor::RESET));
            }
        }
    }

    Logger::log(LogLevel::WARNING, "No object found with tag '" + tag + "' and type '" + demangleType(typeName) + "' in file: " + filename);
    return nullptr;
}

void ItemManager::asyncImportSingleObject_Json(const std::string& filename, const std::string& typeName, const std::string& tag) {
    std::thread([this, filename, typeName, tag]() {
        auto item = this->importSingleObject_Json(filename, typeName, tag);
        if (item) {
            std::lock_guard<std::mutex> lock(mutex_);
            items[tag] = std::move(item);  // safely inserts into store
            Logger::log(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully.");
        } else {
            Logger::log(LogLevel::WARNING, "Async import failed for tag '" + tag + "' from file '" + filename + "'.");
        }
    }).detach();
}

bool ItemManager::exportToFile_Binary(const std::string& filename) const {
    if (items.empty()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: No items found for export to file '" + filename + "'." + Logger::getColorCode(LogColor::RESET));
    }

    std::vector<uint8_t> buffer;

    for (const auto& [tag, item] : items) {
        json serializedJson = item->serialize();
        serializedJson["id"] = item->getId();
        serializedJson["tag"] = tag;
        serializedJson["type"] = item->getTypeName();

        std::string type = item->getTypeName();
        std::string tagStr = item->getTag();
        std::string jsonStr = serializedJson.dump();

        uint32_t typeSize = static_cast<uint32_t>(type.size());
        uint32_t tagSize  = static_cast<uint32_t>(tagStr.size());
        uint32_t dataSize = static_cast<uint32_t>(jsonStr.size());

        auto append = [&buffer](const void* data, size_t size) {
            const auto* bytes = static_cast<const uint8_t*>(data);
            buffer.insert(buffer.end(), bytes, bytes + size);
        };

        append(&typeSize, sizeof(typeSize));
        append(type.data(), typeSize);
        append(&tagSize, sizeof(tagSize));
        append(tagStr.data(), tagSize);
        append(&dataSize, sizeof(dataSize));
        append(jsonStr.data(), dataSize);

        Logger::log(LogLevel::INFO, "Exported binary object with tag '" + tag + "' of type '" + demangleType(type) + "' [hex]:");

        auto dumpHex = [](const void* data, size_t size) {
            const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
            for (size_t i = 0; i < size; ++i) {
                std::printf("%02X ", bytes[i]);
                if ((i + 1) % 16 == 0) std::cout << '\n';
            }
            std::cout << "\n";
        };

        dumpHex(&typeSize, sizeof(typeSize));
        dumpHex(type.data(), typeSize);
        dumpHex(&tagSize, sizeof(tagSize));
        dumpHex(tagStr.data(), tagSize);
        dumpHex(&dataSize, sizeof(dataSize));
        dumpHex(jsonStr.data(), dataSize);
    }

    if (!AtomicFileWriter::writeAtomicallyBinary(filename, buffer)) {
        Logger::log(LogLevel::ERR, "Failed atomic binary export to '" + filename + "'.");
        return false;
    }

    Logger::log(LogLevel::INFO, "Binary export to '" + filename + "' completed successfully.");
    return true;
}

void ItemManager::asyncExportToFile_Binary(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            bool success = this->exportToFile_Binary(filename);
            if (!success) {
                Logger::log(LogLevel::WARNING, "asyncExportToFile_Binary failed for file: " + filename);
            } else {
                Logger::log(LogLevel::INFO, "asyncExportToFile_Binary completed successfully for file: " + filename);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error( Logger::getColorCode(LogColor::RED) + ":::| WARNING: Exception in asyncExportToFile_Binary: " + std::string(e.what()) + Logger::getColorCode(LogColor::RESET));
        }
    }).detach();
}

bool ItemManager::importFromFile_Binary(const std::string& filename) {
    Logger::log(LogLevel::INFO, "Importing binary file: " + filename);

    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        Logger::log(LogLevel::ERR, "Cannot open binary file '" + filename + "' for reading.");
        return false;
    }

    undoHistory.push_back(cloneCurrentState());
    redoQueue = {};
    items.clear();

    while (in.peek() != EOF) {
        uint32_t typeSize = 0, tagSize = 0, dataSize = 0;

        in.read(reinterpret_cast<char*>(&typeSize), sizeof(typeSize));
        if (in.gcount() != sizeof(typeSize)) break;

        std::string type(typeSize, '\0');
        in.read(type.data(), typeSize);
        if (in.gcount() != static_cast<std::streamsize>(typeSize)) break;

        in.read(reinterpret_cast<char*>(&tagSize), sizeof(tagSize));
        if (in.gcount() != sizeof(tagSize)) break;

        std::string tag(tagSize, '\0');
        in.read(tag.data(), tagSize);
        if (in.gcount() != static_cast<std::streamsize>(tagSize)) break;

        in.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
        if (in.gcount() != sizeof(dataSize)) break;

        std::string jsonStr(dataSize, '\0');
        in.read(jsonStr.data(), dataSize);
        if (in.gcount() != static_cast<std::streamsize>(dataSize)) break;

        Logger::log(LogLevel::DEBUG, "Processing binary object with tag '" + tag + "' of type '" + demangleType(type) + "' [hex]:");
        for (size_t i = 0; i < dataSize; ++i) {
            std::printf("%02X ", static_cast<unsigned char>(jsonStr[i]));
            if ((i + 1) % 16 == 0) std::cout << '\n';
        }
        std::cout << "\n";

        json serialized;
        try {
            serialized = json::parse(jsonStr);
            Logger::log(LogLevel::DEBUG, "Binary JSON parsed successfully for tag: " + tag);
        } catch (const json::parse_error& err) {
            Logger::log(LogLevel::ERR, "Failed to parse JSON for tag '" + tag + "': " + std::string(err.what()));
            continue;
        }

        if (!serialized.contains("id") && !tag.empty()) {
            serialized["id"] = tag;  // Optional fallback for legacy
        }

        int version = 1; // Default to version 1 if not present
        if (serialized.contains("version")) {
            version = serialized["version"].get<int>();
        }

        json upgraded = migrationRegistry.upgradeToLatest(type, version, serialized);
        Logger::log(LogLevel::DEBUG, "Schema migration applied (if needed) for tag: " + tag + " to latest version.");

        auto desIt = deserializers.find(type);
        if (desIt == deserializers.end()) {
            Logger::log(LogLevel::WARNING, "No deserializer registered for type: " + type + " — skipping.");
            continue;
        }

        try {
            auto object = desIt->second(upgraded, tag);
            if (!object) {
                Logger::log(LogLevel::WARNING, "Deserializer returned null for tag: " + tag);
                continue;
            }

            items[tag] = object;
            Logger::log(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' and type '" + type + "' from binary file: " + filename);
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + std::string(e.what()));
            continue;
        }
    }

    in.close();
    Logger::log(LogLevel::INFO, "Binary import from '" + filename + "' completed successfully with " + std::to_string(items.size()) + " items.");
    return true;
}

void ItemManager::asyncImportFromFile_Binary(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            this->importFromFile_Binary(filename);  // Thread-safe if core is locked
            Logger::log(LogLevel::INFO, "asyncImportFromFile_Binary completed successfully for file: " + filename);
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception in asyncImportFromFile_Binary: " + std::string(e.what()));
        }
    }).detach();
}

std::shared_ptr<BaseItem> ItemManager::importSingleObject_Binary(const std::string& filename, const std::string& type, const std::string& tag) {
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Cannot open binary file '" + filename + "' for reading." + Logger::getColorCode(LogColor::RESET));
    }


    while (in.peek() != EOF) {
        uint32_t typeSize = 0, tagSize = 0, dataSize = 0;

        in.read(reinterpret_cast<char*>(&typeSize), sizeof(typeSize));
        if (in.gcount() != sizeof(typeSize)) break;

        std::string entryType(typeSize, '\0');
        in.read(entryType.data(), typeSize);
        if (in.gcount() != static_cast<std::streamsize>(typeSize)) break;

        in.read(reinterpret_cast<char*>(&tagSize), sizeof(tagSize));
        if (in.gcount() != sizeof(tagSize)) break;

        std::string entryTag(tagSize, '\0');
        in.read(entryTag.data(), tagSize);
        if (in.gcount() != static_cast<std::streamsize>(tagSize)) break;

        in.read(reinterpret_cast<char*>(&dataSize), sizeof(dataSize));
        if (in.gcount() != sizeof(dataSize)) break;

        std::string jsonStr(dataSize, '\0');
        in.read(jsonStr.data(), dataSize);
        if (in.gcount() != static_cast<std::streamsize>(dataSize)) break;

        if (entryType == type && entryTag == tag) {
            Logger::log(LogLevel::DEBUG, "Matched binary object for tag '" + tag + "' of type '" + demangleType(type) + "'");

            auto dumpHex = [](const void* data, size_t size) {
                const unsigned char* bytes = reinterpret_cast<const unsigned char*>(data);
                for (size_t i = 0; i < size; ++i) {
                    std::printf("%02X ", bytes[i]);
                    if ((i + 1) % 16 == 0) std::cout << '\n';
                }
                std::cout << "\n";
            };

            dumpHex(&typeSize, sizeof(typeSize));
            dumpHex(entryType.data(), entryType.size());
            dumpHex(&tagSize, sizeof(tagSize));
            dumpHex(entryTag.data(), entryTag.size());
            dumpHex(&dataSize, sizeof(dataSize));
            dumpHex(jsonStr.data(), jsonStr.size());

            json serialized;
            try {
                serialized = json::parse(jsonStr);
            } catch (...) {
                throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Failed to parse JSON for tag: '" + tag + "'" + Logger::getColorCode(LogColor::RESET));
            }

            if (!serialized.contains("id") && !tag.empty()) {
                serialized["id"] = tag;
            }

            int version = 1;  // Assume version 1 for old binary
            if (serialized.contains("version")) {
                version = serialized["version"].get<int>();
            }

            json upgraded = migrationRegistry.upgradeToLatest(entryType, version, serialized);

            auto it = deserializers.find(entryType);
            if (it == deserializers.end()) {
                throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: No deserializer registered for type '" + demangleType(entryType) + "'" + Logger::getColorCode(LogColor::RESET));
            }

            auto object = it->second(upgraded, tag);
            if (!object) {
                throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Deserializer returned null for tag '" + tag + "'" + Logger::getColorCode(LogColor::RESET));
            }

            undoHistory.push_back(cloneCurrentState());
            redoQueue = {};
            items[tag] = object;  // Safely insert into store

            Logger::log(LogLevel::INFO, "Successfully imported object with tag '" + tag + "' from file '" + filename + "'");
            return object;
        }
    }

    Logger::log(LogLevel::WARNING, "No matching object found for tag '" + tag + "' and type '" + demangleType(type) + "' in file '" + filename + "'");
    return nullptr;
}

void ItemManager::asyncImportSingleObject_Binary(const std::string& filename, const std::string& typeName, const std::string& tag) {
    std::thread([this, filename, typeName, tag]() {
        auto item = this->importSingleObject_Binary(filename, typeName, tag);
        if (item) {
            std::lock_guard<std::mutex> lock(mutex_);
            items[tag] = std::move(item);
            Logger::log(LogLevel::INFO, "Async binary import of '" + tag + "' succeeded.");
        } else {
            Logger::log(LogLevel::WARNING, "Async binary import failed for tag '" + tag + "' from file '" + filename + "'.");
        }
    }).detach();
}

bool ItemManager::exportToFile_XML(const std::string& filename) const {
    if (items.empty()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Cannot export to file '" + filename + "' — no items found." + Logger::getColorCode(LogColor::RESET));
    }

    tinyxml2::XMLDocument doc;
    auto* root = doc.NewElement("SmartStore");
    doc.InsertFirstChild(root);

    for (const auto& [tag, item] : items) {
        if (!item) {
            Logger::log(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.");
            continue;
        }

        Logger::log(LogLevel::INFO, "Exporting item with tag: " + tag + " of type: " + demangleType(item->getTypeName()));

        auto* itemElement = doc.NewElement("Item");

        auto* tagElement = doc.NewElement("Tag");
        tagElement->SetText(tag.c_str());
        itemElement->InsertEndChild(tagElement);

        auto* typeElement = doc.NewElement("Type");
        typeElement->SetText(item->getTypeName().c_str());
        itemElement->InsertEndChild(typeElement);

        auto* dataElement = doc.NewElement("Data");

        nlohmann::json wrapped;
        wrapped["id"] = item->getId();
        wrapped["tag"] = tag;
        wrapped["type"] = item->getTypeName();

        nlohmann::json userData = item->toJson();
        if (!userData.is_object()) {
            wrapped["data"] = { {"value", userData} };
        } else {
            wrapped["data"] = userData;
        }

        std::ostringstream oss;
        oss << wrapped;
        dataElement->SetText(oss.str().c_str());
        itemElement->InsertEndChild(dataElement);
        root->InsertEndChild(itemElement);

        std::cout << Logger::getColorCode(LogColor::YELLOW) << wrapped.dump(4) << "\n" + Logger::getColorCode(LogColor::RESET) + "\n";
        Logger::log(LogLevel::INFO, "Successfully added item with tag '" + tag + "' to XML structure.");
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    std::string xmlContent = printer.CStr();

    if (!AtomicFileWriter::writeAtomically(filename, xmlContent)) {
        Logger::log(LogLevel::ERR, "Failed to write XML atomically to file: " + filename);
        return false;
    }

    Logger::log(LogLevel::INFO, "XML export completed successfully to file: " + filename);
    return true;
}

void ItemManager::asyncExportToFile_XML(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            bool success = this->exportToFile_XML(filename);
            if (!success) {
                Logger::log(LogLevel::ERR, "asyncExportToFile_XML failed for file: " + filename);
            } else {
                Logger::log(LogLevel::INFO, "asyncExportToFile_XML completed successfully for file: " + filename);
            }
        } catch (const std::exception& e) {
            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Exception in asyncExportToFile_XML: " + std::string(e.what()) + Logger::getColorCode(LogColor::RESET));
        }
    }).detach();
}

bool ItemManager::importFromFile_XML(const std::string& filename) {
    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError result = doc.LoadFile(filename.c_str());
    if (result != tinyxml2::XML_SUCCESS) {
        Logger::log(LogLevel::ERR, "Failed to read XML file '" + filename + "' — error code: " + std::to_string(result));
        return false;
    }

    auto* root = doc.FirstChildElement("SmartStore");
    if (!root) {
        Logger::log(LogLevel::ERR, "Missing <SmartStore> root in XML file '" + filename + "'");
        return false;
    }

    
    int loadedCount = 0;
    for (auto* itemElement = root->FirstChildElement("Item"); itemElement; itemElement = itemElement->NextSiblingElement("Item")) {
        auto* tagElement = itemElement->FirstChildElement("Tag");
        auto* typeElement = itemElement->FirstChildElement("Type");
        auto* dataElement = itemElement->FirstChildElement("Data");
        auto* versionElement = itemElement->FirstChildElement("Version");

        const char* tagTextPtr  = tagElement  ? tagElement->GetText()  : nullptr;
        const char* typeTextPtr = typeElement ? typeElement->GetText() : nullptr;
        const char* dataTextPtr = dataElement ? dataElement->GetText() : nullptr;
        const char* versionTextPtr = versionElement ? versionElement->GetText() : nullptr;

        if (!tagTextPtr || !typeTextPtr || !dataTextPtr) {
            Logger::log(LogLevel::WARNING, "Skipping <Item> with missing tag, type, or data.");
            continue;
        }

        std::string tag       = tagTextPtr;
        std::string typeName  = typeTextPtr;
        std::string dataText  = dataTextPtr;
        int version = versionTextPtr ? std::atoi(versionTextPtr) : 1;

        if (tag.empty() || typeName.empty() || dataText.empty()) {
            Logger::log(LogLevel::WARNING, "Skipping <Item> with empty fields: tag='" + tag + "', type='" + demangleType(typeName) + "', data='" + dataText + "'");
            continue;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(dataText);
            if (!j.contains("id") && !tag.empty()) j["id"] = tag;
            if (!j.contains("tag")) j["tag"] = tag;
            if (!j.contains("type")) j["type"] = typeName;
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "JSON parse error in item '" + tag + "': " + e.what());
            continue;
        }

        Logger::log(LogLevel::INFO, "Found item in XML: tag='" + tag + "', type='" + demangleType(typeName) + "'");
        std::cout << Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        Logger::log(LogLevel::DEBUG, "Upgrading item '" + tag + "' of type '" + demangleType(typeName) + "' from version: " + std::to_string(version));
        json upgraded = migrationRegistry.upgradeToLatest(typeName, version, j);

        auto it = deserializers.find(typeName);
        if (it == deserializers.end()) {
            Logger::log(LogLevel::WARNING, "No deserializer registered for type '" + demangleType(typeName) + "' — skipping item with tag '" + tag + "'");
            continue;
        }

        try {
            auto item = it->second(upgraded, tag);
            if (item) {
                items[tag] = item;
                Logger::log(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from XML.");
                loadedCount++;
            } else {
                Logger::log(LogLevel::ERR, "Deserializer returned null for tag '" + tag + "' — skipping.");
            }
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + e.what());
        }
    }

    Logger::log(LogLevel::INFO, "XML import completed with " + std::to_string(loadedCount) + " items loaded from file: " + filename);
    return true;
}

void ItemManager::asyncImportFromFile_XML(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            bool success = this->importFromFile_XML(filename);
            if (!success) {
                Logger::log(LogLevel::ERR, "asyncImportFromFile_XML failed for file: " + filename);
            } else {
                Logger::log(LogLevel::INFO, "asyncImportFromFile_XML completed successfully for file: " + filename);
            }
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::ERR, "Exception in asyncImportFromFile_XML: " + std::string(ex.what()));
        } catch (...) {
            Logger::log(LogLevel::ERR, "Unknown exception in asyncImportFromFile_XML");
        }
    }).detach();  // Run the thread in background
}

std::optional<std::shared_ptr<BaseItem>> ItemManager::importSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag) {
    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        Logger::log(LogLevel::ERR, "Failed to load XML file: " + filename);
        return std::nullopt;
    }

    auto* root = doc.FirstChildElement("SmartStore");
    if (!root) {
        Logger::log(LogLevel::ERR, "Missing <SmartStore> root element in XML file: " + filename);
        return std::nullopt;
    }

    for (auto* itemElement = root->FirstChildElement("Item"); itemElement; itemElement = itemElement->NextSiblingElement("Item")) {
        auto* tagElement  = itemElement->FirstChildElement("Tag");
        auto* typeElement = itemElement->FirstChildElement("Type");
        auto* dataElement = itemElement->FirstChildElement("Data");

        const char* tagText  = tagElement  ? tagElement->GetText()  : nullptr;
        const char* typeText = typeElement ? typeElement->GetText() : nullptr;
        const char* dataText = dataElement ? dataElement->GetText() : nullptr;

        if (!tagText || !typeText || !dataText)
            continue;

        if (std::string(tagText) != tag || std::string(typeText) != type)
            continue;

        try {
            nlohmann::json j = nlohmann::json::parse(dataText);

            if (!j.contains("id") && tagText) j["id"] = tagText;
            if (!j.contains("tag")) j["tag"] = tagText;
            if (!j.contains("type")) j["type"] = typeText;

            Logger::log(LogLevel::INFO, "Found matching item in XML: tag='" + std::string(tagText) + "', type='" + demangleType(std::string(typeText)) + "'");
            std::cout << Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

            json upgraded = migrationRegistry.upgradeToLatest(type, 1, j); // Assumes version 1 if none is specified.
            Logger::log(LogLevel::DEBUG, "Upgrading item '" + std::string(tagText) + "' of type '" + demangleType(std::string(typeText)) + "' to latest version.");

            auto it = deserializers.find(type);
            if (it == deserializers.end()) {
                Logger::log(LogLevel::ERR, "No deserializer registered for type '" + demangleType(type) + "' — cannot import item with tag '" + tag + "'");
                return std::nullopt;
            }

            Logger::log(LogLevel::INFO, "Attempting to import item with tag '" + tag + "' from XML.");
            auto item = it->second(upgraded, tag);

            undoHistory.push_back(cloneCurrentState());
            redoQueue = {};
            items[tag] = item;

            return item;
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Failed to parse JSON data for tag '" + tag + "': " + e.what());
            return std::nullopt;
        }
    }

    Logger::log(LogLevel::INFO, "No matching item found for tag '" + tag + "' and type '" + demangleType(type) + "' in XML file: " + filename);
    return std::nullopt;
}

void ItemManager::asyncImportSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag) {
    std::thread([this, filename, type, tag]() {
        try {
            auto result = this->importSingleObject_XML(filename, type, tag);
            if (result.has_value() && result.value()) {
                std::lock_guard lock(mutex_);  // Ensure thread-safe map update
                items[tag] = result.value();
                Logger::log(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully from XML file: " + filename);
            } else {
                Logger::log(LogLevel::WARNING, "Async import failed or returned null for tag '" + tag + "' from XML file: " + filename);
            }
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::ERR, "Exception in asyncImportSingleObject_XML: " + std::string(ex.what()));
        } catch (...) {
            Logger::log(LogLevel::ERR, "Unknown exception in asyncImportSingleObject_XML");
        }
    }).detach();
}

bool ItemManager::exportToFile_CSV(const std::string& filename) const {
    if (items.empty()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: No items found for export to file for CSV'" + filename + "'." + Logger::getColorCode(LogColor::RESET));
    }

    std::ostringstream oss;
    oss << "id,tag,type,data\n"; // CSV header

    for (const auto& [tag, item] : items) {
        if (!item) {
            Logger::log(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.");
            continue;
        }

        const std::string& id = item->getId();
        const std::string& type = item->getTypeName();

        std::string dataStr;
        try {
            json j = item->toJson();
            dataStr = j.is_string() ? j.get<std::string>() : j.dump();
        } catch (const std::exception& e) {
            Logger::log(LogLevel::WARNING, "Failed to serialize item '" + tag + "': " + e.what());
            dataStr = "{}";
        }

        // Debug preview in terminal
        Logger::log(LogLevel::INFO, "Exporting item: id='" + id + "', tag='" + tag + "', type='" + demangleType(type) + "'");
        std::cout << Logger::getColorCode(LogColor::YELLOW) + "{\n"
                  << "  \"id\": \"" << id << "\",\n"
                  << "  \"tag\": \"" << tag << "\",\n"
                  << "  \"type\": \"" << type << "\",\n"
                  << "  \"data\": " << dataStr << "\n"
                  << "}\n" + Logger::getColorCode(LogColor::RESET);

        auto escapeCSV = [](const std::string& field) {
            std::string escaped = "\"";
            for (char c : field) {
                escaped += (c == '"') ? "\"\"" : std::string(1, c);
            }
            escaped += "\"";
            return escaped;
        };

        oss << escapeCSV(id) << ","
            << escapeCSV(tag) << ","
            << escapeCSV(type) << ","
            << escapeCSV(dataStr) << "\n";

        std::cout << Logger::getColorCode(LogColor::CYAN) + ":::| Item '" << tag << "' written to CSV.\n" + Logger::getColorCode(LogColor::RESET);
    }

    if (!AtomicFileWriter::writeAtomically(filename, oss.str())) {
        Logger::log(LogLevel::ERR, "Failed to write CSV atomically to file: " + filename);
        return false;
    }

    Logger::log(LogLevel::INFO, "CSV export completed successfully to file: " + filename);
    return true;
}

void ItemManager::asyncExportToFile_CSV(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            if (this->exportToFile_CSV(filename)) {
                Logger::log(LogLevel::INFO, "asyncExportToFile_CSV completed successfully for file: " + filename);
            } else {
                Logger::log(LogLevel::ERR, "asyncExportToFile_CSV failed for file: " + filename);
            }
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::ERR, "Exception in asyncExportToFile_CSV: " + std::string(ex.what()));
        } catch (...) {
            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Unknown error in asyncExportToFile_CSV" + Logger::getColorCode(LogColor::RESET));
        }
    }).detach();
}

bool ItemManager::importFromFile_CSV(const std::string& filename) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Cannot open CSV file: " + filename + Logger::getColorCode(LogColor::RESET));
    }

    std::string header;
    std::getline(file, header); // Skip header row
    if (header != "id,tag,type,data") {
        Logger::log(LogLevel::ERR, "Unexpected CSV header format in file: " + filename);
        return false;
    }

    undoHistory.push_back(cloneCurrentState());
    redoQueue = {};
    items.clear();

    int loadedCount = 0;
    std::string line;

    while (std::getline(file, line)) {
        std::vector<std::string> fields;
        std::string field;
        bool inQuotes = false;

        for (size_t i = 0; i < line.size(); ++i) {
            char c = line[i];
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    field += '"';
                    ++i;
                } else {
                    inQuotes = !inQuotes;
                }
            } else if (c == ',' && !inQuotes) {
                fields.push_back(field);
                field.clear();
            } else {
                field += c;
            }
        }
        fields.push_back(field); // Final field

        if (fields.size() != 4) {
            Logger::log(LogLevel::WARNING, "Malformed CSV row: '" + line + "' — skipping.");
            continue;
        }

        auto unquote = [](std::string s) {
            if (!s.empty() && s.front() == '"' && s.back() == '"') {
                s = s.substr(1, s.size() - 2);
            }
            return s;
        };

        std::string id      = unquote(fields[0]);
        std::string tag     = unquote(fields[1]);
        std::string type    = unquote(fields[2]);
        std::string dataStr = unquote(fields[3]);

        json j;
        try {
            json parsedData;
            try {
                parsedData = json::parse(dataStr);
            } catch (...) {
                parsedData = dataStr;
            }

            int version = 1;
            if (parsedData.is_object() && parsedData.contains("version")) {
                version = parsedData["version"];
            }

            json upgradedData = migrationRegistry.upgradeToLatest(type, version, parsedData);
            Logger::log(LogLevel::DEBUG, "Upgrading item '" + tag + "' of type '" + demangleType(type) + "' from version: " + std::to_string(version));

            j["data"] = upgradedData;
            j["id"]   = id;
            j["tag"]  = tag;
            j["type"] = type;
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Failed to construct JSON for tag '" + tag + "': " + e.what());
            continue;
        }

        Logger::log(LogLevel::INFO, "Processing CSV row: id='" + id + "', tag='" + tag + "', type='" + type + "'");
        std::cout << "\n" + Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        auto it = deserializers.find(type);
        if (it == deserializers.end()) {
            Logger::log(LogLevel::WARNING, "No deserializer registered for type '" + demangleType(type) + "' — skipping item with tag '" + tag + "'");
            continue;
        }

        try {
            auto item = it->second(j, tag);
            if (item) {
                items[tag] = item;
                loadedCount++;
                Logger::log(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from CSV.");
            } else {
                Logger::log(LogLevel::WARNING, "Deserializer returned null for tag '" + tag + "' — skipping.");
            }
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + e.what());
        }
    }

    Logger::log(LogLevel::INFO, "CSV import completed with " + std::to_string(loadedCount) + " items loaded from file: " + filename);
    return true;
}

void ItemManager::asyncImportFromFile_CSV(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            if (this->importFromFile_CSV(filename)) {
                Logger::log(LogLevel::INFO, "asyncImportFromFile_CSV completed successfully for file: " + filename);
            } else {
                Logger::log(LogLevel::ERR, "asyncImportFromFile_CSV failed for file: " + filename);
            }
        } catch (const std::exception& ex) {
            Logger::log(LogLevel::ERR, "Exception in asyncImportFromFile_CSV: " + std::string(ex.what()));
        } catch (...) {
            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| WARNING: Unknown error in asyncImportFromFile_CSV" + Logger::getColorCode(LogColor::RESET));
        }
    }).detach();
}

std::shared_ptr<BaseItem> ItemManager::importSingleObject_CSV(const std::string& filename, const std::string& type, const std::string& tag) {
    std::ifstream file(filename);
    if (!file.is_open()) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + "\n:::| ERROR: Cannot open CSV file: " + filename + "\n" + Logger::getColorCode(LogColor::RESET));
    }

    std::string header;
    std::getline(file, header);  // Skip header
    if (header != "id,tag,type,data") {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::| ERROR: Unexpected CSV header format in file: " + filename + Logger::getColorCode(LogColor::RESET));
    }

    auto unquote = [](std::string s) -> std::string {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') {
            s = s.substr(1, s.size() - 2);
            size_t pos = 0;
            while ((pos = s.find("\"\"", pos)) != std::string::npos) {
                s.replace(pos, 2, "\"");
                pos += 1;
            }
        }
        return s;
    };

    std::string line;
    while (std::getline(file, line)) {
        std::stringstream ss(line);
        std::string idField, tagField, typeField, dataField;

        std::getline(ss, idField, ',');
        std::getline(ss, tagField, ',');
        std::getline(ss, typeField, ',');
        std::getline(ss, dataField);

        const std::string id      = unquote(idField);
        const std::string tagIn   = unquote(tagField);
        const std::string typeIn  = unquote(typeField);
        const std::string dataStr = unquote(dataField);

        if (tagIn != tag || typeIn != type)
            continue;

        json rawData;
        try {
            rawData = json::parse(dataStr);
        } catch (const std::exception& e) {
            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + "\n:::| ERROR: Failed to parse JSON data for tag '" + tagIn + "': " + e.what() + "\n" + Logger::getColorCode(LogColor::RESET));
        }

        int version = 1;
        if (rawData.is_object() && rawData.contains("version")) {
            version = rawData["version"];
        }

        json upgradedData = migrationRegistry.upgradeToLatest(typeIn, version, rawData);

        json wrapper;
        wrapper["id"]   = id;
        wrapper["tag"]  = tagIn;
        wrapper["type"] = typeIn;
        wrapper["data"] = upgradedData;

        std::cout << Logger::getColorCode(LogColor::CYAN) + "\n>>> Matched CSV row: tag='" << tag << "', type='" << demangleType(type) << "'\n" + Logger::getColorCode(LogColor::YELLOW);
        std::cout << Logger::getColorCode(LogColor::YELLOW) << wrapper.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        auto it = deserializers.find(typeIn);
        if (it == deserializers.end()) {
            Logger::log(LogLevel::ERR, "No deserializer registered for type '" + demangleType(typeIn) + "' — cannot import item with tag '" + demangleType(tagIn) + "'");
            return nullptr;
        }

        try {
            auto item = it->second(wrapper, tagIn);
            Logger::log(LogLevel::INFO, "Attempting to import item with tag '" + demangleType(tagIn) + "' from CSV.");

            // Undo/Redo support (only if it is actually imported)
            undoHistory.push_back(cloneCurrentState());
            redoQueue = {};
            items[tagIn] = item;

            return item;
        } catch (const std::exception& e) {
            throw std::runtime_error(Logger::getColorCode(LogColor::RED) + "\n:::| ERROR: Failed to deserialize item with tag '" + tagIn + "': " + e.what() + "\n" + Logger::getColorCode(LogColor::RESET));
        }
    }

    throw std::runtime_error(Logger::getColorCode(LogColor::MAGENTA) + "\n:::| INFO: No matching item found for tag '" + tag + "' and type '" + demangleType(type) + "' in CSV file: " + filename + "\n" + Logger::getColorCode(LogColor::RESET));
}

void ItemManager::asyncImportSingleObject_CSV(const std::string& filename, const std::string& type,const std::string& tag){
    std::thread([this, filename, type, tag]() {
        try {
            auto item = this->importSingleObject_CSV(filename, type, tag);
            if (item) {
                std::lock_guard<std::mutex> lock(mutex_); // protect shared state
                items[tag] = item;
                Logger::log(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully from CSV file: " + filename);
            } else {
                Logger::log(LogLevel::WARNING, "Async import failed or returned null for tag '" + tag + "' from CSV file: " + filename);
            }
        } catch (const std::exception& e) {
            Logger::log(LogLevel::ERR, "Exception in asyncImportSingleObject_CSV: " + std::string(e.what()));
        } catch (...) {
            Logger::log(LogLevel::ERR, "Unknown error in asyncImportSingleObject_CSV");
        }
    }).detach();
}

void ItemManager::listRegisteredTypes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    std::cout << Logger::getColorCode(LogColor::CYAN) +":::| Registered Types:\n" + Logger::getColorCode(LogColor::RESET);
    for (const auto& entry : registeredTypes) {
        std::cout << " - " << demangleType(entry.first) << std::endl;
    }
}

void ItemManager::filterByTag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::cout <<Logger::getColorCode(LogColor::CYAN) + "\n :::::: Items filtered by tag: " << tag << " ::::::\n" + Logger::getColorCode(LogColor::RESET);
    
    bool found = false;
    for (const auto& [key, item] : items) {
        if (key == tag) {
            item->display();  // Assuming 'display' is a method of BaseItem or its derived classes
            found = true;
        }
    }
    
    if (!found) {
        Logger::log(LogLevel::INFO, "No items found with tag: " + tag);
    }
}

void ItemManager::sortItemsByTag() const {
    std::lock_guard<std::mutex> lock(mutex_);
   
    if (items.empty()) {
        Logger::log(LogLevel::INFO, "No items to sort by tag.");
        return;
    }

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::::: Items Sorted By Tag ::::::\n" + Logger::getColorCode(LogColor::RESET);

    // Create a temporary std::map which automatically sorts by key (tag)
    std::map<std::string, const std::shared_ptr<BaseItem>&> sortedItems;
    for (const auto& [tag, item] : items) {
        sortedItems.emplace(tag, item);
    }

    // Display items in sorted order
    for (const auto& [tag, item] : sortedItems) {
        std::cout << Logger::getColorCode(LogColor::CYAN) + "[ " + Logger::getColorCode(LogColor::RESET) << tag <<  Logger::getColorCode(LogColor::CYAN) + " ]" + Logger::getColorCode(LogColor::RESET);
        item->display();
    }
}

void ItemManager::displayAllClasses() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::unordered_map<std::string, int> classCounts;

    if(items.empty()) {
        Logger::log(LogLevel::INFO, "No items available to display classes.");
        return;
    }

    for (const auto& [tag, item] : items) {
        classCounts[item->getTypeName()]++;
    }

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::::: Unique Item Classes ::::::\n" + Logger::getColorCode(LogColor::RESET);

    for (const auto& [type, count] : classCounts) {
        std::cout << Logger::getColorCode(LogColor::BLUE) + ":::| " +  Logger::getColorCode(LogColor::RESET)  <<  demangleType(type) <<  Logger::getColorCode(LogColor::BLUE) + "   X" + Logger::getColorCode(LogColor::RESET) << count << '\n';
    }
}

const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& ItemManager::getItemMapStore() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return items;
}





