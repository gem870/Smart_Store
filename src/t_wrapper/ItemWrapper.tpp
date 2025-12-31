
//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::


#pragma once

#include "ItemWrapper.h"
#include <sstream>
#include <typeinfo>
#include <memory>
#include <iostream>
#include <type_traits>


// Helper trait to check if a type is streamable (C++17-compatible)
template<typename T, typename = void>
struct is_streamable : std::false_type {};

template<typename T>
struct is_streamable<T, std::void_t<decltype(std::declval<std::ostream&>() << std::declval<T>())>> : std::true_type {};


// :: Template function implementations for ItemWrapper
// ****************************************************


template<typename T>
void ItemWrapper<T>::display() const {
    std::cout << "Type: " << demangleType(getTypeName());
    if (!tag.empty()) {
        std::cout << " | Tag: " << tag;

        if (!data) {
            std::cout << " | Value: [null data]\n";
            return;
        }

        if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
            std::cout << " | Value: " << *data << "\n";
        } else {
            std::cout << " | Value: [non-streamable type]\n";
        }
    } else {
        std::cout << " :::| --> { No type found } <--\n";
    }
}

template<typename T>
std::string ItemWrapper<T>::getTypeName() const {
    return typeid(T).name();
}

template<typename T>
json ItemWrapper<T>::serialize() const {

    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot serialize null data." + Logger::getColorCode(LogColor::RESET));
    }

    json j;
    j["id"] = id_;
    j["tag"] = tag;
    j["type"] = getTypeName();

#if defined(__cpp_concepts) && __cpp_concepts >= 201907L
    // C++20 concepts-based checks
    if constexpr (has_to_json<T>::value) {
        nlohmann::json dataJson;
        to_json(dataJson, *data);
        j["data"] = dataJson;
        return j;
    } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
        j["data"] = *data;
        return j;
    } else if constexpr (requires(const T& obj) { { obj.serialize() } -> std::same_as<json>; }) {
        j["data"] = data->serialize();
        return j;
    } else if constexpr (requires(std::ostream& os, const T& obj) { os << obj; }) {
        std::ostringstream oss;
        oss << *data;
        j["data"] = oss.str();
        return j;
    } else {
        // fallback: store as empty object
        j["data"] = nlohmann::json();
        return j;
    }
#else
    // SFINAE-based checks for C++17 and older
    if constexpr (has_to_json<T>::value) {
        nlohmann::json dataJson;
        to_json(dataJson, *data);
        j["data"] = dataJson;
        return j;
    } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
        j["data"] = *data;
        return j;
    } else if constexpr (has_serialize<T>::value) {
        j["data"] = data->serialize();
        return j;
    } else if constexpr (is_streamable<T>::value) {
        std::ostringstream oss;
        oss << *data;
        j["data"] = oss.str();
        return j;
    } else {
        // fallback: store as empty object
        j["data"] = nlohmann::json();
        return j;
    }
#endif
}

template<typename T>
std::shared_ptr<BaseItem> ItemWrapper<T>::clone() const {
    return std::make_shared<ItemWrapper<T>>(std::make_shared<T>(*data), tag);
}

template<typename T>
std::string ItemWrapper<T>::getTag() const {
    return tag;
}

template<typename T>
T& ItemWrapper<T>::getData() {
    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot access null data." + Logger::getColorCode(LogColor::RESET));
    }
    return *data;
}

template<typename T>
const T& ItemWrapper<T>::getData() const {
    if (!data) {
        throw std::runtime_error(Logger::getColorCode(LogColor::RED) + ":::|WARNING: Cannot access null data." + Logger::getColorCode(LogColor::RESET));
    }
    return *data;
}

template<typename T>
T& ItemWrapper<T>::getMutableData() {
    return *data;
}

template<typename T>
nlohmann::json ItemWrapper<T>::toJson() const {
    if constexpr (has_to_json<T>::value) {
        nlohmann::json j;
        to_json(j, *data);
        return j;
    } else if constexpr (std::is_arithmetic_v<T> || std::is_same_v<T, std::string>) {
        return nlohmann::json(*data);
    } else {
        // fallback: return an empty object or string representation
        return nlohmann::json();
    }
}








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

    template<typename T>
    void ItemWrapper<T>::sendMessage(const std::string& payload, const std::string& recipientID) {
        std::string actualRecipient = recipientID.empty() ? getId() : recipientID;

        if (_networkManager) {
            if (auto* networkAgent = dynamic_cast<NetworkAgent*>(_networkManager.get())) {
                networkAgent->sendMessage(payload, actualRecipient);
            } else {
                LOG_CONTEXT(LogLevel::ERR,
                            "sendMessage not supported by this Network Agent.",
                            {});
            }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "NetworkManager not initialized.", {});
        }
    }

    template<typename T>
    void ItemWrapper<T>::receiveMessage(const Message& msg)  {
        if(_networkManager){
          if(auto* networkAgent = dynamic_cast<NetworkAgent*>(_networkManager.get())){
            networkAgent->receiveMessage(msg);
          } else {
            LOG_CONTEXT(LogLevel::ERR, "ReceivingMessage not supported by Network Agent.", {});
          }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "NetworkManager not initialized.", {});
        }
    }








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

