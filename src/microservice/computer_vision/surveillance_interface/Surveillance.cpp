#include "Surveillance.hpp"
#include <err_log/Logger.hpp>
#include <fstream>
#include <chrono>
#include <cstdlib>
#include <system_error>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace {
// nlohmann::json has no built-in std::optional support without an ADL
// to_json overload -- empty becomes JSON null, matching how a genuinely
// absent optional reading should read once something on the other end
// actually consumes these.
template<typename T>
nlohmann::json optToJson(const std::optional<T>& value) {
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

// Env var naming the built SmartSurveillance.exe's absolute path. Unset ->
// start() falls back to sentinel-only behavior (manual/dev launch still
// expected) rather than guessing a fragile relative path across two
// independently-built executables with unrelated output directories.
constexpr const char* kExePathEnvVar = "SMART_SURVEILLANCE_EXE_PATH";

#ifdef _WIN32
bool isPidAlive(unsigned long pid) {
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, static_cast<DWORD>(pid));
    if (!h) return false;
    DWORD exitCode = 0;
    bool alive = GetExitCodeProcess(h, &exitCode) && exitCode == STILL_ACTIVE;
    CloseHandle(h);
    return alive;
}

std::optional<unsigned long> spawnEngineProcess() {
    const char* exePath = std::getenv(kExePathEnvVar);
    if (!exePath || !*exePath) {
        LOG_CONTEXT(LogLevel::WARNING,
                    std::string("Surveillance::start -- ") + kExePathEnvVar +
                    " not set, cannot spawn the engine process automatically; "
                    "writing sentinel only (manual/dev launch still expected).",
                    {});
        return std::nullopt;
    }

    STARTUPINFOA si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::string cmdLine = "\"" + std::string(exePath) + "\"";
    BOOL ok = CreateProcessA(exePath, cmdLine.data(), nullptr, nullptr, FALSE,
                              CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si, &pi);
    if (!ok) {
        LOG_CONTEXT(LogLevel::ERR,
                    "Surveillance::start -- failed to spawn " + std::string(exePath) +
                    " (GetLastError=" + std::to_string(GetLastError()) + ").",
                    {});
        return std::nullopt;
    }

    CloseHandle(pi.hThread);
    unsigned long pid = pi.dwProcessId;
    CloseHandle(pi.hProcess);
    LOG_CONTEXT(LogLevel::INFO,
                "Surveillance::start -- spawned engine process, pid " + std::to_string(pid) + ".",
                {});
    return pid;
}
#else
bool isPidAlive(unsigned long) { return false; }

std::optional<unsigned long> spawnEngineProcess() {
    LOG_CONTEXT(LogLevel::WARNING,
                "Surveillance::start -- automatic process spawn is only implemented on Windows; writing sentinel only.",
                {});
    return std::nullopt;
}
#endif

// Reads back the pid this handle (or a prior one) recorded in the sentinel,
// if any. Tolerates older/foreign sentinel content (e.g. a bare timestamp
// from before this JSON format existed) by treating anything unparseable as
// "no known pid" rather than an error.
std::optional<unsigned long> readSentinelPid(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return std::nullopt;
    try {
        nlohmann::json j;
        in >> j;
        if (j.contains("pid") && !j.at("pid").is_null()) {
            return j.at("pid").get<unsigned long>();
        }
    } catch (const std::exception&) {
    }
    return std::nullopt;
}
}

Surveillance::Surveillance() = default;

std::filesystem::path Surveillance::sentinelPath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "surveillance.run";
}

