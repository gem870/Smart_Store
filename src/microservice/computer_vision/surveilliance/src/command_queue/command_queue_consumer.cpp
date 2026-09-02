#include "command_queue_consumer.hpp"
#include "record_event/event_recorder.hpp"
#include "../../../err_log/Logger.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

// ADL to_json overloads for the plain DB result structs (database.hpp) --
// none of them have their own serialization, so it's added here, next to
// the only place in the codebase that needs it, rather than on the structs
// themselves. Must sit at true global/file scope, NOT inside the anonymous
// namespace below -- ADL resolves an unqualified to_json(j, val) call by
// searching the namespaces where val's type is actually declared (global,
// here), and an anonymous namespace's contents are only reachable there via
// an implicit using-directive, which ADL does not follow (confirmed the hard
// way: MSVC rejected every call site below with "no user-defined-conversion
// operator" when these lived inside namespace {} ). `embedding` is
// deliberately omitted on the tracked-entity structs -- internal/large,
// never exposed anywhere else either.
void to_json(nlohmann::json& j, const DBTrackedFace& f) {
    j = nlohmann::json{
        {"id", f.id}, {"name", f.name}, {"imagePath", f.imagePath},
        {"timestamp", f.timestamp}, {"cameraId", f.cameraId},
        {"cameraRegion", f.cameraRegion}, {"status", f.status},
        {"description", f.description},
    };
}

void to_json(nlohmann::json& j, const DBTrackedPlate& p) {
    j = nlohmann::json{
        {"id", p.id}, {"name", p.name}, {"imagePath", p.imagePath},
        {"timestamp", p.timestamp}, {"cameraId", p.cameraId},
        {"cameraRegion", p.cameraRegion}, {"status", p.status},
        {"description", p.description},
    };
}

void to_json(nlohmann::json& j, const DBTrackedWeapon& w) {
    j = nlohmann::json{
        {"id", w.id}, {"name", w.name}, {"imagePath", w.imagePath},
        {"timestamp", w.timestamp}, {"cameraId", w.cameraId},
        {"cameraRegion", w.cameraRegion}, {"status", w.status},
        {"description", w.description},
    };
}

void to_json(nlohmann::json& j, const DBRegionHistoryEntry& e) {
    j = nlohmann::json{
        {"id", e.id}, {"entityId", e.entityId}, {"cameraId", e.cameraId},
        {"cameraRegion", e.cameraRegion}, {"timestamp", e.timestamp},
    };
}

void to_json(nlohmann::json& j, const DBEvent& e) {
    j = nlohmann::json{
        {"id", e.id}, {"type", e.type}, {"detail", e.detail},
        {"timestamp", e.timestamp}, {"cameraId", e.cameraId},
        {"cameraRegion", e.cameraRegion},
    };
}

void to_json(nlohmann::json& j, const DBRecording& r) {
    j = nlohmann::json{
        {"id", r.id}, {"cameraId", r.cameraId}, {"cameraRegion", r.cameraRegion},
        {"eventType", r.eventType}, {"filePath", r.filePath}, {"reason", r.reason},
        {"timestamp", r.timestamp}, {"durationSeconds", r.durationSeconds},
        {"fileSizeBytes", r.fileSizeBytes},
    };
}

void to_json(nlohmann::json& j, const DBDailyFaceMetrics& m) {
    j = nlohmann::json{
        {"detectionDate", m.detectionDate}, {"totalDetections", m.totalDetections},
        {"timestamp", m.timestamp},
    };
}

void to_json(nlohmann::json& j, const DBChatMessage& m) {
    j = nlohmann::json{
        {"id", m.id}, {"content", m.content}, {"senderName", m.senderName},
        {"createdAt", m.createdAt},
    };
}

namespace {

// Mirrors Surveillance::commandQueuePath() exactly -- both processes must
// agree on this path without either one depending on the other's headers.
std::filesystem::path commandQueuePath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "commands.jsonl";
}

// Mirrors Surveillance::responsesPath() exactly -- same reasoning as
// commandQueuePath() above, reverse direction.
std::filesystem::path responsesPath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "responses.jsonl";
}

