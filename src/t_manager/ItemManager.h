

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
#include "err_log/Logger.hpp"
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
    
   /**
   * @brief Clone the current state of items.
   * @return A deep copy of the current state.
   */
    State cloneCurrentState() const;

    /**
     * @brief Save the current state of items for undo functionality.
     */
    void saveState();
    
   /**
   * @brief Register a new type T with the ItemManager.
   * @tparam T The type to register.
   */
    template<typename T>
    void registerType();

    /**
     * @brief Get the compiler type name for a given type T.
     * @tparam T The type to get the name for.
     */
    template<typename T>
    static std::string getCompilerTypeName();

    /**
     * @brief Get the schema for a specific type.
     * @param type The type name.
     * @return The schema as JSON.
     */
    json getSchemaForType(std::string type) const;

    /**
     * @brief Deserialize an item from JSON based on its type ID.
     * @param j The JSON object containing the item data.
     * @return A shared pointer to the deserialized BaseItem.
     */
    template<typename T>
    std::shared_ptr<BaseItem> deserializeItemById(const json& j);
        
    
    

   
    //::->       PUBLIC FUNCTIONS.
    //****************************************

public:

    /**
    * @brief Default constructor for ItemManager.
    */
    ItemManager() = default;

    /**
     * @brief Destructor for ItemManager.
     *
     * Cleans up all stored items and associated data structures.
     * Catches and logs any exceptions that occur during cleanup.
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

    /**
     * @brief Print all registered item IDs.
    */
    void printIds();

    /**
    * @brief Show the author/architect of "Smart_Store" and "License".
    */
    void showSignature();

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
    * @brief Display all registered deserializers.
    *
    * Iterates through the deserializers map and prints each type name
    * along with its corresponding deserialization function.
    */
    void displayRegisteredDeserializers();

   /**
    * @brief Check if an item with a specific tag exists.
    *
    * @param tag The tag to search for.
    * @return True if the item exists, false otherwise.
    */
    bool hasItem(const std::string& tag) const;

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
     * @brief Modify an item using a given modifier function.
     *
     * @tparam T The type of the item.
     * @param tag The tag of the item to modify.
     * @param modifier A function that modifies the item in place.
     * @return True if modification succeeded, false otherwise.
     */
     template<typename T>
     bool modifyItem(const std::string& tag, const std::function<void(T&)>& modifier);

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
     * @brief Display all items currently stored.
     */
     void displayAll() const;
    
     /**
     * @brief Display items filtered by a specific tag.
     *
     * @param tag The tag to filter items by.
     */
     void displayByTag(const std::string& tag) const;

    /**
    * @brief Remove an item by its tag.
    *
    * @param tag The tag of the item to remove.
    */
    void removeByTag(const std::string& tag);

     /**
     * @brief Undo the last state change.
     */
     void undo();

     /**
     * @brief Redo the last undone state change.
     */
     void redo();

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
    * @brief Display all class names of stored items.
    */
    void displayAllClasses() const;

    /**
    * @brief Get the current state of items.
    *
    * @return A reference to the internal item map store.
    */
    const std::unordered_map<std::string, std::shared_ptr<BaseItem>>& getItemMapStore() const;





    /***************************************************************
     *                   NETWORK AGENT VISION 
     ***************************************************************/

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






      
    /***************************************************************
     *                  COMPUTER VISION SECTION                    *
     ***************************************************************/

   /**************************************************
        FACE RECOGNITION - RESTRICTED AREA MONITOR
    **************************************************/  

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

   /**
    * @brief Add a detection zone to the motion detection monitor.
    * 
    * @param zone The zone to add.
    * @param tag The tag associated with it's value "Item" for monitor and access to Item specific features to use.
    */
    template<typename T>
    void cvMdn_addMonitorDetectionZone(const Zone& zone, std::string& tag);





                  
      /*******************************************
       *   COMPUTER VISION - FACE WATCH LISTENER *
       *******************************************/

    /**
     * @brief Monitor a camera for faces.
     * 
     * @param cascadePath Path to the Haar cascade XML file.
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






      /**************************************************
       *@brief COMPUTER VISION - MOTION DETECTION MONITOR
       **************************************************/

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




         /****************************************************************
          *                    Networking section 
          ****************************************************************/
        
        void networkMessage_Send(std::string& msg, std::string& tag);

    };
