

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <opencv2/highgui.hpp> 
#include "surveillance_controller.hpp"
#include "alarm/dispatch/AlarmDispatcher.hpp"
#include "cloud/cloud_client.hpp"
#include <nlohmann/json.hpp>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <sstream>
#include <thread>
#include <future>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unordered_set>
#include "util/helperFunctions.hpp"

#ifdef _WIN32
#include <windows.h>
#endif


namespace {
    // How many consecutive failed cap.read() calls processCamera() tolerates
    // before deciding a camera has genuinely disconnected mid-session, not
    // just dropped one frame. Paired with a small backoff sleep on each
    // failure (see processCamera()) - without that backoff, a backend that
    // returns failure immediately (rather than blocking) turns this into a
    // tight busy-spin pegging a CPU core, which is almost certainly what
    // caused the reported "UI pause" when a camera was unplugged live.
    constexpr int kMaxConsecutiveReadFailures = 30;
    constexpr auto kReadFailureBackoff = std::chrono::milliseconds(100);

    // Detects a camera that keeps returning cap.read()==true with a
    // genuine, non-empty frame even after being physically unplugged - a
    // real, documented quirk of some Windows camera backends/drivers
    // (DSHOW included, now the primary backend here - see
    // verifyCameraSource()'s doc comment in config.cpp on the same quirk
    // at open time) where the last successfully decoded frame just keeps
    // getting handed back instead of read() ever failing. Without this,
    // kMaxConsecutiveReadFailures above never trips - read() keeps
    // "succeeding" - so a camera unplugged this way never gets flagged
    // disconnected, never enters probation, and nobody is ever notified.
    // Sampled periodically (not every frame) since the diff itself
    // (cv::absdiff + cv::mean) is real per-call cost, and this only needs
    // to catch a feed stuck for multiple seconds, not react to it
    // instantly. kMinLivenessDiff matches verifyCameraSource()'s own
    // threshold exactly - same measured real-camera-sensor-noise-vs-
    // frozen-frame gap, same reasoning: any genuine sensor, even pointed
    // at a static scene, never returns two byte-identical frames.
    constexpr auto kFreezeCheckInterval = std::chrono::seconds(3);
    constexpr double kMinLivenessDiff = 0.5;

    // Probation: a mid-session-disconnected camera gets re-verified
    // frequently (kProbationRecheckInterval) so reconnecting it is noticed
    // and restored close to immediately, rather than waiting on a long,
    // widely-spaced check cadence. It only gives up - moving the camera to
    // pendingCameras - once it's been trying continuously for
    // kProbationMaxDuration with no success (elapsed wall-clock time, not a
    // discrete check count).
    constexpr auto kProbationRecheckInterval = std::chrono::seconds(5);
    constexpr auto kProbationMaxDuration = std::chrono::minutes(75);
    // How often probationLoop() wakes to see if anything is due - just a
    // polling granularity, not the actual per-camera recheck cadence above.
    // Kept well under kProbationRecheckInterval so a due check fires
    // promptly rather than sitting for a whole poll cycle after becoming due.
    constexpr auto kProbationPollInterval = std::chrono::seconds(2);

    std::filesystem::path getDefaultRecordingsDirectory() {
        const char* homeDir = nullptr;
    #if defined(_WIN32)
        homeDir = std::getenv("USERPROFILE");
    #else
        homeDir = std::getenv("HOME");
    #endif
        std::filesystem::path basePath;
        if (homeDir && *homeDir) {
            basePath = std::filesystem::path(homeDir);
        } else {
            basePath = std::filesystem::current_path();
        }

    #if defined(_WIN32)
        return basePath / "Documents" / "Surveillance Recordings";
    #elif defined(__APPLE__)
        return basePath / "Library" / "Application Support" / "Surveillance Recordings";
    #else
        return basePath / ".local" / "share" / "Surveillance Recordings";
    #endif
    }
}

SurveillanceController::SurveillanceController(FeatureConfigManager& features,
                                                 GlobalDetectionProcessor<FaceEntry, DBTrackedFace>& identityMgr,
                                                 GlobalDetectionProcessor<VehicleEntry, DBTrackedPlate>& vehicleIdentityMgr,
                                                 GlobalDetectionProcessor<WeaponEntry, DBTrackedWeapon>& weaponIdentityMgr,
                                                 Database& db,
                                                 CloudClient* cloudClient)
 : features_(features),
   identityMgr_(identityMgr),
   vehicleIdentityMgr_(vehicleIdentityMgr),
   weaponIdentityMgr_(weaponIdentityMgr),
   cloudClient_(cloudClient),
   actionEventProcessor_(db),
   recordingEventProcessor_(db)
{
    // Same reasoning as main.cpp's cameraNameResolver for the identity
    // processors - lets logged action events show the camera's actual
    // display name instead of a fixed placeholder.
    actionEventProcessor_.setCameraNameResolver([this](const std::string& camId) -> std::string {
        CameraConfig cam;
        return features_.getCameraConfig(camId, cam) ? cam.name : "";
    });
    recordingEventProcessor_.setCameraNameResolver([this](const std::string& camId) -> std::string {
        CameraConfig cam;
        return features_.getCameraConfig(camId, cam) ? cam.name : "";
    });

    reaperThread_ = std::thread(&SurveillanceController::reaperLoop, this);
    probationThread_ = std::thread(&SurveillanceController::probationLoop, this);
}

SurveillanceController::~SurveillanceController() {
    stop();
}

// Get user-accessible recordings directory path
std::string SurveillanceController::getRecordingsPath() const {
    auto recordingsPath = getDefaultRecordingsDirectory();
    std::filesystem::create_directories(recordingsPath);
    return recordingsPath.string();
}