void Surveillance::start() {
    if (running_) {
        LOG_CONTEXT(LogLevel::WARNING, "Surveillance start requested but already running.", {});
        return;
    }

    const auto path = sentinelPath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_CONTEXT(LogLevel::ERR,
                    "Failed to create surveillance sentinel directory: " + ec.message(),
                    {});
        return;
    }

    // If a sentinel from a prior managed start is still present and its
    // recorded pid is genuinely alive, adopt it rather than spawning a
    // second engine process -- running_ alone can't tell us this across an
    // ItemManager restart (it's in-memory, per-instance; see isRunning()'s
    // own doc comment), but the sentinel's pid survives on disk.
    std::optional<unsigned long> pid;
    if (std::filesystem::exists(path, ec)) {
        auto existingPid = readSentinelPid(path);
        if (existingPid && isPidAlive(*existingPid)) {
            pid = existingPid;
            LOG_CONTEXT(LogLevel::INFO,
                        "Surveillance::start -- engine process already running (pid " +
                        std::to_string(*pid) + "), adopting it.",
                        {});
        }
    }
    if (!pid) {
        pid = spawnEngineProcess();
    }

    nlohmann::json sentinelJson;
    sentinelJson["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    sentinelJson["pid"] = optToJson(pid);

    std::ofstream sentinel(path, std::ios::trunc);
    if (!sentinel) {
        LOG_CONTEXT(LogLevel::ERR,
                    "Failed to write surveillance sentinel file at " + path.string(),
                    {});
        return;
    }
    sentinel << sentinelJson.dump();
    sentinel.close();

    running_ = true;
    LOG_CONTEXT(LogLevel::INFO,
                "Surveillance sentinel written -- marked as should-be-running.",
                {});
}

void Surveillance::stop() {
    if (!running_) {
        return;
    }

    std::error_code ec;
    std::filesystem::remove(sentinelPath(), ec);
    if (ec) {
        LOG_CONTEXT(LogLevel::WARNING,
                    "Failed to remove surveillance sentinel file: " + ec.message(),
                    {});
    }

    running_ = false;
    LOG_CONTEXT(LogLevel::INFO, "Surveillance sentinel removed.", {});
}

bool Surveillance::isRunning() const {
    return running_;
}

std::filesystem::path Surveillance::commandQueuePath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "commands.jsonl";
}

std::filesystem::path Surveillance::responsesPath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "responses.jsonl";
}

std::string Surveillance::writeCommand(const std::string& command, const nlohmann::json& params) {
    const auto path = commandQueuePath();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        LOG_CONTEXT(LogLevel::ERR,
                    "Failed to create surveillance command queue directory: " + ec.message(),
                    {});
        return "";
    }

    // Append-only, one JSON object per line (JSON Lines) -- a single writer
    // here, so no locking needed on this side. The reader
    // (CommandQueueConsumer, in the surveillance app's own build) is
    // responsible for truncating/rotating this file as it drains it.
    std::ofstream out(path, std::ios::app);
    if (!out) {
        LOG_CONTEXT(LogLevel::ERR,
                    "Failed to open surveillance command queue at " + path.string(),
                    {});
        return "";
    }

    const auto nowSeconds = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
    // Unique for this process's lifetime -- collisions across a restart are
    // harmless since responsesPath() is drained/truncated well before then
    // in practice, and correlation only ever needs to hold within one
    // still-open request/response window.
    std::string requestId = std::to_string(nowSeconds) + "-" + std::to_string(requestCounter_.fetch_add(1));

    nlohmann::json entry;
    entry["requestId"] = requestId;
    entry["command"] = command;
    entry["params"] = params;
    entry["timestamp"] = nowSeconds;
    out << entry.dump() << "\n";
    out.close();

    LOG_CONTEXT(LogLevel::INFO, "Surveillance command queued: " + command, {});
    return requestId;
}

