

#ifndef SURVEILLANCE_CONTROLLER_HPP
#define SURVEILLANCE_CONTROLLER_HPP

#pragma once

#include <opencv2/opencv.hpp>
#include <thread>
#include <vector>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <atomic>
#include <chrono>
#include "config_file/config.hpp"
#include "object_detection/weapon_detection.hpp"
#include "cpu/action_processor/action_event_processor.hpp"
#include "cpu/recording_processor/recording_event_processor.hpp"

// Forward-declared, not included - only a pointer is stored here; the .cpp
// includes cloud_client.hpp where the full definition is needed.
class CloudClient;

// SurveillanceController is a hidden engine: it has no camera-management API
// of its own. FeatureConfigManager is the system's single entry point, at
// both compile time (its type is what callers - main.cpp, a future API/UI
// layer - construct and hold) and runtime (its live state is what this
// engine reacts to). To add/remove a camera, callers mutate the same
// FeatureConfigManager instance this controller was given directly -
// features.addCamera(...) / features.removeCamera(...) - and the
// reconciliation loop below notices and spawns/tears down the worker thread
// + window, typically within ~1s. The controller itself only ever starts,
// runs, and stops.
class SurveillanceController {
public:
    SurveillanceController(FeatureConfigManager& features,
                            GlobalDetectionProcessor<FaceEntry, DBTrackedFace>& identityMgr,
                            GlobalDetectionProcessor<VehicleEntry, DBTrackedPlate>& vehicleIdentityMgr,
                            GlobalDetectionProcessor<WeaponEntry, DBTrackedWeapon>& weaponIdentityMgr,
                            Database& db,
                            CloudClient* cloudClient = nullptr);
    ~SurveillanceController();

    // Initialize subsystems (DB, face recognition, object detection, alerts)
    bool init();

    // Multi-camera orchestration
    void run();

    // Stop all camera threads
    void stop();

    // Thread-safe external shutdown request - lets a caller on any thread
    // (e.g. SurveillanceLifecycleWatcher, on its own poll thread) trip the
    // same exit path mainDisplayLoop()'s ESC handling already takes, without
    // touching running_/OpenCV window state directly from a foreign thread.
    // See mainDisplayLoop()'s loop condition and both cv::waitKey() checks.
    void requestExternalStop();

private:
    // Worker for each camera. disconnectedFlag is set by this function
    // itself (not the caller, unlike stopFlag) right before returning if it
    // gives up after sustained frame-read failure mid-session - see its doc
    // comment at the top of the function body for why this is distinct from
    // a caller-driven stop.
    //
    // skipInitialVerify: true only when reconcileCameraWorkers() is
    // respawning this worker immediately after probationLoop() itself just
    // confirmed the camera live (see recentlyVerifiedCameras_) - skips this
    // function's own redundant re-verification of the exact same camera a
    // moment later, cutting reconnect latency roughly in half. False (the
    // normal case) for every other spawn reason, where no recent
    // verification can be assumed.
    void processCamera(const CameraConfig& cam, std::shared_ptr<std::atomic<bool>> stopFlag,
                       std::shared_ptr<std::atomic<bool>> disconnectedFlag,
                       bool skipInitialVerify = false);

    // Reconciles running worker threads/windows against features_'s live
    // camera list: spawns workers for newly-added cameras, stops+tears down
    // workers for removed ones. Must be called from the main thread (creates/
    // destroys OpenCV windows). Called once at the top of run(), then
    // periodically from mainDisplayLoop().
    void reconcileCameraWorkers();
    void createCameraWindow(const CameraConfig& cam);

    // Mouse-click support for the bounding-box "click for details" feature
    // (see BoundingBoxAnnotator/FaceRecognition::findFaceAt()). OpenCV's C
    // API needs a static/free-function callback + a void* userdata, so
    // onMouseTrampoline() is that static entry point, resolving back to a
    // real member function via the ClickContext it's given at registration
    // time in createCameraWindow(). Fires on the main thread (the same
    // thread that already owns windowNames_/cams_mtx_ - see
    // mainDisplayLoop()'s cv::waitKey() calls, which pump HighGUI's event
    // queue), so no additional synchronization is needed beyond the
    // existing faceRecMtx_-guarded lookup inside handleCameraClick().
    struct ClickContext {
        SurveillanceController* self;
        std::string camId;
    };
    static void onMouseTrampoline(int event, int x, int y, int flags, void* userdata);
    void handleCameraClick(const std::string& camId, int x, int y);

    // Hands a stopped worker thread off to the reaper instead of joining it
    // inline. reconcileCameraWorkers() calls this from the main thread after
    // signaling stopFlags_ - the actual (potentially long) join happens on
    // reaperThread_ instead, so a camera stuck in a blocking cap.read() can't
    // freeze the main display loop or the other cameras' worker threads
    // (which share cams_mtx_ with this teardown path).
    void retireWorker(std::thread worker);
    void reaperLoop();

