

#pragma once

#include "t_wrapper/ItemWrapper.h"
#include <unordered_map>
#include <functional>
#include <stack>
#include <vector>
#include <optional>
#include <map>
#include <iostream>
#include <nlohmann/json.hpp>
#include <fstream>
#include <typeindex>
#include <deque>
#include <queue>
#include <cxxabi.h>
#include <memory>
#include <string>
#include <typeinfo>
#include "versionForMigration/MigrationRegistry.h"
#include <mutex>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#endif

//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::



using json = nlohmann::json;
constexpr size_t MAX_UNDO_HISTORY = 50;
constexpr size_t MAX_REDO_HISTORY = 50;




class ItemManager {
public:
    // State Manager.
    // This is a type alias for the state of the ItemManager, which is a map of item tags to BaseItem pointers.
    // It allows for easy management of the current state, including undo and redo operations.
    using State = std::unordered_map<std::string, std::shared_ptr<BaseItem>>;

private:
    // Main storage.
    // This is a map that stores all items by their tags. The tag is a unique identifier for each item.
    // It allows for quick access to items by their tag, which is useful for operations like
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> items;

    //Queues for managing redo and undo functions.
    std::deque<State> undoHistory; // works like a queue (can trim front)
    std::queue<State> redoQueue;   // replaces redoStack

    //::->       DATA STRUCTURES.
    //****************************************

    // Maps type names to their usage count. This allows tracking how many times each type is used
    // This is useful for optimization and understanding which types are most common in the system
    std::unordered_map<std::string, int> typeUsage;
    
    // Maps item IDs to their BaseItem pointers for fast lookup. This allows quick access to items by their unique ID
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> idMap;

    // Maps for managing type registration and schema. This maps type names to their std::type_index for fast lookup
    std::unordered_map<std::string, std::type_index> registeredTypes;

    // Maps type names to their schema functions. This maps type names to functions that return their schema as json
    std::unordered_map<std::string, std::function<json()>> schemaRegistry;

    // Maps type names to their deserialization functions. This maps type names to functions that deserialize json into BaseItem pointers
    std::unordered_map<std::string, std::function<std::shared_ptr<BaseItem>(const json&, const std::string&)>> deserializers;
    
    // thread-safety gatekeeper
    mutable std::mutex mutex_;

    

    //::->       PRIVATE FUNCTIONS.
    //****************************************

    MigrationRegistry migrationRegistry;
    
    // Helper function to clone the current state of items
    State cloneCurrentState() const;

    //Automatic save for the redo and undo history.
    void saveState();
    
    template<typename T>
    void registerType();

    template<typename T>
    static std::string getCompilerTypeName();

    json getSchemaForType(std::string type) const;

    template<typename T>
    std::shared_ptr<BaseItem> deserializeItemById(const json& j);
        
    
    

   
    //::->       PUBLIC FUNCTIONS.
    //****************************************

public:

