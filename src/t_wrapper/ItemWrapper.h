
//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  Â© 2025 Victor. All rights reserved. ::
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
#include <cstdint>
#include <string>
#include <optional>
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
#include <vector>
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
* @brief Log the current itemâ€™s ID and tag to the console.
* 
* Provides human-readable output for debugging and monitoring.
*/
void logId() const override {
    std::cout << Logger::getColorCode(LogColor::WHITE)
              + "::: [ItemWrapper] Tag: " << tag << " | ID: " << id_
              << Logger::getColorCode(LogColor::RESET) << std::endl;
}

/**
* @brief Display the itemâ€™s details in a human-readable format.
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
        | Surveillance Funtions                                                               |
        =======================================================================================
        | Thin handle onto the standalone surveillance system -- start/stop/status only, via  |
        | a sentinel file the surveillance app's own frontend watches. See Surveillance.hpp    |
        | for why this stays thin rather than owning that app's FeatureConfigManager state.    |
        =======================================================================================
*/
private:
    std::unique_ptr<BaseMicroservice> _surveillanceManager =
                MicroserviceManager::createMicroObjects("Surveillance");
public:

/**
* @brief Mark surveillance as started (writes the sentinel file).
*/
void startSurveillance();

/**
* @brief Mark surveillance as stopped (removes the sentinel file).
*/
void stopSurveillance();

/**
* @brief Check whether surveillance is currently marked as running.
*
* @return true if running, false otherwise.
*/
bool isSurveillanceRunning() const;

/**
* @brief Queue a cloud-sync enable/disable command.
*/
void setSurveillanceCloudEnabled(bool enabled);

/**
* @brief Queue the rest of the cloud settings (base URL, station name,
* hardware token, poll interval).
*/
void setSurveillanceCloudSettings(const std::string& baseUrl,
                                  const std::string& stationName,
                                  const std::string& hardwareToken,
                                  int pollIntervalSec);

/**
* @brief Queue a new-camera command.
*/
void addSurveillanceCamera(const std::string& name,
                           const std::string& id,
                           const std::string& source,
                           bool enableFaceRecognition);

/**
* @brief Queue a per-camera feature toggle command.
*/
void setSurveillanceCameraFeature(const std::string& cameraId, const std::string& featureKey, bool value);

/**
* @brief Queue an edit to an existing camera's identity/connection fields.
*/
void updateSurveillanceCamera(const std::string& camId,
                              const std::string& name,
                              const std::string& source);

/**
* @brief Queue a camera-removal command.
*/
void removeSurveillanceCamera(const std::string& camId);

/**
* @brief Queue a pending-camera-plug-in command.
*/
void plugInSurveillancePendingCamera(const std::string& camName, const std::string& camId);

/**
* @brief Queue a plug-out (to pending, or delete) command.
*/
void plugOutSurveillancePendingCamera(bool isDelete, const std::string& camName, const std::string& camId);

/**
* @brief Queue a feature toggle applied to every configured camera.
*/
void setSurveillanceFeatureForAllCameras(const std::string& featureKey, bool value);

/**
* @brief Queue a model upload/replace command.
*/
void upLoadSurveillanceModel(const std::string& modelType, const std::string& modelPath);

/**
* @brief Queue a model-settings reset command.
*/
void resetSurveillanceModelSettings();

/**
* @brief Queue a beep-channel enable/disable command.
*/
void setSurveillanceBeepEnabled(bool enabled);

/**
* @brief Queue a voice-channel enable/disable command.
*/
void setSurveillanceVoiceEnabled(bool enabled);

/**
* @brief Queue a voice-gender command.
*/
void setSurveillanceVoiceGender(const std::string& gender);

/**
* @brief Queue a station-location command.
*/
void setSurveillanceStationLocation(double latitude, double longitude);

/**
* @brief Queue a face-settings reset command for one camera.
*/
void resetSurveillanceFaceSettings(const std::string& camId);

/**
* @brief Queue a motion-settings reset command for one camera.
*/
void resetSurveillanceMotionSettings(const std::string& camId);

/**
* @brief Queue an add-motion-zone command for one camera.
*/
void addSurveillanceMotionZone(const std::string& camId,
                               const std::string& regionName,
                               int x, int y, int width, int height,
                               bool restricted);

/**
* @brief Queue a clear-motion-zones command for one camera.
*/
void clearSurveillanceMotionZones(const std::string& camId);

/**
* @brief Queue an add-plate-to-watchlist command.
*/
void addSurveillancePlateToWatchlist(const std::string& plate);