    // Runs on its own background thread (never the main/display thread -
    // each check calls verifyCameraSource(), which can block for real
    // hundreds of ms to seconds; doing that from reconcileCameraWorkers()'s
    // 1s main-thread tick would reintroduce the exact stall this whole
    // mechanism exists to avoid). Re-verifies every camera in
    // probationCameras_ every kProbationRecheckInterval (a few seconds) -
    // deliberately frequent, so reconnecting a camera is noticed and
    // restored close to immediately rather than after a long wait. A
    // success lets reconcileCameraWorkers() naturally respawn its worker;
    // only once a camera has been trying continuously for
    // kProbationMaxDuration with no success does it move to pendingCameras
    // via plugOutPendingCamera() - a one-way transition from here, matching
    // a user-initiated "remove": the system decides a camera needs to go to
    // pending, but only a deliberate plugInPendingCamera() call brings one
    // back, never an automatic sweep.
    void probationLoop();

    // --- Members ---
    // Injected, externally owned (constructed once at the top level - see
    // main.cpp - and shared with whatever else needs to manage cameras).
    FeatureConfigManager& features_;
    GlobalDetectionProcessor<FaceEntry, DBTrackedFace>& identityMgr_;
    GlobalDetectionProcessor<VehicleEntry, DBTrackedPlate>& vehicleIdentityMgr_;
    GlobalDetectionProcessor<WeaponEntry, DBTrackedWeapon>& weaponIdentityMgr_;
    // Nullable - a null cloudClient_ (the default) means frame streaming is
    // simply skipped in processCamera(), same as running with no cloud
    // integration configured at all.
    CloudClient* cloudClient_ = nullptr;

    AlertManager alerts_;
    // Action detection's own alert path - see ActionEventProcessor's class
    // comment for why this isn't a GlobalDetectionProcessor instantiation.
    // Unlike alerts_ (a synchronous console log line, no persistence),
    // this actually writes to the events table and dispatches through
    // AlarmDispatcher, off the calling thread.
    ActionEventProcessor actionEventProcessor_;
    // Same reasoning, for completed EventRecorder clips - see
    // RecordingEventProcessor's class comment.
    RecordingEventProcessor recordingEventProcessor_;
    bool running_ = false;
    // Set by requestExternalStop() from a foreign thread; checked alongside
    // running_ in mainDisplayLoop()'s loop condition - a second way to trip
    // the exact same shutdown path ESC already takes, without changing
    // running_'s own single-thread-owned semantics.
    std::atomic<bool> externalStopRequested_{false};

    FaceRecognition faceRec_;
    ObjectDetection masterObjectDetector_;
    // Separate model from masterObjectDetector_ above, not an additional
    // class on it - coco.names has no weapon/gun class, so this runs its
    // own YOLO forward pass alongside the general one. See
    // ModelSettings::weapon*Path / ai_models/weapon/SOURCE_AND_LICENSE.txt.
    // WeaponDetection (not ObjectDetection) because it also needs to publish
    // into weaponIdentityMgr_ - see weapon_detection.hpp's class comment for
    // why ObjectDetection's own processor hook can't be reused for that.
    WeaponDetection masterWeaponDetector_;
    VehicleMonitor  masterVehicleDetector_;
    MotionDetection masterMotionDetector;
    ActionDetection masterActionDetector_;


    std::unordered_map<std::string, std::unique_ptr<EventRecorder>>  cameraRecorders_;
    std::unordered_map<std::string, std::unique_ptr<NightVision>> nightVisions_;
    std::unordered_map<std::string, std::unique_ptr<ObjectDetection>>  objectDetectors_;
    std::unordered_map<std::string, std::unique_ptr<WeaponDetection>>  weaponDetectors_;
    std::unordered_map<std::string, std::unique_ptr<ActionDetection>>  actionDetectors_;
    std::unordered_map<std::string, std::unique_ptr<MotionDetection>>  cameraMotions_;
    std::unordered_map<std::string, std::unique_ptr<VehicleMonitor>> vehicleMonitors_;
    std::unordered_map<std::string, std::unique_ptr<FaceRecognition>> faceRecognizers_;

    std::unordered_map<std::string, std::thread> workers_;
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> stopFlags_;
    // Worker -> reconcileCameraWorkers() direction (opposite of stopFlags_):
    // set true by processCamera() itself when it gives up after sustained
    // mid-session read failure, so the next reconciliation tick can tell
    // "this worker finished because the camera disconnected" apart from a
    // caller-driven stop. Guarded by cams_mtx_, same as stopFlags_.
    std::unordered_map<std::string, std::shared_ptr<std::atomic<bool>>> disconnectedFlags_;
    std::unordered_map<std::string, std::string> windowNames_; // camId -> OpenCV window name

    // camId -> the CameraConfig each running worker was spawned with. Used by
    // reconcileCameraWorkers() to detect source/width/height/name edits made
    // via FeatureConfigManager::updateCamera() - a running cv::VideoCapture
    // can't be hot-swapped, so a changed camera is torn down and respawned
    // rather than updated in place.
    std::unordered_map<std::string, CameraConfig> workerConfigs_;

    // camId -> which tile slot its window occupies (see createCameraWindow()).
    // A slot is freed (erased here) whenever that camera's window is
    // destroyed, so a respawned/reconnected camera - whether from a config
    // change, probation exit, or anything else - reuses a free slot instead
    // of tiling ever further away from the original grid on every restart.
    std::unordered_map<std::string, size_t> windowTileIndex_;

