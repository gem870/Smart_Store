
//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  Â© 2025 Victor. All rights reserved. ::
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
        | Surveillance Funtions                                                               |
        =======================================================================================
        | Thin handle onto the standalone surveillance system -- start/stop/status only, via  |
        | a sentinel file the surveillance app's own frontend watches. See Surveillance.hpp    |
        | for why this stays thin rather than owning that app's FeatureConfigManager state.    |
        =======================================================================================
*/

template<typename T>
void ItemWrapper<T>::startSurveillance() {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->start();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Start surveillance failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when starting.", {});
    }
}

template<typename T>
void ItemWrapper<T>::stopSurveillance() {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->stop();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Stop surveillance failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when stopping.", {});
    }
}

template<typename T>
bool ItemWrapper<T>::isSurveillanceRunning() const {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            return surveillance->isRunning();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Check surveillance running failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when checking running state.", {});
    }
    return false;
}

#define SURV_W_FORWARD0(WrapperName, Call, LogText) \
template<typename T> \
void ItemWrapper<T>::WrapperName() { \
    if (_surveillanceManager) { \
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) { \
            surveillance->Call(); \
        } else { \
            LOG_CONTEXT(LogLevel::ERR, LogText " failed (type mismatch).", {}); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when " LogText ".", {}); \
    } \
}

// Same shape as SURV_W_FORWARD0, for the read/query methods that return a
// requestId instead of being fire-and-forget -- see Surveillance.hpp's
// DataBaseManager section doc comment.
#define SURV_W_FORWARD0_R(WrapperName, Call, LogText) \
template<typename T> \
std::string ItemWrapper<T>::WrapperName() { \
    if (_surveillanceManager) { \
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) { \
            return surveillance->Call(); \
        } else { \
            LOG_CONTEXT(LogLevel::ERR, LogText " failed (type mismatch).", {}); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when " LogText ".", {}); \
    } \
    return ""; \
}

SURV_W_FORWARD0(resetSurveillanceModelSettings, resetModelSettings, "resetting surveillance model settings")
SURV_W_FORWARD0(clearSurveillancePlateWatchlist, clearPlateNumberFromWatchList, "clearing surveillance plate watchlist")
SURV_W_FORWARD0_R(getSurveillanceAllTrackedFaces, getAllTrackedFaces, "getting all tracked faces")
SURV_W_FORWARD0_R(getSurveillanceUnknownTrackedFaces, getUnknownTrackedFaces, "getting unknown tracked faces")
SURV_W_FORWARD0_R(getSurveillanceAuthorizedTrackedFaces, getAuthorizedTrackedFaces, "getting authorized tracked faces")
SURV_W_FORWARD0_R(getSurveillanceWatchlistTrackedFaces, getWatchlistTrackedFaces, "getting watchlist tracked faces")
SURV_W_FORWARD0(deleteSurveillanceAllTrackedFaces, deleteAllTrackedFaces, "deleting all tracked faces")
SURV_W_FORWARD0(deleteSurveillanceAllAuthorizedFaces, deleteAllAuthorizedFaces, "deleting all authorized faces")
SURV_W_FORWARD0(deleteSurveillanceAllWatchlistFaces, deleteAllWatchlistFaces, "deleting all watchlist faces")
SURV_W_FORWARD0_R(getSurveillanceAllTrackedPlates, getAllTrackedPlates, "getting all tracked plates")
SURV_W_FORWARD0_R(getSurveillanceUnknownTrackedPlates, getUnknownTrackedPlates, "getting unknown tracked plates")
SURV_W_FORWARD0_R(getSurveillanceAuthorizedTrackedPlates, getAuthorizedTrackedPlates, "getting authorized tracked plates")
SURV_W_FORWARD0_R(getSurveillanceWatchlistTrackedPlates, getWatchlistTrackedPlates, "getting watchlist tracked plates")
SURV_W_FORWARD0(deleteSurveillanceAllTrackedPlates, deleteAllTrackedPlates, "deleting all tracked plates")
SURV_W_FORWARD0(deleteSurveillanceAllAuthorizedPlates, deleteAllAuthorizedPlates, "deleting all authorized plates")
SURV_W_FORWARD0(deleteSurveillanceAllWatchlistPlates, deleteAllWatchlistPlates, "deleting all watchlist plates")
SURV_W_FORWARD0_R(getSurveillanceAllTrackedWeapons, getAllTrackedWeapons, "getting all tracked weapons")
SURV_W_FORWARD0_R(getSurveillanceUnknownTrackedWeapons, getUnknownTrackedWeapons, "getting unknown tracked weapons")
SURV_W_FORWARD0_R(getSurveillanceAuthorizedTrackedWeapons, getAuthorizedTrackedWeapons, "getting authorized tracked weapons")
SURV_W_FORWARD0_R(getSurveillanceWatchlistTrackedWeapons, getWatchlistTrackedWeapons, "getting watchlist tracked weapons")
SURV_W_FORWARD0(deleteSurveillanceAllTrackedWeapons, deleteAllTrackedWeapons, "deleting all tracked weapons")
SURV_W_FORWARD0(deleteSurveillanceAllAuthorizedWeapons, deleteAllAuthorizedWeapons, "deleting all authorized weapons")
SURV_W_FORWARD0(deleteSurveillanceAllWatchlistWeapons, deleteAllWatchlistWeapons, "deleting all watchlist weapons")
SURV_W_FORWARD0_R(getSurveillanceAllEvents, getAllEvents, "getting all events")
SURV_W_FORWARD0(deleteSurveillanceAllEvents, deleteAllEvents, "deleting all events")
SURV_W_FORWARD0_R(getSurveillanceAllRecordings, getAllRecordings, "getting all recordings")
SURV_W_FORWARD0(deleteSurveillanceAllRecordings, deleteAllRecordings, "deleting all recordings")
SURV_W_FORWARD0_R(getSurveillanceAllDailyFaceMetrics, getAllDailyFaceMetrics, "getting all daily face metrics")
SURV_W_FORWARD0(deleteSurveillanceAllDailyFaceMetrics, deleteAllDailyFaceMetrics, "deleting all daily face metrics")

#undef SURV_W_FORWARD0
#undef SURV_W_FORWARD0_R

#define SURV_W_FORWARD1(WrapperName, ArgType, ArgName, Call, LogText) \
template<typename T> \
void ItemWrapper<T>::WrapperName(ArgType ArgName) { \
    if (_surveillanceManager) { \
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) { \
            surveillance->Call(ArgName); \
        } else { \
            LOG_CONTEXT(LogLevel::ERR, LogText " failed (type mismatch).", {}); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when " LogText ".", {}); \
    } \
}

// Same shape as SURV_W_FORWARD1, for the read/query methods that return a
// requestId instead of being fire-and-forget.
#define SURV_W_FORWARD1_R(WrapperName, ArgType, ArgName, Call, LogText) \
template<typename T> \
std::string ItemWrapper<T>::WrapperName(ArgType ArgName) { \
    if (_surveillanceManager) { \
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) { \
            return surveillance->Call(ArgName); \
        } else { \
            LOG_CONTEXT(LogLevel::ERR, LogText " failed (type mismatch).", {}); \
        } \
    } else { \
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when " LogText ".", {}); \
    } \
    return ""; \
}

SURV_W_FORWARD1(setSurveillanceCloudEnabled, bool, enabled, setCloudEnabled, "setting surveillance cloud enabled")
SURV_W_FORWARD1(removeSurveillanceCamera, const std::string&, camId, removeCamera, "removing surveillance camera")
SURV_W_FORWARD1(setSurveillanceBeepEnabled, bool, enabled, setBeepEnabled, "setting surveillance beep enabled")
SURV_W_FORWARD1(setSurveillanceVoiceEnabled, bool, enabled, setVoiceEnabled, "setting surveillance voice enabled")
SURV_W_FORWARD1(setSurveillanceVoiceGender, const std::string&, gender, setVoiceGender, "setting surveillance voice gender")
SURV_W_FORWARD1(resetSurveillanceFaceSettings, const std::string&, camId, resetFaceSettings, "resetting surveillance face settings")
SURV_W_FORWARD1(resetSurveillanceMotionSettings, const std::string&, camId, resetMotionSettings, "resetting surveillance motion settings")
SURV_W_FORWARD1(clearSurveillanceMotionZones, const std::string&, camId, clearMotionZones, "clearing surveillance motion zones")
SURV_W_FORWARD1(addSurveillancePlateToWatchlist, const std::string&, plate, addPlateNumberToWatchlist, "adding surveillance plate to watchlist")
SURV_W_FORWARD1(removeSurveillancePlateFromWatchlist, const std::string&, plate, removePlateNumberFromWatchlist, "removing surveillance plate from watchlist")
SURV_W_FORWARD1(setSurveillanceAccountId, const std::string&, accountId, setAccountId, "setting surveillance account id")
SURV_W_FORWARD1(setSurveillanceKafkaBrokerAddress, const std::string&, brokerAddress, setKafkaBrokerAddress, "setting surveillance kafka broker address")
SURV_W_FORWARD1_R(getSurveillanceTrackedFaceById, const std::string&, faceId, getTrackedFaceById, "getting tracked face by id")
SURV_W_FORWARD1(deleteSurveillanceTrackedFace, const std::string&, id, deleteTrackedFace, "deleting tracked face")
SURV_W_FORWARD1_R(getSurveillanceFaceRegionHistory, const std::string&, faceId, getFaceRegionHistory, "getting face region history")
SURV_W_FORWARD1_R(getSurveillanceTrackedPlateById, const std::string&, plateId, getTrackedPlateById, "getting tracked plate by id")
SURV_W_FORWARD1(deleteSurveillanceTrackedPlate, const std::string&, id, deleteTrackedPlate, "deleting tracked plate")
SURV_W_FORWARD1_R(getSurveillancePlateRegionHistory, const std::string&, plateId, getPlateRegionHistory, "getting plate region history")
SURV_W_FORWARD1_R(getSurveillanceObjectRegionHistory, const std::string&, objectId, getObjectRegionHistory, "getting object region history")
SURV_W_FORWARD1_R(getSurveillanceTrackedWeaponById, const std::string&, weaponId, getTrackedWeaponById, "getting tracked weapon by id")
SURV_W_FORWARD1(deleteSurveillanceTrackedWeapon, const std::string&, id, deleteTrackedWeapon, "deleting tracked weapon")
SURV_W_FORWARD1_R(getSurveillanceWeaponRegionHistory, const std::string&, weaponId, getWeaponRegionHistory, "getting weapon region history")
SURV_W_FORWARD1(deleteSurveillanceUserById, const std::string&, userId, deleteUserById, "deleting surveillance user by id")
SURV_W_FORWARD1_R(getSurveillanceEventById, const std::string&, eventId, getEventById, "getting event by id")
SURV_W_FORWARD1(deleteSurveillanceEventById, const std::string&, eventId, deleteEventById, "deleting event by id")
SURV_W_FORWARD1_R(getSurveillanceRecordingsByCameraId, const std::string&, cameraId, getRecordingsByCameraId, "getting recordings by camera id")
SURV_W_FORWARD1_R(getSurveillanceRecordingById, const std::string&, id, getRecordingById, "getting recording by id")
SURV_W_FORWARD1(deleteSurveillanceRecordingById, const std::string&, id, deleteRecordingById, "deleting recording by id")
SURV_W_FORWARD1(pruneSurveillanceTelemetryBefore, long long, beforeTimestamp, pruneTelemetryBefore, "pruning surveillance telemetry")
SURV_W_FORWARD1_R(getSurveillanceDailyFaceMetricsByDate, const std::string&, detectionDate, getDailyFaceMetricsByDate, "getting daily face metrics by date")
SURV_W_FORWARD1(deleteSurveillanceDailyFaceMetricsByDate, const std::string&, detectionDate, deleteDailyFaceMetricsByDate, "deleting daily face metrics by date")
SURV_W_FORWARD1_R(getSurveillanceRecentChatMessages, int, limit, getRecentChatMessages, "getting recent chat messages")

#undef SURV_W_FORWARD1
#undef SURV_W_FORWARD1_R

template<typename T>
void ItemWrapper<T>::setSurveillanceCloudSettings(const std::string& baseUrl,
                                                  const std::string& stationName,
                                                  const std::string& hardwareToken,
                                                  int pollIntervalSec) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setCloudSettings(baseUrl, stationName, hardwareToken, pollIntervalSec);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance cloud settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting cloud settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::addSurveillanceCamera(const std::string& name,
                                           const std::string& id,
                                           const std::string& source,
                                           bool enableFaceRecognition) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->addCamera(name, id, source, enableFaceRecognition);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Add surveillance camera failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when adding camera.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceCameraFeature(const std::string& cameraId, const std::string& featureKey, bool value) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setCameraFeature(cameraId, featureKey, value);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance camera feature failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting camera feature.", {});
    }
}

template<typename T>
void ItemWrapper<T>::updateSurveillanceCamera(const std::string& camId,
                                              const std::string& name,
                                              const std::string& source) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->updateCamera(camId, name, source);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Update surveillance camera failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when updating camera.", {});
    }
}

template<typename T>
void ItemWrapper<T>::plugInSurveillancePendingCamera(const std::string& camName, const std::string& camId) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->plugInPendingCamera(camName, camId);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Plug in surveillance pending camera failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when plugging in pending camera.", {});
    }
}

template<typename T>
void ItemWrapper<T>::plugOutSurveillancePendingCamera(bool isDelete, const std::string& camName, const std::string& camId) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->plugOutPendingCamera(isDelete, camName, camId);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Plug out surveillance pending camera failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when plugging out pending camera.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceFeatureForAllCameras(const std::string& featureKey, bool value) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setFeatureForAllCameras(featureKey, value);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance feature for all cameras failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting feature for all cameras.", {});
    }
}

