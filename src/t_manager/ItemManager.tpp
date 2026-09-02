
#include "author/author.hpp"
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

std::string ItemManager::demangleType(const std::string& mangledName) const {
#if defined(__GNUC__) || defined(__clang__)
    int status;
    char* demangled = abi::__cxa_demangle(mangledName.c_str(), nullptr, nullptr, &status);

    std::string result;
    if (status == 0 && demangled) {
        result = std::string(demangled);  // Store safely in std::string
        free(demangled);                  // Ensure valid memory cleanup
        demangled = nullptr;              // Prevent accidental reuse
    } else {
        result = mangledName;
    }

    return result;
#elif defined(_MSC_VER)
    return mangledName;
#else
    return "Unknown compiler";
#endif
}





// ::::: MAIN API USER CALLS OR PUBLIC FUNCTIONS ::::::
// ****************************************************

void ItemManager::showSignature() {
    Author::getSignature();
}

void ItemManager::setUseGPU_On() {
    GPUManager::setUseGPU(true);
}

void ItemManager::setUseGPU_Off() {
    GPUManager::setUseGPU(false);
}




template<typename T>
void ItemManager::StateManager::addItem(std::shared_ptr<T> obj, const std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  

    if (tag.empty()) {
        std::string errorMsg = "Tag cannot be empty for item of type: "
                             + parent.demangleType(typeid(T).name());
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(errorMsg)));
    }

    if (!obj) {
        std::string errorMsg = "Cannot add null object with tag: " + tag
                             + " and type: " + parent.demangleType(typeid(T).name());
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(errorMsg)));
    }

    // Check if an item with the same tag already exists
    if (parent.items.find(tag) != parent.items.end()) {
        std::string errorMsg = "Item with tag '" + tag + "' already exists. Cannot add another item of type: "
                             + parent.demangleType(typeid(T).name());
        LOG_CONTEXT(LogLevel::ERR, errorMsg, std::make_exception_ptr(std::runtime_error(errorMsg)));
    }

    std::cout << Logger::getColorCode(LogColor::GREEN)
              << "\nAn item added with tag: " << tag
              << Logger::getColorCode(LogColor::RESET) << std::endl;

    parent.saveState();                                   
    parent.undoHistory.push_back(parent.cloneCurrentState()); 
    parent.redoQueue = {};                                

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    std::cout << "Using C++20 Concepts for Type Registration.\n";
#else
    std::cout << "Using SFINAE-based Registration (C++17 or older).\n";
#endif

    // Automatic Type Registration
    parent.migrationRegistry.registerVersion("User", 3);
    parent.migrationRegistry.registerMigration("User", 1, [](const json& j) {
        json upgraded = j;
        upgraded["age"] = 0;
        return upgraded;
    });
    parent.migrationRegistry.registerMigration("User", 2, [](const json& j) {
        json upgraded = j;
        upgraded["email"] = "unknown@example.com";
        return upgraded;
    });

    parent.registerType<T>(); 

    parent.items[tag] = std::make_shared<ItemWrapper<T>>(std::move(obj), tag);

    for (const auto& [key, value] : parent.items) {
        LOG_CONTEXT(LogLevel::DEBUG,
                    "Item with tag '" + key + "' registered with type: "
                    + parent.demangleType(value->getTypeName()), {});
    }

    LOG_CONTEXT(LogLevel::INFO,
                "Item with tag '" + tag + "' added successfully. Type: "
                + parent.demangleType(ItemManager::getCompilerTypeName<T>()), {});
}

bool ItemManager::StateManager::hasItem(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    if (tag.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "Empty tag provided for hasItem check", false);
        return false;
    }
    if(parent.items.find(tag) != parent.items.end()){
        LOG_CONTEXT(LogLevel::DEBUG, "Item with tag '" + tag + "' exists in ItemManager", true);
        return true;
    } else {
        LOG_CONTEXT(LogLevel::DEBUG, "Item with tag '" + tag + "' does not exist in ItemManager", false);
        return false;
    }
}

template<typename T>
std::optional<T> ItemManager::StateManager::getItem(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getData();
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "", std::make_exception_ptr(std::runtime_error(
                    "Type mismatch for item with tag '" + tag + "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                                                              ", Actual type: " + parent.demangleType(it->second->getTypeName()))));
        }
    } else {
        LOG_CONTEXT(LogLevel::WARNING, "No item found with tag '" + tag + "'", ErrorCode::ITEM_NOT_FOUND);
    }
    return std::nullopt;
}

template<typename T>
T& ItemManager::StateManager::getItemRaw(const std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getMutableData();
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Type mismatch for item with tag '" + tag + "'. Requested type: "
                      + parent.demangleType(typeid(T).name()) + ", Actual type: " + parent.demangleType(it->second->getTypeName()), {});
            throw std::runtime_error("\n:::| Type mismatch for item with tag '" + tag + "'.\n");
        }
    } else {
        LOG_CONTEXT(LogLevel::WARNING, "Item with tag '" + tag + "' not found.", {});
        throw std::runtime_error("\n:::| Item with tag '" + tag + "' not found.\n");
    }
}

template<typename T>
const T& ItemManager::StateManager::getItemRaw(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<const ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getData();
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Type mismatch for item with tag '" + tag + "'. Requested type: " 
                        + parent.demangleType(typeid(T).name()) + ", Actual type: " + parent.demangleType(it->second->getTypeName()), {});
            throw std::runtime_error("\n:::| Please check your item type.\n");
        }
    } else {
        LOG_CONTEXT(LogLevel::WARNING, "Item with tag '" + tag + "' not found.", {});
        throw std::runtime_error("\n:::| Please check your tag name.\n");
    }
}

template<typename T>
bool ItemManager::StateManager::modifyItem(const std::function<void(T&)>& modifier, const std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            parent.undoHistory.push_back(parent.cloneCurrentState());
            parent.redoQueue = {};
            modifier(wrapper->getMutableData());
            LOG_CONTEXT(LogLevel::DEBUG, "Modified item with tag '" + tag + "' of type: " + parent.demangleType(typeid(T).name()), {});
            return true;
        }
    }
    LOG_CONTEXT(LogLevel::WARNING, "Item with tag '" + tag + 
                            "' not found or type mismatch. Requested type: " + parent.demangleType(typeid(T).name()), false);
                            return false;
}

void ItemManager::StateManager::undo() {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    if (!parent.undoHistory.empty()) {
        auto current = parent.cloneCurrentState();            // Save current state
        auto prev = std::move(parent.undoHistory.back());     // Last undo state
        parent.undoHistory.pop_back();

        parent.redoQueue.push(std::move(current));            // Push current into redo
        parent.items = std::move(prev);                       // Restore previous state

        LOG_CONTEXT(LogLevel::DEBUG, "Undo successful. Restored to previous state.", {});
    } else {
        LOG_CONTEXT(LogLevel::INFO, "Nothing to undo.", {});
    }
}

void ItemManager::StateManager::redo() {
    std::lock_guard<std::mutex> lock(parent.mutex_);
  
    if (!parent.redoQueue.empty()) {
        parent.undoHistory.push_back(parent.cloneCurrentState());   // Save current state
        parent.items = std::move(parent.redoQueue.front());         // Restore redo state
        parent.redoQueue.pop();

        LOG_CONTEXT(LogLevel::DEBUG, "Redo successful. Restored to next state.", {});
    } else {
        LOG_CONTEXT(LogLevel::INFO, "Nothing to redo.", {});
    }
}

void ItemManager::StateManager::removeByTag(const std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    if (tag.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "Cannot remove item with empty tag.", ErrorCode::ITEM_NOT_FOUND);
    }

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        parent.undoHistory.push_back(parent.cloneCurrentState());

        std::queue<State> empty;
        std::swap(parent.redoQueue, empty);

        std::string typeName = it->second->getTypeName();
        std::string id = it->second->getId(); // Extract ID before erasing

        if (--parent.typeUsage[typeName] == 0) {
            parent.typeUsage.erase(typeName);
            parent.registeredTypes.erase(typeName);
            parent.deserializers.erase(typeName);
            parent.schemaRegistry.erase(typeName);  //  Clean up schema too
            LOG_CONTEXT(LogLevel::DEBUG, "Removed type: " + parent.demangleType(typeName) + " from registry", {});
        }

        parent.items.erase(it);
        parent.idMap.erase(id); // Now erase from idMap as well

        LOG_CONTEXT(LogLevel::DEBUG, "Removed item with tag '" + tag + "' and id '" + id + "'", {});
    } else {
        LOG_CONTEXT(LogLevel::WARNING, "No item found with tag '" + tag + "' to be removed. -Code: "
                                                         + std::to_string(ErrorCode::ITEM_NOT_FOUND), {});
    }
}

void ItemManager::StateManager::printIds() {
    std::cout << "\033[1;31m::: Debug: ID:  id  | Item Tag \033[0m\n" << std::endl;
    for (const auto& [id, item] : parent.idMap) {
        std::cout << "\033[1;31m::: Debug: ID: " << id << " | Item Tag: " << item->getTag() << "\033[0m\n";
    }
}

void ItemManager::StateManager::displayByTag(const std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        LOG_CONTEXT(LogLevel::DISPLAY, "Displaying item with tag '" + tag + "'", {});
        it->second->display();
        return;
    }

    LOG_CONTEXT(LogLevel::WARNING, "Item with tag '" + tag + "' not found.", ErrorCode::ITEM_NOT_FOUND);
}