std::vector<nlohmann::json> Surveillance::drainResponses() {
    const auto path = responsesPath();
    std::vector<nlohmann::json> results;

    std::error_code existsEc;
    if (!std::filesystem::exists(path, existsEc)) {
        return results;
    }

    std::vector<std::string> lines;
    {
        std::ifstream in(path);
        if (!in) return results;
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty()) lines.push_back(line);
        }
    }
    if (lines.empty()) return results;

    // Truncate before returning, not after -- same reasoning as
    // CommandQueueConsumer::drainQueue(): a response appended in the exact
    // window between the read and the truncate is lost, acceptable for this
    // best-effort local channel, same as every other file-based queue here.
    std::ofstream clear(path, std::ios::trunc);
    clear.close();

    results.reserve(lines.size());
    for (const auto& line : lines) {
        try {
            results.push_back(nlohmann::json::parse(line));
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::WARNING,
                        std::string("Surveillance::drainResponses -- failed to parse a response line: ") + e.what(),
                        {});
        }
    }
    return results;
}

void Surveillance::setCloudEnabled(bool enabled) {
    writeCommand("setCloudEnabled", {{"enabled", enabled}});
}

void Surveillance::setCloudSettings(const std::string& baseUrl,
                                    const std::string& stationName,
                                    const std::string& hardwareToken,
                                    int pollIntervalSec) {
    writeCommand("setCloudSettings", {
        {"baseUrl", baseUrl},
        {"stationName", stationName},
        {"hardwareToken", hardwareToken},
        {"pollIntervalSec", pollIntervalSec},
    });
}

void Surveillance::addCamera(const std::string& name,
                             const std::string& id,
                             const std::string& source,
                             bool enableFaceRecognition) {
    writeCommand("addCamera", {
        {"name", name},
        {"id", id},
        {"source", source},
        {"enableFaceRecognition", enableFaceRecognition},
    });
}

void Surveillance::setCameraFeature(const std::string& cameraId, const std::string& featureKey, bool value) {
    writeCommand("setCameraFeature", {
        {"cameraId", cameraId},
        {"featureKey", featureKey},
        {"value", value},
    });
}

void Surveillance::updateCamera(const std::string& camId,
                                const std::string& name,
                                const std::string& source) {
    writeCommand("updateCamera", {
        {"camId", camId},
        {"name", name},
        {"source", source},
    });
}

void Surveillance::removeCamera(const std::string& camId) {
    writeCommand("removeCamera", {{"camId", camId}});
}

void Surveillance::plugInPendingCamera(const std::string& camName, const std::string& camId) {
    writeCommand("plugInPendingCamera", {
        {"camName", camName},
        {"camId", camId},
    });
}

void Surveillance::plugOutPendingCamera(bool isDelete, const std::string& camName, const std::string& camId) {
    writeCommand("plugOutPendingCamera", {
        {"isDelete", isDelete},
        {"camName", camName},
        {"camId", camId},
    });
}

void Surveillance::setFeatureForAllCameras(const std::string& featureKey, bool value) {
    writeCommand("setFeatureForAllCameras", {
        {"featureKey", featureKey},
        {"value", value},
    });
}

void Surveillance::upLoadModel(const std::string& modelType, const std::string& modelPath) {
    writeCommand("upLoadModel", {
        {"modelType", modelType},
        {"modelPath", modelPath},
    });
}

void Surveillance::resetModelSettings() {
    writeCommand("resetModelSettings", {});
}

void Surveillance::setBeepEnabled(bool enabled) {
    writeCommand("setBeepEnabled", {{"enabled", enabled}});
}

void Surveillance::setVoiceEnabled(bool enabled) {
    writeCommand("setVoiceEnabled", {{"enabled", enabled}});
}

void Surveillance::setVoiceGender(const std::string& gender) {
    writeCommand("setVoiceGender", {{"gender", gender}});
}

void Surveillance::setStationLocation(double latitude, double longitude) {
    writeCommand("setStationLocation", {
        {"latitude", latitude},
        {"longitude", longitude},
    });
}

void Surveillance::resetFaceSettings(const std::string& camId) {
    writeCommand("resetFaceSettings", {{"camId", camId}});
}

void Surveillance::resetMotionSettings(const std::string& camId) {
    writeCommand("resetMotionSettings", {{"camId", camId}});
}