template<typename T>
void ItemWrapper<T>::upLoadSurveillanceModel(const std::string& modelType, const std::string& modelPath) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->upLoadModel(modelType, modelPath);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Upload surveillance model failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when uploading model.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceStationLocation(double latitude, double longitude) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setStationLocation(latitude, longitude);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance station location failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting station location.", {});
    }
}

template<typename T>
void ItemWrapper<T>::addSurveillanceMotionZone(const std::string& camId,
                                               const std::string& regionName,
                                               int x, int y, int width, int height,
                                               bool restricted) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->addMotionZone(camId, regionName, x, y, width, height, restricted);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Add surveillance motion zone failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when adding motion zone.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceFaceSettings(const std::string& camId,
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
                                                 bool debugLogging) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setFaceSettings(camId, scaleFactor, minNeighbors, minFaceSizeWidth, minFaceSizeHeight,
                                          scoreThreshold, nmsThreshold, topK, useEqualizeHist, maxDetections,
                                          maxTrackAgeMs, iouThreshold, debugLogging);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance face settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting face settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceMotionSettings(const std::string& camId,
                                                   double diffThreshold,
                                                   int minArea,
                                                   std::size_t crowdThreshold,
                                                   int loiterSeconds,
                                                   int leftBehindSeconds,
                                                   bool enableTracking,
                                                   bool debugLogging) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setMotionSettings(camId, diffThreshold, minArea, crowdThreshold, loiterSeconds,
                                            leftBehindSeconds, enableTracking, debugLogging);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance motion settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting motion settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceNightVisionSettings(const std::string& camId,
                                                        double gamma,
                                                        bool adaptiveMode,
                                                        int contrastMode,
                                                        double clipLimit,
                                                        int tileSize,
                                                        int denoisingStrength) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setNightVisionSettings(camId, gamma, adaptiveMode, contrastMode, clipLimit,
                                                 tileSize, denoisingStrength);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance night vision settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting night vision settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceObjectDetectionSettings(const std::string& camId,
                                                             int inputSize,
                                                             int backend,
                                                             int target,
                                                             bool trackingEnabled,
                                                             float minConfForDraw) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setObjectDetectionSettings(camId, inputSize, backend, target, trackingEnabled, minConfForDraw);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance object detection settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting object detection settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceVehicleSettings(const std::string& camId,
                                                    const std::string& lang,
                                                    int minPlateConfidence,
                                                    bool enableAlerts,
                                                    bool saveImages) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setVehicleSettings(camId, lang, minPlateConfidence, enableAlerts, saveImages);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance vehicle settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting vehicle settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::setSurveillanceRecorderSettings(const std::string& camId,
                                                     int codec,
                                                     int bitrate,
                                                     int maxDuration,
                                                     uint64_t maxFileSize,
                                                     const std::string& eventType) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->setRecorderSettings(camId, codec, bitrate, maxDuration, maxFileSize, eventType);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Set surveillance recorder settings failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when setting recorder settings.", {});
    }
}

