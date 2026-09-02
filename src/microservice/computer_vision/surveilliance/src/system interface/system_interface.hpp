#pragma once

#include "config_file/config.hpp"
#include "cpu/global_detection_processor/global_detection_processor.hpp"
#include "database/database.hpp"
#include "alarm/voice/voice_alarm_channel.hpp"
#include "alarm/voice/offline_tts_provider.hpp"
#include "alarm/beep/beep_alarm_channel.hpp"
#include "cloud/cloud_client.hpp"
#include "local_admin/local_admin_server.hpp"
#include "surveillance/surveillance_controller.hpp"
#include "command_queue/command_queue_consumer.hpp"
#include "command_queue/surveillance_lifecycle_watcher.hpp"

#include <memory>
#include <string>
#include <vector>

/**
 * @struct systemInterface
 * @brief Singleton entry point that boots and owns the whole surveillance
 * engine -- the database connection, the three identity managers,
 * FeatureConfigManager, the alarm channels, CloudClient, the local admin
 * server, and SurveillanceController.
 *
 * The default constructor does ONLY the setup that never needs a value from
 * a caller: load config from disk (or defaults), connect the database,
 * build the identity managers, construct FeatureConfigManager, wire the
 * camera-name resolver, start the cross-station embedding consumer,
 * hydrate known identities, register the alarm channels, construct
 * CloudClient (safe with cloud sync disabled by default) and the local
 * admin server, then construct and init() SurveillanceController.
 *
 * Anything that genuinely needs a real value supplied later -- cloud
 * settings, camera definitions, admin-user registration, motion zones,
 * feature toggles -- is deliberately NOT called here. That surface already
 * exists as FeatureConfigManager's own public API (see features() below);
 * whoever has a real value to give it calls it directly, whenever that is,
 * the same way any other API is called.
 *
 * Singleton: exactly one instance for the process lifetime, so this file's
 * own main(), and later the native SDL2 local UI or the ItemManager-side
 * thin interface, all reach the SAME running engine rather than each
 * standing up their own copy of it. Copy construction and copy assignment
 * are deleted accordingly.
 */
struct systemInterface {
public:
    /**
     * @brief Access the single shared instance, constructing it (and
     * therefore booting the whole engine) on first call.
     *
     * @throws std::runtime_error if a required startup step fails (database
     * connect, SurveillanceController::init()) -- matches this codebase's
     * existing convention (see main.cpp's outer try/catch) of surfacing
     * startup failures as exceptions rather than swallowing them.
     */
    static systemInterface& getInstance();

    systemInterface(const systemInterface&) = delete;
    systemInterface& operator=(const systemInterface&) = delete;

    /**
     * @brief Run the surveillance engine. Blocks the calling thread until
     * shutdown -- forwards directly to SurveillanceController::run().
     */
    void run();

    /**
     * @brief The real interface surface for anything that needs a value
     * supplied later -- cloud settings, camera add/update, admin-user
     * registration (via features().getDBManager()), motion zones, feature
     * toggles, and everything else FeatureConfigManager already exposes.
     */
    FeatureConfigManager& features();

private:
    systemInterface();
    ~systemInterface() = default;

    // --- Construction-order helpers ---------------------------------------
    // Kept as private statics (not free functions) so they're easy to find
    // next to the members whose initializer expressions call them.
    static SystemConfig loadOrDefaultConfig();
    static std::string deriveStationId(const SystemConfig& cfg);
    static Database& connectDatabase(const SystemConfig& cfg);
    static std::vector<std::unique_ptr<TtsProvider>> makeVoiceProviders(FeatureConfigManager& features);
    static std::unique_ptr<LocalAdminServer> maybeStartLocalAdmin(FeatureConfigManager& features, CloudClient& cloudClient);

    // --- Members -----------------------------------------------------------
    // Declaration order IS construction order in C++ -- this must match the
    // real dependency chain below (each member depends on the ones before
    // it), the same ordering main.cpp's local variables originally followed.
    SystemConfig cfg_;
    Database& db_;
    std::string stationId_;
    GlobalDetectionProcessor<FaceEntry, DBTrackedFace> identityMgr_;
    GlobalDetectionProcessor<VehicleEntry, DBTrackedPlate> vehicleIdentityMgr_;
    GlobalDetectionProcessor<WeaponEntry, DBTrackedWeapon> weaponIdentityMgr_;
    FeatureConfigManager features_;
    VoiceAlarmChannel voiceAlarm_;
    BeepAlarmChannel beepAlarm_;
    CloudClient cloudClient_;
    std::unique_ptr<LocalAdminServer> localAdminServer_;
    SurveillanceController controller_;

    // Drains the command queue the ItemManager-side Surveillance/
    // ItemWrapper/ItemManager passthrough layer writes to and applies each
    // command to features_ -- the reader half Surveillance.hpp's own doc
    // comment always assumed would exist. Started last: it only needs
    // features_ to already exist, and starting it after everything else is
    // constructed means it can never observe a partially-initialized engine.
    CommandQueueConsumer commandQueueConsumer_;

    // Watches Surveillance::sentinelPath() (ItemManager's "should be
    // running" signal) and drives a graceful SurveillanceController shutdown
    // on a present -> absent transition -- see its own class comment for why
    // this needs controller_ to already be constructed (and init()'d) first.
    // Constructed last, same reasoning as commandQueueConsumer_ above.
    SurveillanceLifecycleWatcher lifecycleWatcher_;
};
