
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
    std::unique_ptr<BaseMicroservice> _networkManager = 
                  MicroserviceManager::createMicroObjects("Network Agent");
    std::unique_ptr<BaseMicroservice> _faceRecognitionManager = 
                  MicroserviceManager::createMicroObjects("Face Recognition");   
    std::unique_ptr<BaseMicroservice> _faceWatchlistManager = 
                  MicroserviceManager::createMicroObjects("Face Watchlist");    
    std::unique_ptr<BaseMicroservice> _motionDetectionManager = 
                  MicroserviceManager::createMicroObjects("Motion Detection");                                    
   

    

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


    ItemWrapper(std::shared_ptr<T> obj, const std::string& tag = "")
         : data(std::move(obj)), tag(tag),id_(IdProvider::generateId()) {} 

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

    
   // Returns the unique identifier string for this item.
// Used to distinguish objects within the system.
std::string getId() const override {
    return id_;
}

// Logs the current item’s ID and tag to the console.
// Provides human-readable output for debugging and monitoring.
void logId() const override {
    std::cout << Logger::getColorCode(LogColor::WHITE)
              + "::: [ItemWrapper] Tag: " << tag << " | ID: " << id_
              << Logger::getColorCode(LogColor::RESET) << std::endl;
}

// Displays the item’s details in a human-readable format.
// Typically overridden to show custom attributes.
void display() const override;

// Returns the type name of the wrapped object.
// Useful for reflection, debugging, or serialization.
std::string getTypeName() const override;

// Serializes the item into a JSON object.
// Captures all relevant fields for persistence or network transfer.
json serialize() const override;

// Creates a deep copy of the item and returns it as a shared pointer.
// Ensures safe duplication without breaking ownership semantics.
std::shared_ptr<BaseItem> clone() const override;

// Returns the tag string associated with this item.
// Tags provide a secondary label for categorization or lookup.
std::string getTag() const override;

// Provides mutable access to the underlying data object.
// Allows modification of the wrapped data.
T& getData();

// Provides read-only access to the underlying data object.
// Ensures safe inspection without modification.
const T& getData() const;

// Provides mutable access to the underlying data object.
// Explicitly signals intent to modify the data.
T& getMutableData();

// Converts the item into a JSON representation using nlohmann::json.
// Similar to serialize(), but leverages the nlohmann JSON library directly.
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

// Sends a message to another service or client.
//  - payload: the serialized data to transmit (JSON, XML, etc.).
//  - recipientID: identifier of the target service or client.
// This function encapsulates the networking call, ensuring delivery across nodes.
void sendMessage(const std::string& payload, const std::string& recipientID);

// Receives an incoming message from the network.
//  - msg: a structured Message object containing sender info, payload, and metadata.
// This function acts as the entry point for handling requests, events, or commands
// sent by other services. It can be overridden or extended to implement custom logic.
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


// Launches the restricted area monitoring process using a Haar cascade or other detector.
// If no path is provided, defaults are used.
void runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath);

// Returns a reference to the current set of restricted tracks.
// Each track is keyed by an integer ID and contains face tracking data.
const std::unordered_map<int, FaceTrack>& getRunRestrictedTracks() const;

// Adds a new restricted track by associating an ID with a detected face.
// Useful for registering individuals in restricted zones.
void addRunRestictedTrack(int id, const FaceTrack& track);

// Resets all restricted tracks currently being monitored.
// Clears state and prepares for fresh tracking.
void restRunRestrictedTracks();

// Removes a specific restricted track by ID.
// Allows selective cleanup of tracked individuals.
void removeRunRestrictedTrack(int id);

// Resets the configuration parameters for restricted monitoring to defaults.
// Ensures a clean baseline for detection.
void resetRunRestrictedConfig();

// Sets the scale factor for face detection (image pyramid scaling).
// Higher values speed up detection but may reduce accuracy.
void setRunRestrictedScaleFactor(double scaleFactor);

// Sets the minimum number of neighbor rectangles required for a valid detection.
// Controls sensitivity to false positives.
void setRunRestrictedMinNeighbors(int minNeighbors);

// Sets the minimum face size to detect.
// Prevents small, noisy detections from being considered valid.
void setRunRestrictedMinFaceSize(const cv::Size& size);

// Sets the Intersection-over-Union (IoU) threshold for track matching.
// Higher thresholds enforce stricter overlap requirements between detections and tracks.
void setIouMatchThreshold(double threshold);

// Retrieves the current scale factor used in restricted monitoring.
double getRunRestrictedScaleFactor() const;