/*
    ==================================================================================
    NOTE: INITIALIZATION & MODEL LOADING
    This section performs comprehensive checks to ensure all cameras are accessible and 
    all required models are available before starting the main processing loop.
    By validating resources upfront, we can provide clear feedback to the user 
    about any configuration issues and avoid runtime errors during processing.
*/
bool SurveillanceController::init() {
    LOG_CONTEXT(LogLevel::INFO, "Initializing surveillance system...", {});

    // features_ is the single source of truth for cameras + settings.
    SystemConfig liveCfg = features_.get();

    if (!liveCfg.modelSettings.validateAllModels()) {
        LOG_CONTEXT(LogLevel::ERR, "System has invalid model configuration. Skipping initialization.", {});
        return false;
    }

    bool allCamerasReady = true;
    size_t originalCount = liveCfg.cameras.size();
    size_t activeCount   = 0;

    // Validate models and camera readiness per camera -- verified
    // CONCURRENTLY, not one at a time. verifyCameraSource() retries up to
    // 3x with a 500ms backoff and, for a numeric source that doesn't
    // correspond to a real device, tries BOTH the MSMF and DirectShow
    // backends before giving up on each attempt - a single nonexistent
    // camera can easily cost several seconds on its own (this is exactly
    // what main.cpp's four hardcoded default cameras hit on a machine that
    // doesn't actually have 4 webcams). The old sequential for-loop meant
    // one bad camera source delayed every OTHER camera's window from
    // opening at all, even a perfectly healthy one verified in
    // milliseconds - total startup time was the SUM of every camera's
    // verification cost. Running them concurrently via std::async bounds
    // it to the SLOWEST single camera instead. Safe to parallelize: each
    // call opens its own local cv::VideoCapture with no state shared
    // between cameras, and FeatureConfigManager's methods below are
    // already mutex-guarded internally (see its own class doc comment) so
    // calling them from multiple threads at once is fine.
    LOG_CONTEXT(LogLevel::INFO, "Verifying camera readiness...", {});

    struct CameraVerifyResult {
        std::string camId;
        std::string camName;
        bool ready;
    };

    std::vector<std::future<CameraVerifyResult>> verifyFutures;
    verifyFutures.reserve(liveCfg.cameras.size());
    for (const auto& [camId, cam] : liveCfg.cameras) {
        // Captured by value, not reference - this lambda may run on
        // another thread well after this loop iteration's cam/camId go
        // out of scope.
        verifyFutures.push_back(std::async(std::launch::async, [camId, cam]() -> CameraVerifyResult {
            LOG_CONTEXT(LogLevel::INFO, "Checking camera: " + cam.name, {});
            bool ready = verifyCameraSource(cam.source, cam.name, 3, 500);
            return CameraVerifyResult{camId, cam.name, ready};
        }));
    }

    for (auto& fut : verifyFutures) {
        CameraVerifyResult result = fut.get();

        // Tracks consecutive failed startups per camera - after enough of
        // them in a row, features_ moves this camera into pending cameras
        // on our behalf so it stops being retried every restart. See
        // FeatureConfigManager::recordCameraOpenResult()'s doc comment.
        features_.recordCameraOpenResult(result.camId, result.ready);

        // Capability (re)detection - independent of, and never gated by,
        // `ready` above (see FeatureConfigManager::
        // detectAndUpdateCameraCapabilities()'s doc comment). Runs on every
        // restart for every configured camera, so e.g. a config hand-edited
        // to change `source` from a local index to a network URL gets its
        // type/capabilities kept in sync without any manual trigger.
        features_.detectAndUpdateCameraCapabilities(result.camId);

        if (!result.ready) {
            LOG_CONTEXT(LogLevel::ERR, "Camera " + result.camName + " failed verification and will be skipped.", {});
            allCamerasReady = false;
            continue;
        }

        ++activeCount;
        LOG_CONTEXT(LogLevel::INFO, "Camera " + result.camName + " passed verification checks.", {});
    }

    if (activeCount == 0 && originalCount > 0) {
        LOG_CONTEXT(LogLevel::ERR, "No cameras passed verification! System cannot start.", {});
        return false;
    }
    LOG_CONTEXT(LogLevel::INFO,
    "Active cameras: " + std::to_string(activeCount) + "/" + std::to_string(originalCount),
    {});

    // Deliberately NOT re-checking or auto-restoring cameras sitting in
    // pendingCameras here. A camera only leaves pendingCameras through a
    // deliberate, user-initiated plugInPendingCamera() call (a "toggle back"
    // action) - never automatically, whether that's a background sweep
    // (already rejected - see probationLoop()'s doc comment) or, as here,
    // just because the process happened to restart and the camera happened
    // to now verify. The user may be deliberately keeping a camera pending
    // until they need it again, and a restart silently undoing that would
    // take the decision away from them.
    if (!allCamerasReady) {
        LOG_CONTEXT(LogLevel::WARNING, "Warning: Some cameras are not ready. Check your camera connections and sources.", {});
    }

    if (liveCfg.cameras.empty() && originalCount > 0) {
        LOG_CONTEXT(LogLevel::ERR, "No cameras passed verification! System cannot start.", {});
        return false;
    }
    LOG_CONTEXT(LogLevel::INFO, "Camera verification complete. " + std::to_string(activeCount) + " active camera(s) ready.", {});



    /*
      ====================================================================================
      LOAD MODELS & INITIALIZE BACKENDS
      This section loads all AI models based on the provided configuration and initializes
      the corresponding backends. It also handles fallback scenarios where a model might
      fail to load, ensuring the system can still operate with available features.
    */
    // Initialize face backend per camera if enabled
    if (!liveCfg.modelSettings.dnnModelPath.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "Face recognition enabled. Initializing face detector...", {});

        std::unique_ptr<IFaceDetector> backend = std::make_unique<YuNetFaceDetector>();

        auto faceErr = backend->init(liveCfg.modelSettings.dnnModelPath);
        if (faceErr != FaceError::None) {
            LOG_CONTEXT(LogLevel::ERR,
                "Failed to initialize face detector with model: " + liveCfg.modelSettings.dnnModelPath, {});
            AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                "Face detector failed to initialize - face recognition is unavailable this session."
            });
            return false;
        }

        // Attach backend to FaceRecognition
        faceRec_.setBackend(std::move(backend));
        LOG_CONTEXT(LogLevel::INFO, "Face detector initialized successfully.", {});

        // --- NEW: Link FaceRecognition to the global processor ---
        faceRec_.setGlobalManager(&identityMgr_);

        // Initialize recognizer weights
        if (!liveCfg.modelSettings.faceRecModelPath.empty()) {
            if (!faceRec_.initRecognizer(liveCfg.modelSettings.faceRecModelPath)) {
                LOG_CONTEXT(LogLevel::ERR,
                    "Failed to initialize FaceRecognizerSF with model: " + liveCfg.modelSettings.faceRecModelPath, {});
                AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                    "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                    "Face recognizer failed to initialize - face embedding/identity matching is unavailable this session."
                });
                return false;
            }
            LOG_CONTEXT(LogLevel::INFO, "FaceRecognizerSF initialized successfully.", {});
        } else {
            LOG_CONTEXT(LogLevel::WARNING, "No face recognition weights provided. Embedding extraction disabled.", {});
        }
    } else {
        LOG_CONTEXT(LogLevel::INFO, "Face recognition disabled or no detection model path provided.", {});
    }

   // Object detection model (optional) - load per-camera YOLO models
   if (!liveCfg.modelSettings.yoloCfgPath.empty() && !liveCfg.modelSettings.yoloWeightsPath.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "Initializing YOLO model...", {});

        try {
            cv::dnn::Net yoloNet = cv::dnn::readNetFromDarknet(
                liveCfg.modelSettings.yoloCfgPath,
                liveCfg.modelSettings.yoloWeightsPath
            );

            if (!yoloNet.empty()) {
                std::ifstream ifs(liveCfg.modelSettings.yoloClassesPath.c_str());
                std::vector<std::string> classNames;
                std::string line;
                while (std::getline(ifs, line)) {
                    if (!line.empty()) classNames.push_back(line);
                }

                // Register the model globally
                masterObjectDetector_.setModel(yoloNet, classNames, "yolo_global");

                // --- FIXED: Create per-camera detectors without processor hook ---
                for (const auto& [camId, cam] : liveCfg.cameras) {
                    objectDetectors_[cam.id] = std::make_unique<ObjectDetection>(416);

                    // Attach global YOLO backend
                    objectDetectors_[cam.id]->setModel(yoloNet, classNames, "yolo_global");
                }

                LOG_CONTEXT(LogLevel::INFO,
                    "YOLO model initialized and detectors created with global backend.", {});
            } else {
                LOG_CONTEXT(LogLevel::ERR, "Failed to load YOLO network.", {});
                AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                    "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                    "YOLO object detection model failed to load - object detection is unavailable this session."
                });
            }
        } catch (const cv::Exception& e) {
            LOG_CONTEXT(LogLevel::ERR,
                "OpenCV exception while loading YOLO: " + std::string(e.what()), {});
            AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                "YOLO object detection model failed to load - object detection is unavailable this session."
            });
        }
    }

    // Weapon detection model (optional) - separate model from the general
    // YOLO one above, not an additional class on it (coco.names has no
    // weapon/gun class). Same Darknet format and loading path, just a
    // second model + a second set of per-camera detector instances, mirrored
    // from the block above. Accuracy is unverified - see
    // ai_models/weapon/SOURCE_AND_LICENSE.txt - so this only ever runs for a
    // camera with enableWeaponDetection explicitly set (off by default).
    if (!liveCfg.modelSettings.weaponCfgPath.empty() && !liveCfg.modelSettings.weaponWeightsPath.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "Initializing weapon detection model (unverified accuracy - see ai_models/weapon/SOURCE_AND_LICENSE.txt)...", {});

        try {
            cv::dnn::Net weaponNet = cv::dnn::readNetFromDarknet(
                liveCfg.modelSettings.weaponCfgPath,
                liveCfg.modelSettings.weaponWeightsPath
            );

            if (!weaponNet.empty()) {
                std::ifstream ifs(liveCfg.modelSettings.weaponClassesPath.c_str());
                std::vector<std::string> classNames;
                std::string line;
                while (std::getline(ifs, line)) {
                    if (!line.empty()) classNames.push_back(line);
                }

                masterWeaponDetector_.setModel(weaponNet, classNames, "weapon_global");
                masterWeaponDetector_.setGlobalManager(&weaponIdentityMgr_);

                for (const auto& [camId, cam] : liveCfg.cameras) {
                    weaponDetectors_[cam.id] = std::make_unique<WeaponDetection>();
                    weaponDetectors_[cam.id]->setModel(weaponNet, classNames, "weapon_global");
                    weaponDetectors_[cam.id]->setGlobalManager(&weaponIdentityMgr_);
                }

                LOG_CONTEXT(LogLevel::INFO,
                    "Weapon detection model initialized and detectors created with global backend.", {});
            } else {
                LOG_CONTEXT(LogLevel::ERR, "Failed to load weapon detection network.", {});
                AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                    "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                    "Weapon detection model failed to load - weapon detection is unavailable this session."
                });
            }
        } catch (const cv::Exception& e) {
            LOG_CONTEXT(LogLevel::ERR,
                "OpenCV exception while loading weapon detection model: " + std::string(e.what()), {});
            AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                "system", "", "ModelLoadFailed", helperFunctions::getCurrentTimestamp(), "High",
                "Weapon detection model failed to load - weapon detection is unavailable this session."
            });
        }
    }

    // Action detection model (optional) - load action models if paths provided
   if (!liveCfg.modelSettings.actionModelPaths.empty()) {
        LOG_CONTEXT(LogLevel::INFO, "Initializing action detection model(s)...", {});
        LOG_CONTEXT(LogLevel::DISPLAY, "Loading " +
            std::to_string(liveCfg.modelSettings.actionModelPaths.size()) +
            " action model(s)...", {});

        for (size_t i = 0; i < liveCfg.modelSettings.actionModelPaths.size(); ++i) {
            const auto& modelPath = liveCfg.modelSettings.actionModelPaths[i];

            // Skip missing files to avoid OpenCV throwing on readNetFromONNX
            try {
                std::filesystem::path fsPath(modelPath);
                if (!std::filesystem::exists(fsPath)) {
                    LOG_CONTEXT(LogLevel::WARNING, "Optional model not found: " + modelPath, {});
                    continue;
                }

                cv::dnn::Net net = cv::dnn::readNetFromONNX(fsPath.string());
                if (net.empty()) {
                    LOG_CONTEXT(LogLevel::WARNING, "Failed to load action model from: " + fsPath.string(), {});
                    continue;
                }

                // The behavior this specific file specializes in - used
                // directly as the label any detection from this model gets,
                // and what a future detectX() would substring-match
                // against. Generalizes to any future single-frame
                // per-person-crop classifier model file with no code
                // changes needed here (fighting/falling are handled via
                // their own dedicated whole-frame paths instead - see
                // setFightModel()/setFallModel() - since neither fits this
                // per-crop-classifier shape).
                std::string modelName = fsPath.stem().string();

                masterActionDetector_.addModel(net, modelName, 0.5f);

                LOG_CONTEXT(LogLevel::INFO, "Action model loaded: " + modelName + " (" + fsPath.string() + ")", {});
            } catch (const std::exception& e) {
                LOG_CONTEXT(LogLevel::WARNING, "Error loading action model '" + modelPath + "': " + e.what(), {});
            }
        }
    } else {
        LOG_CONTEXT(LogLevel::INFO, "No action models configured - action detection disabled", {});
    }

    // Whole-frame fight/violence detector (YOLOv8-nano ONNX) - separate load
    // path from the actionModelPaths loop above, since it's a genuine
    // multi-box detector (own NMS/decode) rather than a single-frame
    // classifier - see ActionDetection::setFightModel()'s doc comment and
    // ai_models/action/fight_SOURCE_AND_LICENSE.txt.
    if (!liveCfg.modelSettings.fightModelPath.empty()) {
        try {
            std::filesystem::path fightPath(liveCfg.modelSettings.fightModelPath);
            if (!std::filesystem::exists(fightPath)) {
                LOG_CONTEXT(LogLevel::WARNING, "Optional model not found: " + liveCfg.modelSettings.fightModelPath, {});
            } else {
                cv::dnn::Net fightNet = cv::dnn::readNetFromONNX(fightPath.string());
                if (fightNet.empty()) {
                    LOG_CONTEXT(LogLevel::WARNING, "Failed to load fight detection model from: " + fightPath.string(), {});
                } else {
                    masterActionDetector_.setFightModel(fightNet);
                    LOG_CONTEXT(LogLevel::INFO, "Fight detection model loaded: " + fightPath.string(), {});
                }
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::WARNING, "Error loading fight detection model: " + std::string(e.what()), {});
        }
    }

    // Whole-frame fall detector (YOLOv11-nano ONNX) - same load pattern as
    // the fight detector above. See ActionDetection::setFallModel()'s doc
    // comment and ai_models/action/fall_SOURCE_AND_LICENSE.txt.
    if (!liveCfg.modelSettings.fallModelPath.empty()) {
        try {
            std::filesystem::path fallPath(liveCfg.modelSettings.fallModelPath);
            if (!std::filesystem::exists(fallPath)) {
                LOG_CONTEXT(LogLevel::WARNING, "Optional model not found: " + liveCfg.modelSettings.fallModelPath, {});
            } else {
                cv::dnn::Net fallNet = cv::dnn::readNetFromONNX(fallPath.string());
                if (fallNet.empty()) {
                    LOG_CONTEXT(LogLevel::WARNING, "Failed to load fall detection model from: " + fallPath.string(), {});
                } else {
                    masterActionDetector_.setFallModel(fallNet);
                    LOG_CONTEXT(LogLevel::INFO, "Fall detection model loaded: " + fallPath.string(), {});
                }
            }
        } catch (const std::exception& e) {
            LOG_CONTEXT(LogLevel::WARNING, "Error loading fall detection model: " + std::string(e.what()), {});
        }
    }

    // Motion detection backend - initialize global motion detector with DNN
    // if a real person-detection model is configured, otherwise use
    // classical (MOG2 + HOG, no external model needed). Deliberately reads
    // modelSettings.motionPersonModelPath here, NOT dnnModelPath (the face
    // detector's model) - see motionPersonModelPath's doc comment in
    // settings.hpp for the bug this fixes: motion's DNN backend was
    // silently loading the face model and producing zero detections every
    // frame, forever, with no error.
    if (!liveCfg.modelSettings.motionPersonModelPath.empty()) {
        auto backend = std::make_shared<DnnMotionBackend>();
        if (backend->init(liveCfg.modelSettings.motionPersonModelPath,
                        liveCfg.modelSettings.actionModelPaths.empty() ? ""
                        : liveCfg.modelSettings.actionModelPaths[0])) {
            masterMotionDetector.setBackend(backend, /*alreadyInitialized=*/true);
            LOG_CONTEXT(LogLevel::INFO, "DNN motion backend initialized successfully.", {});
        } else {
            LOG_CONTEXT(LogLevel::ERR, "Failed to initialize DNN motion backend with model: " + liveCfg.modelSettings.motionPersonModelPath, {});
            masterMotionDetector.setBackend(std::make_shared<ClassicalMotionBackend>(), /*alreadyInitialized=*/true);
        }
    } else {
        LOG_CONTEXT(LogLevel::INFO, "No DNN motion person-detection model configured - using classical motion detection.", {});
    }

    // Global Vehicle Detector
    if (!liveCfg.modelSettings.vehicleCascadePath.empty() &&
        !liveCfg.modelSettings.plateCascadePath.empty() &&
        !liveCfg.modelSettings.tessdataPath.empty()) {

        LOG_CONTEXT(LogLevel::INFO, "Vehicle detection enabled. Initializing vehicle detector...", {});

        if (!masterVehicleDetector_.setModel(
                liveCfg.modelSettings.vehicleCascadePath,
                liveCfg.modelSettings.plateCascadePath,
                liveCfg.modelSettings.tessdataPath,   // tessdata path for OCR
                "eng")) {                          // language can be configurable

            LOG_CONTEXT(LogLevel::ERR, "Failed to initialize global vehicle detector with cascades and tessdata.", {});
            return false;
        }

        LOG_CONTEXT(LogLevel::INFO, "Global vehicle detector initialized successfully.", {});
    } else {
        LOG_CONTEXT(LogLevel::INFO, "No vehicle cascades or tessdata configured - vehicle detection disabled.", {});
    }

    std::filesystem::create_directories("faces");
    std::filesystem::create_directories("recordings");

    return true;
}

