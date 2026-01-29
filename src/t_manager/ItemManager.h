

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
//#include <cxxabi.h> already defined below
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
constexpr size_t MAX_UNDO_HISTORY = 50; /**< Maximum undo history states to retain */
constexpr size_t MAX_REDO_HISTORY = 50; /**< Maximum redo history states to retain */

   /**
     * @class ItemManager
     * @brief Central manager for storing, serializing, and managing items in the Smart_Store framework.
     * 
     * ItemManager is the core component that provides:
     * - **State Management**: Stores items indexed by tags with undo/redo functionality
     * - **Type Registration**: Maintains type registry for serialization/deserialization
     * - **Persistence**: Supports multiple formats (JSON, XML, Binary, CSV)
     * - **Thread Safety**: Uses mutex for concurrent access protection
     * - **Async Operations**: Provides asynchronous file I/O methods
     * 
     * @details
     * ItemManager uses a sophisticated internal architecture:
     * 
     * **Storage Structures:**
     * - @ref items: Main item storage by tag (unordered_map)
     * - @ref idMap: Fast lookup by unique ID
     * - @ref typeUsage: Tracks type frequency statistics
     * - @ref undoHistory: Deque of previous states (limited to MAX_UNDO_HISTORY)
     * - @ref redoQueue: Queue for redo operations
     * 
     * **Type System:**
     * - @ref registeredTypes: Maps type names to type_index
     * - @ref schemaRegistry: Functions returning JSON schema for types
     * - @ref deserializers: Functions reconstructing items from JSON
     * 
     * **Thread Safety:**
     * All public methods are thread-safe via @ref mutex_. Internal state access
     * is protected with std::lock_guard.
     * 
     * **Undo/Redo System:**
     * - Maintains complete state snapshots in undoHistory deque
     * - Limited to MAX_UNDO_HISTORY (default 50) to control memory
     * - redoQueue tracks undone states for redo operations
     * - saveState() automatically called after modifications
     * 
     * **Serialization Support:**
     * - JSON: Native support via nlohmann/json
     * - XML: Requires tinyxml2 library integration
     * - Binary: Custom binary format
     * - CSV: Tabular export format
     * 
     * @tparam <none> Non-template class
     * 
     * @note All item tags must be unique within an ItemManager instance
     * @note Items are always managed via shared_ptr<BaseItem>
     * @note Type T must be wrapped in ItemWrapper before storage
     * 
     * @author Victor
     * @date 2025
     * @see ItemWrapper
     * @see BaseItem
     * @see GlobalItemManager
     */
class ItemManager
{
public:
    /**
     * @typedef State
     * @brief Type alias for a complete ItemManager state snapshot.
     * 
     * Represents all items at a specific point in time, used for
     * undo/redo operations. Maps user-defined tags to shared pointers
     * of BaseItem objects.
     */
    using State = std::unordered_map<std::string, std::shared_ptr<BaseItem>>;

private:
   /**
     * @brief Main storage container for all items.
     * 
     * Key: User-defined tag (unique identifier within this manager)
     * Value: Shared pointer to the wrapped item (BaseItem)
     * 
     * Uses unordered_map for O(1) average lookup by tag.
     */
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> items;

   /**
     * @brief History deque for undo operations.
     * 
     * Stores complete state snapshots as a deque (not stack) to allow
     * trimming from the front when exceeding MAX_UNDO_HISTORY.
     * Works LIFO (Last In, First Out) semantically.
     * 
     * @note Limited to MAX_UNDO_HISTORY entries (default 50)
     * @note Oldest states are removed when limit exceeded
     */
    std::deque<State> undoHistory;

    /**
     * @brief Queue for managing redo operations.
     * 
     * Stores states that have been undone, allowing them to be
     * restored via redo(). When new modifications occur, redoQueue is cleared.
     * 
     * @note Replaces traditional redo stack with queue for FIFO semantics
     */
    std::queue<State> redoQueue;

