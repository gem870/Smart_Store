#ifndef SURVEILLANCE_INTERFACE_HPP
#define SURVEILLANCE_INTERFACE_HPP

#include <interface/BaseMicroservice.hpp>
#include <nlohmann/json.hpp>
#include <atomic>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

/**
 * @class Surveillance
 * @brief ItemManager-facing handle for the standalone surveillance system.
 *
 * Deliberately thin: it does not own CloudClient/SurveillanceController,
 * FeatureConfigManager, or any of the standalone surveillance app's heavy
 * state directly -- that stack now lives in systemInterface, inside the
 * surveillance app's own build (see
 * microservice/computer_vision/surveilliance/src/system interface/
 * system_interface.hpp), which pulls in dependencies (SQLCipher, FAISS,
 * Kafka, its detection headers) this build deliberately never links.
 *
 * ItemManagerProject and the surveillance app are two separate executables
 * -- there is no in-process call path from here to systemInterface.
 *
 * start()/stop() write/delete a small sentinel file at a fixed, absolute,
 * working-directory-independent path. `SurveillanceLifecycleWatcher`
 * (surveilliance/src/command_queue/surveillance_lifecycle_watcher.hpp),
 * constructed as part of systemInterface itself, watches that path and
 * drives a graceful SurveillanceController shutdown on a present -> absent
 * transition. start() also attempts to spawn the engine process directly
 * (see SMART_SURVEILLANCE_EXE_PATH in Surveillance.cpp) when it isn't
 * already running, falling back to sentinel-only (manual/dev launch still
 * expected) if that env var isn't set. This was originally documented as the
 * native SDL2 local frontend's job once that exists; built here instead for
 * now per an explicit call to not block on that still-on-hold work.
 *
 * Every other method here (setCloudSettings, addCamera, ...) -- anything
 * that needs a real value supplied by a caller, per this project's own
 * "just pass it, like an API" design -- appends a small JSON command to a
 * second file, commandQueuePath(), instead of calling FeatureConfigManager
 * directly (impossible across the process boundary) or touching the real
 * surveillance_config.json (which this process has no business writing).
 * The frontend is responsible for reading and applying each queued command
 * through FeatureConfigManager's real methods, the same way it applies the
 * sentinel. This class never validates or interprets a command's contents
 * -- it only ever appends what it's given.
 */
class Surveillance : public BaseMicroservice {
public:
    /**
     * @brief Default constructor. Starts in the stopped state.
     */
    Surveillance();

    /**
     * @brief Write the sentinel file, marking surveillance as
     * should-be-running.
     *
     * Called with ItemManager::mutex_ held by the caller (see
     * ItemManager::ComputerVision::cvSurv_start), so this must stay quick --
     * one small file write, nothing blocking.
     */
    void start();

    /**
     * @brief Delete the sentinel file.
     *
     * Same locking contract as start(): called with ItemManager::mutex_
     * held, must stay quick.
     */
    void stop();

    /**
     * @brief Whether start() has been called without a matching stop() --
     * local state only.
     *
     * This does NOT confirm the surveillance frontend process is actually
     * alive; it only reflects whether this handle believes the sentinel is
     * present. Confirming real liveness needs a heartbeat/PID signal back
     * from the frontend itself, once that exists -- deliberately deferred.
     */
    bool isRunning() const;

    /**
     * @brief The fixed, absolute path both this process and the
     * surveillance frontend must agree on.
     *
     * Rooted under the OS temp directory for now -- writable without
     * special permissions in any environment (including tests), unlike a
     * bare relative filename (which would resolve differently depending on
     * whichever directory each separate process happens to be launched
     * from) or a fixed machine-wide location (which may need elevated
     * permissions). Swap for a real shared install path once one is
     * decided for actual deployment.
     */
    static std::filesystem::path sentinelPath();

    /**
     * @brief Turn cloud sync on/off. See FeatureConfigManager::setCloudEnabled.
     */
    void setCloudEnabled(bool enabled);

