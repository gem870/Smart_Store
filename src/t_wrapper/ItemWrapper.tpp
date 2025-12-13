
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

          =============================
          | Networking Agent Funtions |
          =============================
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

          =============================
          | Face Recognition Funtions |
          =============================
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

          =============================
          |  Face Watchlist Funtions  |
          =============================
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
    
            ============================
            | Motion Detection Funtions |
            ============================
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