/**
* @brief Queue a remove-plate-from-watchlist command.
*/
void removeSurveillancePlateFromWatchlist(const std::string& plate);

/**
* @brief Queue a clear-plate-watchlist command.
*/
void clearSurveillancePlateWatchlist();

/**
* @brief Queue a set-account-id command.
*/
void setSurveillanceAccountId(const std::string& accountId);

/**
* @brief Queue a set-kafka-broker-address command.
*/
void setSurveillanceKafkaBrokerAddress(const std::string& brokerAddress);

/**
* @brief Queue a set-face-settings command for one camera.
*/
void setSurveillanceFaceSettings(const std::string& camId,
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
                                 bool debugLogging);

/**
* @brief Queue a set-motion-settings command for one camera.
*/
void setSurveillanceMotionSettings(const std::string& camId,
                                   double diffThreshold,
                                   int minArea,
                                   std::size_t crowdThreshold,
                                   int loiterSeconds,
                                   int leftBehindSeconds,
                                   bool enableTracking,
                                   bool debugLogging);

/**
* @brief Queue a set-night-vision-settings command for one camera.
*/
void setSurveillanceNightVisionSettings(const std::string& camId,
                                        double gamma,
                                        bool adaptiveMode,
                                        int contrastMode,
                                        double clipLimit,
                                        int tileSize,
                                        int denoisingStrength);

/**
* @brief Queue a set-object-detection-settings command for one camera.
*/
void setSurveillanceObjectDetectionSettings(const std::string& camId,
                                            int inputSize,
                                            int backend,
                                            int target,
                                            bool trackingEnabled,
                                            float minConfForDraw);

/**
* @brief Queue a set-vehicle-settings command for one camera.
*/
void setSurveillanceVehicleSettings(const std::string& camId,
                                    const std::string& lang,
                                    int minPlateConfidence,
                                    bool enableAlerts,
                                    bool saveImages);

/**
* @brief Queue a set-recorder-settings command for one camera.
*/
void setSurveillanceRecorderSettings(const std::string& camId,
                                     int codec,
                                     int bitrate,
                                     int maxDuration,
                                     uint64_t maxFileSize,
                                     const std::string& eventType);

// --- DataBaseManager passthroughs -- same fire-and-forget command queue.
// Read/query methods (get*/getAll*/getById-style, plus
// validateSurveillanceUserPassword) return a requestId string -- call
// drainSurveillanceResponses() below later to retrieve results and match
// them back up by requestId (see Surveillance::drainResponses()'s own doc
// comment). Every other (mutating) method here stays void/fire-and-forget,
// unchanged. ---

// Tracked Faces
std::string getSurveillanceAllTrackedFaces();
std::string getSurveillanceUnknownTrackedFaces();
std::string getSurveillanceAuthorizedTrackedFaces();
std::string getSurveillanceWatchlistTrackedFaces();
std::string getSurveillanceTrackedFaceById(const std::string& faceId);
void deleteSurveillanceTrackedFace(const std::string& id);
void deleteSurveillanceAllTrackedFaces();
void deleteSurveillanceAllAuthorizedFaces();
void deleteSurveillanceAllWatchlistFaces();
void registerSurveillanceFaceFromImage(const std::string& imagePath,
                                       const std::string& name,
                                       const std::string& status,
                                       const std::string& description,
                                       const std::string& externalId,
                                       bool broadcastToCloud);
std::string getSurveillanceFaceRegionHistory(const std::string& faceId);

// Tracked Plates
void logSurveillanceTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description);
std::string getSurveillanceAllTrackedPlates();
std::string getSurveillanceUnknownTrackedPlates();
std::string getSurveillanceAuthorizedTrackedPlates();
std::string getSurveillanceWatchlistTrackedPlates();
std::string getSurveillanceTrackedPlateById(const std::string& plateId);
void deleteSurveillanceTrackedPlate(const std::string& id);
void deleteSurveillanceAllTrackedPlates();
void deleteSurveillanceAllAuthorizedPlates();
void deleteSurveillanceAllWatchlistPlates();
void registerSurveillancePlateFromImage(const std::string& imagePath,
                                        const std::string& plateNumber,
                                        const std::string& status,
                                        const std::string& externalId);
std::string getSurveillancePlateRegionHistory(const std::string& plateId);

// Tracked Objects
std::string getSurveillanceObjectRegionHistory(const std::string& objectId);