    /**
     * @brief The rest of the cloud settings, bundled into one call since
     * they're always configured together in practice. See
     * FeatureConfigManager::setCloudBaseUrl/setCloudStationName/
     * setCloudHardwareToken/setCloudPollIntervalSec.
     *
     * stationName is an explicit override, not a required value: pass ""
     * to leave the name alone and let it come from hardwareToken instead --
     * FeatureConfigManager::setCloudHardwareToken() seeds a name from the
     * token's own embedded slug the moment a NEW token is set, and
     * CloudClient corrects that to the cloud's exact name as soon as the
     * first control-state poll succeeds (see setCloudHardwareToken()'s doc
     * comment in config.hpp). Whoever applies this queued command must call
     * setCloudHardwareToken() BEFORE conditionally calling
     * setCloudStationName() with a non-empty stationName here, so an
     * explicit name always wins over the token-derived guess rather than
     * being silently overwritten by it.
     */
    void setCloudSettings(const std::string& baseUrl,
                          const std::string& stationName,
                          const std::string& hardwareToken,
                          int pollIntervalSec);

    /**
     * @brief Define a new camera. See FeatureConfigManager::addCamera and
     * CameraConfig's own fields in config.hpp -- only the identity/
     * connection fields are exposed here; per-feature toggles go through
     * setCameraFeature() below, matching addCamera()/setCameraFeature()'s
     * own real split.
     *
     * No width/height here -- that was only ever for sizing the legacy
     * cv::imshow debug window (surveillance_controller.cpp), which the
     * SDL2/OpenGL UI replaces; a rendered frame sizes itself from the
     * actual pixels it receives, same as the dashboard's own player (see
     * streamCanvasPlayer.tsx, which sizes its canvas from each incoming
     * frame's real dimensions, never a pre-declared value). CameraConfig's
     * width/height fields still exist engine-side (config.hpp, defaulted
     * 640/480) purely because the local video recorder's initialize() call
     * needs SOME target size up front -- see that field's doc comment in
     * config.hpp for the open question of whether the engine should instead
     * derive it from the real opened capture stream.
     */
    void addCamera(const std::string& name,
                   const std::string& id,
                   const std::string& source,
                   bool enableFaceRecognition);

    /**
     * @brief Toggle one feature on one camera. See
     * FeatureConfigManager::setCameraFeature -- featureKey matches that
     * method's own key strings (e.g. "faceRecognition", "motionDetection",
     * "nightVision", ...).
     */
    void setCameraFeature(const std::string& cameraId, const std::string& featureKey, bool value);

    /**
     * @brief Edit an existing camera's identity/connection fields. See
     * FeatureConfigManager::updateCamera -- id is immutable, toggles go
     * through setCameraFeature(), so only name/source here.
     *
     * No width/height here, same reasoning as addCamera() above. Whoever
     * applies this queued command fetches the camera's existing
     * CameraConfig and carries its current width/height through unchanged
     * into the updated one passed to FeatureConfigManager::updateCamera
     * (that engine method always takes a complete CameraConfig -- there's
     * no partial-update entry point below it) -- an edit through this path
     * never touches those fields at all now, not even to reset them.
     */
    void updateCamera(const std::string& camId,
                      const std::string& name,
                      const std::string& source);

    /**
     * @brief Permanently remove a camera. See FeatureConfigManager::removeCamera.
     */
    void removeCamera(const std::string& camId);

    /**
     * @brief Move a pending camera into the active set. See
     * FeatureConfigManager::plugInPendingCamera.
     */
    void plugInPendingCamera(const std::string& camName, const std::string& camId);

    /**
     * @brief Move an active camera to pending, or delete it outright. See
     * FeatureConfigManager::plugOutPendingCamera.
     */
    void plugOutPendingCamera(bool isDelete, const std::string& camName, const std::string& camId);

    /**
     * @brief Apply one feature toggle across every currently configured
     * camera. See FeatureConfigManager::setFeatureForAllCameras.
     */
    void setFeatureForAllCameras(const std::string& featureKey, bool value);

    /**
     * @brief Upload/replace a detection model file. See
     * FeatureConfigManager::upLoadModel.
     */
    void upLoadModel(const std::string& modelType, const std::string& modelPath);

    /**
     * @brief Reset model settings to defaults. See
     * FeatureConfigManager::resetModelSettings.
     */
    void resetModelSettings();

