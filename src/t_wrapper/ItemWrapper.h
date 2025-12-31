
//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::

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
#include "microservice_interface/MicroserviceManager.hpp"



using json = nlohmann::json;

//    ==========================================================
//   |-- This ItemWrapper class  wraps every single item        |
//   |-- that are stored in the ItemManagert class.             |
//    ==========================================================





// :: Forward declaration of IdProvider to generate unique IDs
// ***********************************************************

namespace IdProvider {

    /**
     * @brief Generate a unique ID for an item.
     * @return A unique ID string.
     */
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
    

    /**
     * @brief Demangle a type name.
     * @param mangledName The mangled type name.
     * @return The demangled type name.
     */
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

/**
 * @brief Constructor to create an ItemWrapper with given data and optional tag.
 * @param obj Shared pointer to the data object.
 * @param tag Optional tag string.
 */
ItemWrapper(std::shared_ptr<T> obj, const std::string& tag = "")
        : data(std::move(obj)), tag(tag),id_(IdProvider::generateId()) {} 

/**
 * @brief Constructor to create an ItemWrapper from a JSON object.     
 */
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

    
/**
* @brief Get the unique identifier string for this item.
* 
* Used to distinguish objects within the system.
* 
* @return std::string Unique identifier string.
*/
std::string getId() const override {
    return id_;
}

/**
* @brief Log the current item’s ID and tag to the console.
* 
* Provides human-readable output for debugging and monitoring.
*/
void logId() const override {
    std::cout << Logger::getColorCode(LogColor::WHITE)
              + "::: [ItemWrapper] Tag: " << tag << " | ID: " << id_
              << Logger::getColorCode(LogColor::RESET) << std::endl;
}

/**
* @brief Display the item’s details in a human-readable format.
* 
* Typically overridden to show custom attributes.
*/
void display() const override;


/**
* @brief Returns the type name of the wrapped object.
* @return The type name as a string.
*/
std::string getTypeName() const override;

/**
* @brief Serialize the item into a JSON object.
* 
* Captures all relevant fields for persistence or network transfer.
* 
* @return json JSON object containing serialized item data.
*/
json serialize() const override;

/**
* @brief Create a deep copy of the item.
* 
* Returns a shared pointer to a new instance, ensuring safe duplication
* without breaking ownership semantics.
* 
* @return std::shared_ptr<BaseItem> Deep copy of the item.
*/
std::shared_ptr<BaseItem> clone() const override;

/**
* @brief Get the tag string associated with this item.
* 
* Tags provide a secondary label for categorization or lookup.
* 
* @return std::string Tag string.
*/
std::string getTag() const override;

/**
* @brief Provide mutable access to the underlying data object.
* 
* Allows modification of the wrapped data.
* 
* @return T& Reference to the underlying data object.
*/
T& getData();

/**
* @brief Provide read-only access to the underlying data object.
* 
* Ensures safe inspection without modification.
* 
* @return const T& Const reference to the underlying data object.
*/
const T& getData() const;

/**
* @brief Provide mutable access to the underlying data object.
* 
* Explicitly signals intent to modify the data.
* 
* @return T& Reference to the underlying data object.
*/
T& getMutableData();

/**
* @brief Convert the item into a JSON representation using nlohmann::json.
* 
* Similar to serialize(), but leverages the nlohmann JSON library directly.
* 
* @return nlohmann::json JSON representation of the item.
*/
nlohmann::json toJson() const override;


    






/*
            MICROSERVEICE SECTION
        *****************************

        =======================================================================================
        | Networking Agent Funtions                                                           |
        =======================================================================================
        |                                  Messaging API                                      |

        | These functions provide a communication interface for Smart_Store microservices.    |
        | They allow objects to send and receive messages across the network using the        |
        | underlying net_curl/libcurl infrastructure.                                         |
        |                                                                                     |
        | Core responsibilities:                                                              |
        |  - Enable distributed services to exchange data (payloads, commands, events).       |
        |  - Abstract away low-level networking details, exposing a clean API.                |
        |  - Support extensibility: any object inheriting BaseMicroservice can participate    |
        |    in messaging without reimplementing transport logic.                             |
        =======================================================================================
*/
private:
    std::unique_ptr<BaseMicroservice> _networkManager = 
                  MicroserviceManager::createMicroObjects("Network Agent");   
public:

/**
* @brief Send a message to another service or client.
* 
* Encapsulates the networking call, ensuring delivery across nodes.
* 
* @param payload     Serialized data to transmit (e.g., JSON, XML).
* @param recipientID Identifier of the target service or client.
*/
void sendMessage(const std::string& payload, const std::string& recipientID);

/**
* @brief Receive an incoming message from the network.
* 
* Acts as the entry point for handling requests, events, or commands sent by other services.
* Can be overridden or extended to implement custom logic.
* 
* @param msg Structured Message object containing sender info, payload, and metadata.
*/
void receiveMessage(const Message& msg);









/*
            MICROSERVEICE SECTION
        *****************************

        =========================================================================================
        | Face Recognition Funtions                                                             |
        =========================================================================================
        |                           Restricted Area Monitoring API                              |
                                    
        | These functions provide a complete interface for managing face recognition            |
        | within restricted zones. The design follows Smart_Store’s philosophy:                 |
        | every object can inherit computer vision features and expose them when needed.        | 
        |                                                                                       |
        | Core responsibilities:                                                                |
        |  - Start monitoring with a given cascade classifier.                                  |
        |  - Manage tracked faces (add, remove, reset).                                         |
        |  - Configure detection parameters (scale factor, neighbors, face size, IOU threshold).|
        |  - Query current configuration and tracked state.                                     |
        =========================================================================================
*/
private:
    std::unique_ptr<BaseMicroservice> _faceRecognitionManager = 
                  MicroserviceManager::createMicroObjects("Face Recognition");   
public:

/**
* @brief Launch the restricted area monitoring process using a Haar cascade or other detector.
* 
* If no cascadePath is provided, default detection models are used.
* 
* @param cameraIndex Index of the camera to monitor (e.g., 0 for default webcam).
* @param cascadePath Path to Haar cascade XML or other detector model file.
*/
void runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath);