// Retrieves the current minimum neighbors setting.
int getRunRestrictedMinNeighbors() const;

// Retrieves the current minimum face size setting.
cv::Size getRunRestrictedMinFaceSize() const;

// Retrieves the current IoU threshold for track matching.
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

// Start monitoring the camera feed using a Haar cascade or DNN model.
// If cascadePath is provided, it loads that specific face/feature detector.
void monitorCamera(const std::string& cascadePath = "");

// Get the current detection threshold used in monitorCamera (e.g., confidence level).
double getmonitorCamerathreshold() const;

// Set a new detection threshold for monitorCamera (controls sensitivity).
void setmonitorCamerathreshold(double newThreshold);

// Reset the detection threshold back to its default value.
void resetmonitorCamerathreshold();

// Get the currently configured cascade file path (used for face/object detection).
std::string getmonitorCameracascadePath() const;

// Set a new cascade file path (e.g., Haar cascade XML or DNN model file).
void setmonitorCameracascadePath(const std::string& path);

// Load a set of known face images from file paths into memory for recognition.
// Each file should represent one known individual.
void loadKnownFaces(const std::vector<std::string>& filePaths);

// Get the number of known faces currently loaded into memory.
size_t getKnownFaceCount() const;

// Clear all loaded known faces from memory (reset recognition database).
void resetKnownFaces();





    /*
              MICROSERVEICE SECTION
          *****************************

          =======================================================================================
          | Motion Detection Funtions                                                          |
          =======================================================================================
          |                           Unified Monitor API                                      |

          | These functions provide a complete interface for managing motion detection         |
          | within the Smart_Store framework. They allow any object inheriting BaseMicroservice|
          | to expose motion detection capabilities when needed.                               |
          |                                                                                     |
          | Core responsibilities:                                                              |
          |  - Start unified monitoring on a camera feed.                                       |
          |  - Manage zones (add, clear).                                                       |
          |  - Configure detection parameters (diff threshold, min area, crowd threshold, etc.).|
          |  - Query current configuration and tracking state.                                  |
          =======================================================================================
    */

// Set the pixel intensity difference threshold used for motion detection.
// Higher values make detection less sensitive to small changes.
void setMonitorDetectionDiffThreshold(double threshold);

// Set the minimum contour area (in pixels) required to count as motion.
// Filters out small noise or irrelevant movements.
void setMonitorDetectionMinArea(int area);

// Set the crowd threshold (number of people/objects) required to trigger a crowd alert.
void setMonitorDetectionCrowdThreshold(std::size_t threshold);

// Set the loitering threshold in seconds (time an object/person must remain to trigger alert).
void setMonitorDetectionLoiterSeconds(int loiterSeconds);

// Bulk update of all motion detection configuration parameters at once.
void setMonitorDetectionConfig(double diffThreshold,
                               int minArea,
                               std::size_t crowdThreshold,
                               int loiterSeconds,
                               int leftBehindSeconds,
                               bool enableTracking);

// Register a callback function to be invoked when an alert is triggered.
// The callback receives the alert message string.
void setMonitorDetectionAlertCallback(std::function<void(const std::string&)> cb);

// Reset motion detection configuration back to default values.
void resetMonitorDetectionConfig();

// Reset the entire motion detection system (config + state).
void resetMonitorDetection();

// Clear all defined detection zones (restricted areas).
void clearMonitorDetectionZones();

// Check if object/person tracking is currently enabled.
bool isMonitorDetectionTrackingEnabled() const;

// Check if an alert callback has been registered.
bool hasMonitorDetectionCallback() const;


// Get the current pixel intensity difference threshold.
double getMonitorDetectionDiffThreshold() const;

// Get the current minimum contour area threshold.
int getMonitorDetectionMinArea() const;

// Get the current crowd threshold (number of people/objects).
std::size_t getMonitorDetectionCrowdThreshold() const;

// Get the current loitering threshold in seconds.
int getMonitorDetectionLoiterSeconds() const;

// Get the current left-behind object threshold in seconds.
int getMonitorDetectionLeftBehindSeconds() const;

// Start monitoring a camera feed by index (e.g., 0 for default webcam).
void runMonitorDetectionMonitorCamera(int cameraIndex);

// Adds a new detection zone (region of interest) to be monitored for motion events
void addMonitorDetectionZone(const Zone& zone);



    
};

#include "ItemWrapper.tpp"  // Template definitions should be included at the end of the header


#endif // ITEM_MANAGER_H