void Surveillance::addMotionZone(const std::string& camId,
                                 const std::string& regionName,
                                 int x, int y, int width, int height,
                                 bool restricted) {
    writeCommand("addMotionZone", {
        {"camId", camId},
        {"regionName", regionName},
        {"x", x},
        {"y", y},
        {"width", width},
        {"height", height},
        {"restricted", restricted},
    });
}

void Surveillance::clearMotionZones(const std::string& camId) {
    writeCommand("clearMotionZones", {{"camId", camId}});
}

void Surveillance::addPlateNumberToWatchlist(const std::string& plate) {
    writeCommand("addPlateNumberToWatchlist", {{"plate", plate}});
}

void Surveillance::removePlateNumberFromWatchlist(const std::string& plate) {
    writeCommand("removePlateNumberFromWatchlist", {{"plate", plate}});
}

void Surveillance::clearPlateNumberFromWatchList() {
    writeCommand("clearPlateNumberFromWatchList", {});
}

void Surveillance::setAccountId(const std::string& accountId) {
    writeCommand("setAccountId", {{"accountId", accountId}});
}

void Surveillance::setKafkaBrokerAddress(const std::string& brokerAddress) {
    writeCommand("setKafkaBrokerAddress", {{"brokerAddress", brokerAddress}});
}

void Surveillance::setFaceSettings(const std::string& camId,
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
    writeCommand("setFaceSettings", {
        {"camId", camId},
        {"scaleFactor", scaleFactor},
        {"minNeighbors", minNeighbors},
        {"minFaceSizeWidth", minFaceSizeWidth},
        {"minFaceSizeHeight", minFaceSizeHeight},
        {"scoreThreshold", scoreThreshold},
        {"nmsThreshold", nmsThreshold},
        {"topK", topK},
        {"useEqualizeHist", useEqualizeHist},
        {"maxDetections", maxDetections},
        {"maxTrackAgeMs", maxTrackAgeMs},
        {"iouThreshold", iouThreshold},
        {"debugLogging", debugLogging},
    });
}

void Surveillance::setMotionSettings(const std::string& camId,
                                     double diffThreshold,
                                     int minArea,
                                     std::size_t crowdThreshold,
                                     int loiterSeconds,
                                     int leftBehindSeconds,
                                     bool enableTracking,
                                     bool debugLogging) {
    writeCommand("setMotionSettings", {
        {"camId", camId},
        {"diffThreshold", diffThreshold},
        {"minArea", minArea},
        {"crowdThreshold", crowdThreshold},
        {"loiterSeconds", loiterSeconds},
        {"leftBehindSeconds", leftBehindSeconds},
        {"enableTracking", enableTracking},
        {"debugLogging", debugLogging},
    });
}

void Surveillance::setNightVisionSettings(const std::string& camId,
                                          double gamma,
                                          bool adaptiveMode,
                                          int contrastMode,
                                          double clipLimit,
                                          int tileSize,
                                          int denoisingStrength) {
    writeCommand("setNightVisionSettings", {
        {"camId", camId},
        {"gamma", gamma},
        {"adaptiveMode", adaptiveMode},
        {"contrastMode", contrastMode},
        {"clipLimit", clipLimit},
        {"tileSize", tileSize},
        {"denoisingStrength", denoisingStrength},
    });
}

void Surveillance::setObjectDetectionSettings(const std::string& camId,
                                              int inputSize,
                                              int backend,
                                              int target,
                                              bool trackingEnabled,
                                              float minConfForDraw) {
    writeCommand("setObjectDetectionSettings", {
        {"camId", camId},
        {"inputSize", inputSize},
        {"backend", backend},
        {"target", target},
        {"trackingEnabled", trackingEnabled},
        {"minConfForDraw", minConfForDraw},
    });
}

void Surveillance::setVehicleSettings(const std::string& camId,
                                      const std::string& lang,
                                      int minPlateConfidence,
                                      bool enableAlerts,
                                      bool saveImages) {
    writeCommand("setVehicleSettings", {
        {"camId", camId},
        {"lang", lang},
        {"minPlateConfidence", minPlateConfidence},
        {"enableAlerts", enableAlerts},
        {"saveImages", saveImages},
    });
}

