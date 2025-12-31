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
#include <memory>
#include <string>
#include <typeinfo>
#include "versionForMigration/MigrationRegistry.h"
#include <mutex>
#if defined(__GNUC__) || defined(__clang__)
#include <cxxabi.h>
#include "err_log/Logger.hpp"
#endif

using json = nlohmann::json;
constexpr size_t MAX_UNDO_HISTORY = 50;
constexpr size_t MAX_REDO_HISTORY = 50;

/**
* @class ItemManager
* @brief Core class responsible for managing items, their states, and type registration.
*
* The ItemManager provides a centralized system for storing, tracking, and manipulating
* items represented by BaseItem. It supports undo/redo functionality, type registration,
* schema management, and migration handling. Thread safety is ensured via an internal mutex.
*
* @note This class is the central hub of the Smart_Store framework, coordinating
*       state management, serialization, and type handling.
*/
class ItemManager {
public:
    /**
    * @typedef State
    * @brief Represents a snapshot of the current item collection.
    *
    * A State is defined as a mapping from string identifiers to shared pointers
    * of BaseItem instances. It is used for undo/redo history and state cloning.
    */
    using State = std::unordered_map<std::string, std::shared_ptr<BaseItem>>;

private:
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> items; ///< Active items managed by the ItemManager
    std::deque<State> undoHistory;   ///< History of previous states for undo operations
    std::queue<State> redoQueue;     ///< Queue of states for redo operations

    std::unordered_map<std::string, int> typeUsage; ///< Tracks usage count of registered types
    std::unordered_map<std::string, std::shared_ptr<BaseItem>> idMap; ///< Maps unique IDs to items
    std::unordered_map<std::string, std::type_index> registeredTypes; ///< Registry of type names to type indices
    std::unordered_map<std::string, std::function<json()>> schemaRegistry; ///< Registry of type schemas for serialization
    std::unordered_map<std::string, std::function<std::shared_ptr<BaseItem>(const json&, const std::string&)>> deserializers; ///< Registry of deserialization functions

    mutable std::mutex mutex_; ///< Mutex to ensure thread safety during operations
    MigrationRegistry migrationRegistry; ///< Handles migration of item versions and schemas

    /**
    * @brief Creates a deep copy of the current state of items.
    * @return A cloned State representing the current item collection.
    */
    State cloneCurrentState() const;

    /**
    * @brief Saves the current state into the undo history.
    *
    * This function captures the current snapshot of items and pushes it
    * onto the undoHistory deque.
    */
    void saveState();

    /**
    * @brief Registers a new item type with the ItemManager.
    *
    * @tparam T The type to register, derived from BaseItem.
    *
    * This function updates the registeredTypes, schemaRegistry, and deserializers
    * to support serialization and deserialization of the new type.
    */
    template<typename T>
    void registerType();

    /**
    * @brief Retrieves the compiler-generated type name for a given type.
    *
    * @tparam T The type whose name should be retrieved.
    * @return A string representing the compiler-specific type name.
    */
    template<typename T>
    static std::string getCompilerTypeName();


    /**
    * @brief Get the compiler type name of a given type.
    *
    * This function uses `typeid` and demangling to produce a human-readable type name.
    *
    * @param mangledName The mangled type name returned by the compiler.
    * @return A demangled, human-readable type name.
    */
    std::string demangleType(const std::string& mangledName) const;

    /**
    * @brief Retrieves the JSON schema for a registered item type.
    *
    * @param type The string identifier of the item type.
    * @return A JSON object representing the schema of the requested type.
    *
    * @note If the type is not registered, this function may throw or return
    *       an empty schema depending on implementation.
    */
    json getSchemaForType(std::string type) const;

    /**
    * @brief Deserializes an item of type T from JSON using its ID.
    *
    * @tparam T The item type to deserialize, derived from BaseItem.
    * @param j The JSON object containing serialized item data.
    * @return A shared pointer to the deserialized BaseItem instance.
    *
    * @note This template function is declared in the header to match its
    *       implementation in the corresponding .tpp file.
    * @warning The JSON must contain valid data for the specified type T,
    *          otherwise deserialization may fail or throw an exception.
    */
    template<typename T>
    std::shared_ptr<BaseItem> deserializeItemById(const json& j);


    public:
    /**
    * @brief Default constructor for ItemManager.
    *
    * Initializes an empty ItemManager instance with no items, types, or history.
    * All internal containers start in their default state.
    */
    ItemManager() = default;

