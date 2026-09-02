#pragma once

#include "t_wrapper/ItemWrapper.h"
#include "gpu_manager/gpu_manager.hpp"
#include <unordered_map>
#include <functional>
#include <stack>
#include <vector>
#include <cstdint>
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
    *
    * Additionally, this constructor initializes GPU support by invoking
    * GPUManager::init() to detect compatible hardware and sets GPU usage
    * to enabled via GPUManager::setUseGPU(true). This ensures that, if
    * available, GPU acceleration will be leveraged for computeâ€‘intensive
    * operations such as OpenCV DNN inference or other microservice tasks.
    */
    ItemManager() {
        GPUManager::init();        ///< Detect GPU availability
        GPUManager::setUseGPU(true); ///< Enable GPU acceleration by 
    }


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

    /**
    * @brief Enable GPU usage globally.
    *
    * Convenience method equivalent to calling setUseGPU(true).
    * This ensures that all services in the framework will attempt to use GPU acceleration,
    * provided a CUDA-capable GPU is available. If no GPU is detected, services will
    * automatically fall back to CPU.
    *
    * Typical usage:
    *   GPUManager::setUseGPU_On();
    *
    * After calling this, GPUManager::useGPU() will return true if a GPU is available,
    * and services should configure themselves to run on GPU.
    */
    void setUseGPU_On();
   
    /**
    * @brief Disable GPU usage globally.
    *
    * Convenience method equivalent to calling setUseGPU(false).
    * This ensures that all services in the framework will fall back to CPU,
    * regardless of whether a CUDA-capable GPU is available.
    *
    * Typical usage:
    *   GPUManager::setUseGPU_Off();
    *
    * After calling this, GPUManager::useGPU() will always return false,
    * and all dependent services should configure themselves to run on CPU.
    */
    void setUseGPU_Off();





       /*******************************************************
       * @brief          ITEM/OBJECT STATE MANAGER            *
       *******************************************************/

    /**
    * @class ItemManager::StateManager
    * @brief Nested helper class that manages state operations for an ItemManager instance.
    *
    * The StateManager provides controlled access to the parent ItemManager, enabling
    * operations such as adding, modifying, or removing items while ensuring thread safety
    * and consistency. It does not own data itself; instead, it manipulates the parentâ€™s
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
    * ensuring that file operations are performed in a controlled and threadâ€‘safe manner.
    *
    * @note This class does not own data itself; it operates on the parent ItemManagerâ€™s
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




       /*******************************************************
        * @brief COMPUTER VISION - FACE REGONITION  MANAGER   *
        *******************************************************/

    /**
    * @class ItemManager::ComputerVision
    * @brief Nested helper class that manages computer vision operations for an ItemManager instance.
    *
    * The ComputerVision class provides functionality to integrate visionâ€‘related features
    * into the parent ItemManager. This may include tasks such as scanning from a camera,
    * processing images, or applying recognition algorithms. It encapsulates visionâ€‘specific
    * responsibilities, ensuring that these operations are performed in a controlled manner
    * and tied directly to the parent ItemManagerâ€™s state.
    *
    * @note This class does not own data itself; it operates on the parent ItemManagerâ€™s
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



          /***********************************************
           * @brief COMPUTER VISION - DOCUMENT SCANNER   * 
           ***********************************************/

    /**
    * @brief Set the Canny edge detection thresholds.
    * 
    * @param low  Lower threshold for the Canny edge detector.
    * @param high Upper threshold for the Canny edge detector.
    * @param tag  Identifier of the item to apply settings to.
    */
    template<typename T>
    void cvDOC_setEdgeThreshold(int low, int high, std::string& tag);
    /**
    * @brief Set the minimum contour area for detection.
    * 
    * @param area Minimum area (in pixels) that a contour must have to be considered valid.
    * @param tag  Identifier of the item to apply settings to.
    */
    template<typename T>
    void cvDOC_setContourMinArea(double area, std::string& tag);
    /**
    * @brief Set the output image size.
    * 
    * @param width  Desired output width in pixels.
    * @param height Desired output height in pixels.
    * @param tag    Identifier of the item to apply settings to.
    */
    template<typename T>
    void cvDOC_setOutputSize(int width, int height, std::string& tag);
    /**
    * @brief Set the sharpening amount applied to the output.
    * 
    * @param amount Sharpening intensity (e.g., 0.0 = none, higher values = stronger sharpening).
    * @param tag    Identifier of the item to apply settings to.
    */
    template<typename T>
    void cvDOC_setSharpening(double amount, std::string& tag);
    /**
    * @brief Get the lower Canny edge threshold.
    * 
    * @param tag Identifier of the item to query.
    * @return int Current lower threshold value.
    */
    template<typename T>
    int cvDOC_getCannyLow(std::string& tag) const;
    /**
    * @brief Get the upper Canny edge threshold.
    * 
    * @param tag Identifier of the item to query.
    * @return int Current upper threshold value.
    */
    template<typename T>
    int cvDOC_getCannyHigh(std::string& tag) const;
    /**
    * @brief Get the minimum contour area used for detection.
    * 
    * @param tag Identifier of the item to query.
    * @return double Current minimum contour area in pixels.
    */
    template<typename T>
    double cvDOC_getMinContourArea(std::string& tag) const;
    /**
    * @brief Get the configured output width.
    * 
    * @param tag Identifier of the item to query.
    * @return int Current output width in pixels.
    */
    template<typename T>
    int cvDOC_getOutputWidth(std::string& tag) const;
    /**
    * @brief Get the configured output height.
    * 
    * @param tag Identifier of the item to query.
    * @return int Current output height in pixels.
    */
    template<typename T>
    int cvDOC_getOutputHeight(std::string& tag) const;
    /**
    * @brief Get the sharpening amount applied to the output.
    * 
    * @param tag Identifier of the item to query.
    * @return double Current sharpening intensity.
    */
    template<typename T>
    double cvDOC_getSharpening(std::string& tag) const;
    /**
    * @brief Run the scanner with the specified mode and input/output paths.
    * 
    * @param mode   Processing mode (e.g., "scan", "detect", "process").
    * @param input  Path to the input file or stream.
    * @param output Path to the output file or destination.
    * @param tag    Identifier of the item to run the scanner on.
    */
    template<typename T>
    void cvDOC_run(const std::string& mode, const std::string& input, const std::string& output, std::string& tag);




          /***********************************************
           * @brief COMPUTER VISION - SURVEILLANCE       *
           ***********************************************/

    /**
    * @brief Mark the surveillance item identified by tag as started.
    * @param tag The tag associated with the item to start surveillance on.
    */
    template<typename T>
    void cvSurv_start(std::string& tag);

    /**
    * @brief Mark the surveillance item identified by tag as stopped.
    * @param tag The tag associated with the item to stop surveillance on.
    */
    template<typename T>
    void cvSurv_stop(std::string& tag);

    /**
    * @brief Check whether the surveillance item identified by tag is running.
    * @param tag The tag associated with the item to query.
    * @return true if running, false otherwise.
    */
    template<typename T>
    bool cvSurv_isRunning(std::string& tag) const;

    /**
    * @brief Queue a cloud-sync enable/disable command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setCloudEnabled(bool enabled, std::string& tag);

    /**
    * @brief Queue the rest of the cloud settings for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setCloudSettings(const std::string& baseUrl,
                                 const std::string& stationName,
                                 const std::string& hardwareToken,
                                 int pollIntervalSec,
                                 std::string& tag);

    /**
    * @brief Queue a new-camera command for the surveillance item identified
    * by tag. No width/height -- see Surveillance::addCamera's doc comment.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_addCamera(const std::string& name,
                          const std::string& id,
                          const std::string& source,
                          bool enableFaceRecognition,
                          std::string& tag);

    /**
    * @brief Queue a per-camera feature toggle command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setCameraFeature(const std::string& cameraId,
                                 const std::string& featureKey,
                                 bool value,
                                 std::string& tag);

    /**
    * @brief Queue an edit to an existing camera's identity/connection
    * fields for the surveillance item identified by tag. No width/height --
    * see Surveillance::updateCamera's doc comment.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_updateCamera(const std::string& camId,
                             const std::string& name,
                             const std::string& source,
                             std::string& tag);

    /**
    * @brief Queue a camera-removal command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_removeCamera(const std::string& camId, std::string& tag);

    /**
    * @brief Queue a pending-camera-plug-in command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_plugInPendingCamera(const std::string& camName, const std::string& camId, std::string& tag);

    /**
    * @brief Queue a plug-out (to pending, or delete) command for the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_plugOutPendingCamera(bool isDelete, const std::string& camName, const std::string& camId, std::string& tag);

    /**
    * @brief Queue a feature toggle applied to every configured camera for
    * the surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setFeatureForAllCameras(const std::string& featureKey, bool value, std::string& tag);

    /**
    * @brief Queue a model upload/replace command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_upLoadModel(const std::string& modelType, const std::string& modelPath, std::string& tag);

    /**
    * @brief Queue a model-settings reset command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_resetModelSettings(std::string& tag);

    /**
    * @brief Queue a beep-channel enable/disable command for the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setBeepEnabled(bool enabled, std::string& tag);

    /**
    * @brief Queue a voice-channel enable/disable command for the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setVoiceEnabled(bool enabled, std::string& tag);

    /**
    * @brief Queue a voice-gender command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setVoiceGender(const std::string& gender, std::string& tag);

    /**
    * @brief Queue a station-location command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setStationLocation(double latitude, double longitude, std::string& tag);

    /**
    * @brief Queue a face-settings reset command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_resetFaceSettings(const std::string& camId, std::string& tag);

    /**
    * @brief Queue a motion-settings reset command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_resetMotionSettings(const std::string& camId, std::string& tag);

    /**
    * @brief Queue an add-motion-zone command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_addMotionZone(const std::string& camId,
                              const std::string& regionName,
                              int x, int y, int width, int height,
                              bool restricted,
                              std::string& tag);

    /**
    * @brief Queue a clear-motion-zones command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_clearMotionZones(const std::string& camId, std::string& tag);

    /**
    * @brief Queue an add-plate-to-watchlist command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_addPlateToWatchlist(const std::string& plate, std::string& tag);

    /**
    * @brief Queue a remove-plate-from-watchlist command for the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_removePlateFromWatchlist(const std::string& plate, std::string& tag);

    /**
    * @brief Queue a clear-plate-watchlist command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_clearPlateWatchlist(std::string& tag);

    /**
    * @brief Queue a set-account-id command for the surveillance item
    * identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setAccountId(const std::string& accountId, std::string& tag);

    /**
    * @brief Queue a set-kafka-broker-address command for the surveillance
    * item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setKafkaBrokerAddress(const std::string& brokerAddress, std::string& tag);

    /**
    * @brief Queue a set-face-settings command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setFaceSettings(const std::string& camId,
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
                               std::string& tag);

    /**
    * @brief Queue a set-motion-settings command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setMotionSettings(const std::string& camId,
                                 double diffThreshold,
                                 int minArea,
                                 std::size_t crowdThreshold,
                                 int loiterSeconds,
                                 int leftBehindSeconds,
                                 bool enableTracking,
                                 bool debugLogging,
                                 std::string& tag);

    /**
    * @brief Queue a set-night-vision-settings command for one camera on
    * the surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setNightVisionSettings(const std::string& camId,
                                      double gamma,
                                      bool adaptiveMode,
                                      int contrastMode,
                                      double clipLimit,
                                      int tileSize,
                                      int denoisingStrength,
                                      std::string& tag);

    /**
    * @brief Queue a set-object-detection-settings command for one camera
    * on the surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setObjectDetectionSettings(const std::string& camId,
                                          int inputSize,
                                          int backend,
                                          int target,
                                          bool trackingEnabled,
                                          float minConfForDraw,
                                          std::string& tag);

    /**
    * @brief Queue a set-vehicle-settings command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setVehicleSettings(const std::string& camId,
                                  const std::string& lang,
                                  int minPlateConfidence,
                                  bool enableAlerts,
                                  bool saveImages,
                                  std::string& tag);

    /**
    * @brief Queue a set-recorder-settings command for one camera on the
    * surveillance item identified by tag.
    * @param tag The tag associated with the item.
    */
    template<typename T>
    void cvSurv_setRecorderSettings(const std::string& camId,
                                   int codec,
                                   int bitrate,
                                   int maxDuration,
                                   uint64_t maxFileSize,
                                   const std::string& eventType,
                                   std::string& tag);

    // --- DataBaseManager passthroughs -- same tag lookup + dispatch shape
    // as everything above. Read/query methods (get*-style, plus
    // cvSurv_validateUserPassword) return a requestId string -- call
    // cvSurv_drainResponses() below later to retrieve results (see
    // Surveillance::drainResponses()'s own doc comment). Every other
    // (mutating) method here stays void/fire-and-forget, unchanged. ---

    // Tracked Faces
    template<typename T> std::string cvSurv_getAllTrackedFaces(std::string& tag);
    template<typename T> std::string cvSurv_getUnknownTrackedFaces(std::string& tag);
    template<typename T> std::string cvSurv_getAuthorizedTrackedFaces(std::string& tag);
    template<typename T> std::string cvSurv_getWatchlistTrackedFaces(std::string& tag);
    template<typename T> std::string cvSurv_getTrackedFaceById(const std::string& faceId, std::string& tag);
    template<typename T> void cvSurv_deleteTrackedFace(const std::string& id, std::string& tag);
    template<typename T> void cvSurv_deleteAllTrackedFaces(std::string& tag);
    template<typename T> void cvSurv_deleteAllAuthorizedFaces(std::string& tag);
    template<typename T> void cvSurv_deleteAllWatchlistFaces(std::string& tag);
    template<typename T>
    void cvSurv_registerFaceFromImage(const std::string& imagePath,
                                      const std::string& name,
                                      const std::string& status,
                                      const std::string& description,
                                      const std::string& externalId,
                                      bool broadcastToCloud,
                                      std::string& tag);
    template<typename T> std::string cvSurv_getFaceRegionHistory(const std::string& faceId, std::string& tag);

    // Tracked Plates
    template<typename T>
    void cvSurv_logTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description, std::string& tag);
    template<typename T> std::string cvSurv_getAllTrackedPlates(std::string& tag);
    template<typename T> std::string cvSurv_getUnknownTrackedPlates(std::string& tag);
    template<typename T> std::string cvSurv_getAuthorizedTrackedPlates(std::string& tag);
    template<typename T> std::string cvSurv_getWatchlistTrackedPlates(std::string& tag);
    template<typename T> std::string cvSurv_getTrackedPlateById(const std::string& plateId, std::string& tag);
    template<typename T> void cvSurv_deleteTrackedPlate(const std::string& id, std::string& tag);
    template<typename T> void cvSurv_deleteAllTrackedPlates(std::string& tag);
    template<typename T> void cvSurv_deleteAllAuthorizedPlates(std::string& tag);
    template<typename T> void cvSurv_deleteAllWatchlistPlates(std::string& tag);
    template<typename T>
    void cvSurv_registerPlateFromImage(const std::string& imagePath,
                                       const std::string& plateNumber,
                                       const std::string& status,
                                       const std::string& externalId,
                                       std::string& tag);
    template<typename T> std::string cvSurv_getPlateRegionHistory(const std::string& plateId, std::string& tag);

    // Tracked Objects
    template<typename T> std::string cvSurv_getObjectRegionHistory(const std::string& objectId, std::string& tag);

    // Tracked Weapons
    template<typename T> std::string cvSurv_getAllTrackedWeapons(std::string& tag);
    template<typename T> std::string cvSurv_getUnknownTrackedWeapons(std::string& tag);
    template<typename T> std::string cvSurv_getAuthorizedTrackedWeapons(std::string& tag);
    template<typename T> std::string cvSurv_getWatchlistTrackedWeapons(std::string& tag);
    template<typename T> std::string cvSurv_getTrackedWeaponById(const std::string& weaponId, std::string& tag);
    template<typename T> void cvSurv_deleteTrackedWeapon(const std::string& id, std::string& tag);
    template<typename T> void cvSurv_deleteAllTrackedWeapons(std::string& tag);
    template<typename T> void cvSurv_deleteAllAuthorizedWeapons(std::string& tag);
    template<typename T> void cvSurv_deleteAllWatchlistWeapons(std::string& tag);
    template<typename T>
    void cvSurv_registerWeaponFromImage(const std::string& imagePath,
                                        const std::string& name,
                                        const std::string& status,
                                        const std::string& description,
                                        const std::string& externalId,
                                        std::string& tag);
    template<typename T> std::string cvSurv_getWeaponRegionHistory(const std::string& weaponId, std::string& tag);

    // User Management
    template<typename T>
    void cvSurv_registerUser(const std::string& username,
                             const std::string& passwordHash,
                             const std::string& role,
                             const std::string& name,
                             const std::string& imagePath,
                             const std::string& phoneNumber,
                             const std::string& email,
                             int isActive,
                             std::string& tag);
    template<typename T>
    std::string cvSurv_validateUserPassword(const std::string& username, const std::string& inputPlaintextPassword, std::string& tag);
    template<typename T>
    void cvSurv_updateUserProfile(const std::string& username,
                                  const std::string& passwordHash,
                                  const std::string& role,
                                  const std::string& name,
                                  const std::string& imagePath,
                                  const std::string& phoneNumber,
                                  const std::string& email,
                                  int isActive,
                                  std::string& tag);
    template<typename T> void cvSurv_updateUserStatus(const std::string& userId, int activeState, std::string& tag);
    template<typename T> void cvSurv_deleteUserById(const std::string& userId, std::string& tag);
    template<typename T> void cvSurv_updateLastLogin(const std::string& userId, const std::string& timestamp, std::string& tag);
    template<typename T> void cvSurv_changeUserPassword(const std::string& userId, const std::string& newPasswordHash, std::string& tag);

    // Events
    template<typename T> std::string cvSurv_getAllEvents(std::string& tag);
    template<typename T> std::string cvSurv_getEventById(const std::string& eventId, std::string& tag);
    template<typename T> void cvSurv_deleteAllEvents(std::string& tag);
    template<typename T> void cvSurv_deleteEventById(const std::string& eventId, std::string& tag);

    // Recordings
    template<typename T> std::string cvSurv_getAllRecordings(std::string& tag);
    template<typename T> std::string cvSurv_getRecordingsByCameraId(const std::string& cameraId, std::string& tag);
    template<typename T> std::string cvSurv_getRecordingById(const std::string& id, std::string& tag);
    template<typename T> void cvSurv_deleteAllRecordings(std::string& tag);
    template<typename T> void cvSurv_deleteRecordingById(const std::string& id, std::string& tag);

    // Telemetry
    template<typename T>
    void cvSurv_logTelemetry(const std::string& metricType,
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
                             std::string& tag);
    template<typename T> void cvSurv_pruneTelemetryBefore(long long beforeTimestamp, std::string& tag);

    // Daily Face Metrics
    template<typename T>
    void cvSurv_insertDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp, std::string& tag);
    template<typename T> std::string cvSurv_getAllDailyFaceMetrics(std::string& tag);
    template<typename T> std::string cvSurv_getDailyFaceMetricsByDate(const std::string& detectionDate, std::string& tag);
    template<typename T> void cvSurv_deleteAllDailyFaceMetrics(std::string& tag);
    template<typename T> void cvSurv_deleteDailyFaceMetricsByDate(const std::string& detectionDate, std::string& tag);

    // Chat
    template<typename T>
    void cvSurv_insertChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt, std::string& tag);
    template<typename T> std::string cvSurv_getRecentChatMessages(int limit, std::string& tag);

    // Retrieves whatever query results have arrived since the last call --
    // see Surveillance::drainResponses()'s own doc comment. A poll, not a
    // blocking wait; there is no synchronous round trip across the process
    // boundary.
    template<typename T> std::vector<json> cvSurv_drainResponses(std::string& tag);

};




        /*****************************************************
         * @brief        ALARM MANAGEMENT MANAGER            *
         *****************************************************/



    /**
 * @class AlarmManager
 * @brief Microservice helper class that manages alarm operations for an ItemManager instance.
 *
 * The AlarmManager class provides functionality to integrate alarmâ€‘related features
 * into the parent ItemManager. This may include tasks such as monitoring conditions,
 * triggering alerts, and resetting alarms. It encapsulates alarmâ€‘specific responsibilities,
 * ensuring that these operations are performed in a controlled manner and tied directly
 * to the parent ItemManagerâ€™s state.
 *
 * @note This class does not own data itself; it operates on the parent ItemManagerâ€™s
 *       internal structures. It must always be constructed with a reference to an ItemManager.
 */     