void ItemManager::StateManager::displayAll() const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    LOG_CONTEXT(LogLevel::DISPLAY, ":::::: Types Stored ::::::", {});
    if (!parent.items.empty()) {
        for (const auto& [_, item] : parent.items) item->display();
    }else{
        LOG_CONTEXT(LogLevel::INFO, "No items found to display. ", ErrorCode::ITEM_NOT_FOUND);
    }
}

void ItemManager::StateManager::displayAllClasses() const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    std::unordered_map<std::string, int> classCounts;

    if(parent.items.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "No items available to display classes.", {});
        return;
    }

    for (const auto& [tag, item] : parent.items) {
        classCounts[item->getTypeName()]++;
    }

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::::: Unique Item Classes ::::::\n" + Logger::getColorCode(LogColor::RESET);

    for (const auto& [type, count] : classCounts) {
        std::cout << Logger::getColorCode(LogColor::BLUE) + ":::| " +  Logger::getColorCode(LogColor::RESET)  
                  <<  parent.demangleType(type) <<  Logger::getColorCode(LogColor::BLUE) + "   X" + Logger::getColorCode(LogColor::RESET) << count << '\n';
    }
}

void ItemManager::StateManager::displayRegisteredDeserializers() {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    
    std::cout << Logger::getColorCode(LogColor::MAGENTA) << "\n:::| Registered Deserializers in ItemManager |:::\n" << Logger::getColorCode(LogColor::RESET);
    
    if (parent.deserializers.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "No deserializers registered", {});
        return;
    }

    for (const auto& entry : parent.deserializers) {
        LOG_CONTEXT(LogLevel::DEBUG, "Type: " + parent.demangleType(entry.first) + " -> Deserialization Function Exists", {});
    }

    std::cout << "\n::::::::::::::::::::::::::::::::::::::::::::::::\n";

    std::cout << Logger::getColorCode(LogColor::MAGENTA) << "\n:::| Registered types in ItemManager |:::\n" << Logger::getColorCode(LogColor::RESET);
    if (parent.registeredTypes.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "No types registered", {});
        return;
    }
    for (const auto& entry : parent.registeredTypes) {
        LOG_CONTEXT(LogLevel::DEBUG, "Type: " + parent.demangleType(entry.first) + " -> Type Index: " + parent.demangleType(entry.second.name()), {});
    }
    std::cout << "\n::::::::::::::::::::::::::::::::::::::::::::::::\n";
}

void ItemManager::StateManager::listRegisteredTypes() const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    
    std::cout << Logger::getColorCode(LogColor::CYAN) +":::| Registered Types:\n" + Logger::getColorCode(LogColor::RESET);
    for (const auto& entry : parent.registeredTypes) {
        std::cout << " - " << parent.demangleType(entry.first) << std::endl;
    }
}

void ItemManager::StateManager::filterByTag(const std::vector<std::string>& tags) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    std::cout << Logger::getColorCode(LogColor::CYAN)
              << "\n ::::::| Items filtered by tags |::::::\n"
              << Logger::getColorCode(LogColor::RESET);

    if (tags.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "No tags provided for filtering.", {});
        return;
    }

    for (const auto& tag : tags) {
        auto it = parent.items.find(tag);
        if (it != parent.items.end()) {
            it->second->display();  // Found: display the item
        } else {
            LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "'.", {});  //  Not found: log it
        }
    }
}

void ItemManager::StateManager::sortItemsByTag() const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
   
    if (parent.items.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "No items to sort by tag.", {});
        return;
    }

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::::: Items Sorted By Tag ::::::\n" + Logger::getColorCode(LogColor::RESET);

    // Create a temporary std::map which automatically sorts by key (tag)
    std::map<std::string, const std::shared_ptr<BaseItem>&> sortedItems;
    for (const auto& [tag, item] : parent.items) {
        sortedItems.emplace(tag, item);
    }

    // Display items in sorted order
    for (const auto& [tag, item] : sortedItems) {
        std::cout << Logger::getColorCode(LogColor::CYAN) + "[ " + Logger::getColorCode(LogColor::RESET) << tag <<  Logger::getColorCode(LogColor::CYAN) + " ]" + Logger::getColorCode(LogColor::RESET);
        item->display();
    }
}

const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& ItemManager::StateManager::getItemMapStore() const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    return parent.items;
}










void ItemManager::IO_ToFileManager::exportToFile_Json(const std::string& filename) const {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "Cannot export to empty filename.", ErrorCode::ITEM_NOT_FOUND);
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting JSON export to file: " + filename, {});
    
    if (parent.items.empty()) {
            LOG_CONTEXT(LogLevel::WARNING, "No items found to export.", ErrorCode::ITEM_NOT_FOUND);
    }

    nlohmann::json jArray = nlohmann::json::array();

    for (const auto& [tag, item] : parent.items) {
        if (!item) {
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " â€” skipping.", {});
            continue;
        }


        nlohmann::json entry;
        entry["id"] = item->getId();
        entry["tag"] = tag;
        entry["type"] = item->getTypeName();

        try {
            entry["data"] = item->serialize();
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "Serialization failed for item '" + tag + "': " + e.what(), {});
            continue;
        }

        auto schema = parent.getSchemaForType(item->getTypeName());
        if (!schema.is_null()) {
            entry["schema"] = schema;
            LOG_CONTEXT(LogLevel::DEBUG, "Attached schema for type: " + parent.demangleType(item->getTypeName()), {});
        }

        jArray.push_back(entry);

        LOG_CONTEXT(LogLevel::INFO, "Exporting item with tag: " + tag + " of type: " + parent.demangleType(item->getTypeName()), {});
        std::cout << Logger::getColorCode(LogColor::CYAN)
                  << entry.dump(4) 
                  << Logger::getColorCode(LogColor::RESET) + "\n";

        LOG_CONTEXT(LogLevel::INFO, "Added entry for tag: " + tag, {});
    }

    std::string jsonContent = jArray.dump(4);

    if (!AtomicFileWriter::writeAtomically(filename, jsonContent)) {
            LOG_CONTEXT(LogLevel::ERR, "Failed atomic write to file: " + filename, ErrorCode::FILE_LOAD_FAILED);
    }

    LOG_CONTEXT(LogLevel::INFO, "Exported " + std::to_string(jArray.size()) + " items to file (atomically): " + filename, {});
}

void ItemManager::IO_ToFileManager::asyncExportToFile_Json(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            this->exportToFile_Json(filename);  // Thread-safe at its core
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "asyncExportToFile_Json error: " + std::string(e.what()))));
        }
    }).detach();  // Fire-and-forget
}

void ItemManager::IO_ToFileManager::importFromFile_Json(const std::string& filename) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot import from empty filename.", ErrorCode::ITEM_NOT_FOUND);
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting JSON import from file: " + filename, {});
    
    std::ifstream in(filename);
    if (!in) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot open file for reading: " + filename, ErrorCode::FILE_LOAD_FAILED);
    }

    json parsedJson;
    in >> parsedJson;

    std::cout << Logger::getColorCode(LogColor::CYAN) + "\n:::| Loaded JSON content from file:\n" 
                                        << Logger::getColorCode(LogColor::RESET) << parsedJson.dump(2) << "\n";
    LOG_CONTEXT(LogLevel::DEBUG, "JSON file loaded successfully: " + filename, {});

    if (parsedJson.is_array()) {
        LOG_CONTEXT(LogLevel::DEBUG, "Processing JSON array format.", {});
    } else if (parsedJson.contains("items") && parsedJson["items"].is_array()) {
        parsedJson = parsedJson["items"];
        LOG_CONTEXT(LogLevel::DEBUG, "Processing JSON with 'items' key.", {});
    } else {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Invalid JSON format: " + filename + " Expected an array or 'items' key.")));
    }

    parent.undoHistory.push_back(parent.cloneCurrentState());
    parent.redoQueue = {};
    parent.items.clear();

    int importCount = 0;

    for (const auto& entry : parsedJson) {
        if (!entry.contains("tag") || !entry.contains("type") || !entry.contains("data")) {
            LOG_CONTEXT(LogLevel::WARNING, "Skipping entry due to missing keys: 'tag', 'type', or 'data'.", {});
            continue;
        }

        std::string tag = entry["tag"].get<std::string>();
        std::string typeName = entry["type"].get<std::string>();
        int version = entry.value("version", 1);
        json rawData = entry["data"];

        LOG_CONTEXT(LogLevel::INFO, "Importing item: '" + tag + "' of type: '" + parent.demangleType(typeName) + "'", {});

        if (!rawData.contains("id") && entry.contains("id")) {
            rawData["id"] = entry["id"];
        }

        if (entry.contains("schema")) {
            LOG_CONTEXT(LogLevel::DEBUG, "Schema detected for type: " + parent.demangleType(typeName), {});
            parent.schemaRegistry[typeName] = [schema = entry["schema"]]() {
                return schema;
            };
        }

        json upgraded = parent.migrationRegistry.upgradeToLatest(typeName, version, rawData);
        LOG_CONTEXT(LogLevel::DEBUG, "Schema migration applied (if needed) for '" + tag + "' to latest version.", {});

        auto typeIt = parent.registeredTypes.find(typeName);
        if (typeIt == parent.registeredTypes.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "Unknown type: " + parent.demangleType(typeName) + " â€” skipping.", {});
            continue;
        }

        auto desIt = parent.deserializers.find(typeName);
        if (desIt == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + parent.demangleType(typeName) + " â€” skipping.", {});
            continue;
        }

        LOG_CONTEXT(LogLevel::INFO, "Attempting to deserialize item with tag '" + tag + "' and type '" + parent.demangleType(typeName) + "'.", {});
        std::cout << Logger::getColorCode(LogColor::CYAN)
                  << entry.dump(4) 
                  << Logger::getColorCode(LogColor::RESET) + "\n";

        try {
            auto newItem = desIt->second(upgraded, tag);
            if (newItem) {
                parent.items[tag] = std::move(newItem);
                LOG_CONTEXT(LogLevel::INFO, "Item '" + tag + "' imported successfully.", {});
                ++importCount;
            } else {
                LOG_CONTEXT(LogLevel::ERR, "Deserializer returned null for tag: " + tag, {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Error during deserialization of '" + tag + "': " + e.what())));
        }
    }

    LOG_CONTEXT(LogLevel::INFO, "Completed import of " + std::to_string(importCount) + " item(s) from JSON file: " + filename, {});
}

void ItemManager::IO_ToFileManager::asyncImportFromFile_Json(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            this->importFromFile_Json(filename);  // Thread-safe core
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Error during async import from file '" + filename + "': " + e.what())));
        }
    }).detach();  // Fire-and-forget style
}