    /**
    * @brief Destructor for ItemManager.
    *
    * Cleans up all managed resources, including items, type registries,
    * schema/deserializer mappings, and undo/redo histories. Ensures thread safety
    * by locking the internal mutex during cleanup.
    *
    * @note Any exceptions thrown during cleanup are caught and logged to std::cerr
    *       to prevent program termination.
    */
    ~ItemManager() {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            items.clear();             ///< Clear all managed items
            idMap.clear();             ///< Clear ID-to-item mappings
            registeredTypes.clear();   ///< Clear registered type information
            schemaRegistry.clear();    ///< Clear schema registry
            deserializers.clear();     ///< Clear deserializer registry
            typeUsage.clear();         ///< Clear type usage statistics
            undoHistory.clear();       ///< Clear undo history
            while (!redoQueue.empty()) redoQueue.pop(); ///< Clear redo queue
        } catch (const std::exception& e) {
            std::cerr << ":::| ERROR during ItemManager cleanup: " << e.what() << "\n";
        }
    }

    /**
    * @brief Display the signature of the ItemManager class.
    */
    void showSignature();





       /*******************************************************
       * @brief          ITEM/OBJECT STATE MANAGER            *
       *******************************************************/

    /**
    * @class ItemManager::StateManager
    * @brief Nested helper class that manages state operations for an ItemManager instance.
    *
    * The StateManager provides controlled access to the parent ItemManager, enabling
    * operations such as adding, modifying, or removing items while ensuring thread safety
    * and consistency. It does not own data itself; instead, it manipulates the parent’s
    * internal structures (items, undo/redo history, migration registry, etc.).
    *
    * @note A StateManager cannot exist independently. It must always be constructed
    *       with a reference to an ItemManager.
    */
    class StateManager {
        ItemManager& parent;  ///< Reference to the parent ItemManager instance

        public:
        /**
        * @brief Constructs a StateManager bound to a specific ItemManager.
        *
        * @param p Reference to the ItemManager that this StateManager will operate on.
        *
        * @note The constructor is marked explicit to prevent accidental implicit conversions.
        */
        explicit StateManager(ItemManager& p) : parent(p) {}
    
    
    
        /**
        * @brief Add an item with a specific tag.
        *
        * @tparam T The type of the item.
        * @param obj Shared pointer to the item object.
        * @param tag The tag associated with the item.
        */
        template<typename T>
        void addItem(std::shared_ptr<T> obj, const std::string& tag);
    
        /**
        * @brief Check if an item with a specific tag exists.
        *
        * @param tag The tag to search for.
        * @return True if the item exists, false otherwise.
        */
        bool hasItem(const std::string& tag) const;
    
        /**
        * @brief Retrieve an item by tag.
        *
        * @tparam T The type of the item.
        * @param tag The tag of the item to retrieve.
        * @return An optional containing the item if found, otherwise empty.
        */
        template<typename T>
        std::optional<T> getItem(const std::string& tag) const;
    
        /**
        * @brief Retrieve a raw reference to a BaseItem by tag.
        *
        * @tparam T The type of the item.
        * @param tag The tag of the item to retrieve.
        * @return Reference to the item.
        */
        template<typename T>
        T& getItemRaw(const std::string& tag);
    
        template<typename T>
        const T& getItemRaw(const std::string& tag) const;
    
        /**
        * @brief Modify an item using a given modifier function.
        *
        * @tparam T The type of the item.
        * @param tag The tag of the item to modify.
        * @param modifier A function that modifies the item in place.
        * @return True if modification succeeded, false otherwise.
        */
        template<typename T>
        bool modifyItem(const std::function<void(T&)>& modifier, const std::string& tag);
    
        /**
        * @brief Undo the last state change.
        */
        void undo();
    
        /**
        * @brief Redo the last undone state change.
        */
        void redo();
    
        /**
        * @brief Remove an item by its tag.
        *
        * @param tag The tag of the item to remove.
        */
        void removeByTag(const std::string& tag);
    
        /**
        *@brief Print all registered item IDs.
        */ 
        void printIds();
    
        /**
        * @brief Display items filtered by a specific tag.
        *
        * @param tag The tag to filter items by.
        */
        void displayByTag(const std::string& tag) const;
    
        /**
        * @brief Display all items currently stored.
        */
        void displayAll() const;
    
        /**
        * @brief Display all class names of stored items.
        */
        void displayAllClasses() const;
    
        /**
        * @brief Display all registered deserializers.
        *
        * Iterates through the deserializers map and prints each type name
        * along with its corresponding deserialization function.
        */
        void displayRegisteredDeserializers();
    
        // --- Similar documentation continues for Binary, XML, CSV import/export functions ---
        // (to avoid repetition, each follows the same pattern: sync + async, file path, type, tag)
    
        /**
        * @brief List all registered types for serialization and deserialization.
        */
        void listRegisteredTypes() const;
    
        /**
        * @brief Filter items by a set of tags.
        *
        * @param tags Vector of tags to filter by.
        */
        void filterByTag(const std::vector<std::string>& tags) const;
    
        /**
        * @brief Sort items by their tag.
        */
        void sortItemsByTag() const; 
    
        /**
        * @brief Get the current state of items.
        *
        * @return A reference to the internal item map store.
        */
        const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& getItemMapStore() const;
    };





       /*****************************************************
       * @brief    IMPORT AND EXPORT TO FILE MANAGER        *
       *****************************************************/

    /**
    * @class ItemManager::IO_ToFileManager
    * @brief Nested helper class that manages file export operations for an ItemManager instance.
    *
    * The IO_ToFileManager provides functionality to serialize and export the state of the
    * parent ItemManager to external files (e.g., JSON). It encapsulates I/O responsibilities,
    * ensuring that file operations are performed in a controlled and thread‑safe manner.
    *
    * @note This class does not own data itself; it operates on the parent ItemManager’s
    *       internal structures. It must always be constructed with a reference to an ItemManager.
    */
    class IO_ToFileManager{
        ItemManager& parent;
    public:
    /**
    * @brief Constructs an IO_ToFileManager bound to a specific ItemManager.
    *
    * @param p Reference to the ItemManager that this IO_ToFileManager will operate on.
    *
    * @note The constructor is marked explicit to prevent accidental implicit conversions.
    */
    explicit IO_ToFileManager(ItemManager& p) : parent(p) {}

     /**
     * @brief Import items from a JSON file.
     *
     * @param filename Path to the JSON file.
     */
     void importFromFile_Json(const std::string& filename);

     /***
     * @brief Asynchronously import items from a JSON file.
     * 
     * @param filename Path to the JSON file.
     */
     void asyncImportFromFile_Json(const std::string& filename);

     /**
     * @brief Export all items to a JSON file.
     * 
     * @param filename Path to the JSON file.
     */
     void exportToFile_Json(const std::string& filename) const;

     /**
     * @brief Asynchronously export items to a JSON file.
     *
     * @param filename Path to the JSON file.
     */
     void asyncExportToFile_Json(const std::string& filename) const;

     /**
     * @brief Import a single object from a JSON file.
     *
     * @param filename Path to the JSON file.
     * @param typeName The type of the object.
     * @param tag The tag to assign to the object.
     */
     std::shared_ptr<BaseItem> importSingleObject_Json(const std::string& filename, 
                                                       const std::string& type, 
                                                       const std::string& tag);

     /**
     * @brief Asynchronously import a single object from a JSON file.
     *
     * @param filename Path to the JSON file.
     * @param typeName The type name of the object to import.
     * @param tag The tag to assign to the imported object.
     */
     void asyncImportSingleObject_Json(const std::string& filename, 
                                       const std::string& typeName, 
                                       const std::string& tag);

     /**
     * @brief Export all items to a binary file.
     *
     * @param filename Path to the binary file.
     * @return True if export succeeded, false otherwise.
     */
     bool exportToFile_Binary(const std::string& filename) const;

     /**
     * @brief Asynchronously export all items to a binary file.
     *
     * @param filename Path to the binary file.
     */
     void asyncExportToFile_Binary(const std::string& filename) const;

     /**
     * @brief Import items from a binary file.
     *
     * @param filename Path to the binary file.
     * @return True if import succeeded, false otherwise.
     */
     bool importFromFile_Binary(const std::string& filename);

     /**
     * @brief Asynchronously import items from a binary file.
     *
     * @param filename Path to the binary file.
     */
     void asyncImportFromFile_Binary(const std::string& filename);

     /**
     * @brief Import a single object from a binary file.
     *
     * @param filename Path to the binary file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     * @return Shared pointer to the imported object.
     */
     std::shared_ptr<BaseItem> importSingleObject_Binary(const std::string& filename, 
                                                         const std::string& type, 
                                                         const std::string& tag);

     /**
     * @brief Asynchronously import a single object from a binary file.
     *
     * @param filename Path to the binary file.
     * @param typeName The type name of the object to import.
     * @param tag The tag to assign to the imported object.
     */
     void asyncImportSingleObject_Binary(const std::string& filename, 
                                         const std::string& typeName, 
                                         const std::string& tag);

     /**
     * @brief Export all items to an XML file.
     *
     * @param filename Path to the XML file.
     * @return True if export succeeded, false otherwise.
     */
     bool exportToFile_XML(const std::string& filename) const;

     /**
     * @brief Import items from an XML file.
     *
     * @param filename Path to the XML file.
     * @return True if import succeeded, false otherwise.
     */
     void asyncExportToFile_XML(const std::string& filename) const;

     /**
     * @brief Import items from an XML file.
     *
     * @param filename Path to the XML file.
     * @return True if import succeeded, false otherwise.
     */
     bool importFromFile_XML(const std::string& filename);

     /**
     * @brief Asynchronously import a single object from an XML file.
     *
     * @param filename Path to the XML file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     */
     void asyncImportFromFile_XML(const std::string& filename);

     /**
     * @brief Import a single object from an XML file.
     *
     * @param filename Path to the XML file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     * @return Optional containing a shared pointer to the imported object if successful.
     */
     std::optional<std::shared_ptr<BaseItem>> importSingleObject_XML(const std::string& filename, 
                                                                     const std::string& type, 
                                                                     const std::string& tag);

     /**
     * @brief Asynchronously import a single object from an XML file.
     *
     * @param filename Path to the XML file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     */
     void asyncImportSingleObject_XML(const std::string& filename, 
                                      const std::string& type, 
                                      const std::string& tag);

     /**
     * @brief Export all items to a CSV file.
     *
     * @param filename Path to the CSV file.
     * @return True if export succeeded, false otherwise.
     */
     bool exportToFile_CSV(const std::string& filename) const;

     /**
     * @brief Asynchronously export all items to a CSV file.
     *
     * @param filename Path to the CSV file.
     */
     void asyncExportToFile_CSV(const std::string& filename) const;

     /**
     * @brief Import items from a CSV file.
     *
     * @param filename Path to the CSV file.
     * @return True if import succeeded, false otherwise.
     */
     bool importFromFile_CSV(const std::string& filename);

     /**
     * @brief Asynchronously import items from a CSV file.
     *
     * @param filename Path to the CSV file.
     */
     void asyncImportFromFile_CSV(const std::string& filename);

     /**
     * @brief Import a single object from a CSV file.
     *
     * @param filename Path to the CSV file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     * @return Shared pointer to the imported object.
     */
     std::shared_ptr<BaseItem> importSingleObject_CSV(const std::string& filename, 
                                                      const std::string& type, 
                                                      const std::string& tag);

     /**
     * @brief Asynchronously import a single object from a CSV file.
     * 
     * @param filename Path to the CSV file.
     * @param type The type of the object to import.
     * @param tag The tag to assign to the imported object.
     */ 
     void asyncImportSingleObject_CSV(const std::string& filename, 
                                      const std::string& type, 
                                      const std::string& tag);

    };




       /****************************************************
       * @brief        NETWORK CONNECTION MANAGER          *
       ****************************************************/

    /**
    * @class ItemManager::NetworkManager
    * @brief Nested helper class that manages networking operations for an ItemManager instance.
    *
    * The NetworkManager provides functionality to handle communication tasks related to the
    * parent ItemManager. This may include sending and receiving data, managing connections,
    * or integrating with external services. It encapsulates networking responsibilities,
    * ensuring that operations are performed in a controlled and thread‑safe manner.
    *
    * @note This class does not own data itself; it operates on the parent ItemManager’s
    *       internal structures. It must always be constructed with a reference to an ItemManager.
    */
    class NetworkManager{
        ItemManager& parent;
        public:
        
        /**
        * @brief Constructs a NetworkManager bound to a specific ItemManager.
        *
        * @param p Reference to the ItemManager that this NetworkManager will operate on.
        *
        * @note The constructor is marked explicit to prevent accidental implicit conversions.
        */
        explicit NetworkManager(ItemManager& p) : parent(p) {}

        /**
        * @brief Send a network message.
        *
        * @param msg The message to send.
        * @param tag The tag associated with the message.
        * @return True if the message was sent successfully, false otherwise.
        */
        template<typename T>
        bool networkMessage_Send(std::string& msg, std::string& tag);

        /**
        * @brief Receive a network message.
        * 
        * @param msg The received message.
        * @param tag The tag associated with the message.
        * @return True if the message was received successfully, false otherwise.
        */ 
        template<typename T>
        bool networkMessage_Receive(Message msg, std::string& tag);
    };




       /*******************************************************
       * @brief COMPUTER VISION - FACE REGONITION  MANAGER    *
       *******************************************************/

    /**
    * @class ItemManager::ComputerVision
    * @brief Nested helper class that manages computer vision operations for an ItemManager instance.
    *
    * The ComputerVision class provides functionality to integrate vision‑related features
    * into the parent ItemManager. This may include tasks such as scanning from a camera,
    * processing images, or applying recognition algorithms. It encapsulates vision‑specific
    * responsibilities, ensuring that these operations are performed in a controlled manner
    * and tied directly to the parent ItemManager’s state.
    *
    * @note This class does not own data itself; it operates on the parent ItemManager’s
    *       internal structures. It must always be constructed with a reference to an ItemManager.
    */
    class ComputerVision{
        ItemManager& parent;
 
        public:
        /**
        * @brief Constructs a ComputerVision helper bound to a specific ItemManager.
        *
        * @param p Reference to the ItemManager that this ComputerVision will operate on.
        *
        * @note The constructor is marked explicit to prevent accidental implicit conversions.
        */
        ComputerVision(ItemManager& p) : parent(p) {}

       /**
       * @brief Run restricted area monitor on a camera feed.
       * 
       * @param cameraIndex The index of the camera to monitor.
       * @param cascadePath Path to the Haar cascade XML file.
       */ 
       template<typename T>
       void cvFgn_runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath, std::string& tag);
   
       /**
       * @brief Get the tracks from the restricted area monitor.
       *
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return A reference to the map of tracks.
       */
       template<typename T>
       const std::unordered_map<int, FaceTrack>& cvFgn_getRunRestrictedTracks(std::string& tag) const;
   
       /**
       * @brief Add a track to the restricted area monitor.
       * @param id The ID of the track.
       * @param track The track to add.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_addRunRestrictedTrack(int id, const FaceTrack& track, std::string& tag);
   
       /**
       * @brief Remove a track from the restricted area monitor.
       * @param id The ID of the track to remove.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_removeRunRestrictedTrack(int id, std::string& tag);
   
       /**
       * @brief Reset the configuration of the restricted area monitor.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */ 
       template<typename T>
       void cvFgn_resetRunRestrictedConfig(std::string& tag);
   
       /**
       * @brief Set the scale factor for the restricted area monitor.
       * 
       * @param scaleFactor The new scale factor.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_setRunRestrictedScaleFactor(double scaleFactor, std::string& tag);
   
       /**
       * @brief Set the minimum neighbors for the restricted area monitor.
       * 
       * @param minNeighbors The new minimum neighbors.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_setRunRestrictedMinNeighbors(int minNeighbors, std::string& tag);
   
       /**
       * @brief Set the minimum face size for the restricted area monitor.
       * 
       * @param size The new minimum face size.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_setRunRestrictedMinFaceSize(const cv::Size& size, std::string& tag);
   
       /**
       * @brief Set the IOU match threshold for the restricted area monitor.
       * 
       * @param threshold The new IOU match threshold.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFgn_setIouMatchThreshold(double threshold, std::string& tag);
   
       /**
       * @brief Get the scale factor for the restricted area monitor.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The scale factor.
       */
       template<typename T>
       double cvFgn_getRunRestrictedScaleFactor(std::string& tag) const;
   
       /**
       * @brief Get the minimum neighbors for the restricted area monitor.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       int cvFgn_getRunRestrictedMinNeighbors(std::string& tag) const;
   
       /**
       * @brief Get the minimum face size for the restricted area monitor.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The minimum face size.
       */
       template<typename T>
       cv::Size cvFgn_getRunRestrictedMinFaceSize(std::string& tag) const;
   
       /**
       * @brief Get the IOU match threshold for the restricted area monitor.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The IOU match threshold.
       */
       template<typename T>
       double cvFgn_getIouMatchThreshold(std::string& tag) const;
   
    




            /************************************************
            * @brief COMPUTER VISION - FACE WATCH LISTENER  *
            ************************************************/

       /**
       * @brief Monitor a camera for faces.
       * 
       * @param cascadePath Path to the Haar cascade XML file.   
       * Set "cascadePath" empty string if you don't have for the system to use it's own cascade.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFwl_monitorCamera(const std::string& cascadePath, std::string& tag);
       
       /**
       * @brief Get the threshold for face recognition in the monitored camera.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The recognition threshold.
       */
       template<typename T>
       double cvFwl_getMonitorCameraThreshold(std::string& tag) const;
   
       /**
       * @brief Set the threshold for face recognition in the monitored camera.
       * 
       * @param newThreshold The new recognition threshold.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFwl_setMonitorCameraThreshold(double newThreshold, std::string& tag);
   
       /**
       * @brief Reset the threshold for face recognition in the monitored camera to default.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFwl_resetMonitorCameraThreshold(std::string& tag);
   
       /**
       * @brief Get the path to the Haar cascade used for face detection in the monitored camera.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The path to the Haar cascade XML file.
       */
       template<typename T>
       std::string cvFwl_getMonitorCameraCascadePath(std::string& tag) const;
   
       /**
       * @brief Set the path to the Haar cascade used for face detection in the monitored camera
       * 
       * @param path The new path to the Haar cascade XML file.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */ 
       template<typename T>
       void cvFwl_setMonitorCameracascadePath(const std::string& path, std::string& tag);
   
       /**
       * @brief Set the path to the Haar cascade used for face detection in the monitored camera. 
       * 
       * @param path The new path to the Haar cascade XML file.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvFwl_setMonitorCameraCascadePath(const std::string& path, std::string& tag);
   
       /**
       * @brief Add a known face to the watch listener.
       * 
       * @param filePath Path to the image file of the known face.
       * @param tag The tag associated with the listener.
       */ 
       template<typename T>
       void cvFwl_loadKnownFaces(const std::vector<std::string>& filePaths, std::string& tag);
   
       /**
       * @brief Get the count of known faces in the watch listener.
       * 
       * @param tag The tag associated with the listener.
       * @return The count of known faces.
       */ 
       template<typename T>
       size_t cvFwl_getKnownFaceCount(std::string& tag) const;
      
       /**
       * @brief Reset the known faces in the watch listener.
       * 
       * @param tag The tag associated with the listener.
       */
       template<typename T>
       void cvFwl_resetKnownFaces(std::string& tag);







           /****************************************************
           *@brief COMPUTER VISION - MOTION DETECTION MONITOR * 
           ****************************************************/

       /**
       * @brief Set the difference threshold for motion detection. 
       *  
       * @param threshold The new difference threshold.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */  
       template<typename T>
       void cvMdn_setMonitorDetectionDiffThreshold(double threshold, std::string& tag);
   
       /**
       * @brief Set the minimum area for motion detection.
       * 
       * @param minArea The new minimum area.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_setMonitorDetectionLoiterSeconds(int loiterSeconds, std::string& tag);
   
       /**
       * @brief Set the crowd threshold for motion detection.
       *
       * @param enableTracking Whether to enable tracking.
       * @param minArea The new minimum area.
       * @param crowdThreshold The new crowd threshold.
       * @param loiterSeconds The new loiter seconds.
       * @param leftBehindSeconds The new left behind seconds.
       * @param diffThreshold The new difference threshold.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_setMonitorDetectionConfig(double diffThreshold,
                                            int minArea,
                                            std::size_t crowdThreshold,
                                            int loiterSeconds,
                                            int leftBehindSeconds,
                                            bool enableTracking,
                                            std::string& tag);
        
       /**
       * @brief Set the minimum area for motion detection.                                     
       * 
       * @param minArea The new minimum area.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_setMonitorDetectionMinArea(int minArea, std::string& tag);
   
       /**
       * @brief Set the alert callback for motion detection.
       * 
       * @param cb The callback function to be called when an alert is triggered.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_setMonitorDetectionAlertCallback(std::function<void(const std::string&)> cb, std::string& tag);
   
       /**
       * @brief Enable or disable tracking for motion detection.
       * 
       * @param enableTracking Whether to enable tracking.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_resetMonitorDetectionConfig(std::string& tag);  
   
       /**
       * @brief Reset the motion detection monitor.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_resetMonitorDetection(std::string& tag);   
   
       /**
       * @brief Clear all detection zones for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_clearMonitorDetectionZones(std::string& tag); 
   
       /**
       * @brief Check if tracking is enabled for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return True if tracking is enabled, false otherwise.
       */
       template<typename T>
       bool cvMdn_isMonitorDetectionTrackingEnabled(std::string& tag) const;
   
       /**
       * @brief Check if a monitor detection callback is set.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return True if a callback is set, false otherwise.
       */
       template<typename T>
       bool cvMdn_hasMonitorDetectionCallback(std::string& tag) const;
   
       /**
       * @brief Get the difference threshold for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The difference threshold.
       */
       template<typename T>
       double cvMdn_getMonitorDetectionDiffThreshold(std::string& tag) const;
   
       /**
       * @brief Get the minimum area for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The minimum area.
       */
       template<typename T>
       int cvMdn_getMonitorDetectionMinArea(std::string& tag) const;
   
       /**
       * @brief Get the crowd threshold for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The crowd threshold.
       */
       template<typename T>
       std::size_t cvMdn_getMonitorDetectionCrowdThreshold(std::string& tag) const;
   
       /**
       * @brief Get the loiter seconds for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The loiter seconds.
       */
       template<typename T>
       int cvMdn_getMonitorDetectionLoiterSeconds(std::string& tag) const;
   
       /**
       * @brief Get the left behind seconds for motion detection.
       * 
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The left behind seconds.
       */
       template<typename T>
       int cvMdn_getMonitorDetectionLeftBehindSeconds(std::string& tag) const;
   
       /**
       * @brief Run motion detection on a camera feed.
       * 
       * @param cameraIndex The index of the camera to monitor.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_runMonitorDetectionMonitorCamera(int cameraIndex, std::string& tag);
   
       /**
       * @brief Add a detection zone to the motion detection monitor.
       * 
       * @param zone The zone to add.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvMdn_addMonitorDetectionZone(const Zone& zone, std::string& tag);






            /***********************************************
            * @brief COMPUTER VISION - QR CODE SCANNER     * 
            ***********************************************/
    
       /**
       * @brief Scan a QR code from a camera feed.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The scanned QR code data as a string.
       */
       template<typename T>    
       std::string cvQRC_scanFromCamera(std::string& tag);
       
       /**
       * @brief Scan a QR code from an image file.
       * @param imagePath The path to the image file.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       std::string cvQRC_scanFromFile(const std::string& imagePath, std::string& tag);
   
       /**
       * @brief Reset the QR code scanner configuration.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvQRC_resetConfig(std::string& tag);
   
       /**
       * @brief Check if preview is enabled for the QR code scanner.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return True if preview is enabled, false otherwise.
       */
       template<typename T>
       bool cvQRC_isPreviewEnabled(std::string& tag) const;
   
       /**
       * @brief Get the camera index used by the QR code scanner.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       * @return The camera index.
       */
       template<typename T>
       int cvQRC_getCameraIndex(std::string& tag) const;
   
       /**
       * @brief Set the camera index for the QR code scanner.
       * @param index The camera index to set.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvQRC_setCameraIndex(int index, std::string& tag);
   
       /**
       * @brief Enable or disable preview for the QR code scanner.
       * @param enabled True to enable preview, false to disable.
       * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
       */
       template<typename T>
       void cvQRC_setPreviewEnabled(bool enabled, std::string& tag);

    };




        /*****************************************************
        * @brief        ALARM MANAGEMENT MANAGER             *
        *****************************************************/


    /**
    * @class AlarmManager
    * @brief Microservice helper class that manages alarm operations for an ItemManager instance.
    *
    * The AlarmManager class provides functionality to integrate alarm‑related features
    * into the parent ItemManager. This may include tasks such as monitoring conditions,
    * triggering alerts, and resetting alarms. It encapsulates alarm‑specific responsibilities,
    * ensuring that these operations are performed in a controlled manner and tied directly
    * to the parent ItemManager’s state.
    *
    * @note This class does not own data itself; it operates on the parent ItemManager’s
    *       internal structures. It must always be constructed with a reference to an ItemManager.
    */     
    class AlarmManager{
       ItemManager& parent;
       
       public:
       /**
       * @brief Constructs an AlarmManager bound to a specific ItemManager.
       *
       * @param p Reference to the ItemManager that this AlarmManager will operate on.
       *
       * @note The constructor is marked explicit to prevent accidental implicit conversions.
       */
       explicit AlarmManager(ItemManager& p) : parent(p) {}

       /**
       * @enum Event
       * @brief Enumeration of possible alarm events.
       *
       * This enum defines the set of alarm events that can be triggered
       * within the AlarmManager. Each event corresponds to a specific
       * condition or action that may require an alarm response.
       *
       * - Intrusion: Unauthorized entry detected.
       * - Door_open: Door has been opened.
       * - Fire: Fire hazard detected.
       * - Access: Access granted or attempted.
       * - Warming: Warning or pre‑alert condition.
       * - Confirmation: Confirmation of an action or event.
       */
        enum class Event{
            Intrusion,
            Door_open,
            Fire,
            Access,
            Warming,
            Confirmation
        };

       /**
       * @brief Trigger an alarm for a specific event on an item.
       *
       * This function locks the parent ItemManager, locates the item
       * associated with the given tag, and forwards the alarm event
       * to the corresponding ItemWrapper instance. It also logs
       * the event using its string representation for clarity.
       *
       * @tparam T The type of the item wrapper.
       * @param event The alarm event to trigger (e.g., Intrusion, Fire).
       * @param tag The tag identifying the item to trigger the alarm on.
       *
       * @note If the item is not found or the type cast fails, appropriate
       *       warnings or errors are logged. Thread safety is ensured
       *       using std::lock_guard.
       */
       template<typename T>
       void triggerAlarm(Event event, std::string& tag);

       private:
       /**
       * @brief Convert an Event enum value to its string representation.
       *
       * @param event The Event enum value to convert.
       * @return A human‑readable string corresponding to the event.
       *
       * @note This helper is used for logging and reporting purposes,
       *       ensuring that alarm events are displayed as descriptive text.
       */
       inline std::string eventToString(Event event) const;
    };




  

};