/**
* @brief Get the current set of restricted tracks.
* 
* Each track is keyed by an integer ID and contains face tracking data.
* 
* @return const std::unordered_map<int, FaceTrack>& Reference to restricted tracks.
*/
const std::unordered_map<int, FaceTrack>& getRunRestrictedTracks() const;

/**
* @brief Add a new restricted track by associating an ID with a detected face.
* 
* Useful for registering individuals in restricted zones.
* 
* @param id    Unique identifier for the track.
* @param track FaceTrack object containing tracking data.
*/
void addRunRestictedTrack(int id, const FaceTrack& track);

/**
* @brief Reset all restricted tracks currently being monitored.
* 
* Clears state and prepares for fresh tracking.
*/
void restRunRestrictedTracks();

/**
* @brief Remove a specific restricted track by ID.
* 
* Allows selective cleanup of tracked individuals.
* 
* @param id Unique identifier of the track to remove.
*/
void removeRunRestrictedTrack(int id);

/**
* @brief Reset the configuration parameters for restricted monitoring to defaults.
* 
* Ensures a clean baseline for detection.
*/
void resetRunRestrictedConfig();

/**
* @brief Set the scale factor for face detection (image pyramid scaling).
* 
* Higher values speed up detection but may reduce accuracy.
* 
* @param scaleFactor Scale factor for detection.
*/
void setRunRestrictedScaleFactor(double scaleFactor);

/**
* @brief Set the minimum number of neighbor rectangles required for a valid detection.
* 
* Controls sensitivity to false positives.
* 
* @param minNeighbors Minimum number of neighbors.
*/
void setRunRestrictedMinNeighbors(int minNeighbors);

/**
* @brief Set the minimum face size to detect.
* 
* Prevents small, noisy detections from being considered valid.
* 
* @param size Minimum face size (cv::Size).
*/
void setRunRestrictedMinFaceSize(const cv::Size& size);