template<typename T>
void ItemWrapper<T>::runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            cvVision->runRestrictedAreaMonitor(cameraIndex, cascadePath);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Run restricted area monitor failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Run restricted area monitor not initialized.", {});
    }
}

template<typename T>
const std::unordered_map<int, FaceTrack>& ItemWrapper<T>::getRunRestrictedTracks() const {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            return cvVision->getTracks();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get tracks failed.", {});
            static const std::unordered_map<int, FaceTrack> emptyMap;
            return emptyMap;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get tracks not initialized.", {});
        static const std::unordered_map<int, FaceTrack> emptyMap;
        return emptyMap;
    }
}

template<typename T>
void ItemWrapper<T>::addRunRestictedTrack(int id, const FaceTrack& track) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            cvVision->addTrack(id, track);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to add ID and face for recognition.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for adding faces and IDs")));
    }
}

template<typename T>
void ItemWrapper<T>::restRunRestrictedTracks() {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            cvVision->resetTracking();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to reset face recognition tracking.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for resetting face recognition tracking.")));
    }
}

template<typename T>
void ItemWrapper<T>::removeRunRestrictedTrack(int id) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            cvVision->removeTrack(id);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to remove ID and face for recognition.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for removing faces and IDs")));
    }
}

template<typename T>
void ItemWrapper<T>::resetRunRestrictedConfig() {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            cvVision->resetConfig();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to reset face recognition configuration.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for resetting face recognition configuration.")));
    }
}

template<typename T>
void ItemWrapper<T>::setRunRestrictedScaleFactor(double scaleFactor) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            FaceError err = cvVision->setScaleFactor(scaleFactor);
            if (err != FaceError::None) {
                LOG_CONTEXT(LogLevel::ERR, "Failed to set scale factor.", {});
            }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set scale factor failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set scale factor not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setRunRestrictedMinNeighbors(int minNeighbors) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            FaceError err = cvVision->setMinNeighbors(minNeighbors);
            if (err != FaceError::None) {
                LOG_CONTEXT(LogLevel::ERR, "Failed to set minimum neighbors.", {});
            }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set minimum neighbors failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set minimum neighbors not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setRunRestrictedMinFaceSize(const cv::Size& size) {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            FaceError err = cvVision->setMinFaceSize(size);
            if (err != FaceError::None) {
                LOG_CONTEXT(LogLevel::ERR, "Failed to set minimum face size.", {});
            }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set minimum face size failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set minimum face size not initialized.", {});
    }
}
 
template<typename T>
void ItemWrapper<T>::setIouMatchThreshold(double threshold) {   
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            FaceError err = cvVision->setIouMatchThreshold(threshold);
            if (err != FaceError::None) {
                LOG_CONTEXT(LogLevel::ERR, "Failed to set IOU match threshold.", {});
            }
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set IOU match threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set IOU match threshold not initialized.", {});
    }
}

template<typename T>
double ItemWrapper<T>::getRunRestrictedScaleFactor() const {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            return cvVision->getScaleFactor();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get scale factor failed.", {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get scale factor not initialized.", {});
        return 0.0;
    }
}

template<typename T>
int ItemWrapper<T>::getRunRestrictedMinNeighbors() const {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            return cvVision->getMinNeighbors();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get minimum neighbors failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get minimum neighbors not initialized.", {});
        return 0;
    }
}

template<typename T>
cv::Size ItemWrapper<T>::getRunRestrictedMinFaceSize() const {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            return cvVision->getMinFaceSize();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get minimum face size failed.", {});
            return cv::Size();
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get minimum face size not initialized.", {});
        return cv::Size();
    }
}

template<typename T>
double ItemWrapper<T>::getIouMatchThreshold() const {
    if (_faceRecognitionManager) {
        if (auto* cvVision = dynamic_cast<FaceRecognition*>(_faceRecognitionManager.get())) {
            return cvVision->getIouMatchThreshold();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get IOU match threshold failed.", {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get IOU match threshold not initialized.", {});
        return 0.0;
    }
}









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