void SurveillanceController::run() {
    running_ = true;

    // Populate initial worker threads/windows from features_ (the source of
    // truth for cameras). Runs on the main thread, same as the periodic
    // reconciliation calls below.
    reconcileCameraWorkers();

    // Run the main display loop on the main thread
    mainDisplayLoop();

    // mainDisplayLoop() returns as soon as ESC is pressed but never itself
    // signals stopFlags_ - without routing through stop() here, every
    // worker's `while (running_ && !stopFlag->load())` (processCamera())
    // would keep running_ true and stopFlag_ unset forever, so the join
    // loop below would block indefinitely (confirmed live: nothing short
    // of a hard process kill ended a session). stop() is also what
    // finalizes any still-active EventRecorder - closing over an unbounded
    // manual/auto-triggered recording so its file is actually playable
    // instead of corrupted (missing moov atom) the way a hard kill leaves
    // it. stop() is safe to call again from the destructor afterward - the
    // maps it touches are already empty and its threads already joined.
    stop();
}

void SurveillanceController::createCameraWindow(const CameraConfig& cam) {
    cv::namedWindow(cam.name, cv::WINDOW_NORMAL);
    cv::resizeWindow(cam.name, cam.width, cam.height);

    // Screen dimensions
    int screenW = 1920; // fallback width
    int screenH = 1080; // fallback height

    #ifdef _WIN32
        screenW = GetSystemMetrics(SM_CXSCREEN);
        screenH = GetSystemMetrics(SM_CYSCREEN);
    #endif

    // Avoid overlap when multiple cameras are used; tile horizontally.
    // Reuses the lowest tile slot not currently occupied by another live
    // window, rather than an ever-climbing counter - without this, a camera
    // that goes through several disconnect/reconnect (or config-change
    // restart) cycles keeps getting tiled further and further from the
    // original grid, eventually landing off-screen or stacked behind
    // another window: still rendering every frame, just not somewhere
    // anyone would see it. Caller (reconcileCameraWorkers()) already holds
    // cams_mtx_, which also guards windowTileIndex_.
    int margin = 10;
    int maxCols = std::max(1, screenW / (cam.width + margin));
    size_t index = 0;
    {
        std::unordered_set<size_t> used;
        for (const auto& [camId, idx] : windowTileIndex_) used.insert(idx);
        while (used.count(index)) ++index;
    }
    windowTileIndex_[cam.id] = index;
    int col = static_cast<int>(index) % maxCols;
    int row = static_cast<int>(index) / maxCols;

    int targetX = margin + col * (cam.width + margin);
    int targetY = std::max(0, screenH - cam.height - 40 - row * (cam.height + margin));

    if (targetY + cam.height > screenH) {
        targetY = std::max(0, screenH - cam.height - 40);
    }

    cv::moveWindow(cam.name, targetX, targetY);
    windowNames_[cam.id] = cam.name;

    // Registers fresh on every respawn (config change / probation exit /
    // reconnect) since the old window - and its old callback registration -
    // was already destroyed via cv::destroyWindow() before this runs again.
    auto ctx = std::make_unique<ClickContext>(ClickContext{this, cam.id});
    cv::setMouseCallback(cam.name, &SurveillanceController::onMouseTrampoline, ctx.get());
    clickContexts_[cam.id] = std::move(ctx);
}

void SurveillanceController::onMouseTrampoline(int event, int x, int y, int /*flags*/, void* userdata) {
    if (event != cv::EVENT_LBUTTONDOWN) return;
    auto* ctx = static_cast<ClickContext*>(userdata);
    if (ctx && ctx->self) ctx->self->handleCameraClick(ctx->camId, x, y);
}

// Click-for-details: hit-tests the clicked camera's last-drawn face boxes,
// and on a resolved hit, pulls the full record from the DB and logs it.
// Deliberately logs rather than overlaying the description onto the live
// frame - see BoundingBoxAnnotator/FaceRecognition's design notes: burning
// text onto frames from here would need real cross-thread frame-mutation
// handling for UX that's likely getting replaced once a real frontend
// exists to render this same data properly.
void SurveillanceController::handleCameraClick(const std::string& camId, int x, int y) {
    FaceRecognition* faceRecPtr = nullptr;
    {
        std::lock_guard<std::mutex> lock(faceRecMtx_);
        auto it = faceRecognizers_.find(camId);
        if (it != faceRecognizers_.end()) faceRecPtr = it->second.get();
    }
    if (!faceRecPtr) return;

    BoundingBoxAnnotator::Box hit;
    if (!faceRecPtr->findFaceAt(x, y, hit) || hit.entityId.empty()) {
        LOG_CONTEXT(LogLevel::DISPLAY, "[Click] No resolved identity at that point on " + camId, {});
        return;
    }
    DBTrackedFace rec = Database::getInstance().getTrackedFaceById(hit.entityId);
    if (rec.id.empty()) {
        LOG_CONTEXT(LogLevel::DISPLAY, "[Click] No DB record for id " + hit.entityId, {});
        return;
    }
    LOG_CONTEXT(LogLevel::DISPLAY,
        "[Click] " + rec.name + " (" + rec.status + ") - " + rec.description +
        " | last seen " + rec.cameraRegion + " @ " + rec.timestamp, {});
}

void SurveillanceController::reconcileCameraWorkers() {
    // Snapshot outside cams_mtx_ - features_ has its own internal lock and
    // cams_mtx_ only needs to protect workers_/stopFlags_/windowNames_.
    auto liveCameras = features_.get().cameras;

    std::lock_guard<std::mutex> lk(cams_mtx_);

    // Stop + tear down workers for cameras no longer present in features_, or
    // whose connection fields changed via updateCamera(). A running
    // cv::VideoCapture was opened once at thread spawn and can't be
    // redirected to a new source/size in place, so a changed camera is torn
    // down here and picked back up by the spawn pass below, same as a
    // brand-new camera.
    for (auto it = workers_.begin(); it != workers_.end(); ) {
        const std::string camId = it->first;
        auto liveIt = liveCameras.find(camId);
        bool stillExists = liveIt != liveCameras.end();

        bool needsRestart = false;
        if (stillExists) {
            auto cfgIt = workerConfigs_.find(camId);
            if (cfgIt != workerConfigs_.end()) {
                const CameraConfig& spawned = cfgIt->second;
                const CameraConfig& live = liveIt->second;
                needsRestart = spawned.source != live.source ||
                               spawned.width  != live.width  ||
                               spawned.height != live.height ||
                               spawned.name   != live.name;
            }
        }

        // Set by processCamera() itself (not us) right before it returned
        // after sustained mid-session read failure - see disconnectedFlags_'
        // doc comment. Only meaningful checked here, not in the branch
        // above, since a config change or removal already means teardown
        // regardless of whether the camera also happened to be failing.
        bool disconnected = false;
        if (stillExists && !needsRestart) {
            auto discIt = disconnectedFlags_.find(camId);
            if (discIt != disconnectedFlags_.end() && discIt->second->load()) {
                disconnected = true;
            }
        }

        if (stillExists && !needsRestart && !disconnected) {
            ++it;
            continue;
        }

        auto flagIt = stopFlags_.find(camId);
        if (flagIt != stopFlags_.end()) {
            flagIt->second->store(true);
        }
        // Hand the join off to the reaper instead of blocking here on the
        // main thread while holding cams_mtx_ - processCamera()'s cap.read()
        // has no timeout, so a stalled stream could otherwise freeze this
        // whole reconciliation pass (and, via cams_mtx_, every other
        // camera's per-frame processing) until it unblocks.
        retireWorker(std::move(it->second));
        stopFlags_.erase(camId);
        disconnectedFlags_.erase(camId);
        workerConfigs_.erase(camId);

        auto winIt = windowNames_.find(camId);
        if (winIt != windowNames_.end()) {
            cv::destroyWindow(winIt->second);
            windowNames_.erase(winIt);
        }
        windowTileIndex_.erase(camId);
        clickContexts_.erase(camId);

        // Teardown is no longer synchronous with the worker actually
        // stopping, so it may still push one more frame under this camId
        // before it notices stopFlag_. Drop anything already queued for it
        // so a stale frame can't reach cv::imshow() after the window above
        // was just destroyed.
        {
            std::lock_guard<std::mutex> dlock(displayQueueMtx_);
            latestFrames_.erase(camId);
        }

        if (disconnected) {
            // Not "removed" - the camera is still in features_'s live list,
            // just not running a worker right now. probationLoop() owns it
            // from here: periodic re-checks, off the main thread, until it
            // either reconnects or exhausts its budget (see its doc comment).
            std::lock_guard<std::mutex> plock(probationMtx_);
            auto now = std::chrono::steady_clock::now();
            probationCameras_[camId] = ProbationEntry{now, now + kProbationRecheckInterval};
            LOG_CONTEXT(LogLevel::WARNING, "Camera worker stopped (disconnected mid-session), entering probation: " + camId, {});
            // The spoken "entering probation" announcement fires from
            // processCamera() itself, on that camera's own worker thread,
            // right where the disconnect was actually detected - not here.
            // This function runs on the main thread and already holds
            // cams_mtx_ for its whole body; it's also the thread responsible
            // for cv::imshow()/cv::waitKey() for every camera window, so
            // nothing that doesn't strictly need to run here should add to
            // how long that takes.
        } else {
            LOG_CONTEXT(LogLevel::INFO,
                (needsRestart ? "Camera worker restarting (config changed): "
                              : "Camera worker stopped and removed: ") + camId, {});
        }
        it = workers_.erase(it);
    }

    // Spawn workers for cameras present in features_ but not yet running
    // (brand-new cameras, and ones just torn down above for a restart).
    // Cameras currently on probation are deliberately skipped here -
    // probationLoop() owns when they're allowed to try again, not this
    // every-~1s pass, or a probationary camera would just get a fresh
    // worker spawned for it immediately, defeating the whole point of the
    // 2.5-minute recheck interval.
    for (const auto& [camId, cam] : liveCameras) {
        if (workers_.find(cam.id) != workers_.end()) continue;
        bool skipVerify = false;
        {
            std::lock_guard<std::mutex> plock(probationMtx_);
            if (probationCameras_.count(cam.id)) continue;
            auto rvIt = recentlyVerifiedCameras_.find(cam.id);
            if (rvIt != recentlyVerifiedCameras_.end()) {
                skipVerify = true;
                recentlyVerifiedCameras_.erase(rvIt);
            }
        }

        createCameraWindow(cam);

        auto flag = std::make_shared<std::atomic<bool>>(false);
        auto disconnectedFlag = std::make_shared<std::atomic<bool>>(false);
        stopFlags_[cam.id] = flag;
        disconnectedFlags_[cam.id] = disconnectedFlag;
        workerConfigs_[cam.id] = cam;
        workers_[cam.id] = std::thread(&SurveillanceController::processCamera, this, cam, flag, disconnectedFlag, skipVerify);

        LOG_CONTEXT(LogLevel::INFO, "Camera worker spawned: " + cam.id + " (" + cam.name + ")", {});
    }
}

void SurveillanceController::mainDisplayLoop() {
    auto lastReconcile = std::chrono::steady_clock::now();

    // Main display loop on main thread
    while (running_ && !externalStopRequested_.load()) {
        auto now = std::chrono::steady_clock::now();
        if (now - lastReconcile >= std::chrono::seconds(1)) {
            reconcileCameraWorkers();
            lastReconcile = now;
        }

        std::unordered_map<std::string, DisplayFrame> ready;
        {
            std::unique_lock<std::mutex> lock(displayQueueMtx_);
            // Wait for at least one camera to have a pending frame
            displayQueueCV_.wait_for(lock, std::chrono::milliseconds(10),
                [this] { return !latestFrames_.empty(); });
            // Take every camera's currently-pending frame in one go rather
            // than one per loop iteration - with N cameras, draining one at
            // a time means each individual window only refreshes every
            // ~N loop ticks even though every camera has a fresh frame
            // waiting right now.
            ready.swap(latestFrames_);
        }

        if (ready.empty()) {
            // Process OpenCV event loop even with no frames ready
            if (cv::waitKey(10) == 27) break; // ESC to stop
            continue;
        }

        // Display every camera's latest frame (on main thread, so this is safe)
        for (auto& [camId, frame] : ready) {
            cv::imshow(frame.windowName, frame.frame);
        }
        if (cv::waitKey(10) == 27) break; // ESC to stop
    }

    // Clean up windows
    {
        std::lock_guard<std::mutex> lk(cams_mtx_);
        for (const auto& [camId, windowName] : windowNames_) {
            cv::destroyWindow(windowName);
        }
        windowNames_.clear();
        windowTileIndex_.clear();
        clickContexts_.clear();
    }
}