#include "ItemManager.tpp"





/**
 * @class GlobalItemManager
 * @brief Singleton wrapper that manages a single global ItemManager instance.
 *
 * This class ensures that only one ItemManager exists throughout the lifetime
 * of the application. It provides controlled access to the ItemManager and
 * allows resetting the instance when needed.
 *
 * Usage example:
 * @code
 * // Access the global ItemManager
 * auto& manager = GlobalItemManager::getInstance().getItemManager();
 * manager.addItem("msg_tag");
 *
 * // Reset the ItemManager (careful with dangling references!)
 * GlobalItemManager::getInstance().resetItemManager();
 * @endcode
 */
class GlobalItemManager {
private:
    /**
     * @brief Private constructor to enforce singleton pattern.
     *
     * Initializes the unique_ptr<ItemManager> with a new ItemManager instance.
     */
    GlobalItemManager() : itemManager(std::make_unique<ItemManager>()) {}

    /// Pointer to the managed ItemManager instance.
    std::unique_ptr<ItemManager> itemManager;

public:
    /// Delete copy constructor to prevent copying the singleton.
    GlobalItemManager(const GlobalItemManager&) = delete;

    /// Delete assignment operator to prevent copying the singleton.
    GlobalItemManager& operator=(const GlobalItemManager&) = delete;

    /**
     * @brief Access the singleton instance.
     *
     * @return Reference to the single GlobalItemManager instance.
     *
     * This method is thread-safe since C++11 guarantees static local
     * initialization is atomic.
     */
    static GlobalItemManager& getInstance() {
        static GlobalItemManager instance; // Initialized once, thread-safe
        return instance;
    }

    /**
     * @brief Access the underlying ItemManager.
     *
     * @return Reference to the managed ItemManager.
     *
     * Note: If resetItemManager() is called, previously obtained references
     * may become invalid. Always fetch a fresh reference when needed.
     */
    ItemManager& getItemManager() {
        return *itemManager;
    }

    /**
     * @brief Reset the ItemManager instance.
     *
     * Creates a new ItemManager and replaces the old one.
     * Useful for testing or reinitialization.
     *
     * @warning This is not thread-safe. If multiple threads access
     * getItemManager() while resetItemManager() is called, race conditions
     * may occur. Consider adding synchronization if needed.
     */
    void resetItemManager() {
        itemManager = std::make_unique<ItemManager>();
        std::cout << "::: Debug: ItemManager instance reset.\n";
    }
};