template<typename T>
std::optional<T> optFromJson(const nlohmann::json& params, const std::string& key) {
    if (!params.contains(key) || params.at(key).is_null()) return std::nullopt;
    return params.at(key).get<T>();
}

} // namespace

CommandQueueConsumer::CommandQueueConsumer(FeatureConfigManager& features)
    : features_(features) {
    thread_ = std::thread(&CommandQueueConsumer::workerLoop, this);
}

CommandQueueConsumer::~CommandQueueConsumer() {
    running_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void CommandQueueConsumer::workerLoop() {
    while (running_.load()) {
        drainQueue();

        std::unique_lock<std::mutex> lock(cvMtx_);
        cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !running_.load(); });
    }
    // One last drain on the way out so a command queued right before
    // shutdown isn't silently dropped.
    drainQueue();
}

void CommandQueueConsumer::drainQueue() {
    const auto path = commandQueuePath();
    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc)) return;

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        if (!in) return;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) lines.push_back(line);
        }
    }
    if (lines.empty()) return;

    // Truncate before applying, not after: this queue has no in-flight
    // "processing" state, so a crash mid-apply is strictly better leaving a
    // few commands unapplied (loggable, re-triggerable by the original
    // caller) than replaying the same batch forever on every restart.
    std::ofstream clear(path, std::ios::trunc);
    clear.close();

    for (const auto& line : lines) {
        try {
            nlohmann::json entry = nlohmann::json::parse(line);
            std::string command = entry.value("command", "");
            std::string requestId = entry.value("requestId", "");
            nlohmann::json params = entry.contains("params") ? entry.at("params") : nlohmann::json::object();
            if (command.empty()) {
                LOG_CONTEXT(LogLevel::WARNING, "CommandQueueConsumer: queue line missing 'command', skipped.", {});
                continue;
            }
            dispatch(requestId, command, params);
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::WARNING,
                        std::string("CommandQueueConsumer: failed to parse/apply a queued line: ") + e.what(),
                        {});
        } catch (...) {
            LOG_CONTEXT(LogLevel::WARNING, "CommandQueueConsumer: unknown exception applying a queued line.", {});
        }
    }
}

void CommandQueueConsumer::writeResponse(const std::string& requestId, const std::string& command, const nlohmann::json& result) {
    if (requestId.empty()) {
        // No requestId to correlate against (e.g. an older/foreign queue
        // entry written before this field existed) -- nothing useful to do.
        return;
    }

    const auto path = responsesPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_CONTEXT(LogLevel::ERR,
                    "CommandQueueConsumer: failed to create responses directory: " + ec.message(),
                    {});
        return;
    }

    std::ofstream out(path, std::ios::app);
    if (!out) {
        LOG_CONTEXT(LogLevel::ERR,
                    "CommandQueueConsumer: failed to open responses file at " + path.string(),
                    {});
        return;
    }

    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    nlohmann::json entry;
    entry["requestId"] = requestId;
    entry["command"] = command;
    entry["result"] = result;
    entry["timestamp"] = nowSeconds;
    out << entry.dump() << "\n";
}

