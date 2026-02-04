
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
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.", {});
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
            LOG_CONTEXT(LogLevel::WARNING, "Unknown type: " + parent.demangleType(typeName) + " — skipping.", {});
            continue;
        }

        auto desIt = parent.deserializers.find(typeName);
        if (desIt == parent.deserializers.end()) {
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + parent.demangleType(typeName) + " — skipping.", {});
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
                LOG_CONTEXT(LogLevel::WARNING, "Unknown type: " + parent.demangleType(typeName) + " — skipping.", {});
                return nullptr;
            }

            auto desIt = parent.deserializers.find(typeName);
            if (desIt == parent.deserializers.end()) {
                LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + parent.demangleType(typeName) + " — skipping.", {});
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
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type: " + type + " — skipping.", {});
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
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.", {});
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
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty — cannot proceed with XML import.", false);
        return false;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting XML import from file: " + filename, {});

    tinyxml2::XMLDocument doc;
    tinyxml2::XMLError result = doc.LoadFile(filename.c_str());
    if (result != tinyxml2::XML_SUCCESS) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to read XML file '" + filename + "' — error code: " + std::to_string(result), false);
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
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type '" + parent.demangleType(typeName) + "' — skipping item with tag '" + tag + "'", {});
            continue;
        }

        try {
            auto item = it->second(upgraded, tag);
            if (item) {
                parent.items[tag] = item;
                LOG_CONTEXT(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from XML.", {});
                loadedCount++;
            } else {
                LOG_CONTEXT(LogLevel::ERR, "Deserializer returned null for tag '" + tag + "' — skipping.", {});
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
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty — cannot import from XML.", {});
        return std::nullopt;
    }

    LOG_CONTEXT(LogLevel::INFO, "Attempting to import single XML object from file: " + filename + 
                                        " with type '" + parent.demangleType(type) + "' and tag '" + tag + "'", {});

    if (filename.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty — cannot import from XML.", {});
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
                                                            + "' — cannot import item with tag '" + tag + "'", {});
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
            LOG_CONTEXT(LogLevel::ERR, "Null item found for tag: " + tag + " — skipping.", {});
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
            LOG_CONTEXT(LogLevel::WARNING, "Malformed CSV row: '" + line + "' — skipping.", {});
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
            LOG_CONTEXT(LogLevel::WARNING, "No deserializer registered for type '" + parent.demangleType(type) + "' — skipping item with tag '" + tag + "'", {});
            continue;
        }

        try {
            auto item = it->second(j, tag);
            if (item) {
                parent.items[tag] = item;
                loadedCount++;
                LOG_CONTEXT(LogLevel::INFO, "Successfully imported item with tag '" + tag + "' from CSV.", {});
            } else {
                LOG_CONTEXT(LogLevel::WARNING, "Deserializer returned null for tag '" + tag + "' — skipping.", {});
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
        LOG_CONTEXT(LogLevel::ERR, "Filename is empty — cannot proceed with CSV import.", {});
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
                                                    "' — cannot import item with tag '" + parent.demangleType(tagIn) + "'", {});
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









template<typename T>
bool ItemManager::NetworkManager::networkMessage_Send(std::string& msg, std::string& tag) {
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->sendMessage(msg, "");
            return true;
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Network message failed with tag '" + tag + "'. Requested type: " 
                        + parent.demangleType(typeid(T).name()) + ", Actual type: " + parent.demangleType(it->second->getTypeName()), {});
            throw std::runtime_error("\n:::| Please check your item type.\n");
        }
        
    }else{
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to send network message.", ErrorCode::FLAG_FALSE);
        return false;
    }
}

template<typename T>
bool ItemManager::NetworkManager::networkMessage_Receive(Message msg, std::string& tag){
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->receiveMessage(msg);
            return true;
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "Network message failed with tag '" + tag + "'. Requested type: " 
                        + parent.demangleType(typeid(T).name()) + ", Actual type: " + parent.demangleType(it->second->getTypeName()), {});
            throw std::runtime_error("\n:::| Please check your item type.\n");
        }
        
    }else{
        LOG_CONTEXT(LogLevel::ERR, "No item found with tag '" + tag + "' to send network message.", ErrorCode::FLAG_FALSE);
        return false;
    }
}