template<typename T>
void ItemWrapper<T>::monitorCamera(const std::string& cascadePath){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->monitorCamera(cascadePath);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Monitor camera failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Monitor camera not initialized.", {});
    }
}

template<typename T>
double ItemWrapper<T>::getmonitorCamerathreshold() const {
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            return cvVision->getThreshold();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor camera threshold failed.", {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor camera threshold not initialized.", {});
        return 0.0;
    }
}

template<typename T>
void ItemWrapper<T>::setmonitorCamerathreshold(double newThreshold){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->setThreshold(newThreshold);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor camera threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor camera threshold not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::resetmonitorCamerathreshold(){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->resetThreshold();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Reset monitor camera threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Reset monitor camera threshold not initialized.", {});
    }
}

template<typename T>
std::string ItemWrapper<T>::getmonitorCameracascadePath() const {
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            return cvVision->getCascadePath();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor camera cascade path failed.", {});
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor camera cascade path not initialized.", {});
        return "";
    }
}

template<typename T>
void ItemWrapper<T>::setmonitorCameracascadePath(const std::string& path){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->setCascadePath(path);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor camera cascade path failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor camera cascade path not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::loadKnownFaces(const std::vector<std::string>& filePaths){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->loadKnownFaces(filePaths);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Load known faces failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Load known faces not initialized.", {});
    }
}

template<typename T>
size_t ItemWrapper<T>::getKnownFaceCount() const {
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            return cvVision->getKnownFaceCount();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get known face count failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get known face count not initialized.", {});
        return 0;
    }
}

template<typename T>
void ItemWrapper<T>::resetKnownFaces(){
    if (_faceWatchlistManager) {
        if (auto* cvVision = dynamic_cast<FaceWatchlist*>(_faceWatchlistManager.get())) {
            cvVision->resetKnownFaces();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Reset known faces failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Reset known faces not initialized.", {});
    }
}









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

template<typename T>
void ItemWrapper<T>::setMonitorDetectionDiffThreshold(double threshold) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setDiffThreshold(threshold);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor detection diff threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor detection diff threshold not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setMonitorDetectionMinArea(int minArea) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setMinArea(minArea);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor detection min area failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor detection min area not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setMonitorDetectionCrowdThreshold(std::size_t crowdThreshold) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setCrowdThreshold(crowdThreshold);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor detection crowd threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor detection crowd threshold not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setMonitorDetectionLoiterSeconds(int loiterSeconds) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setLoiterSeconds(loiterSeconds);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor detection loiter seconds failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor detection loiter seconds not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setMonitorDetectionAlertCallback(std::function<void(const std::string&)> cb) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setAlertCallback(cb);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor detection alert callback failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor detection alert callback not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::resetMonitorDetectionConfig(){
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->resetConfig();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Reset monitor camera config failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Reset monitor camera config not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::resetMonitorDetection() {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->reset();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Reset monitor detection failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Reset monitor detection not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::clearMonitorDetectionZones() {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->clearZones();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Clear monitor detection zones failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Clear monitor detection zones not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setMonitorDetectionConfig(double diffThreshold,
                                               int minArea,
                                               std::size_t crowdThreshold,
                                               int loiterSeconds,
                                               int leftBehindSeconds,
                                               bool enableTracking){
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->setConfig(diffThreshold,
                                minArea,
                                crowdThreshold,
                                loiterSeconds,
                                leftBehindSeconds,
                                enableTracking);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set monitor camera config failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set monitor camera config not initialized.", {});
    }
}

template<typename T>
bool ItemWrapper<T>::isMonitorDetectionTrackingEnabled() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->isTrackingEnabled();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection tracking enabled failed.", {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection tracking enabled not initialized.", {});
        return false;
    }
}

template<typename T>
bool ItemWrapper<T>::hasMonitorDetectionCallback() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->hasMotionCallback();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Check monitor detection callback failed.", {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Check monitor detection callback not initialized.", {});
        return false;
    }
}

template<typename T>
double ItemWrapper<T>::getMonitorDetectionDiffThreshold() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->getDiffThreshold();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection diff threshold failed.", {});
            return 0.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection diff threshold not initialized.", {});
        return 0.0;
    }
}

template<typename T>
int ItemWrapper<T>::getMonitorDetectionMinArea() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->getMinArea();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection min area failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection min area not initialized.", {});
        return 0;
    }
}