    // camId -> the userdata handed to that camera's cv::setMouseCallback()
    // registration (see createCameraWindow()/onMouseTrampoline()). Only
    // touched from inside createCameraWindow()/reconcileCameraWorkers(),
    // already under cams_mtx_ - unique_ptr so the ClickContext's address
    // stays stable for OpenCV to hold onto for the window's lifetime.
    std::unordered_map<std::string, std::unique_ptr<ClickContext>> clickContexts_;


    std::mutex liveStreamMtx_;
    std::mutex cams_mtx_;
    std::mutex nightVisions_mtx_;
    std::mutex recorders_mtx_;
    std::mutex faceRecMtx_;

    // Per-camera cooldown for the RestrictedZoneMotion voice alarm - unlike
    // the routine motion beep (rate-limited centrally by BeepAlarmChannel
    // itself), VoiceAlarmChannel has no built-in rate-limiting at all, since
    // every other voice-worthy event (a resolved identity, a camera
    // lifecycle change) is naturally infrequent. Motion inside a restricted
    // zone is not - without this, it re-dispatches every frame motion
    // persists, backing VoiceAlarmChannel's queue up into a continuous
    // drone. Guarded separately from cams_mtx_ since it's only ever touched
    // by each camera's own worker thread, one key each - no real
    // contention, just needs to be map-safe.
    std::map<std::string, std::chrono::steady_clock::time_point> lastRestrictedZoneAlarm_;
    std::mutex restrictedZoneAlarmMtx_;

    // Per-camera cooldown for the weapon-detection voice alarm - same
    // reasoning as lastRestrictedZoneAlarm_ above: VoiceAlarmChannel has no
    // built-in rate-limiting, and a weapon persisting in frame would
    // otherwise re-dispatch every single frame it's detected. 10s per
    // camera, matching the restricted-zone cooldown's cadence.
    std::map<std::string, std::chrono::steady_clock::time_point> lastWeaponAlarm_;
    std::mutex weaponAlarmMtx_;

    // Async reaper: joining a torn-down camera's thread can block (no
    // timeout on cv::VideoCapture::read() today - see ARCHITECTURE.md §7 /
    // the discussion this was built from), so the join happens here instead
    // of inline in reconcileCameraWorkers() on the main thread. Mirrors the
    // dedicated-drain-thread pattern GlobalDetectionProcessor already uses
    // for its disk-write queue (diskWorkerThread_/diskQueue_/diskMtx_/diskCv_).
    std::deque<std::thread> retiring_;
    std::mutex retireMtx_;
    std::condition_variable retireCv_;
    std::thread reaperThread_;
    std::atomic<bool> reaperRunning_{true};

    // Runtime-only bookkeeping for cameras that disconnected mid-session
    // (see probationLoop()'s doc comment) - deliberately not persisted to
    // CameraConfig/surveillance_config.json, unlike consecutiveFailedOpens:
    // this tracks an in-progress retry cycle for the current process's
    // lifetime, not a fact that should survive a restart. A full restart
    // already goes through SurveillanceController::init()'s own, separate
    // startup verification instead.
    struct ProbationEntry {
        std::chrono::steady_clock::time_point enteredAt;    // for the overall give-up timer
        std::chrono::steady_clock::time_point nextCheckTime; // for the frequent recheck cadence
    };
    std::unordered_map<std::string, ProbationEntry> probationCameras_;
    std::mutex probationMtx_;
    std::condition_variable probationCv_;
    std::thread probationThread_;
    std::atomic<bool> probationRunning_{true};

    // camIds probationLoop() just confirmed live (moments ago, on its own
    // thread) and removed from probationCameras_ - reconcileCameraWorkers()
    // checks and consumes (erases) this on its next spawn pass, and if
    // present, tells the freshly-spawned processCamera() to skip its own
    // redundant re-verification of the same camera (see
    // processCamera()'s skipInitialVerify doc comment). Guarded by
    // probationMtx_, same as probationCameras_ - both are only ever touched
    // by probationLoop() and reconcileCameraWorkers(), which already
    // coordinate through that mutex.
    std::unordered_set<std::string> recentlyVerifiedCameras_;

    // Display frame hand-off for main thread rendering. Keyed by cameraId
    // rather than a FIFO queue - a worker overwrites its camera's single
    // pending entry every frame instead of appending, so there is never a
    // backlog to work through: mainDisplayLoop() always renders whatever is
    // most recent, and a camera that briefly can't keep up just skips
    // straight to its next real frame instead of the display catching up
    // through a queue of stale ones first.
    struct DisplayFrame {
        std::string windowName;
        cv::Mat frame;
        std::string cameraId;
    };
    std::unordered_map<std::string, DisplayFrame> latestFrames_;
    std::mutex displayQueueMtx_;
    std::condition_variable displayQueueCV_;

    void mainDisplayLoop();

    std::string getRecordingsPath() const;
};

#endif // SURVEILLANCE_CONTROLLER_HPP