template<typename T>
void ItemWrapper<T>::registerSurveillanceFaceFromImage(const std::string& imagePath,
                                                        const std::string& name,
                                                        const std::string& status,
                                                        const std::string& description,
                                                        const std::string& externalId,
                                                        bool broadcastToCloud) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->registerFaceFromImage(imagePath, name, status, description, externalId, broadcastToCloud);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Register surveillance face from image failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when registering face from image.", {});
    }
}

template<typename T>
void ItemWrapper<T>::logSurveillanceTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->logTrackedPlate(plateNumber, status, description);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Log surveillance tracked plate failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when logging tracked plate.", {});
    }
}

template<typename T>
void ItemWrapper<T>::registerSurveillancePlateFromImage(const std::string& imagePath,
                                                         const std::string& plateNumber,
                                                         const std::string& status,
                                                         const std::string& externalId) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->registerPlateFromImage(imagePath, plateNumber, status, externalId);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Register surveillance plate from image failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when registering plate from image.", {});
    }
}

template<typename T>
void ItemWrapper<T>::registerSurveillanceWeaponFromImage(const std::string& imagePath,
                                                          const std::string& name,
                                                          const std::string& status,
                                                          const std::string& description,
                                                          const std::string& externalId) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->registerWeaponFromImage(imagePath, name, status, description, externalId);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Register surveillance weapon from image failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when registering weapon from image.", {});
    }
}