template<typename T>
std::size_t ItemWrapper<T>::getMonitorDetectionCrowdThreshold() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->getCrowdThreshold();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection crowd threshold failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection crowd threshold not initialized.", {});
        return 0;
    }
}

template<typename T>
int ItemWrapper<T>::getMonitorDetectionLoiterSeconds() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->getLoiterSeconds();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection loiter seconds failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection loiter seconds not initialized.", {});
        return 0;
    }
}

template<typename T>
int ItemWrapper<T>::getMonitorDetectionLeftBehindSeconds() const {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            return cvVision->getLeftBehindSeconds();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get monitor detection left behind seconds failed.", {});
            return 0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get monitor detection left behind seconds not initialized.", {});
        return 0;
    }
}

template<typename T>
void ItemWrapper<T>::runMonitorDetectionMonitorCamera(int cameraIndex){
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
            cvVision->runUnifiedMonitor(cameraIndex);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Run monitor camera failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Run monitor camera not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::addMonitorDetectionZone(const Zone& zone) {
    if (_motionDetectionManager) {
        if (auto* cvVision = dynamic_cast<MotionDetection*>(_motionDetectionManager.get())) {
        } else {
            LOG_CONTEXT(LogLevel::ERR,
                        "Add monitor detection zone failed (type mismatch).",
                        {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR,
                    "Monitor detection not initialized when adding zone.",
                    {});
    }
}









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


template<typename T>
std::string ItemWrapper<T>::scanFromCamera(){
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            return cvVision->scanFromCamera();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Run QR scanner monitor camera failed.", {});
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Run QR scanner monitor camera not initialized.", {});
        return "";
    }
}

template<typename T>
std::string ItemWrapper<T>::scanFromFile(const std::string& imagePath) {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            return cvVision->scanFromFile(imagePath);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Scan from file failed.", {});
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Scan from file not initialized.", {});
        return "";
    }
}

template<typename T>
void ItemWrapper<T>::resetConfig() {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            cvVision->resetConfig();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Reset QR code scanner config failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Reset QR code scanner config not initialized.", {});
    }
}

template<typename T>
bool ItemWrapper<T>::isPreviewEnabled() const {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            return cvVision->isPreviewEnabled();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get preview enabled failed.", {});
            return false;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get preview enabled not initialized.", {});
        return false;
    }
}

template<typename T>
int ItemWrapper<T>::getCameraIndex() const {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            return cvVision->getCameraIndex();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get camera index failed.", {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get camera index not initialized.", {});
        return -1;
    }
}

template<typename T>
void ItemWrapper<T>::setCameraIndex(int index) {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            cvVision->setCameraIndex(index);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set camera index failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set camera index not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setPreviewEnabled(bool enabled) {
    if (_qrCodeScannerManager) {
        if (auto* cvVision = dynamic_cast<QRCodeScanner*>(_qrCodeScannerManager.get())) {
            cvVision->setPreviewEnabled(enabled);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set preview enabled failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set preview enabled not initialized.", {});
    }
}









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

template<typename T>
void ItemWrapper<T>::setEdgeThreshold(int lowerThreshold, int upperThreshold) {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            cvVision->setEdgeThresholds(lowerThreshold, upperThreshold);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set edge threshold failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set edge threshold not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setContourMinArea(double area) {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            cvVision->setContourMinArea(area);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set contour min area failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set contour min area not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSharpening(double amount) {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            cvVision->setSharpening(amount);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set sharpening failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set sharpening not initialized.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setOutputSize(int width, int height) {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            cvVision->setOutputSize(width, height);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set output size failed.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Set output size not initialized.", {});
    }
}

template<typename T>
int ItemWrapper<T>::getCannyLow() const {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            return cvVision->getCannyLow();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get Canny low threshold failed.", {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get Canny low threshold not initialized.", {});
        return -1;
    }
}

template<typename T>
int ItemWrapper<T>::getCannyHigh() const {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            return cvVision->getCannyHigh();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get Canny high threshold failed.", {});
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get Canny high threshold not initialized.", {});
        return -1;
    }
}

template<typename T>
double ItemWrapper<T>::getMinContourArea() const {
    if (_documentScanner) {
        if (auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get())) {
            return cvVision->getMinContourArea();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Get min contour area failed.", {});
            return -1.0;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Get min contour area not initialized.", {});
        return -1.0;
    }
}

template<typename T>
int ItemWrapper<T>::getOutputWidth() const {
    if (!_documentScanner) {
        LOG_CONTEXT(LogLevel::ERR, "Get output width failed: scanner not initialized.", {});
        return -1;
    }

    auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get());
    if (!cvVision) {
        LOG_CONTEXT(LogLevel::ERR, "Get output width failed: invalid scanner type.", {});
        return -1;
    }

    return cvVision->getOutputWidth();
}