void SurveillanceController::requestExternalStop() {
    // Just trips the flag - mainDisplayLoop()'s while condition (checked at
    // the top of every ~10ms iteration) picks it up and exits the same way
    // ESC does, which in turn already routes through run()'s own stop() call
    // right after mainDisplayLoop() returns. No window/thread teardown here.
    externalStopRequested_.store(true);
}

void SurveillanceController::stop() {
    // Window teardown is intentionally left to mainDisplayLoop()'s own
    // cleanup (triggered by running_ going false below) - it owns
    // windowNames_ and must be the only place destroying OpenCV windows,
    // to avoid a cross-thread race if stop() is called from another thread
    // while mainDisplayLoop() is still running on the main thread.
    running_ = false;
    {
        std::lock_guard<std::mutex> lk(cams_mtx_);
        for (auto& kv : stopFlags_) {
            kv.second->store(true);
        }
        // Full shutdown: blocking here is fine (unlike the live
        // reconciliation teardown path in reconcileCameraWorkers(), nothing
        // else needs this thread to stay responsive while the process is
        // exiting).
        for (auto& kv : workers_) {
            if (kv.second.joinable()) kv.second.join();
        }
        workers_.clear();
        stopFlags_.clear();
        disconnectedFlags_.clear();
        workerConfigs_.clear();
    }

    // Shut down the reaper only after every live worker above has been
    // joined. Anything handed off to it earlier via retireWorker() (during
    // prior reconciliation passes) still needs to drain first -
    // reaperLoop() keeps joining whatever's left in retiring_ even after
    // reaperRunning_ goes false, so this still waits for all of it.
    reaperRunning_.store(false);
    retireCv_.notify_all();
    if (reaperThread_.joinable()) {
        reaperThread_.join();
    }

    probationRunning_.store(false);
    probationCv_.notify_all();
    if (probationThread_.joinable()) {
        probationThread_.join();
    }

    // Finalize any still-active recordings (manual-unlimited or an
    // in-progress action-triggered clip) now that every worker thread -
    // the only threads that ever call writeFrame() on these - has been
    // joined above. Without this, a recording left running past the
    // toggle-off point never gets a real stopRecording() call on normal
    // shutdown: cv::VideoWriter::release() (which writes the MP4's moov
    // atom/index) never runs, so the file is left corrupted, and no
    // RecordingSummary/DB "recording" event is ever raised for it either.
    {
        std::lock_guard<std::mutex> lk(recorders_mtx_);
        for (auto& [camId, rec] : cameraRecorders_) {
            if (rec && rec->isRecording()) {
                rec->stopRecording();
                recordingEventProcessor_.raise(camId, rec->getLastRecordingSummary());
            }
        }
    }
}

void SurveillanceController::retireWorker(std::thread worker) {
    if (!worker.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(retireMtx_);
        retiring_.push_back(std::move(worker));
    }
    retireCv_.notify_one();
}

void SurveillanceController::reaperLoop() {
    while (true) {
        std::thread worker;
        {
            std::unique_lock<std::mutex> lock(retireMtx_);
            retireCv_.wait(lock, [this] {
                return !retiring_.empty() || !reaperRunning_.load();
            });
            if (retiring_.empty()) {
                // Nothing left to join, and told to stop - done.
                if (!reaperRunning_.load()) return;
                continue;
            }
            worker = std::move(retiring_.front());
            retiring_.pop_front();
        }
        // Blocking here is fine - this thread's only job is to join
        // finished/stopped workers, so nothing else waits on it.
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void SurveillanceController::probationLoop() {
    while (probationRunning_.load()) {
        std::vector<std::string> due;
        {
            std::lock_guard<std::mutex> lock(probationMtx_);
            auto now = std::chrono::steady_clock::now();
            for (const auto& [camId, entry] : probationCameras_) {
                if (now >= entry.nextCheckTime) due.push_back(camId);
            }
        }

        for (const auto& camId : due) {
            CameraConfig cam;
            if (!features_.getCameraConfig(camId, cam)) {
                // Camera was deleted outright (not just disconnected) while
                // on probation - nothing left to re-check.
                std::lock_guard<std::mutex> lock(probationMtx_);
                probationCameras_.erase(camId);
                continue;
            }

            // Single, fast attempt: at a 5s recheck cadence, a transient
            // false negative (e.g. the camera still warming up right after
            // being plugged back in) just gets corrected by the next check
            // a few seconds later, rather than needing built-in retries
            // here the way the slow, widely-spaced old design did. Off the
            // main thread either way - even one attempt can block for real
            // hundreds of ms (open + liveness-diff check), which is exactly
            // why this isn't done from reconcileCameraWorkers().
            bool reconnected = verifyCameraSource(cam.source, cam.name, 1, 0);

            std::lock_guard<std::mutex> lock(probationMtx_);
            auto it = probationCameras_.find(camId);
            if (it == probationCameras_.end()) continue; // removed concurrently

            if (reconnected) {
                LOG_CONTEXT(LogLevel::INFO, "Camera " + cam.name +
                    " reconnected during probation - resuming normal operation.", {});
                // Spoken (High severity), same as the entering-probation and
                // moved-to-pending announcements this pairs with - closes
                // the loop: the user was told this camera went dark and
                // roughly how long before it'd be given up on, so they
                // should also be told when it actually comes back rather
                // than only being able to notice by checking the UI.
                AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                    cam.id, cam.id, "CameraReconnected", helperFunctions::getCurrentTimestamp(), "High",
                    cam.name + " camera reconnected and has resumed normal operation."
                });
                // Just stop tracking it - reconcileCameraWorkers()'s own
                // spawn pass (it still sees this camId in features_'s live
                // list the whole time) picks it back up on its next ~1s tick,
                // so total unplug-to-respawned latency is bounded by
                // kProbationRecheckInterval + ~1s, not minutes.
                probationCameras_.erase(it);
                // Lets the next spawn skip its own redundant re-verification
                // of this exact camera - see recentlyVerifiedCameras_'s doc
                // comment.
                recentlyVerifiedCameras_.insert(camId);
                continue;
            }

            auto elapsed = std::chrono::steady_clock::now() - it->second.enteredAt;
            if (elapsed >= kProbationMaxDuration) {
                LOG_CONTEXT(LogLevel::WARNING, "Camera " + cam.name +
                    " did not reconnect within " +
                    std::to_string(std::chrono::duration_cast<std::chrono::minutes>(kProbationMaxDuration).count()) +
                    " min of probation - moving to pending cameras.", {});
                probationCameras_.erase(it);
                // One-way transition, same as a user-initiated "remove": the
                // system decided this camera needs to go to pending, but
                // only a deliberate plugInPendingCamera() call - not an
                // automatic sweep - brings a pending camera back.
                features_.plugOutPendingCamera(false, cam.name, cam.id);

                // Spoken (High severity) - this is a definitive state change
                // (automatic reconnection has given up entirely) that needs
                // a deliberate, manual restore to undo, so it's worth more
                // than a log line.
                AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                    cam.id, cam.id, "CameraMovedToPending", helperFunctions::getCurrentTimestamp(), "High",
                    cam.name + " camera did not reconnect and has been moved to pending. Manual restore required."
                });
            } else {
                LOG_CONTEXT(LogLevel::DEBUG, "Probation: camera " + cam.name +
                    " still not reconnected (" +
                    std::to_string(std::chrono::duration_cast<std::chrono::minutes>(elapsed).count()) +
                    "/" + std::to_string(std::chrono::duration_cast<std::chrono::minutes>(kProbationMaxDuration).count()) +
                    " min elapsed).", {});
                it->second.nextCheckTime = std::chrono::steady_clock::now() + kProbationRecheckInterval;
            }
        }

        std::unique_lock<std::mutex> lock(probationMtx_);
        probationCv_.wait_for(lock, kProbationPollInterval, [this] { return !probationRunning_.load(); });
    }
}