template<typename T>
void ItemWrapper<T>::registerSurveillanceUser(const std::string& username,
                                              const std::string& passwordHash,
                                              const std::string& role,
                                              const std::string& name,
                                              const std::string& imagePath,
                                              const std::string& phoneNumber,
                                              const std::string& email,
                                              int isActive) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->registerUser(username, passwordHash, role, name, imagePath, phoneNumber, email, isActive);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Register surveillance user failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when registering user.", {});
    }
}

template<typename T>
std::string ItemWrapper<T>::validateSurveillanceUserPassword(const std::string& username, const std::string& inputPlaintextPassword) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            return surveillance->validateUserPassword(username, inputPlaintextPassword);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Validate surveillance user password failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when validating user password.", {});
    }
    return "";
}

template<typename T>
std::vector<json> ItemWrapper<T>::drainSurveillanceResponses() {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            return surveillance->drainResponses();
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Drain surveillance responses failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when draining responses.", {});
    }
    return {};
}

template<typename T>
void ItemWrapper<T>::updateSurveillanceUserProfile(const std::string& username,
                                                    const std::string& passwordHash,
                                                    const std::string& role,
                                                    const std::string& name,
                                                    const std::string& imagePath,
                                                    const std::string& phoneNumber,
                                                    const std::string& email,
                                                    int isActive) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->updateUserProfile(username, passwordHash, role, name, imagePath, phoneNumber, email, isActive);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Update surveillance user profile failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when updating user profile.", {});
    }
}