/**
* @brief Set the Intersection-over-Union (IoU) threshold for track matching.
* 
* Higher thresholds enforce stricter overlap requirements between detections and tracks.
* 
* @param threshold IoU threshold value.
*/
void setIouMatchThreshold(double threshold);

/**
* @brief Get the current scale factor used in restricted monitoring.
* 
* @return double Current scale factor.
*/
double getRunRestrictedScaleFactor() const;

/**
* @brief Get the current minimum neighbors setting.
* 
* @return int Current minimum neighbors value.
*/
int getRunRestrictedMinNeighbors() const;

/**
* @brief Get the current minimum face size setting.
* 
* @return cv::Size Current minimum face size.
*/
cv::Size getRunRestrictedMinFaceSize() const;

/**
* @brief Get the current IoU threshold for track matching.
* 
* @return double Current IoU threshold value.
*/
double getIouMatchThreshold() const;






/*
            MICROSERVEICE SECTION
        *****************************

        =======================================================================================
        | Face Watchlist Funtions                                                             |
        =======================================================================================
        |                           Watchlist Management API                                  |

        | These functions provide a complete interface for managing face watchlists           |
        | within the Smart_Store framework. They allow any object inheriting BaseMicroservice |
        | to expose watchlist capabilities when needed.                                       |
        |                                                                                     |
        | Core responsibilities:                                                              |
        |  - Load known faces into the watchlist.                                             |
        |  - Check detected faces against the watchlist.                                      |
        |  - Configure similarity thresholds and cascade paths.                               |
        |  - Query current watchlist state.                                                   |
        =======================================================================================
*/
private:
    std::unique_ptr<BaseMicroservice> _faceWatchlistManager = 
                  MicroserviceManager::createMicroObjects("Face Watchlist");    
public:

/**
* @brief Start monitoring the camera feed using a Haar cascade or DNN model.
* 
* If a cascadePath is provided, it loads that specific face/feature detector.
* If left empty, a default detector is used.
* 
* @param cascadePath Path to Haar cascade XML or DNN model file (optional).
*/
void monitorCamera(const std::string& cascadePath = "");

/**
* @brief Get the current detection threshold used in monitorCamera.
* 
* Typically represents the confidence level required for detection.
* 
* @return double Current detection threshold value.
*/
double getmonitorCamerathreshold() const;

/**
* @brief Set a new detection threshold for monitorCamera.
* 
* Controls sensitivity: lower values make detection more sensitive, higher values less sensitive.
* 
* @param newThreshold New detection threshold value.
*/
void setmonitorCamerathreshold(double newThreshold);

/**
* @brief Reset the detection threshold back to its default value.
*/
void resetmonitorCamerathreshold();

/**
* @brief Get the currently configured cascade file path.
* 
* This path points to the Haar cascade XML or DNN model file used for face/object detection.
* 
* @return std::string Current cascade file path.
*/
std::string getmonitorCameracascadePath() const;

/**
* @brief Set a new cascade file path for face/object detection.
* 
* @param path Path to Haar cascade XML or DNN model file.
*/
void setmonitorCameracascadePath(const std::string& path);

/**
* @brief Load a set of known face images from file paths into memory for recognition.
* 
* Each file should represent one known individual. Used for face recognition tasks.
* 
* @param filePaths Vector of file paths to known face images.
*/
void loadKnownFaces(const std::vector<std::string>& filePaths);

/**
* @brief Get the number of known faces currently loaded into memory.
* 
* @return size_t Number of known faces.
*/
size_t getKnownFaceCount() const;

/**
* @brief Clear all loaded known faces from memory.
* 
* Resets the recognition database to an empty state.
*/
void resetKnownFaces();