void SurveillanceController::processCamera(const CameraConfig& cam,
                                           std::shared_ptr<std::atomic<bool>> stopFlag,
                                           std::shared_ptr<std::atomic<bool>> disconnectedFlag,
                                           bool skipInitialVerify) {
    try {
        // Spoken (High severity) probation-entry announcement, fired from
        // this camera's own worker thread at the exact point disconnection
        // is detected - not from reconcileCameraWorkers() on the main
        // thread, which also owns cv::imshow()/cv::waitKey() for every
        // camera window and shouldn't do any more work than it has to per
        // ~1s tick. Same wording/severity as before; only where it fires
        // from changed.
        auto raiseProbationEnteredAlarm = [&]() {
            auto maxMinutes = std::chrono::duration_cast<std::chrono::minutes>(kProbationMaxDuration).count();
            AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                cam.id, cam.id, "CameraEnteredProbation", helperFunctions::getCurrentTimestamp(), "High",
                cam.name + " camera disconnected. Attempting to reconnect automatically for the next " +
                    std::to_string(maxMinutes) + " minutes before it is moved to pending."
            });
        };

        // Verify liveness before opening our own long-lived capture handle -
        // reuses the exact same check every other camera-availability
        // decision in this codebase uses (see verifyCameraSource()'s doc
        // comment), rather than the plain cap.open() this used to do on its
        // own with no liveness check at all. Without this, a camera that
        // just failed startup verification - but hasn't hit the 3-strike
        // quarantine threshold yet, so it's still sitting in cfg_.cameras -
        // still gets a worker and a visible window spawned for it here on
        // every reconcileCameraWorkers() pass. A phantom/fallback source's
        // reads still come back "successfully" (just static/identical
        // frames), so the consecutive-read-failure disconnect check below
        // would never trip either - it would just sit there forever showing
        // a permanently blank window, invisible to both probation and the
        // pending-camera flow. One extra open/close cycle here is a one-time
        // cost at worker spawn, not per-frame.
        //
        // Skipped when skipInitialVerify is true - probationLoop() already
        // ran this exact check on this exact camera moments ago before
        // deciding to respawn it; repeating it here would just double the
        // reconnect latency for no new information.
        if (!skipInitialVerify && !verifyCameraSource(cam.source, cam.name, 1, 0)) {
            LOG_CONTEXT(LogLevel::ERR, "Failed to open camera: " + cam.name, {});
            alerts_.raise(Alert{"system", "Failed to open camera: " + cam.name,
                                cam.id, helperFunctions::getCurrentTimestamp()});
            disconnectedFlag->store(true);
            raiseProbationEnteredAlarm();
            return;
        }

        cv::VideoCapture cap;
        // DSHOW first, MSMF as fallback - same fix, same reason as
        // verifyCameraSource()'s probe-open (config.cpp): measured live,
        // this call alone (previously backend-unspecified, i.e. implicit
        // cv::CAP_ANY) took 3.58s to open a real, already-verified camera -
        // OpenCV's auto-selection does NOT reliably avoid the same MSMF
        // slowness verifyCameraSource() was fixed for. This is the capture
        // handle the whole streaming session actually reads frames from
        // (verifyCameraSource() above only opened a throwaway probe to
        // confirm liveness), so on every worker spawn - including every
        // probation-triggered reconnect - this open was silently undoing
        // most of that earlier fix's benefit: probation's own probe-open
        // would come back fast, but the real handle opened right after it,
        // here, was still paying the slow-backend cost every time.
        if (!cam.source.empty() && std::all_of(cam.source.begin(), cam.source.end(), ::isdigit)) {
            int deviceId = std::stoi(cam.source);
            cap.open(deviceId, cv::CAP_DSHOW);
            if (!cap.isOpened()) {
                cap.open(deviceId, cv::CAP_MSMF);
            }
        } else {
            cap.open(cam.source.empty() ? 0 : cam.source);
        }
        if (!cap.isOpened()) {
            // Verified live a moment ago but failed to open now (device went
            // away/became busy in between) - same disconnected treatment as
            // above, not a silent zombie worker entry.
            LOG_CONTEXT(LogLevel::ERR, "Failed to open camera: " + cam.name, {});
            alerts_.raise(Alert{"system", "Failed to open camera: " + cam.name,
                                cam.id, helperFunctions::getCurrentTimestamp()});
            disconnectedFlag->store(true);
            raiseProbationEnteredAlarm();
            return;
        }

        LOG_CONTEXT(LogLevel::INFO, "Camera " + cam.name + " opened successfully.", {});

        // Without this, the backend keeps its own internal frame queue (2-4
        // frames deep on MSMF/DSHOW). If this loop's per-frame pipeline ever
        // takes longer than the camera's native frame interval - one slow
        // frame is enough - cap.read() starts returning whatever's oldest in
        // that queue instead of the current frame, and the backlog never
        // clears on its own since nothing here ever reads faster than
        // real-time to drain it. Requesting a 1-frame buffer asks the
        // backend to drop old frames itself instead of queuing them, so
        // read() stays close to real-time even after an occasional slow
        // frame. Not all backends honor this (return value intentionally
        // ignored) - harmless no-op where unsupported.
        cap.set(cv::CAP_PROP_BUFFERSIZE, 1);

        double fps = cap.get(cv::CAP_PROP_FPS);
        
        // Some cameras report 0 or very low FPS; set a default if invalid
        if (fps <= 0 || fps > 120) {
            fps = 30.0;
        }
        CameraConfig currentCam = cam;

        // Consecutive failed cap.read() calls - a camera unplugged
        // mid-session shows up here, not as an exception. See
        // kMaxConsecutiveReadFailures's doc comment for why this exists:
        // without the backoff sleep below, a backend that fails read()
        // instantly (rather than blocking) turns this into a busy-spin
        // pegging a CPU core for as long as the camera stays disconnected.
        int consecutiveReadFailures = 0;

        // Rolling estimate of this camera's actually-achieved per-frame
        // processing rate, as opposed to `fps` above (the camera's reported
        // nominal capability, used only to seed this and as an upper
        // clamp). See EventRecorder::setFps()'s doc comment for why this
        // exists - the two can differ substantially once real detection
        // work is running, and recordings need the real one. EWMA rather
        // than a flat average so it tracks a session's changing load
        // instead of being anchored by its cold-start frames forever;
        // alpha is high enough to converge in roughly a couple seconds of
        // real frames rather than dozens, since a camera can start
        // recording almost immediately after its worker thread spawns
        // (e.g. a camera configured to record from boot) with no chance to
        // warm up first. framesObserved gates first use - see below.
        double measuredFps = fps;
        int framesObserved = 0;
        constexpr int kFpsWarmupFrames = 8;
        auto lastFrameTime = std::chrono::steady_clock::now();

        // Default-epoch so the first eligible frame after thread spawn
        // always streams immediately rather than waiting out the interval.
        std::chrono::steady_clock::time_point lastStreamSentAt{};

        // Freeze-detection state (see kFreezeCheckInterval's doc comment
        // above) - lastFreezeCheckFrame starts empty so the very first
        // successful read only seeds it rather than being compared against
        // nothing.
        cv::Mat lastFreezeCheckFrame;
        auto lastFreezeCheckAt = std::chrono::steady_clock::now();

        while (running_ && !stopFlag->load()) {
            cv::Mat frame;
            if (!cap.read(frame) || frame.empty()) {
                ++consecutiveReadFailures;
                if (consecutiveReadFailures >= kMaxConsecutiveReadFailures) {
                    LOG_CONTEXT(LogLevel::WARNING, "Camera " + cam.name + " had " +
                        std::to_string(kMaxConsecutiveReadFailures) +
                        " consecutive failed frame reads - treating as disconnected.", {});
                    alerts_.raise(Alert{"system", "Camera disconnected mid-session: " + cam.name,
                                        cam.id, helperFunctions::getCurrentTimestamp()});
                    disconnectedFlag->store(true);
                    raiseProbationEnteredAlarm();
                    break;
                }
                std::this_thread::sleep_for(kReadFailureBackoff);
                continue;
            }
            consecutiveReadFailures = 0;

            // Periodic freeze check - see kFreezeCheckInterval's doc comment
            // above for why this exists (a disconnected camera whose
            // backend keeps handing back its last decoded frame instead of
            // failing read(), which the consecutive-read-failure check right
            // above this can never catch). Deliberately placed before the
            // early-continue paths below (feature-disabled frame skips,
            // etc.) so it keeps sampling regardless of which detection
            // features happen to be toggled on for this camera right now.
            {
                auto now = std::chrono::steady_clock::now();
                if (lastFreezeCheckFrame.empty()) {
                    lastFreezeCheckFrame = frame;
                    lastFreezeCheckAt = now;
                } else if (now - lastFreezeCheckAt >= kFreezeCheckInterval) {
                    bool sameSizeType = frame.size() == lastFreezeCheckFrame.size() &&
                                         frame.type() == lastFreezeCheckFrame.type();
                    bool frozen = false;
                    if (sameSizeType) {
                        cv::Mat diff;
                        cv::absdiff(frame, lastFreezeCheckFrame, diff);
                        cv::Scalar meanDiff = cv::mean(diff);
                        frozen = (meanDiff[0] + meanDiff[1] + meanDiff[2]) / 3.0 <= kMinLivenessDiff;
                    }
                    lastFreezeCheckFrame = frame;
                    lastFreezeCheckAt = now;

                    if (frozen) {
                        LOG_CONTEXT(LogLevel::WARNING, "Camera " + cam.name +
                            " has returned identical frames for " +
                            std::to_string(std::chrono::duration_cast<std::chrono::seconds>(kFreezeCheckInterval).count()) +
                            "s despite cap.read() reporting success - treating as disconnected.", {});
                        alerts_.raise(Alert{"system", "Camera disconnected mid-session: " + cam.name,
                                            cam.id, helperFunctions::getCurrentTimestamp()});
                        disconnectedFlag->store(true);
                        raiseProbationEnteredAlarm();
                        break;
                    }
                }
            }

            // Update the rolling actual-fps estimate off this fully
            // successful read-through-to-here frame (not one that hit the
            // failure/backoff path above, which `continue`s before reaching
            // this point) - see measuredFps's doc comment above the loop.
            {
                auto now = std::chrono::steady_clock::now();
                double intervalSec = std::chrono::duration<double>(now - lastFrameTime).count();
                lastFrameTime = now;
                if (intervalSec > 0.0001) {
                    double instantFps = std::max(1.0, std::min(1.0 / intervalSec, fps));
                    if (framesObserved == 0) {
                        // First real sample - jump straight to it instead of
                        // slow-blending away from the nominal seed.
                        measuredFps = instantFps;
                    } else {
                        constexpr double kEwmaAlpha = 0.35;
                        measuredFps = (kEwmaAlpha * instantFps) + ((1.0 - kEwmaAlpha) * measuredFps);
                    }
                    ++framesObserved;
                }
            }

            /*
                ===============================================================================
                LIVE CONFIG: features_ is the single source of truth. Pull this camera's
                current toggles/settings directly from it every frame - no file polling,
                no separate reload path. If the camera was removed from features_, the
                reconciliation loop will signal stopFlag shortly; just skip this frame
                until it does.

                NOTE: this refresh was previously just a comment with no code behind it -
                currentCam was assigned once above (before the loop) and never touched
                again, so every currentCam.enableXXX check below was reading a one-time
                snapshot from thread spawn, not a live value. A runtime toggle via
                setCameraFeature() (directly, or via the file-watch reload) updated
                FeatureConfigManager's cfg_ correctly, but this worker never re-read it.
                Each feature's *nested settings* (getFaceSettings(), getMotionSettings(),
                ...) were already being re-fetched live every frame further down - only
                the on/off toggles themselves were stale.
            */
            if (!features_.getCameraConfig(cam.id, currentCam)) {
                // Camera no longer exists in features_ - reconcileCameraWorkers() will
                // signal stopFlag_ shortly. Keep processing this frame with the last
                // known config rather than special-casing it here.
            }

            /*
                ===============================================================================
                FACE RECOGNITION
                This section performs face detection and tracking if enabled for the camera. 
            */
            FaceRecognition* faceRecPtr = nullptr;
            bool faceJustCreated = false;
            {
                std::lock_guard<std::mutex> lock(faceRecMtx_);
                auto it = faceRecognizers_.find(cam.id);
                if (it == faceRecognizers_.end()) {
                    // Create new instance if not existing
                    faceRecognizers_[cam.id] = std::make_unique<FaceRecognition>();
                    faceRecPtr = faceRecognizers_[cam.id].get();
                    faceJustCreated = true;

                    // Reuse the global backend initialized in init()
                    faceRecPtr->setBackendRef(faceRec_.getBackend());   // attach shared model
                    // Also share the loaded recognizer (SFace weights), not
                    // just the detector - matchDetections() only extracts an
                    // embedding (and therefore only ever calls
                    // publishAndEnqueue) when this per-camera instance has
                    // its own recognizer_ set. Missing this line was why no
                    // face was ever actually captured/stored at runtime,
                    // despite detection (bounding boxes) working fine.
                    faceRecPtr->setRecognizer(faceRec_.getRecognizer());
                    faceRecPtr->setGlobalManager(&identityMgr_);

                    LOG_CONTEXT(LogLevel::DEBUG,
                        "[Cam " + cam.id + "] FaceRecognition instance created using global backend.", {});
                } else {
                    faceRecPtr = it->second.get();
                }
            }

            if (currentCam.enableFaceRecognition && faceRecPtr) {
                auto f_settings = features_.getFaceSettings(currentCam.id);

                // faceJustCreated: without this, a freshly-created instance's
                // maxTrackAgeMs/iouThreshold/scoreThreshold/debugLogging (all
                // stored setter state, unlike dcfg below which is rebuilt from
                // f_settings fresh every frame regardless of dirty) sit at
                // FaceRecognition's own hardcoded defaults until some
                // unrelated dirty event fires for this camera - addCamera()
                // doesn't markDirty() at all, so a freshly-added camera could
                // run on the wrong values indefinitely.
                if (faceJustCreated || features_.isDirty(cam.id, SettingsDomain::Face)) {
                    faceRecPtr->setMaxTrackAgeMs(f_settings.maxTrackAgeMs);
                    faceRecPtr->setIouThreshold(f_settings.iouThreshold);
                    faceRecPtr->setScoreThreshold(f_settings.scoreThreshold);
                    faceRecPtr->setDebugLogging(f_settings.debugLogging);
                    features_.clearDirty(currentCam.id, SettingsDomain::Face);
                }

                DetectionConfig dcfg;
                dcfg.scaleFactor     = f_settings.scaleFactor;
                dcfg.minNeighbors    = f_settings.minNeighbors;
                dcfg.nmsThreshold    = f_settings.nmsThreshold;
                dcfg.topK            = f_settings.topK;
                dcfg.minFaceSize     = f_settings.minFaceSize;
                dcfg.scoreThreshold  = f_settings.scoreThreshold;
                dcfg.useEqualizeHist = f_settings.useEqualizeHist;
                dcfg.maxDetections   = f_settings.maxDetections;
                dcfg.logToDatabase   = currentCam.enableDatabaseLogging;
                dcfg.trackUnknown    = currentCam.trackUnknownFaces;
                dcfg.restrictUnknown = currentCam.restrictUnknownFaces;

                // Use trackAndProcess instead of detectFaces + trackFaces
                std::vector<FaceTrack> tracks = faceRecPtr->trackAndProcess(cam.id, frame, dcfg);

                LOG_CONTEXT(LogLevel::DEBUG,
                    "[Cam " + cam.id + "] Face detection completed. Tracked: " +
                    std::to_string(tracks.size()), {});

                // Unconditional -- an empty tracks vector must still reach
                // drawTracks() so it clears BoundingBoxAnnotator's stale
                // box list (via setBoxes({})) once the last face leaves
                // frame. Skipping the call here left the annotator holding
                // its last non-empty box list forever, which the per-frame
                // metadata push (below) would otherwise report as
                // permanent ghost boxes on the dashboard.
                faceRecPtr->drawTracks(frame, tracks, cam.id);
            } else if (faceRecPtr) {
                // Toggling face recognition off entirely skips the block
                // above, so trackAndProcess() stops running -- but whatever
                // boxes were tracked at the moment it was disabled stay in
                // BoundingBoxAnnotator until something explicitly clears
                // them. Without this, the dashboard keeps showing (and
                // getFaceBoxes()'s per-frame metadata push keeps reporting)
                // a frozen, no-longer-updating box over wherever a face
                // last was -- looks like it's "still on" even though no
                // detection is actually running. Same drawTracks({}) call
                // the comment above already relies on for the
                // no-face-this-frame case, just reached from the opposite
                // (feature-disabled) direction.
                faceRecPtr->drawTracks(frame, {}, cam.id);
            }

            /*
                ===============================================================================
                NIGHT VISION PROCESSING
                This section applies Night Vision enhancement to the frame if enabled for the camera.            
            */
            NightVision* nvPtr = nullptr;
            bool nvJustCreated = false;
            {
                std::lock_guard<std::mutex> lock(nightVisions_mtx_);
                auto it = nightVisions_.find(cam.id);
                if (it == nightVisions_.end()) {
                    nightVisions_[cam.id] = std::make_unique<NightVision>();
                    nvPtr = nightVisions_[cam.id].get();
                    nvJustCreated = true;
                } else {
                    nvPtr = it->second.get();
                }
            }

            if (currentCam.enableNightVision) {
                // Apply per-camera Night Vision settings. nvJustCreated:
                // without this, a freshly-created instance runs on
                // NightVision's own hardcoded defaults until some unrelated
                // dirty event fires for this camera - addCamera() doesn't
                // markDirty() at all, so a freshly-added camera's configured
                // gamma/contrastMode/clipLimit/tileSize/denoisingStrength
                // could silently never apply.
                if (nvJustCreated || features_.isDirty(cam.id, SettingsDomain::NightVision)) {
                    auto nvSettings = features_.getNightVisionSettings(currentCam.id);

                    nvPtr->setGamma(nvSettings.gamma);
                    nvPtr->setAdaptiveMode(nvSettings.adaptiveMode);
                    nvPtr->setContrastMode(
                        nvSettings.contrastMode == 0 ? NightVision::ContrastMode::HISTOGRAM
                                                    : NightVision::ContrastMode::CLAHE);
                    nvPtr->setClipLimit(nvSettings.clipLimit);
                    nvPtr->setTileSize(nvSettings.tileSize);
                    nvPtr->setDenoisingStrength(nvSettings.denoisingStrength);
                    features_.clearDirty(currentCam.id, SettingsDomain::NightVision);
                }

                // Enhance frame with configured settings
                frame = nvPtr->enhance(frame, NightVision::Mode::COLORED);

                // Ensure output is BGR for downstream modules
                if (frame.channels() == 1) {
                    cv::cvtColor(frame, frame, cv::COLOR_GRAY2BGR);
                }
            }

            /*
                ===============================================================================
                NOTE: MOTION DETECTION SECTION
                This section is designed to be modular and efficient.  The MotionDetection 
                class maintains its own internal state and history, allowing it to analyze frame 
                differences over time to identify motion regions.
            */
            MotionDetection* localMotionPtr = nullptr;
            bool motionJustCreated = false;
            {
                std::lock_guard<std::mutex> lock(cams_mtx_);
                auto it = cameraMotions_.find(cam.id);
                if (it == cameraMotions_.end()) {
                    cameraMotions_[cam.id] = std::make_unique<MotionDetection>();
                    localMotionPtr = cameraMotions_[cam.id].get();
                    motionJustCreated = true;

                    // Attach the global backend instead of re-initializing.
                    // alreadyInitialized=true - this is masterMotionDetector's
                    // own already-init()'d backend, being reused here, not a
                    // fresh one. Getting this wrong left every per-camera
                    // MotionDetection instance's initialized_ stuck false
                    // forever - detectMotion() silently no-op'd on every
                    // frame, indefinitely, with no error (see
                    // MotionDetection::setBackend()'s doc comment).
                    if (masterMotionDetector.hasBackend()) {
                        localMotionPtr->setBackend(masterMotionDetector.getBackend(), /*alreadyInitialized=*/true);
                        LOG_CONTEXT(LogLevel::INFO, "[Cam " + (cam.id) +
                                        "] MotionDetection instance created using global backend.", {});
                    } else {
                        LOG_CONTEXT(LogLevel::WARNING, "[Cam " + (cam.id) +
                                        "] No global motion backend available.", {});
                    }
                } else {
                    localMotionPtr = it->second.get();
                }
            }

            if (currentCam.enableMotionDetection && localMotionPtr) {
                auto& motion = *localMotionPtr;

                // --- SYNC SETTINGS & ZONES ---
                // motionJustCreated: without this, a freshly-created instance
                // runs on MotionDetection's own hardcoded defaults (and with
                // no zones at all) until some unrelated dirty event fires for
                // this camera - addCamera() doesn't markDirty() at all.
                if (motionJustCreated || features_.isDirty(cam.id, SettingsDomain::Motion)) {
                    motion.clearZones();

                    auto motionSettings = features_.getMotionSettings(cam.id);
                    motion.setDiffThreshold(motionSettings.diffThreshold);
                    motion.setMinArea(motionSettings.minArea);
                    motion.setCrowdThreshold(motionSettings.crowdThreshold);
                    motion.setLoiterSeconds(motionSettings.loiterSeconds);
                    motion.setLeftBehindSeconds(motionSettings.leftBehindSeconds);

                    for (const auto& zone : currentCam.motionSettingsZones) {
                        motion.addZone(zone);
                    }

                    features_.clearDirty(cam.id, SettingsDomain::Motion);
                    std::cout << "[INFO][Cam " << cam.id << "] Motion parameters synced. Active zones: "
                            << currentCam.motionSettingsZones.size() << std::endl;
                }

                // --- DETECTION LOGIC ---
                auto regions = motion.detectMotion(frame);
                motion.drawMotion(frame, regions);

                if (!regions.empty()) {
                    alerts_.raise(Alert{
                        "motion",
                        "Motion detected: " + std::to_string(regions.size()) + " regions",
                        cam.id,
                        helperFunctions::getCurrentTimestamp()
                    });

                    bool restrictedHit = false;
                    for (const auto& r : regions) {
                        if (r.inRestrictedZone) { restrictedHit = true; break; }
                    }

                    if (restrictedHit) {
                        // High severity -> VoiceAlarmChannel - same pattern
                        // as RestrictedAreaUnknownFace for faces: the
                        // restricted condition escalates/replaces the
                        // routine notification for this frame rather than
                        // also firing the Low-severity beep below for the
                        // same detection.
                        //
                        // Cooldown gate: unlike the beep below (rate-limited
                        // centrally by BeepAlarmChannel), voice has no
                        // built-in rate-limiting - confirmed live, motion
                        // persisting in a restricted zone re-dispatched
                        // every single frame and backed the voice queue up
                        // into a continuous drone. 10s per camera, same
                        // spirit as BeepAlarmChannel's 7s cooldown.
                        bool shouldSpeak = false;
                        {
                            std::lock_guard<std::mutex> lock(restrictedZoneAlarmMtx_);
                            auto now = std::chrono::steady_clock::now();
                            auto it = lastRestrictedZoneAlarm_.find(cam.id);
                            if (it == lastRestrictedZoneAlarm_.end() || now - it->second >= std::chrono::seconds(10)) {
                                lastRestrictedZoneAlarm_[cam.id] = now;
                                shouldSpeak = true;
                            }
                        }
                        if (shouldSpeak) {
                            AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                                cam.id, cam.id, "RestrictedZoneMotion", helperFunctions::getCurrentTimestamp(),
                                "High", cam.name + " camera detected motion inside a restricted zone."
                            });
                        }
                    } else {
                        // Low severity -> BeepAlarmChannel, not voice -
                        // motion fires far more often than a resolved
                        // identity/restricted-area hit ever would, and the
                        // beep channel already has a rate-limit/cooldown
                        // built specifically for exactly this kind of
                        // frequent, routine event (see its doc comment) -
                        // no need to rate-limit here too.
                        AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                            cam.id, cam.id, "MotionDetected", helperFunctions::getCurrentTimestamp(),
                            "Low", cam.name + " camera detected motion: " + std::to_string(regions.size()) + " region(s)."
                        });
                    }
                }
            }

            /*
                ===============================================================================
                NOTE: ACTION DETECTION SECTION
                This section relies on motion detection to first identify regions of interest
                where people are present.  The action detection model then analyzes these
                regions to classify specific behaviors such as fighting, running, falling, etc.
            */
            // Set by raiseActionEvent() below when any action event fires
            // this frame - consumed after EVENT RECORDER RESOLUTION further
            // down (localRecorderPtr doesn't exist yet at this point in the
            // per-frame flow) to auto-start a 10-minute recording, per
            // explicit request: action detection should be able to kick off
            // recording on its own, separate from the manual toggle.
            bool actionEventFiredThisFrame = false;
            ActionDetection* localActionPtr = nullptr;
            {
                std::lock_guard<std::mutex> lock(cams_mtx_);
                auto it = actionDetectors_.find(cam.id);
                if (it == actionDetectors_.end()) {
                    // Create new instance if not existing
                    actionDetectors_[cam.id] = std::make_unique<ActionDetection>();
                    localActionPtr = actionDetectors_[cam.id].get();

                    // Reuse every globally-loaded model (fight/fall/run/...),
                    // not just one - see ActionDetection::setModels().
                    const auto& globalModels = masterActionDetector_.getModels();

                    if (!globalModels.empty()) {
                        localActionPtr->setModels(globalModels);
                        LOG_CONTEXT(LogLevel::INFO, "[Cam " + (cam.id) +
                                        "] ActionDetection instance created using global backend (" +
                                        std::to_string(globalModels.size()) + " model(s)).", {});
                    } else {
                        LOG_CONTEXT(LogLevel::WARNING, "[Cam " + (cam.id) +
                                        "] No global action model available.", {});
                    }

                    // Whole-frame fight detector - attached separately from
                    // setModels() above since it isn't part of models_ (see
                    // ActionDetection::setFightModel()'s doc comment). A
                    // no-op (empty net) if none was loaded in init().
                    localActionPtr->setFightModel(masterActionDetector_.getFightModel());
                    localActionPtr->setFallModel(masterActionDetector_.getFallModel());
                } else {
                    // Reuse existing instance
                    localActionPtr = it->second.get();
                }
            }

            if (currentCam.enableActionDetection && localActionPtr) {
                auto& localAction = *localActionPtr;
                auto& motion = *localMotionPtr;

                // Runs every frame, unconditionally. This used to be gated on
                // features_.isDirty(cam.id), but that shared per-camera flag
                // is also consumed (and cleared) by the face/night-vision/
                // motion sync blocks above - whichever runs first each frame
                // wins it - so action detection's actual detection logic
                // almost never got a turn.
                try {
                    // Use the specific motion detector for this camera to get regions
                    auto regions = motion.detectMotion(frame);

                    // Convert motion regions to bounding boxes
                    std::vector<cv::Rect> personBoxes;
                    for (const auto& region : regions) {
                        personBoxes.push_back(region.bbox);
                    }

                    // Run isolated action detection
                    auto actionOutput = localAction.detectActions(frame, personBoxes);
                    localAction.drawActions(frame, actionOutput);

                    // --- Behavior Detection ---
                    // actionEventProcessor_.raise() replaces the old
                    // alerts_.raise() calls here - alerts_ (AlertManager) is
                    // just a console log line with no persistence; this
                    // actually writes to the events table and dispatches
                    // through AlarmDispatcher, off this thread. See
                    // ActionEventProcessor's class comment for why this
                    // isn't routed through GlobalDetectionProcessor instead.
                    // Wrapped so every raise site also flags
                    // actionEventFiredThisFrame for the auto-recording
                    // trigger below, without duplicating that line 5 times.
                    auto raiseActionEvent = [&](const std::string& type, const std::string& severity,
                                                 const std::string& detail) {
                        actionEventProcessor_.raise(cam.id, type, severity, detail);
                        // Auto-recording is reserved for critical (High-severity)
                        // events - Fighting/Fall - not routine Low/Medium ones
                        // like Loitering/Running. Those
                        // still raise their alarm above unchanged; they just
                        // don't each kick off their own 10-minute clip.
                        if (severity == "High") {
                            actionEventFiredThisFrame = true;
                        }
                    };
                    // cam.name (e.g. "Entrance") prefixed onto every detail
                    // string below, matching the MotionDetected/
                    // CameraReconnected/CameraEnteredProbation alert phrasing
                    // convention elsewhere in this file - without it, the
                    // spoken/displayed alarm just says e.g. "Fighting
                    // detected" with no indication of which camera/location,
                    // which is useless to a user acting on it.
                    if (localAction.detectFighting(actionOutput)) {
                        raiseActionEvent("Fighting", "High", cam.name + " camera: Fighting detected");
                    }
                    if (localAction.detectRunning(actionOutput)) {
                        raiseActionEvent("Running", "Medium", cam.name + " camera: Running detected");
                    }
                    if (localAction.detectFall(actionOutput)) {
                        raiseActionEvent("Fall", "High", cam.name + " camera: Fall detected");
                    }
                    if (localAction.detectLoitering(actionOutput)) {
                        raiseActionEvent("Loitering", "Low", cam.name + " camera: Loitering detected");
                    }

                } catch (const std::exception& e) {
                    LOG_CONTEXT(LogLevel::WARNING, "[Cam " + cam.id + "] Action detection error: " + e.what(), {});
                }
            }

            /*
                ===============================================================================
                NOTE: EVENT RECORDER RESOLUTION
                Resolved here (before any detection section) rather than down by the actual
                per-frame write, because the object-detection section below also needs to
                start a clip on this same per-camera instance - it used to reach for
                masterRecorder_ instead (a single EventRecorder shared, unguarded, across every
                camera's worker thread - a real data race whenever 2+ cameras had recording +
                object detection on at once). One instance per camera, resolved once, used by
                every section that can trigger a recording.
            */
            EventRecorder* localRecorderPtr = nullptr;
            bool recorderJustCreated = false;
            {
                std::lock_guard<std::mutex> lock(recorders_mtx_);
                auto it = cameraRecorders_.find(cam.id);
                if (it == cameraRecorders_.end()) {
                    cameraRecorders_[cam.id] = std::make_unique<EventRecorder>();
                    localRecorderPtr = cameraRecorders_[cam.id].get();

                    // One-time structural setup only - id/width/height/fps are fixed
                    // for this camera's lifetime. Tunable settings are NOT applied
                    // here; they're synced below, gated by isDirty, so runtime
                    // changes via setRecorderSettings() reach an already-running
                    // camera the same way face/motion/night-vision settings do.
                    //
                    // frame.cols/frame.rows (the real capture this worker just read),
                    // not cam.width/cam.height (CameraConfig's configured value) -
                    // those two can disagree (a camera that doesn't honor a requested
                    // resolution, a video-file source with its own native size, or a
                    // camera added through a path that never had a real resolution to
                    // supply and is just sitting at CameraConfig's 640x480 default -
                    // see Surveillance::addCamera's doc comment). Recording at the
                    // wrong dimensions produces a squashed/cropped clip; the frame
                    // actually being processed right now is the one source of truth
                    // for what this camera is really producing.
                    if (!localRecorderPtr->initialize(cam.id, frame.cols, frame.rows, fps)) {
                        LOG_CONTEXT(LogLevel::ERR, "[Cam " + cam.id + "] Failed to initialize EventRecorder.", {});
                        AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                            cam.id, cam.id, "EventRecorderInitFailed", helperFunctions::getCurrentTimestamp(), "High",
                            cam.name + " camera's event recorder failed to initialize - recording is unavailable for this camera this session."
                        });
                        localRecorderPtr = nullptr;
                    } else {
                        recorderJustCreated = true;
                        LOG_CONTEXT(LogLevel::INFO, "[Cam " + cam.id + "] EventRecorder initialized.", {});
                    }
                } else {
                    localRecorderPtr = it->second.get();
                }
            }

            // Sync tunable recorder settings: on first creation (so a newly
            // spawned camera starts with its configured settings, not defaults)
            // and again whenever features_ reports this camera dirty. No
            // setEnabled() here - EventRecorder is a pure sensor with no on/off
            // state of its own; CameraConfig::enableRecording (checked at each
            // call site below) is the one and only toggle.
            if (localRecorderPtr && (recorderJustCreated || features_.isDirty(cam.id, SettingsDomain::Recorder))) {
                auto r_settings = features_.getRecorderSettings(currentCam.id);
                localRecorderPtr->setCodec(r_settings.codec, r_settings.bitrate);
                localRecorderPtr->setMaxDuration(r_settings.maxDuration);
                localRecorderPtr->setMaxFileSize(r_settings.maxFileSize);
                localRecorderPtr->setEventType(r_settings.eventType);
                features_.clearDirty(cam.id, SettingsDomain::Recorder);
            }

            /*
                ===============================================================================
                NOTE: ACTION-TRIGGERED AUTO-RECORDING
                Per explicit request: an action-detection event (fighting/running/fall/
                loitering/restricted-zone - see actionEventFiredThisFrame, set above in
                the ACTION DETECTION SECTION) should be
                able to start recording on its own, independent of the manual enableRecording
                toggle, for a fixed 10 minutes, then stop automatically. Only starts if nothing
                is recording yet - if the user already has manual recording on (unlimited
                duration, see EVENT RECORDING SECTION below), that already covers this frame
                and takes precedence; a second concurrent auto clip would just be redundant.
                The 10-minute cap is enforced by EventRecorder's own maxDuration_ check inside
                writeFrame() - nothing here needs to track elapsed time itself.
            */
            if (actionEventFiredThisFrame && localRecorderPtr && !localRecorderPtr->isRecording()) {
                localRecorderPtr->setMaxDuration(600); // 10 minutes
                localRecorderPtr->setAutoTriggered(true);
                localRecorderPtr->setEventType("action");
                localRecorderPtr->setFps(framesObserved >= kFpsWarmupFrames ? measuredFps : fps);
                localRecorderPtr->startRecording(getRecordingsPath(), "Action detection triggered");
            }

            /*
                ===============================================================================
                NOTE: OBJECT DETECTION SECTION
                This section is designed to be modular and efficient.  The ObjectDetection
                class maintains its own internal state and model instances, allowing it to
                perform detection with configurable thresholds.
            */
            ObjectDetection* localObjectPtr = nullptr;
            bool objectJustCreated = false;
            {
                std::lock_guard<std::mutex> lock(cams_mtx_);
                auto it = objectDetectors_.find(cam.id);
                if (it == objectDetectors_.end()) {
                    // Create only if it doesn't exist
                    objectDetectors_[cam.id] = std::make_unique<ObjectDetection>(416);
                    localObjectPtr = objectDetectors_[cam.id].get();
                    objectJustCreated = true;

                    // Attach the global YOLO backend immediately
                    const cv::dnn::Net& globalNet = masterObjectDetector_.getModel();
                    const std::vector<std::string>& globalLabels = masterObjectDetector_.getClassNames();
                    const std::string globalName = masterObjectDetector_.getActiveModelName();

                    if (!globalNet.empty()) {
                        localObjectPtr->setModel(globalNet, globalLabels, globalName);

                        LOG_CONTEXT(LogLevel::INFO,
                            "[Cam " + cam.id + "] ObjectDetection instance created using global YOLO backend.",
                            {});
                    } else {
                        LOG_CONTEXT(LogLevel::WARNING,
                            "[Cam " + cam.id + "] No global YOLO model available.",
                            {});
                    }
                } else {
                    localObjectPtr = it->second.get();
                }
            }

            if (currentCam.enableObjectDetection && localObjectPtr) {
                auto& localObject = *localObjectPtr;

                // Get per‑camera settings
                auto odSettings = features_.getObjectDetectionSettings(currentCam.id);

                // objectJustCreated: without this, a freshly-created instance's
                // backend/target (stored setter state - unlike minConfForDraw
                // below, which is read fresh from odSettings every frame
                // regardless of dirty) sit at ObjectDetection's own default
                // until some unrelated dirty event fires for this camera -
                // addCamera() doesn't markDirty() at all.
                if (objectJustCreated || features_.isDirty(cam.id, SettingsDomain::Object)) {
                    localObject.setBackendTarget(odSettings.backend, odSettings.target);
                    features_.clearDirty(cam.id, SettingsDomain::Object);

                    LOG_CONTEXT(LogLevel::INFO,
                        "[Cam " + cam.id + "] Object detection backend set to: " + std::to_string(odSettings.backend),
                        {});
                }

                // Only call detect() — publishAndEnqueue is triggered inside detect()
                // Throttled, not detect() directly - full YOLOv3 on CPU is
                // heavy enough that running it on every single frame
                // visibly slowed the camera down. Running the forward pass
                // once every 5 frames and reusing the last result in
                // between keeps the box mostly current while letting
                // capture/display run at full speed.
                auto objs = localObject.detectThrottled(frame, cam.id, odSettings.minConfForDraw, 0.4f, 5);

                // TEMPORARY diagnostic - confirms whether detect() is
                // returning any raw candidates at all (and at what
                // confidence) to debug why no boxes are showing up live.
                // Throttled to ~once/sec at 30fps rather than every frame.
                // Safe to delete once confirmed working, same one-shot
                // pattern as other verification steps this session.
                {
                    static int frameCounter = 0;
                    if (++frameCounter % 30 == 0) {
                        float topConf = 0.f;
                        std::string topLabel = "none";
                        for (const auto& o : objs) {
                            if (o.confidence > topConf) { topConf = o.confidence; topLabel = o.label; }
                        }
                        LOG_CONTEXT(LogLevel::INFO,
                            "[Cam " + cam.id + "] [OBJ-DEBUG] minConfForDraw=" +
                            std::to_string(odSettings.minConfForDraw) + " detect() returned " +
                            std::to_string(objs.size()) + " object(s), top: " + topLabel +
                            " @ " + std::to_string(topConf), {});
                    }
                }

                localObject.draw(frame, objs);

                for (const auto& o : objs) {
                    if (o.confidence < odSettings.minConfForDraw) continue;
                    if (o.label == "person") continue;

                    alerts_.raise(Alert{
                        "object",
                        "Detected: " + o.label + " (ID:" + std::to_string(o.trackId) + ")",
                        cam.id,
                        helperFunctions::getCurrentTimestamp()
                    });

                    if (currentCam.enableRecording && localRecorderPtr && !localRecorderPtr->isRecording()) {
                        localRecorderPtr->setEventType("object");
                        localRecorderPtr->setFps(framesObserved >= kFpsWarmupFrames ? measuredFps : fps);
                        localRecorderPtr->startRecording(
                            getRecordingsPath(),
                            o.label + " (conf: " + std::to_string(static_cast<int>(o.confidence * 100)) + "%)"
                        );
                    }
                }
            }

            /*
                ===============================================================================
                WEAPON DETECTION SECTION
                Separate model from the general object detector above (coco.names has no
                weapon/gun class), so this is its own detect() call, not an extra label on
                that one's results. Off by default per camera (enableWeaponDetection) since
                the model's accuracy is unverified - see
                ai_models/weapon/SOURCE_AND_LICENSE.txt. Does not trigger localRecorderPtr the
                way the object-detection block above does - a weapon alert is already raised
                below regardless, and whether a weapon detection should also kick off a
                recording is a product decision, not something to add here unprompted.
                ===============================================================================
            */
            WeaponDetection* localWeaponPtr = nullptr;
            {
                std::lock_guard<std::mutex> lock(cams_mtx_);
                auto it = weaponDetectors_.find(cam.id);
                if (it == weaponDetectors_.end()) {
                    weaponDetectors_[cam.id] = std::make_unique<WeaponDetection>();
                    localWeaponPtr = weaponDetectors_[cam.id].get();

                    const cv::dnn::Net& globalWeaponNet = masterWeaponDetector_.getModel();
                    const std::vector<std::string>& globalWeaponLabels = masterWeaponDetector_.getClassNames();
                    const std::string globalWeaponName = masterWeaponDetector_.getActiveModelName();

                    if (!globalWeaponNet.empty()) {
                        localWeaponPtr->setModel(globalWeaponNet, globalWeaponLabels, globalWeaponName);
                        localWeaponPtr->setGlobalManager(&weaponIdentityMgr_);
                        LOG_CONTEXT(LogLevel::INFO,
                            "[Cam " + cam.id + "] Weapon detection instance created using global backend.", {});
                    } else {
                        LOG_CONTEXT(LogLevel::WARNING,
                            "[Cam " + cam.id + "] No global weapon detection model available.", {});
                    }
                } else {
                    localWeaponPtr = it->second.get();
                }
            }

            if (currentCam.enableWeaponDetection && localWeaponPtr) {
                const float weaponConfThreshold = 0.5f;
                // Throttled, not detect() directly - full YOLOv3 on CPU
                // unthrottled visibly slowed the camera down, same issue
                // object detection had before its own throttling was added.
                auto weapons = localWeaponPtr->detectThrottled(frame, cam.id, weaponConfThreshold, 0.4f, 5);
                localWeaponPtr->draw(frame, weapons);

                for (const auto& w : weapons) {
                    if (w.confidence < weaponConfThreshold) continue;

                    alerts_.raise(Alert{
                        "weapon",
                        "Possible weapon detected: " + w.label +
                            " (conf: " + std::to_string(static_cast<int>(w.confidence * 100)) + "%, unverified model)",
                        cam.id,
                        helperFunctions::getCurrentTimestamp()
                    });

                    // Critical is reserved for the weapon-watchlist match
                    // path inside GlobalDetectionProcessor::identify() (a
                    // recognized weapon matching a watchlist entry) - a
                    // raw, not-yet-cross-referenced detection is High
                    // instead, same severity RestrictedZoneMotion/
                    // RestrictedAreaUnknownFace use, still enough to reach
                    // VoiceAlarmChannel. Cooldown gate: same reasoning as
                    // lastRestrictedZoneAlarm_ - voice has no built-in
                    // rate-limiting, and a weapon persisting in frame would
                    // otherwise re-dispatch (and re-speak) every frame.
                    bool shouldSpeak = false;
                    {
                        std::lock_guard<std::mutex> lock(weaponAlarmMtx_);
                        auto now = std::chrono::steady_clock::now();
                        auto it = lastWeaponAlarm_.find(cam.id);
                        if (it == lastWeaponAlarm_.end() || now - it->second >= std::chrono::seconds(10)) {
                            lastWeaponAlarm_[cam.id] = now;
                            shouldSpeak = true;
                        }
                    }
                    if (shouldSpeak) {
                        AlarmDispatcher::getInstance().dispatch(AlarmEvent{
                            cam.id, cam.id, "WeaponDetected", helperFunctions::getCurrentTimestamp(),
                            "High", cam.name + " camera detected a possible " + w.label + "."
                        });
                    }
                }
            }

            /*
                ===============================================================================
                NOTE: VEHICLE DETECTION & LICENSE PLATE RECOGNITION SECTION
                This section is designed to be modular and efficient.  The VehicleMonitor class
                maintains its own internal state and model instances, allowing it to perform detection
            */
            VehicleMonitor* localVehiclePtr = nullptr;
            {
                std::lock_guard<std::mutex> lock(cams_mtx_);
                auto it = vehicleMonitors_.find(cam.id);
                if (it == vehicleMonitors_.end()) {
                    vehicleMonitors_[cam.id] = std::make_unique<VehicleMonitor>();
                    localVehiclePtr = vehicleMonitors_[cam.id].get();

                    // Initialize once when creating: use setModel instead of initialize(cam.id,...)
                    auto modelSettings = features_.get().modelSettings;
                    if (!localVehiclePtr->setModel(modelSettings.vehicleCascadePath,
                                                modelSettings.plateCascadePath,
                                                modelSettings.tessdataPath,
                                                "eng")) {
                        LOG_CONTEXT(LogLevel::ERR, "[Cam " + cam.id + "] Failed to set model for VehicleMonitor.", {});
                        localVehiclePtr = nullptr;
                    } else {
                        // Attach managers
                        localVehiclePtr->setAlertManager(&alerts_);
                        localVehiclePtr->setGlobalManager(&vehicleIdentityMgr_);
                        LOG_CONTEXT(LogLevel::INFO, "[Cam " + cam.id + "] VehicleMonitor model set.", {});
                    }
                } else {
                    localVehiclePtr = it->second.get();
                }
            }

            // Per-frame detection
            if (currentCam.enableVehicleDetection && localVehiclePtr) {
                std::vector<PlateResult> plateResults;
                if (localVehiclePtr->processFrame(frame, plateResults, cam.id,
                                                   currentCam.enableDatabaseLogging,
                                                   currentCam.trackUnknownVehicles,
                                                   currentCam.restrictUnknownVehicles)) {
                    for (const auto& res : plateResults) {
                        std::string alertMsg = "Plate detected: " + res.plateNumber + " (" + res.vehicleType + ")";
                        alerts_.raise(Alert{"vehicle", alertMsg, cam.id, helperFunctions::getCurrentTimestamp()});
                        LOG_CONTEXT(LogLevel::INFO, "[Cam " + cam.id + "] Vehicle plate: " + res.plateNumber + " [" + res.vehicleType + "]", {});
                    }
                }
            }
            
            /*
                ===============================================================================
                NOTE: EVENT RECORDING SECTION
                localRecorderPtr was already resolved and settings-synced up above (see EVENT
                RECORDER RESOLUTION), so this section is just the actual per-frame start/write/
                stop decision, plus handing off to recordingEventProcessor_ once a clip finishes
                (whether that stop was this "manual_recording" toggle, or the object-detection
                section above, or EventRecorder's own maxDuration_/maxFileSize_ limit inside
                writeFrame()) - detected as a recording -> not-recording transition rather than
                threading a callback through every possible stop site.
            */
            bool wasRecording = localRecorderPtr && localRecorderPtr->isRecording();
            if (localRecorderPtr) {
                if (currentCam.enableRecording) {
                    // Only open a new file when not already recording - this
                    // used to call startRecording() unconditionally every
                    // frame, which reopens a fresh VideoWriter (file handle +
                    // codec init) 30x/sec while recording is on, stalling
                    // this camera's whole pipeline and directly causing the
                    // display lag this was fixed for. getRecordingsPath()'s
                    // create_directories() call moves inside the guard too -
                    // it only needs to run once per recording session, not
                    // every frame.
                    if (!localRecorderPtr->isRecording()) {
                        // Manual toggle: unlimited duration, keeps recording
                        // to the same file until the user turns it off - no
                        // fixed-length auto-chunking, per explicit request.
                        localRecorderPtr->setMaxDuration(0);
                        localRecorderPtr->setAutoTriggered(false);
                        localRecorderPtr->setEventType("manual_recording");
                        localRecorderPtr->setFps(framesObserved >= kFpsWarmupFrames ? measuredFps : fps);
                        localRecorderPtr->startRecording(getRecordingsPath(), "manual_recording");
                    } else if (localRecorderPtr->isAutoTriggered()) {
                        // The manual toggle just turned on while an
                        // action-triggered auto-recording was already in
                        // progress - take it over: same file continues, but
                        // it's now unbounded and no longer subject to the
                        // 10-minute auto-stop. Manual always wins.
                        localRecorderPtr->setMaxDuration(0);
                        localRecorderPtr->setAutoTriggered(false);
                    }
                    localRecorderPtr->writeFrame(frame);
                } else if (localRecorderPtr->isRecording() && localRecorderPtr->isAutoTriggered()) {
                    // Action-triggered recording, toggle is off - this clip
                    // isn't governed by the toggle at all; it manages its own
                    // 10-minute lifecycle via maxDuration_ inside
                    // writeFrame(). Keep writing, don't stop it here.
                    localRecorderPtr->writeFrame(frame);
                } else if (localRecorderPtr->isRecording()) {
                    // Manual recording, toggle just went off - stop now.
                    localRecorderPtr->stopRecording();
                }
            }

            if (localRecorderPtr && wasRecording && !localRecorderPtr->isRecording()) {
                recordingEventProcessor_.raise(cam.id, localRecorderPtr->getLastRecordingSummary());
            }

            // Hand off this frame for display on the main thread - overwrites
            // this camera's previous pending frame rather than queuing
            // behind it, so the display can never fall behind and build up
            // a backlog of stale frames (see latestFrames_'s declaration).
            {
                std::unique_lock<std::mutex> lock(displayQueueMtx_);
                latestFrames_[cam.id] = DisplayFrame{cam.name, frame.clone(), cam.id};
                displayQueueCV_.notify_one();
            }

            // CLOUD STREAMING: hand this same fully-annotated frame (every
            // detector above has already drawn into it) to CloudClient for
            // the dashboard's live-view tab. Wall-clock-interval throttled
            // (not the frame-count-modulo pattern used elsewhere, e.g.
            // ObjectDetection::detectThrottled) because streaming cares
            // about delivered cadence to a viewer, and cameras differ in
            // native fps. Gated only on cloudClient_ existing - whether
            // cloud is actually enabled is CloudClient's own decision (see
            // CloudClient::streamLoop()), so postFrame() is cheap and safe
            // to call unconditionally here.
            if (cloudClient_) {
                try {
                    constexpr auto kMinStreamInterval = std::chrono::milliseconds(150);
                    auto nowStream = std::chrono::steady_clock::now();
                    if (nowStream - lastStreamSentAt >= kMinStreamInterval) {
                        std::vector<uchar> jpegBuf;
                        std::vector<int> encodeParams{cv::IMWRITE_JPEG_QUALITY, 75};
                        if (cv::imencode(".jpg", frame, jpegBuf, encodeParams)) {
                            cloudClient_->postFrame(cam.id, std::move(jpegBuf));
                            lastStreamSentAt = nowStream;
                        }

                        // Face bounding-box metadata, same cadence as the
                        // frame it describes. faceRecPtr is null when face
                        // recognition isn't enabled for this camera -- an
                        // empty "boxes" array in that case, not a skipped
                        // send, so a dashboard viewer that had boxes from
                        // before the feature was toggled off sees them
                        // clear rather than stick around stale.
                        nlohmann::json boxesJson = nlohmann::json::array();
                        if (faceRecPtr) {
                            for (const auto& box : faceRecPtr->getFaceBoxes()) {
                                boxesJson.push_back({
                                    {"id", box.entityId},
                                    {"x", box.rect.x},
                                    {"y", box.rect.y},
                                    {"w", box.rect.width},
                                    {"h", box.rect.height},
                                    {"label", box.label},
                                    {"status", box.category == BoundingBoxAnnotator::PersonCategory::Watchlist ? "watchlist"
                                             : box.category == BoundingBoxAnnotator::PersonCategory::Authorized ? "authorized"
                                             : "unknown"},
                                    {"description", box.description},
                                });
                            }
                        }
                        cloudClient_->postFrameMetadata(cam.id, nlohmann::json{{"cameraId", cam.id}, {"boxes", boxesJson}}.dump());
                    }
                } catch (const std::exception& e) {
                    LOG_CONTEXT(LogLevel::WARNING, "[Cam " + cam.id + "] cloud stream encode failed: " + e.what(), {});
                } catch (...) {
                    LOG_CONTEXT(LogLevel::WARNING, "[Cam " + cam.id + "] cloud stream encode failed: unknown exception", {});
                }
            }
        }

    cap.release();
    
    } catch (const std::exception& e) {
        LOG_CONTEXT(LogLevel::ERR, "[Cam " + cam.id + "] Exception: " + std::string(e.what()), {});
    } catch (...) {
        LOG_CONTEXT(LogLevel::ERR, "[Cam " + cam.id + "] Unknown exception.", {});
    }
}
    