std::shared_ptr<BaseItem> ItemManager::IO_ToFileManager::importSingleObject_Json(const std::string& filename, 
                                                                                 const std::string& typeName, 
                                                                                 const std::string& tag) {
    
    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot import from empty filename.", ErrorCode::ITEM_NOT_FOUND);
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting to import single JSON object from file: " + filename, {});
   
    std::ifstream in(filename);
    if (!in) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot open file for reading: " + filename, ErrorCode::FILE_LOAD_FAILED);
    }

    json array;
    try {
        in >> array;
        LOG_CONTEXT(LogLevel::DEBUG, "JSON file parsed successfully.", {});
    } catch (const std::exception& e) {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Failed to parse JSON from file '" + filename + "': " + e.what())));
        
    }

    for (const auto& entry : array) {
        if (entry.value("tag", "") == tag && entry.value("type", "") == typeName) {

            LOG_CONTEXT(LogLevel::INFO, "Found matching object with tag '" + tag + "' and type '" + parent.demangleType(typeName) + "'.", {});

            int version = entry.value("version", 1);
            json rawData = entry["data"];

            std::cout << Logger::getColorCode(LogColor::YELLOW)
                      << entry.dump(4)
                      << Logger::getColorCode(LogColor::RESET) + "\n";

            if (!rawData.contains("id") && entry.contains("id")) {
                rawData["id"] = entry["id"];
            }

            if (entry.contains("schema")) {
                LOG_CONTEXT(LogLevel::DEBUG, "Embedded schema detected for tag: " + tag, {});
                parent.schemaRegistry[typeName] = [schema = entry["schema"]]() {
                    return schema;
                };
            }

            json upgraded = parent.migrationRegistry.upgradeToLatest(typeName, version, rawData);
            LOG_CONTEXT(LogLevel::DEBUG, "Schema migration applied (if needed) to latest version.", {});

            auto typeIt = parent.registeredTypes.find(typeName);
            if (typeIt == parent.registeredTypes.end()) {
                LOG_CONTEXT(LogLevel::WARNING, "Unknown type: " + parent.demangleType(typeName) + " â€” skipping.", {});
                return nullptr;
            }

            auto desIt = parent.deserializers.find(typeName);
            if (desIt == parent.deserializers.end()) {
                LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + parent.demangleType(typeName) + " â€” skipping.", {});
                return nullptr;
            }

            try {
                LOG_CONTEXT(LogLevel::INFO, "Attempting to deserialize item with tag '" + tag + "' and type '" + parent.demangleType(typeName) + "'.", {});
                auto item = desIt->second(upgraded, tag);
                if (item) {
                    LOG_CONTEXT(LogLevel::INFO, "Deserialization successful for tag '" + tag + "'.", {});
                } else {
                    LOG_CONTEXT(LogLevel::ERR, "Deserializer returned null for tag: " + tag, {});
                }

                parent.undoHistory.push_back(parent.cloneCurrentState());
                parent.redoQueue = {};
                parent.items[tag] = item;  // safely inserts into store

                return item;
            } catch (const std::exception& e) {
                LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception during deserialization of '" + tag + "': " + e.what())));
            }
        }
    }
        LOG_CONTEXT(LogLevel::WARNING, "No object found with tag '" + tag + "' and type '" + parent.demangleType(typeName) + "' in file: " + filename, {});
    return nullptr;
}

void ItemManager::IO_ToFileManager::asyncImportSingleObject_Json(const std::string& filename, 
                                                                 const std::string& typeName, 
                                                                 const std::string& tag) {
    std::thread([this, filename, typeName, tag]() {
        auto item = this->importSingleObject_Json(filename, typeName, tag);
        if (item) {
            std::lock_guard<std::mutex> lock(parent.mutex_);
            parent.items[tag] = std::move(item);  // safely inserts into store
            LOG_CONTEXT(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully.", {});
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Async import failed for tag '" + tag + "' from file '" + filename + "'.", {});
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::exportToFile_Binary(const std::string& filename) const {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot export to empty filename.", false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting binary export to file: " + filename, {});

    if (parent.items.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "", std::make_exception_ptr(
                                          std::runtime_error("No items found for export to file '" + filename + "'.")));
    }

    std::vector<uint8_t> buffer;

    for (const auto& [tag, item] : parent.items) {
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

        LOG_CONTEXT(LogLevel::INFO, "Exported binary object with tag '" + tag + "' of type '" + parent.demangleType(type) + "' [hex]:", {});

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
        LOG_CONTEXT(LogLevel::ERR, "Failed atomic binary export to '" + filename + "'.",  true);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "Binary export to '" + filename + "' completed successfully.", true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncExportToFile_Binary(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            bool success = this->exportToFile_Binary(filename);
            if (!success) {
                LOG_CONTEXT(LogLevel::WARNING, "asyncExportToFile_Binary failed for file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::INFO, "asyncExportToFile_Binary completed successfully for file: " + filename, {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception in asyncExportToFile_Binary: " + std::string(e.what()))));
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::importFromFile_Binary(const std::string& filename) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot import from empty filename.", false);
        return false;
    }

   LOG_CONTEXT(LogLevel::INFO, "Attempting binary import from file: " + filename, {});

    std::ifstream in(filename, std::ios::binary);
    if (!in.is_open()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot open binary file '" + filename + "' for reading.", false);
        return false;
    }

    parent.undoHistory.push_back(parent.cloneCurrentState());
    parent.redoQueue = {};
    parent.items.clear();

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

        LOG_CONTEXT(LogLevel::DEBUG, "Processing binary object with tag '" + tag + "' of type '" + parent.demangleType(type) + "' [hex]:", {});
        for (size_t i = 0; i < dataSize; ++i) {
            std::printf("%02X ", static_cast<unsigned char>(jsonStr[i]));
            if ((i + 1) % 16 == 0) std::cout << '\n';
        }
        std::cout << "\n";

        json serialized;
        try {
            serialized = json::parse(jsonStr);
            LOG_CONTEXT(LogLevel::DEBUG, "Binary JSON parsed successfully for tag: " + tag, {});
        } catch (const json::parse_error& err) {
            LOG_CONTEXT(LogLevel::ERR, "Failed to parse JSON for tag '" + tag + "': " + std::string(err.what()), {});
            continue;
        }

        if (!serialized.contains("id") && !tag.empty()) {
            serialized["id"] = tag;  // Optional fallback for legacy
        }

        int version = 1; // Default to version 1 if not present
        if (serialized.contains("version")) {
            version = serialized["version"].get<int>();
        }

        json upgraded = parent.migrationRegistry.upgradeToLatest(type, version, serialized);
        LOG_CONTEXT(LogLevel::DEBUG, "Schema migration applied (if needed) for tag: " + tag + " to latest version.", {});

        auto desIt = parent.deserializers.find(type);
        if (desIt == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + type + " â€” skipping.", {});
            continue;
        }

        try {
            auto object = desIt->second(upgraded, tag);
            if (!object) {
                LOG_CONTEXT(LogLevel::WARNING, "Deserializer returned null for tag: " + tag, {});
                continue;
            }

            parent.items[tag] = object;
            LOG_CONTEXT(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' and type '" + type + "' from binary file: " + filename, {});
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + std::string(e.what()), {});
            continue;
        }
    }

    in.close();
    LOG_CONTEXT(LogLevel::INFO, "Binary import from '" + filename + "' completed successfully with " + std::to_string(parent.items.size()) + " items.", true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncImportFromFile_Binary(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            this->importFromFile_Binary(filename);  // Thread-safe if core is locked
            LOG_CONTEXT(LogLevel::INFO, "asyncImportFromFile_Binary completed successfully for file: " + filename, {});
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception in asyncImportFromFile_Binary: " + std::string(e.what()))));
        }
    }).detach();
}