    /**
     * @brief Maps type names to their usage frequency.
     * 
     * Tracks how many items of each type exist in the manager.
     * Useful for optimization analysis and understanding data distribution.
     * 
     * Key: Demangled type name (e.g., "int", "User")
     * Value: Number of items of that type
     */
    std::unordered_map<std::string, int> typeUsage;
    
    /**
     * @brief Maps unique item IDs to their corresponding BaseItem pointers.
     * 
     * Provides fast O(1) lookup of items by their system-generated ID,
     * complementing the tag-based items map.
     * 
     * Key: Unique ID generated by IdProvider
     * Value: Shared pointer to the item
     * 
     * @note Used internally for ID-based operations
     * @see ItemWrapper::id_
     */
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> idMap;

    /**
     * @brief Maps type names to their std::type_index for identification.
     * 
     * Enables runtime type checking and registration verification.
     * 
     * Key: Demangled type name
     * Value: std::type_index for the registered type
     */
    std::unordered_map<std::string, std::type_index> registeredTypes;

    /**
     * @brief Maps type names to schema-generating functions.
     * 
     * Each registered type can provide a JSON schema describing its structure.
     * Useful for validation and documentation generation.
     * 
     * Key: Type name
     * Value: Function returning JSON schema
     */
    std::unordered_map<std::string, std::function<json()>> schemaRegistry;

    /**
     * @brief Maps type names to deserialization functions.
     * 
     * Stores lambdas/functions capable of reconstructing items
     * of specific types from JSON representations.
     * 
     * Key: Type name
     * Value: Function taking JSON and tag, returning shared_ptr<BaseItem>
     */
    std::unordered_map<std::string, std::function<std::shared_ptr<BaseItem>(const json&, const std::string&)>> deserializers;
    
    // thread-safety gatekeeper
    mutable std::mutex mutex_;

    

    //::->       PRIVATE FUNCTIONS.
    //****************************************

    /**
     * @brief Registry for schema version migration functions.
     * 
     * Handles version compatibility and automatic migration of items
     * when loading from older file formats.
     */
    MigrationRegistry migrationRegistry;
    
    /**
     * @brief Creates a deep copy of the current state.
     * 
     * Clones all items to create an independent state snapshot
     * used for undo/redo functionality.
     * 
     * @return State A new State containing clones of all current items
     * @note Uses BaseItem::clone() for each item
     */
    State cloneCurrentState() const;

     /**
     * @brief Saves current state to undo history.
     * 
     * Creates a state snapshot and adds it to undoHistory.
     * Automatically called after any modification.
     * 
     * Maintains the MAX_UNDO_HISTORY limit by removing oldest states
     * when necessary.
     * 
     * @return void
     * @note Clears redoQueue when new modifications occur
     */
    void saveState();

    /**
     * @brief Registers a type T for serialization/deserialization.
     * 
     * Sets up infrastructure for handling type T in the ItemManager:
     * - Records type name and type_index
     * - Registers serialization schema
     * - Registers deserialization function
     * 
     * @tparam T The type to register
     * 
     * @note Must be called before storing items of type T
     * @note Called automatically by addItem() if needed
     * @see deserializeItemById()
     */
    template<typename T>
    void registerType();

    /**
     * @brief Retrieves the compiler-specific type name for type T.
     * 
     * Uses typeid().name() and demangling to get a human-readable name.
     * 
     * @tparam T The type to get the name for
     * 
     * @return std::string The demangled type name (e.g., "int", "MyClass")
     * 
     * @note Static method - can be called without an instance
     * @note Platform-dependent implementation
     */
    template<typename T>
    static std::string getCompilerTypeName();

     /**
     * @brief Retrieves the JSON schema for a specific type.
     * 
     * Looks up the registered schema function for a type name
     * and returns the schema JSON.
     * 
     * @param type The demangled type name
     * 
     * @return json The JSON schema for the type
     * @throw Throws if type not registered
     * @note Used for validation and documentation
     */
    json getSchemaForType(std::string type) const;