void Surveillance::setRecorderSettings(const std::string& camId,
                                       int codec,
                                       int bitrate,
                                       int maxDuration,
                                       uint64_t maxFileSize,
                                       const std::string& eventType) {
    writeCommand("setRecorderSettings", {
        {"camId", camId},
        {"codec", codec},
        {"bitrate", bitrate},
        {"maxDuration", maxDuration},
        {"maxFileSize", maxFileSize},
        {"eventType", eventType},
    });
}

// ---------------------------------------------------------------------
// DataBaseManager passthroughs
// ---------------------------------------------------------------------

// --- Tracked Faces ---
std::string Surveillance::getAllTrackedFaces() { return writeCommand("getAllTrackedFaces", {}); }
std::string Surveillance::getUnknownTrackedFaces() { return writeCommand("getUnknownTrackedFaces", {}); }
std::string Surveillance::getAuthorizedTrackedFaces() { return writeCommand("getAuthorizedTrackedFaces", {}); }
std::string Surveillance::getWatchlistTrackedFaces() { return writeCommand("getWatchlistTrackedFaces", {}); }
std::string Surveillance::getTrackedFaceById(const std::string& faceId) { return writeCommand("getTrackedFaceById", {{"faceId", faceId}}); }
void Surveillance::deleteTrackedFace(const std::string& id) { writeCommand("deleteTrackedFace", {{"id", id}}); }
void Surveillance::deleteAllTrackedFaces() { writeCommand("deleteAllTrackedFaces", {}); }
void Surveillance::deleteAllAuthorizedFaces() { writeCommand("deleteAllAuthorizedFaces", {}); }
void Surveillance::deleteAllWatchlistFaces() { writeCommand("deleteAllWatchlistFaces", {}); }

void Surveillance::registerFaceFromImage(const std::string& imagePath,
                                         const std::string& name,
                                         const std::string& status,
                                         const std::string& description,
                                         const std::string& externalId,
                                         bool broadcastToCloud) {
    writeCommand("registerFaceFromImage", {
        {"imagePath", imagePath},
        {"name", name},
        {"status", status},
        {"description", description},
        {"externalId", externalId},
        {"broadcastToCloud", broadcastToCloud},
    });
}

std::string Surveillance::getFaceRegionHistory(const std::string& faceId) { return writeCommand("getFaceRegionHistory", {{"faceId", faceId}}); }

// --- Tracked Plates ---
void Surveillance::logTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description) {
    writeCommand("logTrackedPlate", {
        {"plateNumber", plateNumber},
        {"status", status},
        {"description", description},
    });
}
std::string Surveillance::getAllTrackedPlates() { return writeCommand("getAllTrackedPlates", {}); }
std::string Surveillance::getUnknownTrackedPlates() { return writeCommand("getUnknownTrackedPlates", {}); }
std::string Surveillance::getAuthorizedTrackedPlates() { return writeCommand("getAuthorizedTrackedPlates", {}); }
std::string Surveillance::getWatchlistTrackedPlates() { return writeCommand("getWatchlistTrackedPlates", {}); }
std::string Surveillance::getTrackedPlateById(const std::string& plateId) { return writeCommand("getTrackedPlateById", {{"plateId", plateId}}); }
void Surveillance::deleteTrackedPlate(const std::string& id) { writeCommand("deleteTrackedPlate", {{"id", id}}); }
void Surveillance::deleteAllTrackedPlates() { writeCommand("deleteAllTrackedPlates", {}); }
void Surveillance::deleteAllAuthorizedPlates() { writeCommand("deleteAllAuthorizedPlates", {}); }
void Surveillance::deleteAllWatchlistPlates() { writeCommand("deleteAllWatchlistPlates", {}); }