template<typename T>
int ItemWrapper<T>::getOutputHeight() const {
    if (!_documentScanner) {
        LOG_CONTEXT(LogLevel::ERR, "Get output height failed: scanner not initialized.", {});
        return -1;
    }

    auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get());
    if (!cvVision) {
        LOG_CONTEXT(LogLevel::ERR, "Get output height failed: invalid scanner type.", {});
        return -1;
    }

    return cvVision->getOutputHeight();
}

template<typename T>
double ItemWrapper<T>::getSharpening() const {
    if (!_documentScanner) {
        LOG_CONTEXT(LogLevel::ERR, "Get sharpening failed: scanner not initialized.", {});
        return -1.0;
    }

    auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get());
    if (!cvVision) {
        LOG_CONTEXT(LogLevel::ERR, "Get sharpening failed: invalid scanner type.", {});
        return -1.0;
    }

    return cvVision->getSharpening();
}

template<typename T>
void ItemWrapper<T>::run(const std::string& mode,
                         const std::string& input,
                         const std::string& output) {
    if (!_documentScanner) {
        LOG_CONTEXT(LogLevel::ERR, "Run failed: scanner not initialized.", {});
        return;
    }

    auto* cvVision = dynamic_cast<Scanner*>(_documentScanner.get());
    if (!cvVision) {
        LOG_CONTEXT(LogLevel::ERR, "Run failed: invalid scanner type.", {});
        return;
    }

    cvVision->run(mode, input, output);
}








/*
            MICROSERVICE SECTION
        *****************************

        =======================================================================================
        | Document Alarm System Functions                                                     |
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

template<typename T>
void ItemWrapper<T>::triggerAlarm(const std::string& message) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->triggerAlarm(message);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to trigger alarm.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for triggering alarm.")));
    }
}

template<typename T>
void ItemWrapper<T>::setVolume(int level) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->setVolume(level);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to set alarm volume.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for setting alarm volume.")));
    }
}

template<typename T>
void ItemWrapper<T>::setDuration(int seconds) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->setDuration(seconds);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to set alarm duration.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for setting alarm duration.")));
    }
}

template<typename T>
void ItemWrapper<T>::setDefaultTone(const std::string& tone) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->setDefaultTone(tone);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to set default alarm tone.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for setting default alarm tone.")));
    }
}

template<typename T>
void ItemWrapper<T>::setTone(const std::string& toneName, const std::string& filePath) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->setTone(toneName, filePath);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to set alarm tone.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for setting alarm tone.")));
    }
}

template<typename T>
void ItemWrapper<T>::assignTone(const std::string& event, const std::string& toneName) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->assignTone(event, toneName);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to assign alarm tone.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for assigning alarm tone.")));
    }
}

template<typename T>
int ItemWrapper<T>::getVolume() const {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            return alarmSys->getVolume();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to get alarm volume.")));
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for getting alarm volume.")));
        return -1;
    }
}

template<typename T>
int ItemWrapper<T>::getDuration() const {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            return alarmSys->getDuration();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to get alarm duration.")));
            return -1;
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for getting alarm duration.")));
        return -1;
    }
}

template<typename T>
std::string ItemWrapper<T>::getDefaultTone() const {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            return alarmSys->getDefaultTone();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to get default alarm tone.")));
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for getting default alarm tone.")));
        return "";
    }
}

template<typename T>
std::string ItemWrapper<T>::getTone(const std::string& toneName) const {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            return alarmSys->getTone(toneName);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to get alarm tone.")));
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for getting alarm tone.")));
        return "";
    }
}

template<typename T>
std::string ItemWrapper<T>::getAssignedTone(const std::string& event) const {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            return alarmSys->getAssignedTone(event);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to get assigned alarm tone.")));
            return "";
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for getting assigned alarm tone.")));
        return "";
    }
}

template<typename T>
void ItemWrapper<T>::resetAlarmConfig() {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->resetConfig();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to reset alarm config.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for resetting alarm config.")));
    }
}

template<typename T>
void ItemWrapper<T>::playTone(const std::string& toneName) {
    if (_alarmManager) {
        if (auto* alarmSys = dynamic_cast<AlarmSystem*>(_alarmManager.get())) {
            alarmSys->playTone(toneName);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "",
                std::make_exception_ptr(std::runtime_error("Failed to play alarm tone.")));
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "",
            std::make_exception_ptr(std::runtime_error("Error in operation for playing alarm tone.")));
    }
}