    /**
     * @brief Deserializes an item from JSON by its ID.
     * 
     * Reconstructs an item of type T from JSON representation,
     * using the item's unique ID.
     * 
     * @tparam T The type of item to deserialize
     * @param j The JSON object containing serialized data
     * 
     * @return std::shared_ptr<BaseItem> Pointer to deserialized item
     * @note Internal method used by import functions
     */
    template<typename T>
    std::shared_ptr<BaseItem> deserializeItemById(const json& j);
        
    
    

   
    //::->       PUBLIC FUNCTIONS.
    //****************************************

public:

    ItemManager() = default;

    /**
     * @brief Destructor - cleans up all stored items and internal structures.
     * 
     * Safely deallocates all items, maps, and queues. Ensures proper
     * cleanup during ItemManager shutdown, catching and logging any exceptions.
     * 
     * @note Thread-safe - uses lock_guard during cleanup
     */
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

     /**
     * @brief Retrieves the human-readable type name from a compiler mangled name.
     * 
     * Converts mangled type names (from typeid) into readable format:
     * - GCC/Clang: Uses abi::__cxa_demangle()
     * - MSVC: Returns type name as-is
     * - Other: Returns "Unknown compiler"
     * 
     * @param mangledName The compiler-generated mangled type name
     * 
     * @return std::string The demangled type name
     * @note Public utility for name demangling
     */
    std::string demangleType(const std::string& mangledName) const;

    /**
     * @brief Displays all currently registered deserializer functions.
     * 
     * Outputs a list of all types that have deserializers registered,
     * useful for debugging serialization setup.
     * 
     * @return void
     * @note Primarily for diagnostic/logging purposes
     */
    void displayRegisteredDeserializers();

      /**
     * @brief Checks if an item with the specified tag exists.
     * 
     * @param tag The user-defined tag to search for
     * 
     * @return bool True if an item with this tag exists, false otherwise
     * @note O(1) average time complexity
     */
    bool hasItem(const std::string& tag) const;

      /**
     * @brief Adds a new item with specified tag to the manager.
     * 
     * Stores a shared pointer to an object of type T (wrapped in ItemWrapper)
     * and indexes it by the given tag. Automatically registers the type if needed.
     * Creates a state snapshot for undo functionality.
     * 
     * @tparam T The type of the object being added
     * @param obj Shared pointer to the object of type T
     * @param tag User-defined unique identifier for this item
     * 
     * @return void
     * @throw Throws if tag already exists
     * @note Thread-safe - uses mutex protection
     * @note Automatically calls saveState() to enable undo
     * @note If T not yet registered, registerType<T>() is called
     * @see removeByTag()
     */
    template<typename T>
    void addItem(std::shared_ptr<T> obj, const std::string& tag);

       /**
     * @brief Modifies an existing item using a provided function.
     * 
     * Retrieves the item by tag and applies a modifier function to it.
     * Useful for atomic updates without fetching and re-storing.
     * 
     * @tparam T The expected type of the item
     * @param tag The tag of the item to modify
     * @param modifier Function taking T& as parameter and applying changes
     * 
     * @return bool True if modification succeeded, false if item not found or wrong type
     * @note Thread-safe
     * @note Automatically saves state for undo
     * @note Type mismatch silently returns false
     */
     template<typename T>
     bool modifyItem(const std::string& tag, const std::function<void(T&)>& modifier);

         /**
     * @brief Retrieves a copy of an item by tag.
     * 
     * Returns a copy (not reference) of the stored item if it exists
     * and matches type T.
     * 
     * @tparam T The expected type of the item
     * @param tag The tag of the item to retrieve
     * 
     * @return std::optional<T> Contains the item copy if found, std::nullopt otherwise
     * @note Returns a copy, not a reference - modifications don't affect stored item
     * @note Thread-safe
     * @note Type mismatch returns std::nullopt
     */
     template<typename T>
     std::optional<T> getItem(const std::string& tag) const;