    ItemManager() = default;
    ~ItemManager() {
        try {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                items.clear();
                idMap.clear();
                registeredTypes.clear();
                schemaRegistry.clear();
                deserializers.clear();
                typeUsage.clear();
                undoHistory.clear();
                while (!redoQueue.empty()) redoQueue.pop();
            }
        } catch (const std::exception& e) {
            std::cerr << ":::| ERROR during ItemManager cleanup: " << e.what() << "\n";
        }
    }

    void printId()
    {
      std::cout << "\033[1;31m::: Debug: ID:  id  | Item Tag \033[0m\n" << std::endl;
      for (const auto& [id, item] : idMap) {
          std::cout << "\033[1;31m::: Debug: ID: " << id << " | Item Tag: " << item->getTag() << "\033[0m\n";
      }
    }

    void showSignature();

    // Get the compiler type name of a given type T
    // This function uses typeid and demangling to get a human-readable type name.
    std::string demangleType(const std::string& mangledName) const;

     // Display registered deserializers
     // This function displays all registered deserializers in the ItemManager.
     // It iterates through the deserializers map and prints each type name and its corresponding function.
    void displayRegisteredDeserializers();

     // Check if an item with a specific tag exists
    bool hasItem(const std::string& tag) const;

       // Add item with a specific tag
    template<typename T>
    void addItem(std::shared_ptr<T> obj, const std::string& tag);

       // Modify item using a given modifier function
     template<typename T>
     bool modifyItem(const std::string& tag, const std::function<void(T&)>& modifier);

       // Retrieve item by tag
     template<typename T>
     std::optional<T> getItem(const std::string& tag) const;

      // Retrieve raw BaseItem by tag
    template<typename T>
    T& getItemRaw(const std::string& tag);
    
    template<typename T>
    const T& getItemRaw(const std::string& tag) const;

       // Display all items
     void displayAll() const;
    
     void displayByTag(const std::string& tag) const;

       // Remove item by tag
    void removeByTag(const std::string& tag);

       // Undo and redo state changes
     void undo();

     void redo();

       // void importFromFile(const std::string& filename);
     void importFromFile_Json(const std::string& filename);

        // Asynchronously import items from a JSON file
     void asyncImportFromFile_Json(const std::string& filename);

        // Import a single object from a JSON file
     void exportToFile_Json(const std::string& filename) const;

        // Asynchronously export items to a JSON file
     void asyncExportToFile_Json(const std::string& filename) const;

        // Import items from a JSON file
     std::shared_ptr<BaseItem> importSingleObject_Json(const std::string& filename, const std::string& type, const std::string& tag);

        // Asynchronously import a single object from a JSON file
     void asyncImportSingleObject_Json(const std::string& filename, const std::string& typeName, const std::string& tag);

        // Export items to a binary file
     bool exportToFile_Binary(const std::string& filename) const;

        // Asynchronously export items to a binary file
     void asyncExportToFile_Binary(const std::string& filename) const;

        // Import items from a binary file
     bool importFromFile_Binary(const std::string& filename);

        // Asynchronously import items from a binary file
     void asyncImportFromFile_Binary(const std::string& filename);

        // Import a single object from a binary file
     std::shared_ptr<BaseItem> importSingleObject_Binary(const std::string& filename, const std::string& type, const std::string& tag);

        // Asynchronously import a single object from a binary file
     void asyncImportSingleObject_Binary(const std::string& filename, const std::string& typeName, const std::string& tag);

        // Export items to an XML file
     bool exportToFile_XML(const std::string& filename) const;

        // Asynchronously export items to an XML file
     void asyncExportToFile_XML(const std::string& filename) const;

        // Import items from an XML file
     bool importFromFile_XML(const std::string& filename);

        // Asynchronously import items from an XML file
     void asyncImportFromFile_XML(const std::string& filename);

        // Import a single object from an XML file
     std::optional<std::shared_ptr<BaseItem>> importSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag);

        // Asynchronously import a single object from an XML file
     void asyncImportSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag);

        // Export items to a CSV file
     bool exportToFile_CSV(const std::string& filename) const;

        // Asynchronously export items to a CSV file
     void asyncExportToFile_CSV(const std::string& filename) const;

        // Import items from a CSV file
     bool importFromFile_CSV(const std::string& filename);

        // Asynchronously import items from a CSV file
     void asyncImportFromFile_CSV(const std::string& filename);

        // Import a single object from a CSV file
     std::shared_ptr<BaseItem> importSingleObject_CSV(const std::string& filename, const std::string& type, const std::string& tag);

        // Asynchronously import a single object from a CSV file
     void asyncImportSingleObject_CSV(const std::string& filename, const std::string& type, const std::string& tag);

       // Register a type for serialization and deserialization
     void listRegisteredTypes() const;

       //Filter items by tags.
    void filterByTag(const std::vector<std::string>& tags) const;

      // Sort items by tag
    void sortItemsByTag() const;

      // Display all class names of items
    void displayAllClasses() const;

      // Get the current state of items
    const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& getItemMapStore() const;

};
#include "ItemManager.tpp"



class GlobalItemManager {
    private:
        // Private constructor to prevent direct instantiation
        GlobalItemManager() : itemManager(std::make_unique<ItemManager>()) {}
    
        // Pointer to the ItemManager instance
        std::unique_ptr<ItemManager> itemManager;
    
    public:
        // Delete copy constructor and assignment operator to enforce singleton behavior
        GlobalItemManager(const GlobalItemManager&) = delete;
        GlobalItemManager& operator=(const GlobalItemManager&) = delete;
    
        // Static method to access the singleton instance
        static GlobalItemManager& getInstance() {
            static GlobalItemManager instance; // Guaranteed to be initialized once
            return instance;
        }
    
        // Public method to access the ItemManager instance
        ItemManager& getItemManager() {
            return *itemManager;
        }
    
        // Example method to reset the ItemManager instance
        void resetItemManager() {
            itemManager = std::make_unique<ItemManager>();
            std::cout << "::: Debug: ItemManager instance reset.\n";
        }
    };