void Surveillance::registerPlateFromImage(const std::string& imagePath,
                                          const std::string& plateNumber,
                                          const std::string& status,
                                          const std::string& externalId) {
    writeCommand("registerPlateFromImage", {
        {"imagePath", imagePath},
        {"plateNumber", plateNumber},
        {"status", status},
        {"externalId", externalId},
    });
}

std::string Surveillance::getPlateRegionHistory(const std::string& plateId) { return writeCommand("getPlateRegionHistory", {{"plateId", plateId}}); }

// --- Tracked Objects ---
std::string Surveillance::getObjectRegionHistory(const std::string& objectId) { return writeCommand("getObjectRegionHistory", {{"objectId", objectId}}); }

// --- Tracked Weapons ---
std::string Surveillance::getAllTrackedWeapons() { return writeCommand("getAllTrackedWeapons", {}); }
std::string Surveillance::getUnknownTrackedWeapons() { return writeCommand("getUnknownTrackedWeapons", {}); }
std::string Surveillance::getAuthorizedTrackedWeapons() { return writeCommand("getAuthorizedTrackedWeapons", {}); }
std::string Surveillance::getWatchlistTrackedWeapons() { return writeCommand("getWatchlistTrackedWeapons", {}); }
std::string Surveillance::getTrackedWeaponById(const std::string& weaponId) { return writeCommand("getTrackedWeaponById", {{"weaponId", weaponId}}); }
void Surveillance::deleteTrackedWeapon(const std::string& id) { writeCommand("deleteTrackedWeapon", {{"id", id}}); }
void Surveillance::deleteAllTrackedWeapons() { writeCommand("deleteAllTrackedWeapons", {}); }
void Surveillance::deleteAllAuthorizedWeapons() { writeCommand("deleteAllAuthorizedWeapons", {}); }
void Surveillance::deleteAllWatchlistWeapons() { writeCommand("deleteAllWatchlistWeapons", {}); }

void Surveillance::registerWeaponFromImage(const std::string& imagePath,
                                           const std::string& name,
                                           const std::string& status,
                                           const std::string& description,
                                           const std::string& externalId) {
    writeCommand("registerWeaponFromImage", {
        {"imagePath", imagePath},
        {"name", name},
        {"status", status},
        {"description", description},
        {"externalId", externalId},
    });
}

std::string Surveillance::getWeaponRegionHistory(const std::string& weaponId) { return writeCommand("getWeaponRegionHistory", {{"weaponId", weaponId}}); }

// --- User Management ---
void Surveillance::registerUser(const std::string& username,
                                const std::string& passwordHash,
                                const std::string& role,
                                const std::string& name,
                                const std::string& imagePath,
                                const std::string& phoneNumber,
                                const std::string& email,
                                int isActive) {
    writeCommand("registerUser", {
        {"username", username},
        {"passwordHash", passwordHash},
        {"role", role},
        {"name", name},
        {"imagePath", imagePath},
        {"phoneNumber", phoneNumber},
        {"email", email},
        {"isActive", isActive},
    });
}

std::string Surveillance::validateUserPassword(const std::string& username, const std::string& inputPlaintextPassword) {
    return writeCommand("validateUserPassword", {
        {"username", username},
        {"inputPlaintextPassword", inputPlaintextPassword},
    });
}

void Surveillance::updateUserProfile(const std::string& username,
                                     const std::string& passwordHash,
                                     const std::string& role,
                                     const std::string& name,
                                     const std::string& imagePath,
                                     const std::string& phoneNumber,
                                     const std::string& email,
                                     int isActive) {
    writeCommand("updateUserProfile", {
        {"username", username},
        {"passwordHash", passwordHash},
        {"role", role},
        {"name", name},
        {"imagePath", imagePath},
        {"phoneNumber", phoneNumber},
        {"email", email},
        {"isActive", isActive},
    });
}

void Surveillance::updateUserStatus(const std::string& userId, int activeState) {
    writeCommand("updateUserStatus", {
        {"userId", userId},
        {"activeState", activeState},
    });
}