class AlarmManager {
    ItemManager& parent;

public:
    /**
     * @brief Constructs an AlarmManager bound to a specific ItemManager.
     * @param p Reference to the ItemManager that this AlarmManager will operate on.
     */
    explicit AlarmManager(ItemManager& p) : parent(p) {}

    /**
     * @enum Event
     * @brief Enumeration of possible alarm events.
     */
    enum class Event {
        Intrusion,     ///< Unauthorized entry detected.
        Door_open,     ///< Door has been opened.
        Fire,          ///< Fire hazard detected.
        Access,        ///< Access granted or attempted.
        Warming,       ///< Warning or preâ€‘alert condition.
        Confirmation   ///< Confirmation of an action or event.
    };

    /**
     * @brief Trigger an alarm for a specific event on an item.
     * @tparam T The type of the item wrapper.
     * @param event The alarm event to trigger (e.g., Intrusion, Fire).
     * @param tag   The tag identifying the item to trigger the alarm on.
     */
    template<typename T>
    void triggerAlarm(Event event, std::string& tag);

    /**
     * @brief Set the volume level for the alarm.
     * @tparam T The type of the item wrapper.
     * @param level Volume level (0â€“100).
     * @param tag   The tag identifying the item to configure.
     */
    template<typename T>
    void setVolume(int level, std::string& tag);