      /**
     * @brief Retrieves a mutable reference to an item by tag.
     * 
     * Provides direct mutable access to the stored item for efficient updates.
     * 
     * @tparam T The expected type of the item
     * @param tag The tag of the item to retrieve
     * 
     * @return T& Mutable reference to the stored item
     * @throw Throws std::runtime_error if tag not found or type mismatch
     * @note Changes to reference immediately affect stored state
     * @note Thread-safe - though only recommended for single-threaded scenarios
     */
    template<typename T>
    T& getItemRaw(const std::string& tag);

    /**
     * @brief Retrieves a const reference to an item by tag.
     * 
     * Provides direct const (read-only) access to the stored item.
     * 
     * @tparam T The expected type of the item
     * @param tag The tag of the item to retrieve
     * 
     * @return const T& Const reference to the stored item
     * @throw Throws std::runtime_error if tag not found or type mismatch
     * @note Changes cannot be made through this reference
     */
    template<typename T>
    const T& getItemRaw(const std::string& tag) const;

    /**
     * @brief Displays all items currently stored in the manager.
     * 
     * Iterates through all items and calls display() on each,
     * outputting their contents to standard output.
     * 
     * @return void
     * @note Format depends on BaseItem::display() implementation in each type
     */
     void displayAll() const;

    /**
     * @brief Displays a specific item by tag.
     * 
     * Shows the content of a single item identified by tag.
     * 
     * @param tag The tag of the item to display
     * 
     * @return void
     * @note Outputs nothing if tag not found
     */
     void displayByTag(const std::string& tag) const;

      /**
     * @brief Removes an item by its tag.
     * 
     * Deletes the item with the specified tag from storage.
     * Updates all internal indexes (idMap, typeUsage).
     * 
     * @param tag The tag of the item to remove
     * 
     * @return void
     * @note Thread-safe
     * @note Automatically saves state for undo
     * @note Silent if tag not found
     */
    void removeByTag(const std::string& tag);

       // Undo and redo state changes
     void undo();

     void redo();

        /**
     * @brief Imports all items from a JSON file.
     * 
     * Loads a previously exported JSON file containing multiple items
     * and adds them to the manager. Items must have unique tags.
     * 
     * @param filename Path to the JSON file to import
     * 
     * @return void
     * @throw Throws if file cannot be opened or JSON is malformed
     * @note Thread-safe
     * @note Clears existing items if needed
     * @see exportToFile_Json()
     */
     void importFromFile_Json(const std::string& filename);

       /**
     * @brief Asynchronously imports items from a JSON file.
     * 
     * Launches a background thread to load items from a JSON file,
     * allowing the main thread to continue execution.
     * 
     * @param filename Path to the JSON file to import
     * 
     * @return void
     * @note Asynchronous - returns immediately
     * @note Thread-safe via background thread
     * @see importFromFile_Json()
     */
     void asyncImportFromFile_Json(const std::string& filename);

         /**
     * @brief Exports all items to a JSON file.
     * 
     * Serializes all stored items to JSON and writes to file.
     * Creates a human-readable JSON representation.
     * 
     * @param filename Path to the output JSON file
     * 
     * @return void
     * @throw Throws if file cannot be created/written
     * @note Overwrites existing file
     * @see importFromFile_Json()
     */
     void exportToFile_Json(const std::string& filename) const;

     /**
     * @brief Asynchronously exports items to a JSON file.
     * 
     * Launches a background thread to export items to JSON format,
     * non-blocking operation.
     * 
     * @param filename Path to the output JSON file
     * 
     * @return void
     * @note Asynchronous - returns immediately
     * @see exportToFile_Json()
     */
     void asyncExportToFile_Json(const std::string& filename) const;