void Surveillance::deleteUserById(const std::string& userId) { writeCommand("deleteUserById", {{"userId", userId}}); }

void Surveillance::updateLastLogin(const std::string& userId, const std::string& timestamp) {
    writeCommand("updateLastLogin", {
        {"userId", userId},
        {"timestamp", timestamp},
    });
}

void Surveillance::changeUserPassword(const std::string& userId, const std::string& newPasswordHash) {
    writeCommand("changeUserPassword", {
        {"userId", userId},
        {"newPasswordHash", newPasswordHash},
    });
}

// --- Events ---
std::string Surveillance::getAllEvents() { return writeCommand("getAllEvents", {}); }
std::string Surveillance::getEventById(const std::string& eventId) { return writeCommand("getEventById", {{"eventId", eventId}}); }
void Surveillance::deleteAllEvents() { writeCommand("deleteAllEvents", {}); }
void Surveillance::deleteEventById(const std::string& eventId) { writeCommand("deleteEventById", {{"eventId", eventId}}); }

// --- Recordings ---
std::string Surveillance::getAllRecordings() { return writeCommand("getAllRecordings", {}); }
std::string Surveillance::getRecordingsByCameraId(const std::string& cameraId) { return writeCommand("getRecordingsByCameraId", {{"cameraId", cameraId}}); }
std::string Surveillance::getRecordingById(const std::string& id) { return writeCommand("getRecordingById", {{"id", id}}); }
void Surveillance::deleteAllRecordings() { writeCommand("deleteAllRecordings", {}); }
void Surveillance::deleteRecordingById(const std::string& id) { writeCommand("deleteRecordingById", {{"id", id}}); }

// --- Telemetry ---
void Surveillance::logTelemetry(const std::string& metricType,
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
    writeCommand("logTelemetry", {
        {"metricType", metricType},
        {"nodeIp", nodeIp},
        {"cpuUsage", optToJson(cpuUsage)},
        {"ramUsageMb", optToJson(ramUsageMb)},
        {"diskUsagePercent", optToJson(diskUsagePercent)},
        {"temperatureC", optToJson(temperatureC)},
        {"numberOfCameras", numberOfCameras},
        {"numberOfActiveCameras", numberOfActiveCameras},
        {"numberOfNonActiveCameras", numberOfNonActiveCameras},
        {"fps", optToJson(fps)},
        {"latencyMs", optToJson(latencyMs)},
    });
}

void Surveillance::pruneTelemetryBefore(long long beforeTimestamp) {
    writeCommand("pruneTelemetryBefore", {{"beforeTimestamp", beforeTimestamp}});
}

// --- Daily Face Metrics ---
void Surveillance::insertDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp) {
    writeCommand("insertDailyFaceMetrics", {
        {"detectionDate", detectionDate},
        {"totalDetections", totalDetections},
        {"timestamp", timestamp},
    });
}
std::string Surveillance::getAllDailyFaceMetrics() { return writeCommand("getAllDailyFaceMetrics", {}); }
std::string Surveillance::getDailyFaceMetricsByDate(const std::string& detectionDate) { return writeCommand("getDailyFaceMetricsByDate", {{"detectionDate", detectionDate}}); }
void Surveillance::deleteAllDailyFaceMetrics() { writeCommand("deleteAllDailyFaceMetrics", {}); }
void Surveillance::deleteDailyFaceMetricsByDate(const std::string& detectionDate) { writeCommand("deleteDailyFaceMetricsByDate", {{"detectionDate", detectionDate}}); }

// --- Chat ---
void Surveillance::insertChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt) {
    writeCommand("insertChatMessage", {
        {"id", id},
        {"content", content},
        {"senderName", senderName},
        {"createdAt", createdAt},
    });
}
std::string Surveillance::getRecentChatMessages(int limit) { return writeCommand("getRecentChatMessages", {{"limit", limit}}); }