std::shared_ptr<BaseItem> ItemManager::IO_ToFileManager::importSingleObject_Binary(const std::string& filename, 
                                                                                   const std::string& type, 
                                                                                   const std::string& tag) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error("Cannot import from empty filename.")));
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting to import single binary object from file: " 
                                + filename + " with type '" + parent.demangleType(type) + "' and tag '" + tag + "'", {});
    
    std::ifstream in(filename, std::ios::binary);
    if (!in) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot open binary file '" + filename + "' for reading.", ErrorCode::FILE_LOAD_FAILED);
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
            LOG_CONTEXT(LogLevel::DEBUG, "Matched binary object for tag '" + tag + "' of type '" + parent.demangleType(type) + "'", {});

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

            json upgraded = parent.migrationRegistry.upgradeToLatest(entryType, version, serialized);

            auto it = parent.deserializers.find(entryType);
            if (it == parent.deserializers.end()) {
                LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("No deserializer registered for type '" + parent.demangleType(entryType) + "'.")));
            }
            
            auto object = it->second(upgraded, tag);
            if (!object) {
                LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Deserializer returned null for tag '" + tag + "'.")));
            }

            parent.undoHistory.push_back(parent.cloneCurrentState());
            parent.redoQueue = {};
            parent.items[tag] = object;  // Safely insert into store

            LOG_CONTEXT(LogLevel::INFO, "Successfully imported object with tag '" + tag + "' from file '" + filename + "'", {});
            return object;
        }
    }

    LOG_CONTEXT(LogLevel::WARNING, "No matching object found for tag '" + tag + "' and type '" + parent.demangleType(type) + "' in file '" + filename + "'", {});
    return nullptr;
}

void ItemManager::IO_ToFileManager::asyncImportSingleObject_Binary(const std::string& filename, 
                                                                   const std::string& typeName, 
                                                                   const std::string& tag) {
    std::thread([this, filename, typeName, tag]() {
        auto item = this->importSingleObject_Binary(filename, typeName, tag);
        if (item) {
            std::lock_guard<std::mutex> lock(parent.mutex_);
            parent.items[tag] = std::move(item);
            LOG_CONTEXT(LogLevel::INFO, "Async binary import of '" + tag + "' succeeded.", {});
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Async binary import failed for tag '" + tag + "' from file '" + filename + "'.", {});
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::exportToFile_XML(const std::string& filename) const {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot export to empty filename.", ErrorCode::INVALID_INPUT );
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting XML export to file: " + filename, {});

    if (parent.items.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "", std::make_exception_ptr(
                                          std::runtime_error("No items found for XML export to file '" + filename + "'.")));
    }

    tinyxml2::XMLDocument doc;
    auto* root = doc.NewElement("SmartStore");
    doc.InsertFirstChild(root);

    for (const auto& [tag, item] : parent.items) {
        if (!item) {
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " â€” skipping.", {});
            continue;
        }

        LOG_CONTEXT(LogLevel::INFO, "Exporting item with tag: " + tag + " of type: " + parent.demangleType(item->getTypeName()), {});

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
        LOG_CONTEXT(LogLevel::INFO, "Successfully added item with tag '" + tag + "' to XML structure.", {});
    }

    tinyxml2::XMLPrinter printer;
    doc.Print(&printer);
    std::string xmlContent = printer.CStr();

    if (!AtomicFileWriter::writeAtomically(filename, xmlContent)) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to write XML atomically to file: " + filename, false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "XML export completed successfully to file: " + filename, true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncExportToFile_XML(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            bool success = this->exportToFile_XML(filename);
            if (!success) {
                LOG_CONTEXT(LogLevel::ERR, "asyncExportToFile_XML failed for file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::INFO, "asyncExportToFile_XML completed successfully for file: " + filename, {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Exception in asyncExportToFile_XML: " + std::string(e.what()))));
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::importFromFile_XML(const std::string& filename) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty â€” cannot proceed with XML import.", false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting XML import from file: " + filename, {});

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError result = doc.LoadFile(filename.c_str());
    if (result != tinyxml2::XML_SUCCESS) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to read XML file '" + filename + "' â€” error code: " + std::to_string(result), false);
        return false;
    }

    auto* root = doc.FirstChildElement("SmartStore");
    if (!root) {
        LOG_CONTEXT(LogLevel::ERR, "Missing <SmartStore> root in XML file '" + filename + "'", false);
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
            LOG_CONTEXT(LogLevel::WARNING, "Skipping <Item> with missing tag, type, or data.", {});
            continue;
        }

        std::string tag       = tagTextPtr;
        std::string typeName  = typeTextPtr;
        std::string dataText  = dataTextPtr;
        int version = versionTextPtr ? std::atoi(versionTextPtr) : 1;

        if (tag.empty() || typeName.empty() || dataText.empty()) {
            LOG_CONTEXT(LogLevel::WARNING, "Skipping <Item> with empty fields: tag='" + tag + "', type='" 
                                                    + parent.demangleType(typeName) + "', data='" + dataText + "'", {});
            continue;
        }

        nlohmann::json j;
        try {
            j = nlohmann::json::parse(dataText);
            if (!j.contains("id") && !tag.empty()) j["id"] = tag;
            if (!j.contains("tag")) j["tag"] = tag;
            if (!j.contains("type")) j["type"] = typeName;
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "JSON parse error in item '" + tag + "': " + e.what(), {});
            continue;
        }

        LOG_CONTEXT(LogLevel::INFO, "Found item in XML: tag='" + tag + "', type='" + parent.demangleType(typeName) + "'", {});
        std::cout << Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        LOG_CONTEXT(LogLevel::DEBUG, "Upgrading item '" + tag + "' of type '" + parent.demangleType(typeName) 
                                                                    + "' from version: " + std::to_string(version), {});

        json upgraded = parent.migrationRegistry.upgradeToLatest(typeName, version, j);

        auto it = parent.deserializers.find(typeName);
        if (it == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type '" + parent.demangleType(typeName) + "' â€” skipping item with tag '" + tag + "'", {});
            continue;
        }

        try {
            auto item = it->second(upgraded, tag);
            if (item) {
                parent.items[tag] = item;
                LOG_CONTEXT(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from XML.", {});
                loadedCount++;
            } else {
                LOG_CONTEXT(LogLevel::ERR, "Deserializer returned null for tag '" + tag + "' â€” skipping.", {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Error during deserialization of '" + tag + "': " + e.what())));
        }
    }

    LOG_CONTEXT(LogLevel::INFO, "XML import completed with " + std::to_string(loadedCount) + " items loaded from file: " + filename, true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncImportFromFile_XML(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            bool success = this->importFromFile_XML(filename);
            if (!success) {
                LOG_CONTEXT(LogLevel::ERR, "asyncImportFromFile_XML failed for file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::INFO, "asyncImportFromFile_XML completed successfully for file: " + filename, {});
            }
        } catch (const std::exception& ex) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Error in asyncImportFromFile_XML: " + std::string(ex.what()))));
        } catch (...) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Unknown error in asyncImportFromFile_XML")));
        }
    }).detach();  // Run the thread in background
}

std::optional<std::shared_ptr<BaseItem>> ItemManager::IO_ToFileManager::importSingleObject_XML(const std::string& filename, 
                                                                                               const std::string& type, 
                                                                                               const std::string& tag) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty â€” cannot import from XML.", {});
        return std::nullopt;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting to import single XML object from file: " + filename + 
                                        " with type '" + parent.demangleType(type) + "' and tag '" + tag + "'", {});

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty â€” cannot import from XML.", {});
        return std::nullopt;
    }

    tinyxml2::XMLDocument doc;
    if (doc.LoadFile(filename.c_str()) != tinyxml2::XML_SUCCESS) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to load XML file: " + filename, {});
        return std::nullopt;
    }

    auto* root = doc.FirstChildElement("SmartStore");
    if (!root) {
        LOG_CONTEXT(LogLevel::ERR, "Missing <SmartStore> root element in XML file: " + filename, {});
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

            LOG_CONTEXT(LogLevel::INFO, "Found matching item in XML: tag='" + std::string(tagText) + 
                                                    "', type='" + parent.demangleType(std::string(typeText)) + "'", {});

            std::cout << Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

            json upgraded = parent.migrationRegistry.upgradeToLatest(type, 1, j); // Assumes version 1 if none is specified.
            LOG_CONTEXT(LogLevel::DEBUG, "Upgrading item '" + std::string(tagText) + "' of type '" 
                                                        + parent.demangleType(std::string(typeText)) + "' to latest version.", {});

            auto it = parent.deserializers.find(type);
            if (it == parent.deserializers.end()) {
                LOG_CONTEXT(LogLevel::ERR, "No deserializer registered for type '" + parent.demangleType(type) 
                                                            + "' â€” cannot import item with tag '" + tag + "'", {});
                return std::nullopt;
            }

            LOG_CONTEXT(LogLevel::INFO, "Attempting to import item with tag '" + tag + "' from XML.", {});
            auto item = it->second(upgraded, tag);

            parent.undoHistory.push_back(parent.cloneCurrentState());
            parent.redoQueue = {};
            parent.items[tag] = item;

            return item;
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "Failed to parse JSON data for tag '" + tag + "': " + e.what(), {});
            return std::nullopt;
        }
    }

    LOG_CONTEXT(LogLevel::INFO, "No matching item found for tag '" + tag + "' and type '" + parent.demangleType(type)
                                                                                 + "' in XML file: " + filename, {});
    return std::nullopt;
}