// Tracked Weapons
std::string getSurveillanceAllTrackedWeapons();
std::string getSurveillanceUnknownTrackedWeapons();
std::string getSurveillanceAuthorizedTrackedWeapons();
std::string getSurveillanceWatchlistTrackedWeapons();
std::string getSurveillanceTrackedWeaponById(const std::string& weaponId);
void deleteSurveillanceTrackedWeapon(const std::string& id);
void deleteSurveillanceAllTrackedWeapons();
void deleteSurveillanceAllAuthorizedWeapons();
void deleteSurveillanceAllWatchlistWeapons();
void registerSurveillanceWeaponFromImage(const std::string& imagePath,
                                         const std::string& name,
                                         const std::string& status,
                                         const std::string& description,
                                         const std::string& externalId);
std::string getSurveillanceWeaponRegionHistory(const std::string& weaponId);

// User Management
void registerSurveillanceUser(const std::string& username,
                              const std::string& passwordHash,
                              const std::string& role,
                              const std::string& name,
                              const std::string& imagePath,
                              const std::string& phoneNumber,
                              const std::string& email,
                              int isActive);
std::string validateSurveillanceUserPassword(const std::string& username, const std::string& inputPlaintextPassword);
void updateSurveillanceUserProfile(const std::string& username,
                                   const std::string& passwordHash,
                                   const std::string& role,
                                   const std::string& name,
                                   const std::string& imagePath,
                                   const std::string& phoneNumber,
                                   const std::string& email,
                                   int isActive);
void updateSurveillanceUserStatus(const std::string& userId, int activeState);
void deleteSurveillanceUserById(const std::string& userId);
void updateSurveillanceLastLogin(const std::string& userId, const std::string& timestamp);
void changeSurveillanceUserPassword(const std::string& userId, const std::string& newPasswordHash);

// Events
std::string getSurveillanceAllEvents();
std::string getSurveillanceEventById(const std::string& eventId);
void deleteSurveillanceAllEvents();
void deleteSurveillanceEventById(const std::string& eventId);

// Recordings
std::string getSurveillanceAllRecordings();
std::string getSurveillanceRecordingsByCameraId(const std::string& cameraId);
std::string getSurveillanceRecordingById(const std::string& id);
void deleteSurveillanceAllRecordings();
void deleteSurveillanceRecordingById(const std::string& id);

// Telemetry
void logSurveillanceTelemetry(const std::string& metricType,
                              const std::string& nodeIp,
                              std::optional<float> cpuUsage,
                              std::optional<float> ramUsageMb,
                              std::optional<float> diskUsagePercent,
                              std::optional<float> temperatureC,
                              int numberOfCameras,
                              int numberOfActiveCameras,
                              int numberOfNonActiveCameras,
                              std::optional<float> fps,
                              std::optional<int> latencyMs);
void pruneSurveillanceTelemetryBefore(long long beforeTimestamp);

// Daily Face Metrics
void insertSurveillanceDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp);
std::string getSurveillanceAllDailyFaceMetrics();
std::string getSurveillanceDailyFaceMetricsByDate(const std::string& detectionDate);
void deleteSurveillanceAllDailyFaceMetrics();
void deleteSurveillanceDailyFaceMetricsByDate(const std::string& detectionDate);

// Chat
void insertSurveillanceChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt);
std::string getSurveillanceRecentChatMessages(int limit);

// Retrieves whatever query results have arrived since the last call --
// see Surveillance::drainResponses()'s own doc comment. A poll, not a
// blocking wait; there is no synchronous round trip across the process
// boundary.
std::vector<json> drainSurveillanceResponses();






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

class QRCodeScannerWrapper {
private:
ItemWrapper<T>& parent; 
std::unique_ptr<BaseMicroservice> _qrCodeScannerManager = 
                MicroserviceManager::createMicroObjects("QR Code Scanner");
public:        
QRCodeScannerWrapper(ItemWrapper<T>& wrapper) : parent(wrapper) {}   

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

};




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


class DocumentScannerWrapper {
private:
ItemWrapper<T>& parent;
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

};





/*
        MICROSERVICE SECTION
    *****************************

    =======================================================================================
    | Alarm System Functions                                                              |
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

class AlarmSystemWrapper {
   private:
   ItemWrapper<T>& parent;
   std::unique_ptr<BaseMicroservice> _alarmManager = 
                   MicroserviceManager::createMicroObjects("Alarm System");
   public:
   AlarmSystemWrapper(ItemWrapper<T>& wrapper) : parent(wrapper) {}
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
    
};

#include "ItemWrapper.tpp"  // Template definitions should be included at the end of the header

#endif // ITEM_MANAGER_H