/*
            MICROSERVEICE SECTION
        *****************************

        =======================================================================================
        | Motion Detection Funtions                                                           |
        =======================================================================================
        |                           Unified Monitor API                                       |

        | These functions provide a complete interface for managing motion detection          |
        | within the Smart_Store framework. They allow any object inheriting BaseMicroservice |
        | to expose motion detection capabilities when needed.                                |
        |                                                                                     |
        | Core responsibilities:                                                              |
        |  - Start unified monitoring on a camera feed.                                       |
        |  - Manage zones (add, clear).                                                       |
        |  - Configure detection parameters (diff threshold, min area, crowd threshold, etc.).|
        |  - Query current configuration and tracking state.                                  |
        =======================================================================================
*/
private:
    std::unique_ptr<BaseMicroservice> _motionDetectionManager = 
                MicroserviceManager::createMicroObjects("Motion Detection");
public:                  

/**
* @brief Set the pixel intensity difference threshold used for motion detection.
 * 
* Higher values make detection less sensitive to small changes in pixel intensity.
* 
* @param threshold Difference threshold value.
*/
void setMonitorDetectionDiffThreshold(double threshold);

/**
* @brief Set the minimum contour area required to count as motion.
* 
* Filters out small noise or irrelevant movements by ignoring contours smaller than this area.
* 
* @param area Minimum contour area in pixels.
*/
void setMonitorDetectionMinArea(int area);

/**
* @brief Set the crowd threshold required to trigger a crowd alert.
* 
* Defines the number of people/objects that must be detected simultaneously to raise an alert.
* 
* @param threshold Crowd threshold (number of objects).
*/
void setMonitorDetectionCrowdThreshold(std::size_t threshold);

/**
* @brief Set the loitering threshold in seconds.
* 
* Specifies how long an object/person must remain in the same place to trigger a loitering alert.
* 
* @param loiterSeconds Time threshold in seconds.
*/
void setMonitorDetectionLoiterSeconds(int loiterSeconds);

/**
* @brief Bulk update of all motion detection configuration parameters at once.
* 
* @param diffThreshold   Pixel intensity difference threshold.
* @param minArea         Minimum contour area in pixels.
* @param crowdThreshold  Crowd threshold (number of objects).
* @param loiterSeconds   Loitering threshold in seconds.
* @param leftBehindSeconds Left-behind object threshold in seconds.
* @param enableTracking  Enable or disable object/person tracking.
*/
void setMonitorDetectionConfig(double diffThreshold,
                               int minArea,
                               std::size_t crowdThreshold,
                               int loiterSeconds,
                               int leftBehindSeconds,
                               bool enableTracking);

/**
* @brief Register a callback function to be invoked when an alert is triggered.
* 
* The callback receives the alert message string.
* 
* @param cb Callback function taking a std::string message.
*/
void setMonitorDetectionAlertCallback(std::function<void(const std::string&)> cb);

/**
* @brief Reset motion detection configuration back to default values.
*/
void resetMonitorDetectionConfig();

/**
* @brief Reset the entire motion detection system (configuration + state).
*/
void resetMonitorDetection();

/**
* @brief Clear all defined detection zones (restricted areas).
*/
void clearMonitorDetectionZones();

/**
* @brief Check if object/person tracking is currently enabled.
* 
* @return true if tracking is enabled, false otherwise.
*/
bool isMonitorDetectionTrackingEnabled() const;

/**
* @brief Check if an alert callback has been registered.
* 
* @return true if a callback is registered, false otherwise.
*/
bool hasMonitorDetectionCallback() const;

/**
* @brief Get the current pixel intensity difference threshold.
* 
* @return double Current difference threshold value.
*/
double getMonitorDetectionDiffThreshold() const;

/**
* @brief Get the current minimum contour area threshold.
* 
* @return int Current minimum contour area in pixels.
*/
int getMonitorDetectionMinArea() const;

/**
* @brief Get the current crowd threshold.
* 
* @return std::size_t Current crowd threshold (number of objects).
*/
std::size_t getMonitorDetectionCrowdThreshold() const;

/**
* @brief Get the current loitering threshold in seconds.
* 
* @return int Current loitering threshold in seconds.
*/
int getMonitorDetectionLoiterSeconds() const;

/**
* @brief Get the current left-behind object threshold in seconds.
* 
* @return int Current left-behind threshold in seconds.
*/
int getMonitorDetectionLeftBehindSeconds() const;