void ItemManager::IO_ToFileManager::asyncImportSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag) {
    std::thread([this, filename, type, tag]() {
        try {
            auto result = this->importSingleObject_XML(filename, type, tag);
            if (result.has_value() && result.value()) {
                std::lock_guard lock(parent.mutex_);  // Ensure thread-safe map update
                parent.items[tag] = result.value();
                LOG_CONTEXT(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully from XML file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::WARNING, "Async import failed or returned null for tag '" + tag + "' from XML file: " + filename, {});
            }
        } catch (const std::exception& ex) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception in asyncImportSingleObject_XML: " + std::string(ex.what()))));
        } catch (...) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Unknown error in asyncImportSingleObject_XML for tag '" + tag + "' from file: " + filename)));
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::exportToFile_CSV(const std::string& filename) const {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "CSV export failed: empty filename.", true);
        return true;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting CSV export to file: " + filename, {});

    if (parent.items.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "", std::make_exception_ptr(
                                          std::runtime_error("CSV export failed: No items found for export to file '" + filename + "'.")));
    }

    std::ostringstream oss;
    oss << "id,tag,type,data\n"; // CSV header

    for (const auto& [tag, item] : parent.items) {
        if (!item) {
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " â€” skipping.", {});
            continue;
        }

        const std::string& id = item->getId();
        const std::string& type = item->getTypeName();

        std::string dataStr;
        try {
            json j = item->toJson();
            dataStr = j.is_string() ? j.get<std::string>() : j.dump();
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::WARNING, "Failed to serialize item '" + tag + "': " + e.what(), {});
            dataStr = "{}";
        }

        // Debug preview in terminal
        LOG_CONTEXT(LogLevel::INFO, "Exporting item: id='" + id + "', tag='" + tag + "', type='" + parent.demangleType(type) + "'", {});
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
        LOG_CONTEXT(LogLevel::ERR, "Failed to write CSV atomically to file: " + filename, false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "CSV export completed successfully to file: " + filename, true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncExportToFile_CSV(const std::string& filename) const {
    std::thread([this, filename]() {
        try {
            if (this->exportToFile_CSV(filename)) {
                LOG_CONTEXT(LogLevel::INFO, "asyncExportToFile_CSV completed successfully for file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::ERR, "asyncExportToFile_CSV failed for file: " + filename, {});
            }
        } catch (const std::exception& ex) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception in asyncExportToFile_CSV: " + std::string(ex.what()))));
        } catch (...) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Unknown error in asyncExportToFile_CSV for file: " + filename)));
        }
    }).detach();
}

bool ItemManager::IO_ToFileManager::importFromFile_CSV(const std::string& filename) {

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Cannot import from empty filename.", false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting CSV import from file: " + filename, {});

    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Cannot open CSV file '" + filename + "' for reading.")));
    }

    std::string header;
    std::getline(file, header); // Skip header row
    if (header != "id,tag,type,data") {
        LOG_CONTEXT(LogLevel::ERR, "Unexpected CSV header format in file: " + filename, false);
        return false;
    }

    parent.undoHistory.push_back(parent.cloneCurrentState());
    parent.redoQueue = {};
    parent.items.clear();

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
            LOG_CONTEXT(LogLevel::WARNING, "Malformed CSV row: '" + line + "' â€” skipping.", {});
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

            json upgradedData = parent.migrationRegistry.upgradeToLatest(type, version, parsedData);
            LOG_CONTEXT(LogLevel::DEBUG, "Upgrading item '" + tag + "' of type '" + parent.demangleType(type) + 
                                                                        "' from version: " + std::to_string(version), {});

            j["data"] = upgradedData;
            j["id"]   = id;
            j["tag"]  = tag;
            j["type"] = type;
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "Failed to construct JSON for tag '" + tag + "': " + e.what(), {});
            continue;
        }

        LOG_CONTEXT(LogLevel::INFO, "Processing CSV row: id='" + id + "', tag='" + tag + "', type='" + type + "'", {});

        std::cout << "\n" + Logger::getColorCode(LogColor::YELLOW) << j.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        auto it = parent.deserializers.find(type);
        if (it == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type '" + parent.demangleType(type) + "' â€” skipping item with tag '" + tag + "'", {});
            continue;
        }

        try {
            auto item = it->second(j, tag);
            if (item) {
                parent.items[tag] = item;
                loadedCount++;
                LOG_CONTEXT(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from CSV.", {});
            } else {
                LOG_CONTEXT(LogLevel::WARNING, "Deserializer returned null for tag '" + tag + "' â€” skipping.", {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "Exception during deserialization of '" + tag + "': " + e.what(), {});
        }
    }

    LOG_CONTEXT(LogLevel::INFO, "CSV import completed with " + std::to_string(loadedCount) + " items loaded from file: " + filename, true);
    return true;
}

void ItemManager::IO_ToFileManager::asyncImportFromFile_CSV(const std::string& filename) {
    std::thread([this, filename]() {
        try {
            if (this->importFromFile_CSV(filename)) {
                LOG_CONTEXT(LogLevel::INFO, "asyncImportFromFile_CSV completed successfully for file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::ERR, "asyncImportFromFile_CSV failed for file: " + filename, {});
            }
        } catch (const std::exception& ex) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(std::runtime_error(
                                          "Exception in asyncImportFromFile_CSV: " + std::string(ex.what())))   );
        } catch (...) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Unknown error in asyncImportFromFile_CSV for file: " + filename)));
        }
    }).detach();
}

std::shared_ptr<BaseItem> ItemManager::IO_ToFileManager::importSingleObject_CSV(const std::string& filename, 
                                                                                const std::string& type, 
                                                                                const std::string& tag) {
   
    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty â€” cannot proceed with CSV import.", {});
        return nullptr;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting to import single CSV object from file: " + filename + " with type '" 
                                                                    + parent.demangleType(type) + "' and tag '" + tag + "'", {});
    
    std::ifstream file(filename);
    if (!file.is_open()) {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Cannot open CSV file '" + filename + "' for reading.")));
    }

    std::string header;
    std::getline(file, header);  // Skip header
    if (header != "id,tag,type,data") {
        LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Unexpected CSV header format in file: " + filename)));
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
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Failed to parse JSON data for tag '" + tagIn + "': " + e.what())));
        }

        int version = 1;
        if (rawData.is_object() && rawData.contains("version")) {
            version = rawData["version"];
        }

        json upgradedData = parent.migrationRegistry.upgradeToLatest(typeIn, version, rawData);

        json wrapper;
        wrapper["id"]   = id;
        wrapper["tag"]  = tagIn;
        wrapper["type"] = typeIn;
        wrapper["data"] = upgradedData;

        std::cout << Logger::getColorCode(LogColor::CYAN) + "\n>>> Matched CSV row: tag='"
                             << tag << "', type='" << parent.demangleType(type) << "'\n" + Logger::getColorCode(LogColor::YELLOW);

        std::cout << Logger::getColorCode(LogColor::YELLOW) << wrapper.dump(4) << Logger::getColorCode(LogColor::RESET) + "\n";

        auto it = parent.deserializers.find(typeIn);
        if (it == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::ERR, "No deserializer registered for type '" + parent.demangleType(typeIn) + 
                                                    "' â€” cannot import item with tag '" + parent.demangleType(tagIn) + "'", {});
            return nullptr;
        }

        try {
            auto item = it->second(wrapper, tagIn);
            LOG_CONTEXT(LogLevel::INFO, "Attempting to import item with tag '" + parent.demangleType(tagIn) + "' from CSV.", {});

            // Undo/Redo support (only if it is actually imported)
            parent.undoHistory.push_back(parent.cloneCurrentState());
            parent.redoQueue = {};
            parent.items[tagIn] = item;

            return item;
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Failed to deserialize item with tag '" + tagIn + "': " + e.what())));
        }
    }

    LOG_CONTEXT(LogLevel::INFO, "No matching item found for tag '" + tag + "' and type '" + 
                                        parent.demangleType(type) + "' in CSV file: " + filename, {});
                                        return nullptr;
}

void ItemManager::IO_ToFileManager::asyncImportSingleObject_CSV(const std::string& filename, const std::string& type,const std::string& tag){
    std::thread([this, filename, type, tag]() {
        try {
            auto item = this->importSingleObject_CSV(filename, type, tag);
            if (item) {
                std::lock_guard<std::mutex> lock(parent.mutex_); // protect shared state
                parent.items[tag] = item;
                LOG_CONTEXT(LogLevel::INFO, "Async import of single item '" + tag + "' completed successfully from CSV file: " + filename, {});
            } else {
                LOG_CONTEXT(LogLevel::WARNING, "Async import failed or returned null for tag '" + tag + "' from CSV file: " + filename, {});
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Exception in asyncImportSingleObject_CSV: " + std::string(e.what()))));
        } catch (...) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                                          std::runtime_error("Unknown error in asyncImportSingleObject_CSV for tag '" + tag + "' from file: " + filename)));
        }
    }).detach();
}

// ComputerVision.cpp