    /**
     * @brief Set the duration for which the alarm should sound.
     * @tparam T The type of the item wrapper.
     * @param seconds Duration in seconds.
     * @param tag     The tag identifying the item to configure.
     */
    template<typename T>
    void setDuration(int seconds, std::string& tag);

    /**
     * @brief Set the default tone for alarms.
     * @tparam T The type of the item wrapper.
     * @param tone Name of the default tone.
     * @param tag  The tag identifying the item to configure.
     */
    template<typename T>
    void setDefaultTone(const std::string& tone, std::string& tag);

    /**
     * @brief Set a specific tone for alarms.
     * @tparam T The type of the item wrapper.
     * @param toneName Name of the tone to set.
     * @param filePath File path to the tone audio file.
     * @param tag      The tag identifying the item to configure.
     */
    template<typename T>
    void setTone(const std::string& toneName, const std::string& filePath, std::string& tag);

    /**
     * @brief Assign a specific tone to an event.
     * @tparam T The type of the item wrapper.
     * @param event    The event name to associate with the tone.
     * @param toneName The name of the tone to assign.
     * @param tag      The tag identifying the item to configure.
     */
    template<typename T>
    void assignTone(const std::string& event, const std::string& toneName, std::string& tag);

    /**
     * @brief Get the current volume level of the alarm.
     * @tparam T The type of the item wrapper.
     * @param tag The tag identifying the item to query.
     * @return Current volume level (0â€“100).
     */
    template<typename T>
    int getVolume(std::string& tag) const;