    /**
     * @brief Imports a single item from a JSON file.
     * 
     * Loads a specific object of given type from a JSON file with specified tag.
     * 
     * @param filename Path to the JSON file
     * @param type The demangled type name of the item
     * @param tag The tag to assign to the imported item
     * 
     * @return std::shared_ptr<BaseItem> Pointer to the imported item
     * @throw Throws if file/type/item not found
     * @note Adds the item to the manager
     */
     std::shared_ptr<BaseItem> importSingleObject_Json(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Asynchronously imports a single item from JSON.
     * 
     * @param filename Path to the JSON file
     * @param typeName The demangled type name
     * @param tag The tag for the imported item
     * 
     * @return void
     * @note Asynchronous operation
     */
     void asyncImportSingleObject_Json(const std::string& filename, const std::string& typeName, const std::string& tag);

    /**
     * @brief Exports all items to a binary file.
     * 
     * Serializes items in a compact binary format for space-efficient storage.
     * 
     * @param filename Path to the output binary file
     * 
     * @return bool True if export succeeded, false otherwise
     * @note Binary format is not human-readable
     * @see importFromFile_Binary()
     */
     bool exportToFile_Binary(const std::string& filename) const;

    /**
     * @brief Asynchronously exports items to binary format.
     * 
     * @param filename Path to the output binary file
     * @return void
     * @note Asynchronous operation
     */
     void asyncExportToFile_Binary(const std::string& filename) const;

    /**
     * @brief Imports items from a binary file.
     * 
     * Loads items previously exported in binary format.
     * 
     * @param filename Path to the binary file to import
     * 
     * @return bool True if import succeeded, false otherwise
     * @see exportToFile_Binary()
     */
     bool importFromFile_Binary(const std::string& filename);

    /**
     * @brief Asynchronously imports items from binary format.
     * 
     * @param filename Path to the binary file
     * @return void
     * @note Asynchronous operation
     */
     void asyncImportFromFile_Binary(const std::string& filename);

    /**
     * @brief Imports a single item from a binary file.
     * 
     * @param filename Path to the binary file
     * @param type The demangled type name
     * @param tag The tag to assign
     * 
     * @return std::shared_ptr<BaseItem> Pointer to imported item
     */
     std::shared_ptr<BaseItem> importSingleObject_Binary(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Asynchronously imports a single binary object.
     * 
     * @param filename Path to the binary file
     * @param typeName The type name
     * @param tag The item tag
     * @return void
     */
     void asyncImportSingleObject_Binary(const std::string& filename, const std::string& typeName, const std::string& tag);

    /**
     * @brief Exports items to an XML file.
     * 
     * Serializes items in XML format (requires tinyxml2 library).
     * 
     * @param filename Path to the output XML file
     * 
     * @return bool True if export succeeded, false otherwise
     * @note Requires tinyxml2 integration
     * @see importFromFile_XML()
     */
     bool exportToFile_XML(const std::string& filename) const;

    /**
     * @brief Asynchronously exports items to XML format.
     * 
     * @param filename Path to the output XML file
     * @return void
     */
     void asyncExportToFile_XML(const std::string& filename) const;

    /**
     * @brief Imports items from an XML file.
     * 
     * Loads items previously exported in XML format.
     * 
     * @param filename Path to the XML file to import
     * 
     * @return bool True if import succeeded, false otherwise
     * @see exportToFile_XML()
     */
     bool importFromFile_XML(const std::string& filename);

    /**
     * @brief Asynchronously imports items from XML format.
     * 
     * @param filename Path to the XML file
     * @return void
     */
     void asyncImportFromFile_XML(const std::string& filename);

    /**
     * @brief Imports a single item from an XML file.
     * 
     * @param filename Path to the XML file
     * @param type The demangled type name
     * @param tag The item tag
     * 
     * @return std::optional<std::shared_ptr<BaseItem>> Item if found, nullopt otherwise
     */
     std::optional<std::shared_ptr<BaseItem>> importSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Asynchronously imports a single XML object.
     * 
     * @param filename Path to the XML file
     * @param type The type name
     * @param tag The item tag
     * @return void
     */
     void asyncImportSingleObject_XML(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Exports items to a CSV file.
     * 
     * Exports items in comma-separated values format for spreadsheet applications.
     * 
     * @param filename Path to the output CSV file
     * 
     * @return bool True if export succeeded, false otherwise
     * @note CSV format suitable for Excel/Calc
     * @see importFromFile_CSV()
     */
     bool exportToFile_CSV(const std::string& filename) const;

    /**
     * @brief Asynchronously exports items to CSV format.
     * 
     * @param filename Path to the output CSV file
     * @return void
     */
     void asyncExportToFile_CSV(const std::string& filename) const;

    /**
     * @brief Imports items from a CSV file.
     * 
     * Loads items previously exported in CSV format.
     * 
     * @param filename Path to the CSV file to import
     * 
     * @return bool True if import succeeded, false otherwise
     * @see exportToFile_CSV()
     */
     bool importFromFile_CSV(const std::string& filename);

    /**
     * @brief Asynchronously imports items from CSV format.
     * 
     * @param filename Path to the CSV file
     * @return void
     */
     void asyncImportFromFile_CSV(const std::string& filename);

    /**
     * @brief Imports a single item from a CSV file.
     * 
     * @param filename Path to the CSV file
     * @param type The demangled type name
     * @param tag The item tag
     * 
     * @return std::shared_ptr<BaseItem> Pointer to imported item
     */
     std::shared_ptr<BaseItem> importSingleObject_CSV(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Asynchronously imports a single CSV object.
     * 
     * @param filename Path to the CSV file
     * @param type The type name
     * @param tag The item tag
     * @return void
     */
     void asyncImportSingleObject_CSV(const std::string& filename, const std::string& type, const std::string& tag);

    /**
     * @brief Displays all registered type names.
     * 
     * Lists all types that have been registered for serialization/deserialization.
     * 
     * @return void
     * @note Useful for debugging type registry
     */
     void listRegisteredTypes() const;

    /**
     * @brief Filters and displays items matching specified tags.
     * 
     * Shows only items whose tags are in the provided list.
     * 
     * @param tags Vector of tag strings to filter by
     * 
     * @return void
     * @note Items not matching any tag are not displayed
     */
    void filterByTag(const std::vector<std::string>& tags) const;

    /**
     * @brief Sorts and displays all items alphabetically by tag.
     * 
     * @return void
     * @note Display-only operation, doesn't change storage order
     */
    void sortItemsByTag() const;

    /**
     * @brief Displays the class/type name of all stored items.
     * 
     * Outputs a list of type names for each stored item.
     * 
     * @return void
     */
    void displayAllClasses() const;

    /**
     * @brief Provides read-only access to the internal items map.
     * 
     * Returns a const reference to the complete items storage.
     * Useful for iteration or analysis of all stored items.
     * 
     * @return const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& Reference to items map
     * @note Thread-safe for read-only access
     */
    const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& getItemMapStore() const;

};
#include "ItemManager.tpp"


/**
 * @class GlobalItemManager
 * @brief Singleton pattern implementation providing global access to a single ItemManager instance.
 * 
 * This class enforces the singleton design pattern, ensuring that only one ItemManager exists
 * throughout the application's lifetime. It provides thread-safe access to the global item storage.
 * 
 * @details
 * **Singleton Pattern Implementation:**
 * - Private constructor prevents direct instantiation
 * - Copy constructor and assignment operator deleted
 * - Static getInstance() method returns the singleton instance
 * - Guaranteed initialization via static local variable in getInstance()
 * 
 * **Thread Safety:**
 * - Static local variables in getInstance() are thread-safe in C++11 and later
 * - Initial construction is guaranteed atomic
 * 
 * **Usage Pattern:**
 * ```cpp
 * // Access the global ItemManager
 * GlobalItemManager& globalMgr = GlobalItemManager::getInstance();
 * ItemManager& mgr = globalMgr.getItemManager();
 * 
 * // Use the manager normally
 * mgr.addItem(myObject, "tag");
 * ```
 * 
 * **Advantages:**
 * - Single point of access across the entire application
 * - Guaranteed lifetime spanning entire program execution
 * - No manual destruction needed
 * - Clean encapsulation of the ItemManager
 * 
 * @note Use GlobalItemManager::getInstance() to access the global ItemManager
 * @note Cannot be copied or moved - prevents multiple instances
 * @note Call resetItemManager() if you need to reinitialize the manager
 * 
 * @author Victor
 * @date 2025
 * @see ItemManager
 */
class GlobalItemManager {
    private:
          /**
         * @brief Private constructor - prevents external instantiation.
         * 
         * Initializes the singleton instance with a new ItemManager.
         * Called only once by getInstance().
         */
        GlobalItemManager() : itemManager(std::make_unique<ItemManager>()) {}
    
        /**
         * @brief Unique pointer to the single ItemManager instance.
         * 
         * Managed automatically - destroyed when GlobalItemManager
         * is destroyed (at program termination).
         */
        std::unique_ptr<ItemManager> itemManager;
    
    public:
        /**
         * @brief Deleted copy constructor - prevents copying singleton instances.
         * 
         * Enforces singleton pattern by preventing creation of duplicate instances.
         */
        GlobalItemManager(const GlobalItemManager&) = delete;

        /**
         * @brief Deleted copy assignment operator - prevents assignment of singleton instances.
         * 
         * Enforces singleton pattern by preventing copying via assignment.
         */
        GlobalItemManager& operator=(const GlobalItemManager&) = delete;
    
        /**
         * @brief Provides thread-safe access to the singleton instance.
         * 
         * Returns a reference to the global GlobalItemManager instance.
         * Creates the instance on first call using static initialization,
         * which is thread-safe in C++11 and later.
         * 
         * @return GlobalItemManager& Reference to the singleton instance
         * 
         * @note Thread-safe - guaranteed by C++11 static initialization
         * @note First call may have slight overhead due to initialization
         * @note All subsequent calls return immediately with cached instance
         * 
         * **Thread Safety Guarantee:**
         * C++11 and later guarantee that the static variable initialization
         * is performed exactly once, even in multi-threaded environments.
         * 
         * **Usage:**
         * ```cpp
         * GlobalItemManager& global = GlobalItemManager::getInstance();
         * ItemManager& mgr = global.getItemManager();
         * ```
         */
        static GlobalItemManager& getInstance() {
            static GlobalItemManager instance; // Guaranteed to be initialized once
            return instance;
        }
    
        /**
         * @brief Provides access to the managed ItemManager instance.
         * 
         * Returns a reference to the internal ItemManager that handles
         * all item storage and management operations.
         * 
         * @return ItemManager& Reference to the managed ItemManager
         * 
         * **Usage:**
         * ```cpp
         * GlobalItemManager& global = GlobalItemManager::getInstance();
         * ItemManager& mgr = global.getItemManager();
         * mgr.addItem(obj, "tag");
         * ```
         * 
         * @note Always returns the same ItemManager instance
         * @note Reference is valid for the entire program lifetime
         */
        ItemManager& getItemManager() {
            return *itemManager;
        }
    
        /**
         * @brief Reinitializes the ItemManager with a fresh instance.
         * 
         * Destroys the current ItemManager and creates a new one.
         * Useful for testing or complete reset of item storage.
         * 
         * @return void
         * 
         * @warning This operation:
         * - Deletes all items currently in storage
         * - Clears undo/redo history
         * - Resets type registry
         * - Outputs debug message to console
         * 
         * **Use Cases:**
         * - Test setup/teardown
         * - Factory reset scenarios
         * - Memory cleanup in long-running applications
         * 
         * **Example:**
         * ```cpp
         * GlobalItemManager& global = GlobalItemManager::getInstance();
         * global.resetItemManager();  // Start fresh
         * ```
         * 
         * @note Should only be called when you intend to lose all data
         * @note Not thread-safe if called while other threads access the manager
         */
        void resetItemManager() {
            itemManager = std::make_unique<ItemManager>();
            std::cout << "::: Debug: ItemManager instance reset.\n";
        }
    };