    /**
     * @brief Enable/disable the routine-alert beep channel. See
     * FeatureConfigManager::setBeepEnabled.
     */
    void setBeepEnabled(bool enabled);

    /**
     * @brief Enable/disable the High/Critical-alert voice channel. See
     * FeatureConfigManager::setVoiceEnabled.
     */
    void setVoiceEnabled(bool enabled);

    /**
     * @brief Select which installed voice the voice channel speaks with.
     * See FeatureConfigManager::setVoiceGender.
     */
    void setVoiceGender(const std::string& gender);

    /**
     * @brief Set this station's fixed physical coordinates. See
     * FeatureConfigManager::setStationLocation.
     */
    void setStationLocation(double latitude, double longitude);

    /**
     * @brief Reset one camera's face-detection settings to defaults. See
     * FeatureConfigManager::resetFaceSettings.
     */
    void resetFaceSettings(const std::string& camId);

    /**
     * @brief Reset one camera's motion-detection settings to defaults. See
     * FeatureConfigManager::resetMotionSettings.
     */
    void resetMotionSettings(const std::string& camId);

    /**
     * @brief Add a motion zone to one camera. See
     * FeatureConfigManager::addMotionZone and ZoneSettings' own fields
     * (regionName, roi as x/y/width/height, restricted) in settings.hpp.
     */
    void addMotionZone(const std::string& camId,
                       const std::string& regionName,
                       int x, int y, int width, int height,
                       bool restricted);

    /**
     * @brief Clear every motion zone on one camera. See
     * FeatureConfigManager::clearMotionZones.
     */
    void clearMotionZones(const std::string& camId);

    /**
     * @brief Add a plate number to the watchlist. See
     * FeatureConfigManager::addPlateNumberToWatchlist.
     */
    void addPlateNumberToWatchlist(const std::string& plate);

    /**
     * @brief Remove a plate number from the watchlist. See
     * FeatureConfigManager::removePlateNumberFromWatchlist.
     */
    void removePlateNumberFromWatchlist(const std::string& plate);

    /**
     * @brief Clear the entire plate watchlist. See
     * FeatureConfigManager::clearPlateNumberFromWatchList.
     */
    void clearPlateNumberFromWatchList();

    /**
     * @brief Set the account id used to scope cloud-side data to this
     * account. See FeatureConfigManager::setAccountId.
     */
    void setAccountId(const std::string& accountId);

    /**
     * @brief Set the Kafka broker address used for cross-station
     * clustering. See FeatureConfigManager::setKafkaBrokerAddress.
     */
    void setKafkaBrokerAddress(const std::string& brokerAddress);