    /**
     * @brief Get the current duration for which the alarm sounds.
     * @tparam T The type of the item wrapper.
     * @param tag The tag identifying the item to query.
     * @return Current duration in seconds.
     */
    template<typename T>
    int getDuration(std::string& tag) const;

    /**
     * @brief Get the name of the default tone for alarms.
     * @tparam T The type of the item wrapper.
     * @param tag The tag identifying the item to query.
     * @return Name of the default tone.
     */
    template<typename T>
    std::string getDefaultTone(std::string& tag) const;

    /**
     * @brief Get the file path of a specific tone by name.
     * @tparam T The type of the item wrapper.
     * @param toneName The name of the tone.
     * @param tag      The tag identifying the item to query.
     * @return File path of the tone.
     */
    template<typename T>
    std::string getTone(const std::string& toneName, std::string& tag) const;

    /**
     * @brief Get the tone assigned to a specific event.
     * @tparam T The type of the item wrapper.
     * @param event The event name.
     * @param tag   The tag identifying the item to query.
     * @return Name of the tone assigned to the event.
     */
    template<typename T>
    std::string getAssignedTone(const std::string& event, std::string& tag) const;

    /**
     * @brief Reset the alarm system configuration to default values.
     * @tparam T The type of the item wrapper.
     * @param tag The tag identifying the item to reset.
     */
    template<typename T>
    void resetAlarmConfig(std::string& tag);

    /**
     * @brief Play a specific tone by name.
     * @tparam T The type of the item wrapper.
     * @param toneName The name of the tone to play.
     * @param tag      The tag identifying the item to play the tone on.
     */
    template<typename T>
    void playTone(const std::string& toneName, std::string& tag);

private:
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