template<typename T>
void ItemManager::ComputerVision::cvDOC_setEdgeThreshold(int low, int high, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            doc.setEdgeThreshold(low, high);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set edge threshold failed with tag '" + tag + "'.",
                        {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set edge threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvDOC_setContourMinArea(double area, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            doc.setContourMinArea(area);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set contour min area failed with tag '" + tag + "'.",
                        {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set contour min area.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvDOC_setOutputSize(int width, int height, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            doc.setOutputSize(width, height);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set output size failed with tag '" + tag + "'.",
                        {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set output size.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvDOC_setSharpening(double amount, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            doc.setSharpening(amount);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set sharpening failed with tag '" + tag + "'.",
                        {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set sharpening.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
int ItemManager::ComputerVision::cvDOC_getCannyLow(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getCannyLow();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get Canny low failed with tag '" + tag + "'.",
                        {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get Canny low.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1;
    }
}

template<typename T>
int ItemManager::ComputerVision::cvDOC_getCannyHigh(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getCannyHigh();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get Canny high failed with tag '" + tag + "'.",
                        {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get Canny high.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1;
    }
}

template<typename T>
double ItemManager::ComputerVision::cvDOC_getMinContourArea(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getMinContourArea();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get min contour area failed with tag '" + tag + "'.",
                        {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get min contour area.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0.0;
    }
}

template<typename T>
int ItemManager::ComputerVision::cvDOC_getOutputWidth(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getOutputWidth();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get output width failed with tag '" + tag + "'.",
                        {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get output width.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1;
    }
}

template<typename T>
int ItemManager::ComputerVision::cvDOC_getOutputHeight(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getOutputHeight();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get output height failed with tag '" + tag + "'.",
                        {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get output height.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1;
    }
}

template<typename T>
double ItemManager::ComputerVision::cvDOC_getSharpening(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            return doc.getSharpening();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get sharpening failed with tag '" + tag + "'.",
                        {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get sharpening.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0.0;
    }
}

template<typename T>
void ItemManager::ComputerVision::cvDOC_run(const std::string& mode,
                                            const std::string& input,
                                            const std::string& output,
                                            std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::DocumentScannerWrapper doc(*wrapper);
            doc.run(mode, input, output);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Run document scanner failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to run document scanner.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_start(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->startSurveillance();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Start surveillance failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to start surveillance.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_stop(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->stopSurveillance();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Stop surveillance failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to stop surveillance.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
bool ItemManager::ComputerVision::cvSurv_isRunning(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->isSurveillanceRunning();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Check surveillance running failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to check surveillance running state.",
                    ErrorCode::ITEM_NOT_FOUND);
        return false;
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setCloudEnabled(bool enabled, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceCloudEnabled(enabled);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance cloud enabled failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set cloud enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setCloudSettings(const std::string& baseUrl,
                                                           const std::string& stationName,
                                                           const std::string& hardwareToken,
                                                           int pollIntervalSec,
                                                           std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceCloudSettings(baseUrl, stationName, hardwareToken, pollIntervalSec);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance cloud settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set cloud settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_addCamera(const std::string& name,
                                                    const std::string& id,
                                                    const std::string& source,
                                                    bool enableFaceRecognition,
                                                    std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->addSurveillanceCamera(name, id, source, enableFaceRecognition);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Add surveillance camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to add camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setCameraFeature(const std::string& cameraId,
                                                           const std::string& featureKey,
                                                           bool value,
                                                           std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceCameraFeature(cameraId, featureKey, value);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance camera feature failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set camera feature.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_updateCamera(const std::string& camId,
                                                       const std::string& name,
                                                       const std::string& source,
                                                       std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->updateSurveillanceCamera(camId, name, source);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Update surveillance camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to update camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_removeCamera(const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->removeSurveillanceCamera(camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Remove surveillance camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to remove camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_plugInPendingCamera(const std::string& camName, const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->plugInSurveillancePendingCamera(camName, camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Plug in surveillance pending camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to plug in pending camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_plugOutPendingCamera(bool isDelete, const std::string& camName, const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->plugOutSurveillancePendingCamera(isDelete, camName, camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Plug out surveillance pending camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to plug out pending camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setFeatureForAllCameras(const std::string& featureKey, bool value, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceFeatureForAllCameras(featureKey, value);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance feature for all cameras failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set feature for all cameras.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_upLoadModel(const std::string& modelType, const std::string& modelPath, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->upLoadSurveillanceModel(modelType, modelPath);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Upload surveillance model failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to upload model.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_resetModelSettings(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetSurveillanceModelSettings();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset surveillance model settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset model settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setBeepEnabled(bool enabled, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceBeepEnabled(enabled);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance beep enabled failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set beep enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setVoiceEnabled(bool enabled, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceVoiceEnabled(enabled);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance voice enabled failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set voice enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setVoiceGender(const std::string& gender, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceVoiceGender(gender);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance voice gender failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set voice gender.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setStationLocation(double latitude, double longitude, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceStationLocation(latitude, longitude);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance station location failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set station location.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_resetFaceSettings(const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetSurveillanceFaceSettings(camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset surveillance face settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset face settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_resetMotionSettings(const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetSurveillanceMotionSettings(camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset surveillance motion settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset motion settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_addMotionZone(const std::string& camId,
                                                        const std::string& regionName,
                                                        int x, int y, int width, int height,
                                                        bool restricted,
                                                        std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->addSurveillanceMotionZone(camId, regionName, x, y, width, height, restricted);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Add surveillance motion zone failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to add motion zone.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_clearMotionZones(const std::string& camId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->clearSurveillanceMotionZones(camId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Clear surveillance motion zones failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to clear motion zones.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_addPlateToWatchlist(const std::string& plate, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->addSurveillancePlateToWatchlist(plate);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Add surveillance plate to watchlist failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to add plate to watchlist.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_removePlateFromWatchlist(const std::string& plate, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->removeSurveillancePlateFromWatchlist(plate);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Remove surveillance plate from watchlist failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to remove plate from watchlist.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_clearPlateWatchlist(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->clearSurveillancePlateWatchlist();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Clear surveillance plate watchlist failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to clear plate watchlist.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setAccountId(const std::string& accountId, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceAccountId(accountId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance account id failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set account id.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setKafkaBrokerAddress(const std::string& brokerAddress, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceKafkaBrokerAddress(brokerAddress);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance kafka broker address failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set kafka broker address.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setFaceSettings(const std::string& camId,
                                                          double scaleFactor,
                                                          int minNeighbors,
                                                          int minFaceSizeWidth,
                                                          int minFaceSizeHeight,
                                                          double scoreThreshold,
                                                          double nmsThreshold,
                                                          int topK,
                                                          bool useEqualizeHist,
                                                          int maxDetections,
                                                          uint64_t maxTrackAgeMs,
                                                          double iouThreshold,
                                                          bool debugLogging,
                                                          std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceFaceSettings(camId, scaleFactor, minNeighbors, minFaceSizeWidth, minFaceSizeHeight,
                                                 scoreThreshold, nmsThreshold, topK, useEqualizeHist, maxDetections,
                                                 maxTrackAgeMs, iouThreshold, debugLogging);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance face settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set face settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setMotionSettings(const std::string& camId,
                                                            double diffThreshold,
                                                            int minArea,
                                                            std::size_t crowdThreshold,
                                                            int loiterSeconds,
                                                            int leftBehindSeconds,
                                                            bool enableTracking,
                                                            bool debugLogging,
                                                            std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceMotionSettings(camId, diffThreshold, minArea, crowdThreshold, loiterSeconds,
                                                   leftBehindSeconds, enableTracking, debugLogging);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance motion settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set motion settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setNightVisionSettings(const std::string& camId,
                                                                 double gamma,
                                                                 bool adaptiveMode,
                                                                 int contrastMode,
                                                                 double clipLimit,
                                                                 int tileSize,
                                                                 int denoisingStrength,
                                                                 std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceNightVisionSettings(camId, gamma, adaptiveMode, contrastMode, clipLimit,
                                                        tileSize, denoisingStrength);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance night vision settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set night vision settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setObjectDetectionSettings(const std::string& camId,
                                                                     int inputSize,
                                                                     int backend,
                                                                     int target,
                                                                     bool trackingEnabled,
                                                                     float minConfForDraw,
                                                                     std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceObjectDetectionSettings(camId, inputSize, backend, target, trackingEnabled, minConfForDraw);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance object detection settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set object detection settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setVehicleSettings(const std::string& camId,
                                                             const std::string& lang,
                                                             int minPlateConfidence,
                                                             bool enableAlerts,
                                                             bool saveImages,
                                                             std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceVehicleSettings(camId, lang, minPlateConfidence, enableAlerts, saveImages);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance vehicle settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set vehicle settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_setRecorderSettings(const std::string& camId,
                                                              int codec,
                                                              int bitrate,
                                                              int maxDuration,
                                                              uint64_t maxFileSize,
                                                              const std::string& eventType,
                                                              std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setSurveillanceRecorderSettings(camId, codec, bitrate, maxDuration, maxFileSize, eventType);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set surveillance recorder settings failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set recorder settings.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

// --- DataBaseManager passthroughs -- same lock/find/dynamic_cast/forward
// shape as every dispatch method above; only the underlying wrapper call
// and log text change per method. ---

#define DISP_FORWARD0(FnName, WrapperCall, LogText) \
template<typename T> \
void ItemManager::ComputerVision::FnName(std::string& tag) { \
    std::lock_guard<std::mutex> lock(parent.mutex_); \
    auto it = parent.items.find(tag); \
    if (it != parent.items.end()) { \
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get()); \
        if (wrapper) { \
            wrapper->WrapperCall(); \
        } else { \
            LOG_CONTEXT(LogLevel::WARNING, LogText " failed with tag '" + tag + "'.", \
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n"))); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to " LogText ".", ErrorCode::ITEM_NOT_FOUND); \
    } \
}

// Same shape as DISP_FORWARD0, for the read/query dispatch methods that
// return a requestId instead of being fire-and-forget.
#define DISP_FORWARD0_R(FnName, WrapperCall, LogText) \
template<typename T> \
std::string ItemManager::ComputerVision::FnName(std::string& tag) { \
    std::lock_guard<std::mutex> lock(parent.mutex_); \
    auto it = parent.items.find(tag); \
    if (it != parent.items.end()) { \
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get()); \
        if (wrapper) { \
            return wrapper->WrapperCall(); \
        } else { \
            LOG_CONTEXT(LogLevel::WARNING, LogText " failed with tag '" + tag + "'.", \
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n"))); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to " LogText ".", ErrorCode::ITEM_NOT_FOUND); \
    } \
    return ""; \
}

DISP_FORWARD0_R(cvSurv_getAllTrackedFaces, getSurveillanceAllTrackedFaces, "get all tracked faces")
DISP_FORWARD0_R(cvSurv_getUnknownTrackedFaces, getSurveillanceUnknownTrackedFaces, "get unknown tracked faces")
DISP_FORWARD0_R(cvSurv_getAuthorizedTrackedFaces, getSurveillanceAuthorizedTrackedFaces, "get authorized tracked faces")
DISP_FORWARD0_R(cvSurv_getWatchlistTrackedFaces, getSurveillanceWatchlistTrackedFaces, "get watchlist tracked faces")
DISP_FORWARD0(cvSurv_deleteAllTrackedFaces, deleteSurveillanceAllTrackedFaces, "delete all tracked faces")
DISP_FORWARD0(cvSurv_deleteAllAuthorizedFaces, deleteSurveillanceAllAuthorizedFaces, "delete all authorized faces")
DISP_FORWARD0(cvSurv_deleteAllWatchlistFaces, deleteSurveillanceAllWatchlistFaces, "delete all watchlist faces")

DISP_FORWARD0_R(cvSurv_getAllTrackedPlates, getSurveillanceAllTrackedPlates, "get all tracked plates")
DISP_FORWARD0_R(cvSurv_getUnknownTrackedPlates, getSurveillanceUnknownTrackedPlates, "get unknown tracked plates")
DISP_FORWARD0_R(cvSurv_getAuthorizedTrackedPlates, getSurveillanceAuthorizedTrackedPlates, "get authorized tracked plates")
DISP_FORWARD0_R(cvSurv_getWatchlistTrackedPlates, getSurveillanceWatchlistTrackedPlates, "get watchlist tracked plates")
DISP_FORWARD0(cvSurv_deleteAllTrackedPlates, deleteSurveillanceAllTrackedPlates, "delete all tracked plates")
DISP_FORWARD0(cvSurv_deleteAllAuthorizedPlates, deleteSurveillanceAllAuthorizedPlates, "delete all authorized plates")
DISP_FORWARD0(cvSurv_deleteAllWatchlistPlates, deleteSurveillanceAllWatchlistPlates, "delete all watchlist plates")

DISP_FORWARD0_R(cvSurv_getAllTrackedWeapons, getSurveillanceAllTrackedWeapons, "get all tracked weapons")
DISP_FORWARD0_R(cvSurv_getUnknownTrackedWeapons, getSurveillanceUnknownTrackedWeapons, "get unknown tracked weapons")
DISP_FORWARD0_R(cvSurv_getAuthorizedTrackedWeapons, getSurveillanceAuthorizedTrackedWeapons, "get authorized tracked weapons")
DISP_FORWARD0_R(cvSurv_getWatchlistTrackedWeapons, getSurveillanceWatchlistTrackedWeapons, "get watchlist tracked weapons")
DISP_FORWARD0(cvSurv_deleteAllTrackedWeapons, deleteSurveillanceAllTrackedWeapons, "delete all tracked weapons")
DISP_FORWARD0(cvSurv_deleteAllAuthorizedWeapons, deleteSurveillanceAllAuthorizedWeapons, "delete all authorized weapons")
DISP_FORWARD0(cvSurv_deleteAllWatchlistWeapons, deleteSurveillanceAllWatchlistWeapons, "delete all watchlist weapons")

DISP_FORWARD0_R(cvSurv_getAllEvents, getSurveillanceAllEvents, "get all events")
DISP_FORWARD0(cvSurv_deleteAllEvents, deleteSurveillanceAllEvents, "delete all events")

DISP_FORWARD0_R(cvSurv_getAllRecordings, getSurveillanceAllRecordings, "get all recordings")
DISP_FORWARD0(cvSurv_deleteAllRecordings, deleteSurveillanceAllRecordings, "delete all recordings")

DISP_FORWARD0_R(cvSurv_getAllDailyFaceMetrics, getSurveillanceAllDailyFaceMetrics, "get all daily face metrics")
DISP_FORWARD0(cvSurv_deleteAllDailyFaceMetrics, deleteSurveillanceAllDailyFaceMetrics, "delete all daily face metrics")

#undef DISP_FORWARD0
#undef DISP_FORWARD0_R

#define DISP_FORWARD1(FnName, ArgType, ArgName, WrapperCall, LogText) \
template<typename T> \
void ItemManager::ComputerVision::FnName(ArgType ArgName, std::string& tag) { \
    std::lock_guard<std::mutex> lock(parent.mutex_); \
    auto it = parent.items.find(tag); \
    if (it != parent.items.end()) { \
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get()); \
        if (wrapper) { \
            wrapper->WrapperCall(ArgName); \
        } else { \
            LOG_CONTEXT(LogLevel::WARNING, LogText " failed with tag '" + tag + "'.", \
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n"))); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to " LogText ".", ErrorCode::ITEM_NOT_FOUND); \
    } \
}

// Same shape as DISP_FORWARD1, for the read/query dispatch methods that
// return a requestId instead of being fire-and-forget.
#define DISP_FORWARD1_R(FnName, ArgType, ArgName, WrapperCall, LogText) \
template<typename T> \
std::string ItemManager::ComputerVision::FnName(ArgType ArgName, std::string& tag) { \
    std::lock_guard<std::mutex> lock(parent.mutex_); \
    auto it = parent.items.find(tag); \
    if (it != parent.items.end()) { \
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get()); \
        if (wrapper) { \
            return wrapper->WrapperCall(ArgName); \
        } else { \
            LOG_CONTEXT(LogLevel::WARNING, LogText " failed with tag '" + tag + "'.", \
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n"))); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to " LogText ".", ErrorCode::ITEM_NOT_FOUND); \
    } \
    return ""; \
}

DISP_FORWARD1_R(cvSurv_getTrackedFaceById, const std::string&, faceId, getSurveillanceTrackedFaceById, "get tracked face by id")
DISP_FORWARD1(cvSurv_deleteTrackedFace, const std::string&, id, deleteSurveillanceTrackedFace, "delete tracked face")
DISP_FORWARD1_R(cvSurv_getFaceRegionHistory, const std::string&, faceId, getSurveillanceFaceRegionHistory, "get face region history")

DISP_FORWARD1_R(cvSurv_getTrackedPlateById, const std::string&, plateId, getSurveillanceTrackedPlateById, "get tracked plate by id")
DISP_FORWARD1(cvSurv_deleteTrackedPlate, const std::string&, id, deleteSurveillanceTrackedPlate, "delete tracked plate")
DISP_FORWARD1_R(cvSurv_getPlateRegionHistory, const std::string&, plateId, getSurveillancePlateRegionHistory, "get plate region history")

DISP_FORWARD1_R(cvSurv_getObjectRegionHistory, const std::string&, objectId, getSurveillanceObjectRegionHistory, "get object region history")

DISP_FORWARD1_R(cvSurv_getTrackedWeaponById, const std::string&, weaponId, getSurveillanceTrackedWeaponById, "get tracked weapon by id")
DISP_FORWARD1(cvSurv_deleteTrackedWeapon, const std::string&, id, deleteSurveillanceTrackedWeapon, "delete tracked weapon")
DISP_FORWARD1_R(cvSurv_getWeaponRegionHistory, const std::string&, weaponId, getSurveillanceWeaponRegionHistory, "get weapon region history")

DISP_FORWARD1(cvSurv_deleteUserById, const std::string&, userId, deleteSurveillanceUserById, "delete user by id")

DISP_FORWARD1_R(cvSurv_getEventById, const std::string&, eventId, getSurveillanceEventById, "get event by id")
DISP_FORWARD1(cvSurv_deleteEventById, const std::string&, eventId, deleteSurveillanceEventById, "delete event by id")

DISP_FORWARD1_R(cvSurv_getRecordingsByCameraId, const std::string&, cameraId, getSurveillanceRecordingsByCameraId, "get recordings by camera id")
DISP_FORWARD1_R(cvSurv_getRecordingById, const std::string&, id, getSurveillanceRecordingById, "get recording by id")
DISP_FORWARD1(cvSurv_deleteRecordingById, const std::string&, id, deleteSurveillanceRecordingById, "delete recording by id")

DISP_FORWARD1(cvSurv_pruneTelemetryBefore, long long, beforeTimestamp, pruneSurveillanceTelemetryBefore, "prune telemetry")

DISP_FORWARD1_R(cvSurv_getDailyFaceMetricsByDate, const std::string&, detectionDate, getSurveillanceDailyFaceMetricsByDate, "get daily face metrics by date")
DISP_FORWARD1(cvSurv_deleteDailyFaceMetricsByDate, const std::string&, detectionDate, deleteSurveillanceDailyFaceMetricsByDate, "delete daily face metrics by date")

DISP_FORWARD1_R(cvSurv_getRecentChatMessages, int, limit, getSurveillanceRecentChatMessages, "get recent chat messages")

#undef DISP_FORWARD1
#undef DISP_FORWARD1_R

template<typename T>
void ItemManager::ComputerVision::cvSurv_registerFaceFromImage(const std::string& imagePath,
                                                                const std::string& name,
                                                                const std::string& status,
                                                                const std::string& description,
                                                                const std::string& externalId,
                                                                bool broadcastToCloud,
                                                                std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->registerSurveillanceFaceFromImage(imagePath, name, status, description, externalId, broadcastToCloud);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Register surveillance face from image failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to register face from image.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_logTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->logSurveillanceTrackedPlate(plateNumber, status, description);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Log surveillance tracked plate failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to log tracked plate.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_registerPlateFromImage(const std::string& imagePath,
                                                                 const std::string& plateNumber,
                                                                 const std::string& status,
                                                                 const std::string& externalId,
                                                                 std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->registerSurveillancePlateFromImage(imagePath, plateNumber, status, externalId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Register surveillance plate from image failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to register plate from image.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_registerWeaponFromImage(const std::string& imagePath,
                                                                  const std::string& name,
                                                                  const std::string& status,
                                                                  const std::string& description,
                                                                  const std::string& externalId,
                                                                  std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->registerSurveillanceWeaponFromImage(imagePath, name, status, description, externalId);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Register surveillance weapon from image failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to register weapon from image.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_registerUser(const std::string& username,
                                                       const std::string& passwordHash,
                                                       const std::string& role,
                                                       const std::string& name,
                                                       const std::string& imagePath,
                                                       const std::string& phoneNumber,
                                                       const std::string& email,
                                                       int isActive,
                                                       std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->registerSurveillanceUser(username, passwordHash, role, name, imagePath, phoneNumber, email, isActive);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Register surveillance user failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to register user.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
std::string ItemManager::ComputerVision::cvSurv_validateUserPassword(const std::string& username, const std::string& inputPlaintextPassword, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->validateSurveillanceUserPassword(username, inputPlaintextPassword);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Validate surveillance user password failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to validate user password.", ErrorCode::ITEM_NOT_FOUND);
    }
    return "";
}

template<typename T>
std::vector<json> ItemManager::ComputerVision::cvSurv_drainResponses(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->drainSurveillanceResponses();
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Drain surveillance responses failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to drain surveillance responses.", ErrorCode::ITEM_NOT_FOUND);
    }
    return {};
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_updateUserProfile(const std::string& username,
                                                            const std::string& passwordHash,
                                                            const std::string& role,
                                                            const std::string& name,
                                                            const std::string& imagePath,
                                                            const std::string& phoneNumber,
                                                            const std::string& email,
                                                            int isActive,
                                                            std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->updateSurveillanceUserProfile(username, passwordHash, role, name, imagePath, phoneNumber, email, isActive);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Update surveillance user profile failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to update user profile.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_updateUserStatus(const std::string& userId, int activeState, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->updateSurveillanceUserStatus(userId, activeState);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Update surveillance user status failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to update user status.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_updateLastLogin(const std::string& userId, const std::string& timestamp, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->updateSurveillanceLastLogin(userId, timestamp);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Update surveillance last login failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to update last login.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_changeUserPassword(const std::string& userId, const std::string& newPasswordHash, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->changeSurveillanceUserPassword(userId, newPasswordHash);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Change surveillance user password failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to change user password.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_logTelemetry(const std::string& metricType,
                                                       const std::string& nodeIp,
                                                       std::optional<float> cpuUsage,
                                                       std::optional<float> ramUsageMb,
                                                       std::optional<float> diskUsagePercent,
                                                       std::optional<float> temperatureC,
                                                       int numberOfCameras,
                                                       int numberOfActiveCameras,
                                                       int numberOfNonActiveCameras,
                                                       std::optional<float> fps,
                                                       std::optional<int> latencyMs,
                                                       std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->logSurveillanceTelemetry(metricType, nodeIp, cpuUsage, ramUsageMb, diskUsagePercent, temperatureC,
                                              numberOfCameras, numberOfActiveCameras, numberOfNonActiveCameras, fps, latencyMs);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Log surveillance telemetry failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to log telemetry.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_insertDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->insertSurveillanceDailyFaceMetrics(detectionDate, totalDetections, timestamp);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Insert surveillance daily face metrics failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to insert daily face metrics.", ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvSurv_insertChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->insertSurveillanceChatMessage(id, content, senderName, createdAt);
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Insert surveillance chat message failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to insert chat message.", ErrorCode::ITEM_NOT_FOUND);
    }
}






template<typename T>
std::string ItemManager::ComputerVision::cvQRC_scanFromCamera(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            return qr.scanFromCamera();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Scan from camera failed with tag '" + tag + "'.",
                        {});
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to scan from camera.",
                    {});
        return "";
    }
}

template<typename T>
std::string ItemManager::ComputerVision::cvQRC_scanFromFile(const std::string& imagePath, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            return qr.scanFromFile(imagePath);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Scan from file failed with tag '" + tag + "'.",
                        {});
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to scan from file.",
                    {});
        return "";
    }
}

template<typename T>
void ItemManager::ComputerVision::cvQRC_resetConfig(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            qr.resetConfig();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset QR code scanner config failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset QR code scanner config.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
bool ItemManager::ComputerVision::cvQRC_isPreviewEnabled(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            return qr.isPreviewEnabled();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Check preview enabled failed with tag '" + tag + "'.",
                        {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to check preview enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
        return false;
    }
}

template<typename T>
int ItemManager::ComputerVision::cvQRC_getCameraIndex(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            return qr.getCameraIndex();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get camera index failed with tag '" + tag + "'.",
                        {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get camera index.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1;
    }
}

template<typename T>
void ItemManager::ComputerVision::cvQRC_setCameraIndex(int index, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            qr.setCameraIndex(index);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set camera index failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set camera index.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvQRC_setPreviewEnabled(bool enabled, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            typename ItemWrapper<T>::QRCodeScannerWrapper qr(*wrapper);
            qr.setPreviewEnabled(enabled);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set preview enabled failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set preview enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}









template<typename T>
void ItemManager::AlarmManager::triggerAlarm(Event event, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            std::string eventStr = eventToString(event);
            LOG_CONTEXT(LogLevel::INFO, "Triggering alarm: " + eventStr + " for tag '" + tag + "'.", {});
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.triggerAlarm(eventStr);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Trigger alarm failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to trigger alarm.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::AlarmManager::setVolume(int level, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.setVolume(level);
        }
    }
}

template<typename T>
void ItemManager::AlarmManager::setDuration(int seconds, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.setDuration(seconds);
        }
    }
}

template<typename T>
void ItemManager::AlarmManager::setDefaultTone(const std::string& tone, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.setDefaultTone(tone);
        }
    }
}

template<typename T>
void ItemManager::AlarmManager::setTone(const std::string& toneName, const std::string& filePath, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.setTone(toneName, filePath);
        }
    }
}

template<typename T>
void ItemManager::AlarmManager::assignTone(const std::string& event, const std::string& toneName, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.assignTone(event, toneName);
        }
    }
}

template<typename T>
int ItemManager::AlarmManager::getVolume(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            return alarm.getVolume();
        }
    }
    return -1;
}

template<typename T>
int ItemManager::AlarmManager::getDuration(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            return alarm.getDuration();
        }
    }
    return -1;
}

template<typename T>
std::string ItemManager::AlarmManager::getDefaultTone(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            return alarm.getDefaultTone();
        }
    }
    return {};
}

template<typename T>
std::string ItemManager::AlarmManager::getTone(const std::string& toneName, std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            return alarm.getTone(toneName);
        }
    }
    return {};
}

template<typename T>
std::string ItemManager::AlarmManager::getAssignedTone(const std::string& event, std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            return alarm.getAssignedTone(event);
        }
    }
    return {};
}

template<typename T>
void ItemManager::AlarmManager::resetAlarmConfig(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.resetAlarmConfig();
        }
    }
}

template<typename T>
void ItemManager::AlarmManager::playTone(const std::string& toneName, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        if (auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get())) {
            typename ItemWrapper<T>::AlarmSystemWrapper alarm(*wrapper);
            alarm.playTone(toneName);
        }
    }
}


inline std::string ItemManager::AlarmManager::eventToString(Event event) const {
    switch (event) {
        case Event::Intrusion:     return "Intrusion";
        case Event::Door_open:     return "Door_open";
        case Event::Fire:          return "Fire";
        case Event::Access:        return "Access";
        case Event::Warming:       return "Warming";
        case Event::Confirmation:  return "Confirmation";
        default:                   return "Unknown";
    }
}

































    