/**
* @brief Start monitoring a camera feed by index.
* 
* @param cameraIndex Index of the camera (e.g., 0 for default webcam).
*/
void runMonitorDetectionMonitorCamera(int cameraIndex);

/**
* @brief Add a new detection zone (region of interest) to be monitored for motion events.
* 
* @param zone Zone object defining the region of interest.
*/
void addMonitorDetectionZone(const Zone& zone);






/*
            MICROSERVEICE SECTION
        *****************************

        =======================================================================================
        | QR Code Scanner Funtions                                                            |
        =======================================================================================
        |                           Unified Monitor API                                       |

        | These functions provide a complete interface for managing QR code scanning          |
        | within the Smart_Store framework. They allow any object inheriting BaseMicroservice |
        | to expose motion detection capabilities when needed.                                |
        |                                                                                     |
        | Core responsibilities:                                                              |
        |  - Start unified monitoring on a camera feed.                                       |
        |  - Manage zones (add, clear).                                                       |
        |  - Configure detection parameters (threshold, min area, crowd threshold, etc.).     |
        |  - Query current configuration and tracking state.                                  |
        =======================================================================================
*/
private:
std::unique_ptr<BaseMicroservice> _qrCodeScannerManager = 
                MicroserviceManager::createMicroObjects("QR Code Scanner");
public:              

/**
* @brief Scan a QR code from the camera feed.
* @return The decoded QR code string, or empty if none found.
*/
std::string scanFromCamera();

/**
* @brief Scan a QR code from an image file.
* @param imagePath Path to the image file.
* @return The decoded QR code string, or empty if none found.
*/
std::string scanFromFile(const std::string& imagePath);

/**
* @brief Reset the QR code scanner configuration to default values.
*/
void resetConfig();

/**
* @brief Check if preview window is enabled during scanning.
* @return true if preview is enabled, false otherwise.
*/
bool isPreviewEnabled() const;

/**
* @brief Get the current camera index used for scanning.
* @return Camera device index otherwise return -1.
*/
int getCameraIndex() const;

/**
* @brief Set the camera index to use for scanning.
* @param index Camera device index (0 = default webcam).
*/
void setCameraIndex(int index);

/**
* @brief Enable or disable preview window during camera scanning.
* @param enabled True to show preview window, false otherwise.
*/
void setPreviewEnabled(bool enabled);






/*
        MICROSERVEICE SECTION
    *****************************

    =======================================================================================
    | Document Scanner Functions                                                          |
    =======================================================================================
    |                           Unified Monitor API                                       |

    | These functions provide a complete interface for managing document scanning         |
    | within the Smart_Store framework. They allow any object inheriting BaseMicroservice |
    | to expose document scanning capabilities when needed.                               |
    |                                                                                     |
    | Core responsibilities:                                                              |
    |  - Start unified monitoring on a camera feed.                                       |
    |  - Manage zones (add, clear).                                                       |
    |  - Configure detection parameters (threshold, min area, crowd threshold, etc.).     |
    |  - Query current configuration and tracking state.                                  |
    =======================================================================================
*/

private:
    std::unique_ptr<BaseMicroservice> _documentScanner = 
                  MicroserviceManager::createMicroObjects("Document Scanner");   
public:

/**
* @brief Set the Canny edge detection thresholds.
* 
* @param low  Lower threshold for the Canny edge detector.
* @param high Upper threshold for the Canny edge detector.
*/
void setEdgeThreshold(int low, int high);

/**
* @brief Set the minimum contour area for detection.
* 
* @param area Minimum area (in pixels) that a contour must have to be considered valid.
*/
void setContourMinArea(double area);

/**
* @brief Set the output image size.
* 
* @param width  Desired output width in pixels.
* @param height Desired output height in pixels.
*/
void setOutputSize(int width, int height);

/**
* @brief Set the sharpening amount applied to the output.
* 
* @param amount Sharpening intensity (e.g., 0.0 = none, higher values = stronger sharpening).
*/
void setSharpening(double amount);