    /**
     * @brief Set one camera's face-detection settings. See
     * FeatureConfigManager::setFaceSettings and FaceSettings' own fields
     * in settings.hpp -- only resetFaceSettings() (reset-to-default) was
     * exposed before; this is the actual "set these specific values" half.
     */
    void setFaceSettings(const std::string& camId,
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
     * @brief Set one camera's motion-detection settings. See
     * FeatureConfigManager::setMotionSettings and MotionSettings' own
     * fields in settings.hpp -- same reasoning as setFaceSettings() above.
     */
    void setMotionSettings(const std::string& camId,
                           double diffThreshold,
                           int minArea,
                           std::size_t crowdThreshold,
                           int loiterSeconds,
                           int leftBehindSeconds,
                           bool enableTracking,
                           bool debugLogging);

    /**
     * @brief Set one camera's night-vision settings. See
     * FeatureConfigManager::setNightVisionSettings and
     * NightVisionSettings' own fields in settings.hpp. No prior presence
     * at all here (not even a reset) before this.
     */
    void setNightVisionSettings(const std::string& camId,
                                double gamma,
                                bool adaptiveMode,
                                int contrastMode,
                                double clipLimit,
                                int tileSize,
                                int denoisingStrength);

    /**
     * @brief Set one camera's object-detection settings. See
     * FeatureConfigManager::setObjectDetectionSettings and
     * ObjectDetectionSettings' own fields in settings.hpp. No prior
     * presence at all here before this.
     */
    void setObjectDetectionSettings(const std::string& camId,
                                    int inputSize,
                                    int backend,
                                    int target,
                                    bool trackingEnabled,
                                    float minConfForDraw);

    /**
     * @brief Set one camera's vehicle/plate-detection settings. See
     * FeatureConfigManager::setVehicleSettings and VehicleSettings' own
     * fields in settings.hpp -- distinct from the plate watchlist above,
     * this is per-camera OCR/alerting behavior. No prior presence at all
     * here before this.
     */
    void setVehicleSettings(const std::string& camId,
                            const std::string& lang,
                            int minPlateConfidence,
                            bool enableAlerts,
                            bool saveImages);

    /**
     * @brief Set one camera's recording settings. See
     * FeatureConfigManager::setRecorderSettings and RecorderSettings' own
     * fields in settings.hpp. codec is RecorderSettings::toJson()'s own
     * wire representation (static_cast<int>(EventRecorder::Codec)), kept
     * as a plain int here so this header doesn't need to pull in
     * EventRecorder's definition just to name the enum -- this class never
     * interprets a command's contents anyway. No prior presence at all
     * here before this.
     */
    void setRecorderSettings(const std::string& camId,
                             int codec,
                             int bitrate,
                             int maxDuration,
                             uint64_t maxFileSize,
                             const std::string& eventType);

    // ===================================================================
    // DataBaseManager passthroughs (features().getDBManager()) -- same
    // fire-and-forget queue as everything above. For the read/query methods
    // (getAllTrackedFaces, getEventById, ...) there is deliberately no
    // return value: this queue has no response channel yet, so these only
    // ever queue "please run this query" -- nothing comes back across the
    // process boundary today. That's a separate, larger piece of work
    // (a symmetric response file) than reusing this existing queue for the
    // write side. Tests verify the right command/params got queued, the
    // same way every other method here is tested.
    // ===================================================================

    // --- Tracked Faces ---
    // Read/query methods return a requestId (see drainResponses() below) --
    // everything else here stays void/fire-and-forget, unchanged.
    std::string getAllTrackedFaces();
    std::string getUnknownTrackedFaces();
    std::string getAuthorizedTrackedFaces();
    std::string getWatchlistTrackedFaces();
    std::string getTrackedFaceById(const std::string& faceId);
    void deleteTrackedFace(const std::string& id);
    void deleteAllTrackedFaces();
    void deleteAllAuthorizedFaces();
    void deleteAllWatchlistFaces();
    void registerFaceFromImage(const std::string& imagePath,
                               const std::string& name,
                               const std::string& status,
                               const std::string& description,
                               const std::string& externalId = "",
                               bool broadcastToCloud = false);
    std::string getFaceRegionHistory(const std::string& faceId);

    // --- Tracked Plates ---
    void logTrackedPlate(const std::string& plateNumber, const std::string& status, const std::string& description);
    std::string getAllTrackedPlates();
    std::string getUnknownTrackedPlates();
    std::string getAuthorizedTrackedPlates();
    std::string getWatchlistTrackedPlates();
    std::string getTrackedPlateById(const std::string& plateId);
    void deleteTrackedPlate(const std::string& id);
    void deleteAllTrackedPlates();
    void deleteAllAuthorizedPlates();
    void deleteAllWatchlistPlates();
    void registerPlateFromImage(const std::string& imagePath,
                                const std::string& plateNumber,
                                const std::string& status,
                                const std::string& externalId = "");
    std::string getPlateRegionHistory(const std::string& plateId);

    // --- Tracked Objects ---
    std::string getObjectRegionHistory(const std::string& objectId);

    // --- Tracked Weapons ---
    std::string getAllTrackedWeapons();
    std::string getUnknownTrackedWeapons();
    std::string getAuthorizedTrackedWeapons();
    std::string getWatchlistTrackedWeapons();
    std::string getTrackedWeaponById(const std::string& weaponId);
    void deleteTrackedWeapon(const std::string& id);
    void deleteAllTrackedWeapons();
    void deleteAllAuthorizedWeapons();
    void deleteAllWatchlistWeapons();
    void registerWeaponFromImage(const std::string& imagePath,
                                 const std::string& name,
                                 const std::string& status,
                                 const std::string& description,
                                 const std::string& externalId = "");
    std::string getWeaponRegionHistory(const std::string& weaponId);

    // --- User Management ---
    // NOTE: passwordHash/newPasswordHash cross into a plaintext local JSON
    // file (same OS-temp-dir queue as everything else) -- not transmitted
    // over any network, but not encrypted at rest either. Fine for local
    // single-machine testing; revisit before this queue is ever used in a
    // real multi-user deployment.
    void registerUser(const std::string& username,
                      const std::string& passwordHash,
                      const std::string& role,
                      const std::string& name,
                      const std::string& imagePath,
                      const std::string& phoneNumber,
                      const std::string& email,
                      int isActive);
    std::string validateUserPassword(const std::string& username, const std::string& inputPlaintextPassword);
    void updateUserProfile(const std::string& username,
                           const std::string& passwordHash,
                           const std::string& role,
                           const std::string& name,
                           const std::string& imagePath,
                           const std::string& phoneNumber,
                           const std::string& email,
                           int isActive);
    void updateUserStatus(const std::string& userId, int activeState);
    void deleteUserById(const std::string& userId);
    void updateLastLogin(const std::string& userId, const std::string& timestamp);
    void changeUserPassword(const std::string& userId, const std::string& newPasswordHash);

    // --- Events ---
    std::string getAllEvents();
    std::string getEventById(const std::string& eventId);
    void deleteAllEvents();
    void deleteEventById(const std::string& eventId);

    // --- Recordings ---
    std::string getAllRecordings();
    std::string getRecordingsByCameraId(const std::string& cameraId);
    std::string getRecordingById(const std::string& id);
    void deleteAllRecordings();
    void deleteRecordingById(const std::string& id);

    // --- Telemetry ---
    void logTelemetry(const std::string& metricType,
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
    void pruneTelemetryBefore(long long beforeTimestamp);

    // --- Daily Face Metrics ---
    void insertDailyFaceMetrics(const std::string& detectionDate, int totalDetections, const std::string& timestamp);
    std::string getAllDailyFaceMetrics();
    std::string getDailyFaceMetricsByDate(const std::string& detectionDate);
    void deleteAllDailyFaceMetrics();
    void deleteDailyFaceMetricsByDate(const std::string& detectionDate);

    // --- Chat ---
    void insertChatMessage(const std::string& id, const std::string& content, const std::string& senderName, const std::string& createdAt);
    std::string getRecentChatMessages(int limit = 100);

    /**
     * @brief The fixed, absolute path both this process and the
     * surveillance frontend must agree on for the command queue -- same
     * reasoning as sentinelPath(), sitting in the same directory.
     */
    static std::filesystem::path commandQueuePath();

    /**
     * @brief The fixed, absolute path CommandQueueConsumer writes query
     * results to and this class reads them back from -- same directory and
     * reasoning as commandQueuePath(), just the reverse direction.
     */
    static std::filesystem::path responsesPath();

    /**
     * @brief Read + truncate every currently-queued response line, parsed
     * as JSON -- one entry per completed query-style command since the last
     * call, each shaped like {"requestId", "command", "result", "timestamp"}
     * (see CommandQueueConsumer::writeResponse()). Matches
     * CommandQueueConsumer::drainQueue()'s own read-all-then-truncate
     * pattern, just reversed direction (the engine writes, this reads).
     *
     * Every read/query method above (getAllTrackedFaces, getEventById, ...)
     * returns a requestId when it's called; call this later (a poll, not a
     * blocking wait -- there is no synchronous round trip across the process
     * boundary) to retrieve whichever results have arrived since, and match
     * them back up by requestId.
     */
    std::vector<nlohmann::json> drainResponses();

private:
    /**
     * @brief Append one
     * {"requestId": ..., "command": name, "params": params, "timestamp": ...}
     * JSON line to commandQueuePath() and return the generated requestId.
     * Shared by every method above so there's exactly one place that knows
     * the on-disk command format. Every command gets a requestId (even the
     * fire-and-forget ones, which simply discard it) so this stays the one
     * place that assigns them.
     */
    std::string writeCommand(const std::string& command, const nlohmann::json& params);

    std::atomic<bool> running_{false};
    std::atomic<uint64_t> requestCounter_{0};
};

#endif // SURVEILLANCE_INTERFACE_HPP