void CommandQueueConsumer::dispatch(const std::string& requestId, const std::string& command, const nlohmann::json& p) {
    auto& db = features_.getDBManager();

    try {
        // -----------------------------------------------------------------
        // Cloud settings
        // -----------------------------------------------------------------
        if (command == "setCloudEnabled") {
            features_.setCloudEnabled(p.at("enabled").get<bool>());

        } else if (command == "setCloudSettings") {
            // setCloudHardwareToken() FIRST -- it can auto-derive a station
            // name from the token's slug (see its own doc comment in
            // config.hpp); a non-empty stationName here is an explicit
            // override and must win, so it's applied AFTER, only when given.
            // See Surveillance.hpp::setCloudSettings's doc comment for why
            // this order is required.
            features_.setCloudBaseUrl(p.at("baseUrl").get<std::string>());
            features_.setCloudHardwareToken(p.at("hardwareToken").get<std::string>());
            features_.setCloudPollIntervalSec(p.at("pollIntervalSec").get<int>());
            std::string stationName = p.value("stationName", std::string());
            if (!stationName.empty()) {
                features_.setCloudStationName(stationName);
            }

        } else if (command == "setAccountId") {
            features_.setAccountId(p.at("accountId").get<std::string>());

        } else if (command == "setKafkaBrokerAddress") {
            features_.setKafkaBrokerAddress(p.at("brokerAddress").get<std::string>());

        } else if (command == "setStationLocation") {
            features_.setStationLocation(p.at("latitude").get<double>(), p.at("longitude").get<double>());

        // -----------------------------------------------------------------
        // Camera management
        // -----------------------------------------------------------------
        } else if (command == "addCamera") {
            CameraConfig cam;
            cam.name = p.at("name").get<std::string>();
            cam.id = p.at("id").get<std::string>();
            cam.source = p.at("source").get<std::string>();
            cam.onvifEndpoint = p.value("onvifEndpoint", std::string());
            cam.onvifUsername = p.value("onvifUsername", std::string());
            cam.onvifPassword = p.value("onvifPassword", std::string());
            cam.enableFaceRecognition = p.at("enableFaceRecognition").get<bool>();
            // width/height left at CameraConfig's own defaults (640/480) --
            // see Surveillance::addCamera's doc comment.
            features_.addCamera(cam);

        } else if (command == "updateCamera") {
            std::string camId = p.at("camId").get<std::string>();
            CameraConfig existing;
            if (features_.getCameraConfig(camId, existing)) {
                existing.name = p.at("name").get<std::string>();
                existing.source = p.at("source").get<std::string>();
                existing.onvifEndpoint = p.value("onvifEndpoint", existing.onvifEndpoint);
                existing.onvifUsername = p.value("onvifUsername", existing.onvifUsername);
                existing.onvifPassword = p.value("onvifPassword", existing.onvifPassword);
                // width/height carried through unchanged from the existing
                // camera -- see Surveillance::updateCamera's doc comment.
                features_.updateCamera(camId, existing);
            } else {
                LOG_CONTEXT(LogLevel::WARNING, "CommandQueueConsumer: updateCamera - unknown camId '" + camId + "'.", {});
            }

        } else if (command == "setCameraFeature") {
            features_.setCameraFeature(p.at("cameraId").get<std::string>(), p.at("featureKey").get<std::string>(), p.at("value").get<bool>());

        } else if (command == "removeCamera") {
            features_.removeCamera(p.at("camId").get<std::string>());

        } else if (command == "plugInPendingCamera") {
            features_.plugInPendingCamera(p.at("camName").get<std::string>(), p.at("camId").get<std::string>());

        } else if (command == "plugOutPendingCamera") {
            features_.plugOutPendingCamera(p.at("isDelete").get<bool>(), p.at("camName").get<std::string>(), p.at("camId").get<std::string>());

        } else if (command == "setFeatureForAllCameras") {
            features_.setFeatureForAllCameras(p.at("featureKey").get<std::string>(), p.at("value").get<bool>());

        // -----------------------------------------------------------------
        // Model settings
        // -----------------------------------------------------------------
        } else if (command == "upLoadModel") {
            features_.upLoadModel(p.at("modelType").get<std::string>(), p.at("modelPath").get<std::string>());

        } else if (command == "resetModelSettings") {
            features_.resetModelSettings();

        // -----------------------------------------------------------------
        // Alarm settings
        // -----------------------------------------------------------------
        } else if (command == "setBeepEnabled") {
            features_.setBeepEnabled(p.at("enabled").get<bool>());

        } else if (command == "setVoiceEnabled") {
            features_.setVoiceEnabled(p.at("enabled").get<bool>());

        } else if (command == "setVoiceGender") {
            features_.setVoiceGender(p.at("gender").get<std::string>());

        // -----------------------------------------------------------------
        // Per-camera feature settings (whole-struct setters)
        // -----------------------------------------------------------------
        } else if (command == "resetFaceSettings") {
            features_.resetFaceSettings(p.at("camId").get<std::string>());

        } else if (command == "setFaceSettings") {
            FaceSettings s;
            s.scaleFactor = p.at("scaleFactor").get<double>();
            s.minNeighbors = p.at("minNeighbors").get<int>();
            s.minFaceSize = cv::Size(p.at("minFaceSizeWidth").get<int>(), p.at("minFaceSizeHeight").get<int>());
            s.scoreThreshold = p.at("scoreThreshold").get<double>();
            s.nmsThreshold = p.at("nmsThreshold").get<double>();
            s.topK = p.at("topK").get<int>();
            s.useEqualizeHist = p.at("useEqualizeHist").get<bool>();
            s.maxDetections = p.at("maxDetections").get<int>();
            s.maxTrackAgeMs = p.at("maxTrackAgeMs").get<uint64_t>();
            s.iouThreshold = p.at("iouThreshold").get<double>();
            s.debugLogging = p.at("debugLogging").get<bool>();
            features_.setFaceSettings(p.at("camId").get<std::string>(), s);

        } else if (command == "resetMotionSettings") {
            features_.resetMotionSettings(p.at("camId").get<std::string>());

        } else if (command == "setMotionSettings") {
            MotionSettings s;
            s.diffThreshold = p.at("diffThreshold").get<double>();
            s.minArea = p.at("minArea").get<int>();
            s.crowdThreshold = p.at("crowdThreshold").get<std::size_t>();
            s.loiterSeconds = p.at("loiterSeconds").get<int>();
            s.leftBehindSeconds = p.at("leftBehindSeconds").get<int>();
            s.enableTracking = p.at("enableTracking").get<bool>();
            s.debugLogging = p.at("debugLogging").get<bool>();
            features_.setMotionSettings(p.at("camId").get<std::string>(), s);

        } else if (command == "addMotionZone") {
            ZoneSettings zone;
            zone.regionName = p.at("regionName").get<std::string>();
            zone.roi = cv::Rect(p.at("x").get<int>(), p.at("y").get<int>(), p.at("width").get<int>(), p.at("height").get<int>());
            zone.restricted = p.at("restricted").get<bool>();
            features_.addMotionZone(p.at("camId").get<std::string>(), zone);

        } else if (command == "clearMotionZones") {
            features_.clearMotionZones(p.at("camId").get<std::string>());

        } else if (command == "setNightVisionSettings") {
            NightVisionSettings s;
            s.gamma = p.at("gamma").get<double>();
            s.adaptiveMode = p.at("adaptiveMode").get<bool>();
            s.contrastMode = p.at("contrastMode").get<int>();
            s.clipLimit = p.at("clipLimit").get<double>();
            s.tileSize = p.at("tileSize").get<int>();
            s.denoisingStrength = p.at("denoisingStrength").get<int>();
            features_.setNightVisionSettings(p.at("camId").get<std::string>(), s);

        } else if (command == "setObjectDetectionSettings") {
            ObjectDetectionSettings s;
            s.inputSize = p.at("inputSize").get<int>();
            s.backend = p.at("backend").get<int>();
            s.target = p.at("target").get<int>();
            s.trackingEnabled = p.at("trackingEnabled").get<bool>();
            s.minConfForDraw = p.at("minConfForDraw").get<float>();
            features_.setObjectDetectionSettings(p.at("camId").get<std::string>(), s);

        } else if (command == "setVehicleSettings") {
            VehicleSettings s;
            s.lang = p.at("lang").get<std::string>();
            s.minPlateConfidence = p.at("minPlateConfidence").get<int>();
            s.enableAlerts = p.at("enableAlerts").get<bool>();
            s.saveImages = p.at("saveImages").get<bool>();
            features_.setVehicleSettings(p.at("camId").get<std::string>(), s);

        } else if (command == "setRecorderSettings") {
            RecorderSettings s;
            s.codec = static_cast<EventRecorder::Codec>(p.at("codec").get<int>());
            s.bitrate = p.at("bitrate").get<int>();
            s.maxDuration = p.at("maxDuration").get<int>();
            s.maxFileSize = p.at("maxFileSize").get<uint64_t>();
            s.eventType = p.at("eventType").get<std::string>();
            features_.setRecorderSettings(p.at("camId").get<std::string>(), s);

        // -----------------------------------------------------------------
        // Plate watchlist
        // -----------------------------------------------------------------
        } else if (command == "addPlateNumberToWatchlist") {
            features_.addPlateNumberToWatchlist(p.at("plate").get<std::string>());

        } else if (command == "removePlateNumberFromWatchlist") {
            features_.removePlateNumberFromWatchlist(p.at("plate").get<std::string>());

        } else if (command == "clearPlateNumberFromWatchList") {
            features_.clearPlateNumberFromWatchList();

        // -----------------------------------------------------------------
        // DataBaseManager -- Tracked Faces
        // -----------------------------------------------------------------
        } else if (command == "getAllTrackedFaces") {
            writeResponse(requestId, command, db.getAllTrackedFaces());
        } else if (command == "getUnknownTrackedFaces") {
            writeResponse(requestId, command, db.getUnknownTrackedFaces());
        } else if (command == "getAuthorizedTrackedFaces") {
            writeResponse(requestId, command, db.getAuthorizedTrackedFaces());
        } else if (command == "getWatchlistTrackedFaces") {
            writeResponse(requestId, command, db.getWatchlistTrackedFaces());
        } else if (command == "getTrackedFaceById") {
            writeResponse(requestId, command, db.getTrackedFaceById(p.at("faceId").get<std::string>()));
        } else if (command == "deleteTrackedFace") {
            db.deleteTrackedFace(p.at("id").get<std::string>());
        } else if (command == "deleteAllTrackedFaces") {
            db.deleteAllTrackedFaces();
        } else if (command == "deleteAllAuthorizedFaces") {
            db.deleteAllAuthorizedFaces();
        } else if (command == "deleteAllWatchlistFaces") {
            db.deleteAllWatchlistFaces();
        } else if (command == "registerFaceFromImage") {
            db.registerFaceFromImage(p.at("imagePath").get<std::string>(), p.at("name").get<std::string>(),
                                     p.at("status").get<std::string>(), p.at("description").get<std::string>(),
                                     p.value("externalId", std::string()), p.value("broadcastToCloud", false));
        } else if (command == "getFaceRegionHistory") {
            writeResponse(requestId, command, db.getFaceRegionHistory(p.at("faceId").get<std::string>()));

        // -----------------------------------------------------------------
        // DataBaseManager -- Tracked Plates
        // -----------------------------------------------------------------
        } else if (command == "logTrackedPlate") {
            db.logTrackedPlate(p.at("plateNumber").get<std::string>(), p.at("status").get<std::string>(), p.at("description").get<std::string>());
        } else if (command == "getAllTrackedPlates") {
            writeResponse(requestId, command, db.getAllTrackedPlates());
        } else if (command == "getUnknownTrackedPlates") {
            writeResponse(requestId, command, db.getUnknownTrackedPlates());
        } else if (command == "getAuthorizedTrackedPlates") {
            writeResponse(requestId, command, db.getAuthorizedTrackedPlates());
        } else if (command == "getWatchlistTrackedPlates") {
            writeResponse(requestId, command, db.getWatchlistTrackedPlates());
        } else if (command == "getTrackedPlateById") {
            writeResponse(requestId, command, db.getTrackedPlateById(p.at("plateId").get<std::string>()));
        } else if (command == "deleteTrackedPlate") {
            db.deleteTrackedPlate(p.at("id").get<std::string>());
        } else if (command == "deleteAllTrackedPlates") {
            db.deleteAllTrackedPlates();
        } else if (command == "deleteAllAuthorizedPlates") {
            db.deleteAllAuthorizedPlates();
        } else if (command == "deleteAllWatchlistPlates") {
            db.deleteAllWatchlistPlates();
        } else if (command == "registerPlateFromImage") {
            db.registerPlateFromImage(p.at("imagePath").get<std::string>(), p.at("plateNumber").get<std::string>(),
                                      p.at("status").get<std::string>(), p.value("externalId", std::string()));
        } else if (command == "getPlateRegionHistory") {
            writeResponse(requestId, command, db.getPlateRegionHistory(p.at("plateId").get<std::string>()));

        // -----------------------------------------------------------------
        // DataBaseManager -- Tracked Objects
        // -----------------------------------------------------------------
        } else if (command == "getObjectRegionHistory") {
            writeResponse(requestId, command, db.getObjectRegionHistory(p.at("objectId").get<std::string>()));

        // -----------------------------------------------------------------
        // DataBaseManager -- Tracked Weapons
        // -----------------------------------------------------------------
        } else if (command == "getAllTrackedWeapons") {
            writeResponse(requestId, command, db.getAllTrackedWeapons());
        } else if (command == "getUnknownTrackedWeapons") {
            writeResponse(requestId, command, db.getUnknownTrackedWeapons());
        } else if (command == "getAuthorizedTrackedWeapons") {
            writeResponse(requestId, command, db.getAuthorizedTrackedWeapons());
        } else if (command == "getWatchlistTrackedWeapons") {
            writeResponse(requestId, command, db.getWatchlistTrackedWeapons());
        } else if (command == "getTrackedWeaponById") {
            writeResponse(requestId, command, db.getTrackedWeaponById(p.at("weaponId").get<std::string>()));
        } else if (command == "deleteTrackedWeapon") {
            db.deleteTrackedWeapon(p.at("id").get<std::string>());
        } else if (command == "deleteAllTrackedWeapons") {
            db.deleteAllTrackedWeapons();
        } else if (command == "deleteAllAuthorizedWeapons") {
            db.deleteAllAuthorizedWeapons();
        } else if (command == "deleteAllWatchlistWeapons") {
            db.deleteAllWatchlistWeapons();
        } else if (command == "registerWeaponFromImage") {
            db.registerWeaponFromImage(p.at("imagePath").get<std::string>(), p.at("name").get<std::string>(),
                                       p.at("status").get<std::string>(), p.at("description").get<std::string>(),
                                       p.value("externalId", std::string()));
        } else if (command == "getWeaponRegionHistory") {
            writeResponse(requestId, command, db.getWeaponRegionHistory(p.at("weaponId").get<std::string>()));

        // -----------------------------------------------------------------
        // DataBaseManager -- User Management
        // -----------------------------------------------------------------
        } else if (command == "registerUser") {
            db.registerUser(p.at("username").get<std::string>(), p.at("passwordHash").get<std::string>(),
                            p.at("role").get<std::string>(), p.at("name").get<std::string>(),
                            p.at("imagePath").get<std::string>(), p.at("phoneNumber").get<std::string>(),
                            p.at("email").get<std::string>(), p.at("isActive").get<int>());
        } else if (command == "validateUserPassword") {
            bool ok = db.validateUserPassword(p.at("username").get<std::string>(), p.at("inputPlaintextPassword").get<std::string>());
            writeResponse(requestId, command, nlohmann::json{{"valid", ok}});
        } else if (command == "updateUserProfile") {
            db.updateUserProfile(p.at("username").get<std::string>(), p.at("passwordHash").get<std::string>(),
                                 p.at("role").get<std::string>(), p.at("name").get<std::string>(),
                                 p.at("imagePath").get<std::string>(), p.at("phoneNumber").get<std::string>(),
                                 p.at("email").get<std::string>(), p.at("isActive").get<int>());
        } else if (command == "updateUserStatus") {
            db.updateUserStatus(p.at("userId").get<std::string>(), p.at("activeState").get<int>());
        } else if (command == "deleteUserById") {
            db.deleteUserById(p.at("userId").get<std::string>());
        } else if (command == "updateLastLogin") {
            db.updateLastLogin(p.at("userId").get<std::string>(), p.at("timestamp").get<std::string>());
        } else if (command == "changeUserPassword") {
            db.changeUserPassword(p.at("userId").get<std::string>(), p.at("newPasswordHash").get<std::string>());

        // -----------------------------------------------------------------
        // DataBaseManager -- Events
        // -----------------------------------------------------------------
        } else if (command == "getAllEvents") {
            writeResponse(requestId, command, db.getAllEvents());
        } else if (command == "getEventById") {
            writeResponse(requestId, command, db.getEventById(p.at("eventId").get<std::string>()));
        } else if (command == "deleteAllEvents") {
            db.deleteAllEvents();
        } else if (command == "deleteEventById") {
            db.deleteEventById(p.at("eventId").get<std::string>());

        // -----------------------------------------------------------------
        // DataBaseManager -- Recordings
        // -----------------------------------------------------------------
        } else if (command == "getAllRecordings") {
            writeResponse(requestId, command, db.getAllRecordings());
        } else if (command == "getRecordingsByCameraId") {
            writeResponse(requestId, command, db.getRecordingsByCameraId(p.at("cameraId").get<std::string>()));
        } else if (command == "getRecordingById") {
            writeResponse(requestId, command, db.getRecordingById(p.at("id").get<std::string>()));
        } else if (command == "deleteAllRecordings") {
            db.deleteAllRecordings();
        } else if (command == "deleteRecordingById") {
            db.deleteRecordingById(p.at("id").get<std::string>());

        // -----------------------------------------------------------------
        // DataBaseManager -- Telemetry
        // -----------------------------------------------------------------
        } else if (command == "logTelemetry") {
            db.logTelemetry(p.at("metricType").get<std::string>(), p.at("nodeIp").get<std::string>(),
                            optFromJson<float>(p, "cpuUsage"), optFromJson<float>(p, "ramUsageMb"),
                            optFromJson<float>(p, "diskUsagePercent"), optFromJson<float>(p, "temperatureC"),
                            p.at("numberOfCameras").get<int>(), p.at("numberOfActiveCameras").get<int>(),
                            p.at("numberOfNonActiveCameras").get<int>(), optFromJson<float>(p, "fps"),
                            optFromJson<int>(p, "latencyMs"));
        } else if (command == "pruneTelemetryBefore") {
            db.pruneTelemetryBefore(p.at("beforeTimestamp").get<long long>());

        // -----------------------------------------------------------------
        // DataBaseManager -- Daily Face Metrics
        // -----------------------------------------------------------------
        } else if (command == "insertDailyFaceMetrics") {
            DBDailyFaceMetrics m;
            m.detectionDate = p.at("detectionDate").get<std::string>();
            m.totalDetections = p.at("totalDetections").get<int>();
            m.timestamp = p.at("timestamp").get<std::string>();
            db.insertDailyFaceMetrics(m);
        } else if (command == "getAllDailyFaceMetrics") {
            writeResponse(requestId, command, db.getAllDailyFaceMetrics());
        } else if (command == "getDailyFaceMetricsByDate") {
            writeResponse(requestId, command, db.getDailyFaceMetricsByDate(p.at("detectionDate").get<std::string>()));
        } else if (command == "deleteAllDailyFaceMetrics") {
            db.deleteAllDailyFaceMetrics();
        } else if (command == "deleteDailyFaceMetricsByDate") {
            db.deleteDailyFaceMetricsByDate(p.at("detectionDate").get<std::string>());

        // -----------------------------------------------------------------
        // DataBaseManager -- Chat
        // -----------------------------------------------------------------
        } else if (command == "insertChatMessage") {
            DBChatMessage m;
            m.id = p.at("id").get<std::string>();
            m.content = p.at("content").get<std::string>();
            m.senderName = p.at("senderName").get<std::string>();
            m.createdAt = p.at("createdAt").get<std::string>();
            db.insertChatMessage(m);
        } else if (command == "getRecentChatMessages") {
            writeResponse(requestId, command, db.getRecentChatMessages(p.value("limit", 100)));

        } else {
            LOG_CONTEXT(LogLevel::WARNING, "CommandQueueConsumer: unknown queued command '" + command + "', skipped.", {});
        }
    } catch (const std::exception& e) {
        LOG_CONTEXT(LogLevel::WARNING,
                    "CommandQueueConsumer: exception applying command '" + command + "': " + e.what(),
                    {});
    } catch (...) {
        LOG_CONTEXT(LogLevel::WARNING, "CommandQueueConsumer: unknown exception applying command '" + command + "'.", {});
    }
}