/**
* @brief Get the lower Canny edge threshold.
* 
* @return int Current lower threshold value.
*/
int getCannyLow() const;

/**
* @brief Get the upper Canny edge threshold.
* 
* @return int Current upper threshold value.
*/
int getCannyHigh() const;

/**
* @brief Get the minimum contour area used for detection.
* 
* @return double Current minimum contour area in pixels.
*/
double getMinContourArea() const;

/**
* @brief Get the configured output width.
* 
* @return int Current output width in pixels.
*/
int getOutputWidth() const;

/**
* @brief Get the configured output height.
* 
* @return int Current output height in pixels.
*/
int getOutputHeight() const;

/**
* @brief Get the sharpening amount applied to the output.
* 
* @return double Current sharpening intensity.
*/
double getSharpening() const;

/**
* @brief Run the scanner with the specified mode and input/output paths.
* 
* @param mode   Processing mode (e.g., "scan", "detect", "process").
* @param input  Path to the input file or stream.
* @param output Path to the output file or destination.
*/
void run(const std::string& mode, const std::string& input, const std::string& output);







/*
        MICROSERVICE SECTION
    *****************************

    =======================================================================================
    | Alarm System Functions                                                     |
    =======================================================================================
    |                           Unified Alarm System API                                  |

    | The Alarm System microservice provides a standardized interface for monitoring and  |
    | triggering alerts within the Smart_Store framework. It extends BaseMicroservice to  |
    | ensure consistent integration with other services while offering specialized        |
    | detection and alarm capabilities.                                                   |
    |                                                                                     |
    | Core responsibilities:                                                              |
    |  - Trigger alarms and propagate notifications to other subsystems.                  |
    |  - Query current configuration and alarm state for reporting and diagnostics.       |
    =======================================================================================
*/

private:
std::unique_ptr<BaseMicroservice> _alarmManager = 
                MicroserviceManager::createMicroObjects("Alarm System");
public:

/**
* @brief Trigger an alarm with the specified message.
* 
* This function activates the alarm system and propagates the alert to other subsystems.
* @param message The alarm message to be sent.
*/
void triggerAlarm(const std::string& event);

/**
* @brief Set the volume level for the alarm.
* 
* @param level Volume level (0-100).
*/
void setVolume(int level);

/**
* @brief Set the duration for which the alarm should sound.
* 
* @param seconds Duration in seconds.
*/
void setDuration(int seconds);

/**
* @brief Set the default tone for alarms.
* 
* @param tone The name of the default tone to set.
*/
void setDefaultTone(const std::string& tone);

/**
* @brief Set a specific tone for alarms.
* 
* @param toneName The name of the tone to set.
* @param filePath The file path to the tone audio file.
*/
void setTone(const std::string& toneName, const std::string& filePath);

/**
* @brief Assign a specific tone to an event.
* 
* @param event The event name to associate with the tone.
* @param toneName The name of the tone to assign.
*/
void assignTone(const std::string& event, const std::string& toneName);

/**
* @brief Get the current volume level of the alarm.
* @return int Current volume level (0-100).
*/
int getVolume() const;

/**
* @brief Get the current duration for which the alarm sounds.
* @return int Current duration in seconds.
*/
int getDuration() const;

/**
* @brief Get the name of the default tone for alarms.
* @return std::string Name of the default tone.
*/
std::string getDefaultTone() const;

/**
* @brief Get the file path of a specific tone by name.
* @param toneName The name of the tone.
* @return std::string File path of the tone.
*/
std::string getTone(const std::string& toneName) const;

/**
* @brief Get the tone assigned to a specific event.
* @param event The event name.
* @return std::string Name of the tone assigned to the event.
*/
std::string getAssignedTone(const std::string& event) const;

/**
* @brief Reset the alarm system configuration to default values.
*/
void resetAlarmConfig();

/**
* @brief Play a specific tone by name.
* @param toneName The name of the tone to play.
*/
void playTone(const std::string& toneName);
    
};

#include "ItemWrapper.tpp"  // Template definitions should be included at the end of the header

#endif // ITEM_MANAGER_H