template<typename T>
void ItemManager::ComputerVision::cvFgn_runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->runRestrictedAreaMonitor(cameraIndex, cascadePath);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Run restricted area failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        {});
            throw std::runtime_error("\n:::| Please check your item type.\n");
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to Run restricted area.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
const std::unordered_map<int, FaceTrack>& ItemManager::ComputerVision::cvFgn_getRunRestrictedTracks(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    static std::unordered_map<int, FaceTrack> emptyMap; // Return an empty map if no items found
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getRunRestrictedTracks();
        }
    }
    LOG_CONTEXT(LogLevel::WARNING, "No FaceRecognitionItem found to get restricted tracks.", {});
    return emptyMap;
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_addRunRestrictedTrack(int id, const FaceTrack& track, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->addRunRestictedTrack(id, track);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Add restricted track failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to add restricted track.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_removeRunRestrictedTrack(int id, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->removeRunRestrictedTrack(id);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Remove restricted track failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to remove restricted track.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_resetRunRestrictedConfig(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->restRunstrictedConfig();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset restricted configuration failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset restricted configuration.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_setRunRestrictedScaleFactor(double scaleFactor, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->setRunRestrictedScaleFactor(scaleFactor);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set restricted scale factor failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set restricted scale factor.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_setRunRestrictedMinNeighbors(int minNeighbors, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->setRunRestrictedMinNeighbors(minNeighbors);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set restricted min neighbors failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set restricted min neighbors.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_setRunRestrictedMinFaceSize(const cv::Size& size, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->setRunRestrictedMinFaceSize(size);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set restricted min face size failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set restricted min face size.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFgn_setIouMatchThreshold(double threshold, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->setIouMatchThreshold(threshold);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set IOU match threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set IOU match threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
double ItemManager::ComputerVision::cvFgn_getRunRestrictedScaleFactor( std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            return wrapper->getRunRestrictedScaleFactor();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get restricted scale factor failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get restricted scale factor.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0.0;
    }
}

template<typename T>
int ItemManager::ComputerVision::cvFgn_getRunRestrictedMinNeighbors( std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            return wrapper->getRunRestrictedMinNeighbors();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get restricted min neighbors failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get restricted min neighbors.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0;
    }
}

template<typename T>
cv::Size ItemManager::ComputerVision::cvFgn_getRunRestrictedMinFaceSize( std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            return wrapper->getRunRestrictedMinFaceSize();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get restricted min face size failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                      "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get restricted min face size.",
                    ErrorCode::ITEM_NOT_FOUND);
        return cv::Size();
    }
}

template<typename T>
double ItemManager::ComputerVision::cvFgn_getIouMatchThreshold( std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            return wrapper->getIouMatchThreshold();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get IOU match threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get IOU match threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0.0;
    }
}
    







template<typename T>
void ItemManager::ComputerVision::cvFwl_monitorCamera(const std::string& cascadePath, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);

    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->monitorCamera(cascadePath);
        }else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "monitor camera function failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));   
        }    
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get monitor camera threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
double ItemManager::ComputerVision::cvFwl_getMonitorCameraThreshold(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            return wrapper->getMonitorCameraThreshold();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get monitor camera threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get monitor camera threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
        return -1.0;
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_setMonitorCameraThreshold(double newThreshold, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard
    
    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->setMonitorCameraThreshold(newThreshold);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor camera threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set monitor camera threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_resetMonitorCameraThreshold(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if(it != parent.items.end()){
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if(wrapper){
            wrapper->resetMonitorCameraThreshold();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset monitor camera threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset monitor camera threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
std::string ItemManager::ComputerVision::cvFwl_getMonitorCameraCascadePath(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getMonitorCameraCascadePath();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get monitor camera cascade path failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get monitor camera cascade path.",
                    ErrorCode::ITEM_NOT_FOUND);
        return "";
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_setMonitorCameracascadePath(const std::string& path, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorCameraCascadePath(path);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor camera cascade path failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set monitor camera cascade path.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_setMonitorCameraCascadePath(const std::string& path, std::string& tag) { 
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorCameraCascadePath(path);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor camera cascade path failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set monitor camera cascade path.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_loadKnownFaces(const std::vector<std::string>& filePaths, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->loadKnownFaces(filePaths);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Load known faces failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to load known faces.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
size_t ItemManager::ComputerVision::cvFwl_getKnownFaceCount(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getKnownFaceCount();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Get known face count failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to get known face count.",
                    ErrorCode::ITEM_NOT_FOUND);
        return 0;
    }
}

template<typename T>
void ItemManager::ComputerVision::cvFwl_resetKnownFaces(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetKnownFaces();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset known faces failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                                        "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset known faces.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}








template<typename T>
void ItemManager::ComputerVision::cvMdn_setMonitorDetectionMinArea(int minArea, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorDetectionMinArea(minArea);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor detection min area failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set min area.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_setMonitorDetectionDiffThreshold(double threshold, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorDetectionDiffThreshold(threshold);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor detection diff threshold failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set diff threshold.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_setMonitorDetectionLoiterSeconds(int loiterSeconds, std::string& tag) {
    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorDetectionLoiterSeconds(loiterSeconds);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor detection loiter seconds failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set loiter seconds.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_setMonitorDetectionConfig(double diffThreshold,
                                                int minArea,
                                                std::size_t crowdThreshold,
                                                int loiterSeconds,
                                                int leftBehindSeconds,
                                                bool enableTracking,
                                                std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorDetectionConfig(diffThreshold,
                                            minArea,
                                            crowdThreshold,
                                            loiterSeconds,
                                            leftBehindSeconds,
                                            enableTracking);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor detection config failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set monitor detection config.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_setMonitorDetectionAlertCallback(std::function<void(const std::string&)> cb,
                                                        std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setMonitorDetectionAlertCallback(cb);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Set monitor detection alert callback failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to set alert callback.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_resetMonitorDetectionConfig(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetMonitorDetectionConfig();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset monitor detection config failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset monitor detection config.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_resetMonitorDetection(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetMonitorDetection();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Reset monitor detection failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to reset monitor detection.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
void ItemManager::ComputerVision::cvMdn_clearMonitorDetectionZones(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->clearMonitorDetectionZones();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Clear monitor detection zones failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to clear detection zones.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

template<typename T>
bool ItemManager::ComputerVision::cvMdn_isMonitorDetectionTrackingEnabled(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->isMonitorDetectionTrackingEnabled();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Check tracking enabled failed with tag '" + tag + "'.",
                        {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to check tracking enabled.",
                    ErrorCode::ITEM_NOT_FOUND);
        return false;
    }
}

template<typename T>
bool ItemManager::ComputerVision::cvMdn_hasMonitorDetectionCallback(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->hasMonitorDetectionCallback();
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Check monitor detection callback failed with tag '" + tag + "'.",
                        {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to check callback.",
                    ErrorCode::ITEM_NOT_FOUND);
        return false;
    }
}

template<typename T>
double ItemManager::ComputerVision::cvMdn_getMonitorDetectionDiffThreshold(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) return wrapper->getMonitorDetectionDiffThreshold();
    }
    LOG_CONTEXT(LogLevel::ERR, "Failed to get diff threshold for tag '" + tag + "'.", ErrorCode::ITEM_NOT_FOUND);
    return 0.0;
}

template<typename T>
int ItemManager::ComputerVision::cvMdn_getMonitorDetectionMinArea(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) return wrapper->getMonitorDetectionMinArea();
    }
    LOG_CONTEXT(LogLevel::ERR, "Failed to get min area for tag '" + tag + "'.", ErrorCode::ITEM_NOT_FOUND);
    return 0;
}

template<typename T>
std::size_t ItemManager::ComputerVision::cvMdn_getMonitorDetectionCrowdThreshold(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) return wrapper->getMonitorDetectionCrowdThreshold();
    }
    LOG_CONTEXT(LogLevel::ERR, "Failed to get crowd threshold for tag '" + tag + "'.", ErrorCode::ITEM_NOT_FOUND);
    return 0;
}

template<typename T>
int ItemManager::ComputerVision::cvMdn_getMonitorDetectionLoiterSeconds(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) return wrapper->getMonitorDetectionLoiterSeconds();
    }
    LOG_CONTEXT(LogLevel::ERR, "Failed to get loiter seconds for tag '" + tag + "'.", ErrorCode::ITEM_NOT_FOUND);
    return 0;
}

template<typename T>
int ItemManager::ComputerVision::cvMdn_getMonitorDetectionLeftBehindSeconds(std::string& tag) const {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) return wrapper->getMonitorDetectionLeftBehindSeconds();
    }
    LOG_CONTEXT(LogLevel::ERR, "Failed to get left-behind seconds for tag '" + tag + "'.", ErrorCode::ITEM_NOT_FOUND);
    return 0;
}

// ItemManager.tpp
template<typename T>
void ItemManager::ComputerVision::cvMdn_addMonitorDetectionZone(const Zone& zone, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->addMonitorDetectionZone(zone);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Add monitor detection zone failed with tag '" + tag +
                        "'. Requested type: " + parent.demangleType(typeid(T).name()) +
                        ", Actual type: " + parent.demangleType(it->second->getTypeName()),
                        std::make_exception_ptr(std::runtime_error(
                            "\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to add monitor detection zone.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}

// ItemManager.h
template<typename T>
void ItemManager::ComputerVision::cvMdn_runMonitorDetectionMonitorCamera(int cameraIndex, std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->runMonitorDetectionMonitorCamera(cameraIndex);
        } else {
            LOG_CONTEXT(LogLevel::WARNING,
                        "Run monitor detection camera failed with tag '" + tag + "'.",
                        std::make_exception_ptr(std::runtime_error("\n:::| Please check your item type.\n")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "No item found with tag '" + tag + "' to run monitor detection camera.",
                    ErrorCode::ITEM_NOT_FOUND);
    }
}







template<typename T>
std::string ItemManager::ComputerVision::cvQRC_scanFromCamera(std::string& tag) {
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->scanFromCamera();
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->scanFromFile(imagePath);
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->resetConfig();
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->isPreviewEnabled();
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            return wrapper->getCameraIndex();
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setCameraIndex(index);
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            wrapper->setPreviewEnabled(enabled);
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
    std::lock_guard<std::mutex> lock(parent.mutex_);  // Thread guard

    auto it = parent.items.find(tag);
    if (it != parent.items.end()) {
        auto wrapper = dynamic_cast<ItemWrapper<T>*>(it->second.get());
        if (wrapper) {
            std::string eventStr = eventToString(event);
            // Log with string 
            LOG_CONTEXT(LogLevel::INFO, "Triggering alarm: " + eventStr + " for tag '" + tag + "'.", {});
            wrapper->triggerAlarm(eventStr);
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

































    