template<typename T>
void ItemWrapper<T>::updateSurveillanceUserStatus(const std::string& userId, int activeState) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->updateUserStatus(userId, activeState);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Update surveillance user status failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when updating user status.", {});
    }
}

template<typename T>
void ItemWrapper<T>::updateSurveillanceLastLogin(const std::string& userId, const std::string& timestamp) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->updateLastLogin(userId, timestamp);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Update surveillance last login failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when updating last login.", {});
    }
}

template<typename T>
void ItemWrapper<T>::changeSurveillanceUserPassword(const std::string& userId, const std::string& newPasswordHash) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->changeUserPassword(userId, newPasswordHash);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Change surveillance user password failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when changing user password.", {});
    }
}

template<typename T>
void ItemWrapper<T>::logSurveillanceTelemetry(const std::string& metricType,
                                              const std::string& nodeIp,
                                              std::optional<float> cpuUsage,
                                              std::optional<float> ramUsageMb,
                                              std::optional<float> diskUsagePercent,
                                              std::optional<float> temperatureC,
                                              int numberOfCameras,
                                              int numberOfActiveCameras,
                                              int numberOfNonActiveCameras,
                                              std::optional<float> fps,
                                              std::optional<int> latencyMs) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->logTelemetry(metricType, nodeIp, cpuUsage, ramUsageMb, diskUsagePercent, temperatureC,
                                       numberOfCameras, numberOfActiveCameras, numberOfNonActiveCameras, fps, latencyMs);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Log surveillance telemetry failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when logging telemetry.", {});
    }
}

template<typename T>
void ItemWrapper<T>::insertSurveillanceDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->insertDailyFaceMetrics(detectionDate, totalDetections, timestamp);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Insert surveillance daily face metrics failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when inserting daily face metrics.", {});
    }
}

template<typename T>
void ItemWrapper<T>::insertSurveillanceChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt) {
    if (_surveillanceManager) {
        if (auto* surveillance = dynamic_cast<Surveillance*>(_surveillanceManager.get())) {
            surveillance->insertChatMessage(id, content, senderName, createdAt);
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Insert surveillance chat message failed (type mismatch).", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::ERR, "Surveillance not initialized when inserting chat message.", {});
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
std::string ItemWrapper<T>::QRCodeScannerWrapper::scanFromCamera(){
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
std::string ItemWrapper<T>::QRCodeScannerWrapper::scanFromFile(const std::string& imagePath) {
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
void ItemWrapper<T>::QRCodeScannerWrapper::resetConfig() {
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
bool ItemWrapper<T>::QRCodeScannerWrapper::isPreviewEnabled() const {
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
int ItemWrapper<T>::QRCodeScannerWrapper::getCameraIndex() const {
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
void ItemWrapper<T>::QRCodeScannerWrapper::setCameraIndex(int index) {
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
void ItemWrapper<T>::QRCodeScannerWrapper::setPreviewEnabled(bool enabled) {
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
void ItemWrapper<T>::DocumentScannerWrapper::setEdgeThreshold(int lowerThreshold, int upperThreshold) {
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
void ItemWrapper<T>::DocumentScannerWrapper::setContourMinArea(double area) {
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
void ItemWrapper<T>::DocumentScannerWrapper::setSharpening(double amount) {
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
void ItemWrapper<T>::DocumentScannerWrapper::setOutputSize(int width, int height) {
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
int ItemWrapper<T>::DocumentScannerWrapper::getCannyLow() const {
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
int ItemWrapper<T>::DocumentScannerWrapper::getCannyHigh() const {
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
double ItemWrapper<T>::DocumentScannerWrapper::getMinContourArea() const {
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
int ItemWrapper<T>::DocumentScannerWrapper::getOutputWidth() const {
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
int ItemWrapper<T>::DocumentScannerWrapper::getOutputHeight() const {
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
double ItemWrapper<T>::DocumentScannerWrapper::getSharpening() const {
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
void ItemWrapper<T>::DocumentScannerWrapper::run(const std::string& mode,
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
void ItemWrapper<T>::AlarmSystemWrapper::triggerAlarm(const std::string& message) {
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
void ItemWrapper<T>::AlarmSystemWrapper::setVolume(int level) {
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
void ItemWrapper<T>::AlarmSystemWrapper::setDuration(int seconds) {
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
void ItemWrapper<T>::AlarmSystemWrapper::setDefaultTone(const std::string& tone) {
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
void ItemWrapper<T>::AlarmSystemWrapper::setTone(const std::string& toneName, const std::string& filePath) {
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
void ItemWrapper<T>::AlarmSystemWrapper::assignTone(const std::string& event, const std::string& toneName) {
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
int ItemWrapper<T>::AlarmSystemWrapper::getVolume() const {
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
int ItemWrapper<T>::AlarmSystemWrapper::getDuration() const {
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
std::string ItemWrapper<T>::AlarmSystemWrapper::getDefaultTone() const {
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
std::string ItemWrapper<T>::AlarmSystemWrapper::getTone(const std::string& toneName) const {
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
std::string ItemWrapper<T>::AlarmSystemWrapper::getAssignedTone(const std::string& event) const {
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
void ItemWrapper<T>::AlarmSystemWrapper::resetAlarmConfig() {
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
void ItemWrapper<T>::AlarmSystemWrapper::playTone(const std::string& toneName) {
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










