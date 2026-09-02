#include "system_interface.hpp"
#include "alarm/dispatch/AlarmDispatcher.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <thread>

systemInterface& systemInterface::getInstance() {
    static systemInterface instance;
    return instance;
}

SystemConfig systemInterface::loadOrDefaultConfig() {
    SystemConfig cfg;
    if (std::filesystem::exists("surveillance_config.json")) {
        if (loadSystemConfig("surveillance_config.json", cfg)) {
            std::cout << "[INFO] Loaded config from surveillance_config.json" << std::endl;
        } else {
            std::cerr << "[ERROR] Failed to load config, using defaults" << std::endl;
        }
    } else {
        std::cout << "[INFO] No saved config found, creating with defaults" << std::endl;
        cfg.dbUri = "surveillance.db";
        cfg.displayWidth = 800;
        cfg.displayHeight = 600;
        cfg.modelSettings = ModelSettings{};
    }
    cfg.modelSettings.normalizeModelPaths();
    return cfg;
}

std::string systemInterface::deriveStationId(const SystemConfig& cfg) {
    // Reuses the same cloud hardware token already used to authenticate
    // this station to smartsense-core (see cloud_client.cpp) -- unique per
    // station, already persisted across restarts. See SystemConfig's doc
    // comments in config.hpp.
    return cfg.cloudHardwareToken.empty() ? "unconfigured-station" : cfg.cloudHardwareToken;
}

Database& systemInterface::connectDatabase(const SystemConfig& cfg) {
    Database& db = Database::getInstance("systemAssets");
    if (!db.connect(cfg.dbUri)) {
        throw std::runtime_error("Failed to connect to database: " + cfg.dbUri);
    }
    return db;
}

std::vector<std::unique_ptr<TtsProvider>> systemInterface::makeVoiceProviders(FeatureConfigManager& features) {
    // Providers are tried in the order added; only OfflineTtsProvider (SAPI)
    // is wired up today. A cloud provider (e.g. Azure) added later goes at
    // the FRONT of this vector so it's tried first, with OfflineTtsProvider
    // remaining the fallback if it fails.
    std::vector<std::unique_ptr<TtsProvider>> providers;
    providers.push_back(std::make_unique<OfflineTtsProvider>(features));
    return providers;
}

std::unique_ptr<LocalAdminServer> systemInterface::maybeStartLocalAdmin(FeatureConfigManager& features, CloudClient& cloudClient) {
    // Read once at startup, not live-reconfigurable -- see SystemConfig's
    // doc comment on localAdminEnabled.
    SystemConfig startupCfg = features.get();
    if (!startupCfg.localAdminEnabled) {
        return nullptr;
    }
    return std::make_unique<LocalAdminServer>(features, cloudClient, startupCfg.localAdminPort);
}

systemInterface::systemInterface()
    : cfg_(loadOrDefaultConfig())
    , db_(connectDatabase(cfg_))
    , stationId_(deriveStationId(cfg_))
    , identityMgr_(128, std::thread::hardware_concurrency(), cfg_.kafkaBrokerAddress,
                   stationId_ + "-face-processor", stationId_, cfg_.accountId,
                   "camera.embeddings.face." + cfg_.accountId)
    , vehicleIdentityMgr_(128, std::thread::hardware_concurrency(), cfg_.kafkaBrokerAddress,
                          stationId_ + "-vehicle-processor", stationId_, cfg_.accountId,
                          "camera.embeddings.plate." + cfg_.accountId)
    , weaponIdentityMgr_(128, std::thread::hardware_concurrency(), cfg_.kafkaBrokerAddress,
                         stationId_ + "-weapon-processor", stationId_, cfg_.accountId,
                         "camera.embeddings.weapon." + cfg_.accountId)
    , features_(cfg_, identityMgr_, vehicleIdentityMgr_, weaponIdentityMgr_, db_, "surveillance_config.json")
    , voiceAlarm_(makeVoiceProviders(features_), features_)
    , beepAlarm_(features_)
    , cloudClient_(features_)
    , localAdminServer_(maybeStartLocalAdmin(features_, cloudClient_))
    , controller_(features_, identityMgr_, vehicleIdentityMgr_, weaponIdentityMgr_, db_, &cloudClient_)
    , commandQueueConsumer_(features_)
    , lifecycleWatcher_(controller_)
{
    // Lets identify() resolve a camera id (e.g. "cam-entrance") to that
    // camera's display name for cameraRegion, instead of the fixed
    // placeholder every tracked face/plate/object used to get regardless of
    // which camera actually saw them. Wired here (not passed into the
    // GlobalDetectionProcessor constructors above) because features_
    // doesn't exist yet at that point -- same attach-after-construction
    // pattern as hydrateKnownIdentities() below.
    auto cameraNameResolver = [this](const std::string& camId) -> std::string {
        CameraConfig cam;
        return features_.getCameraConfig(camId, cam) ? cam.name : "";
    };
    identityMgr_.setCameraNameResolver(cameraNameResolver);
    vehicleIdentityMgr_.setCameraNameResolver(cameraNameResolver);
    weaponIdentityMgr_.setCameraNameResolver(cameraNameResolver);

    // Cross-station embedding fan-out consumer -- FACE only. Vehicle/weapon
    // aren't wired here because their published "embeddings" aren't real
    // feature vectors yet (see serializeEmbedding()'s doc comment in
    // kafkaManager.hpp). Safe no-op if the reachability probe in
    // GlobalDetectionProcessor's constructor already disabled Kafka for
    // this session.
    identityMgr_.startWorkerLoop([this](const cv::Mat& embedding, const std::string& cameraId,
                                         const std::string& originStationId, const std::string& /*originAccountId*/,
                                         const std::string& sourceProfileId) {
        identityMgr_.mergeRemoteEmbedding(embedding, EmbeddingType::FACE, cameraId, originStationId, sourceProfileId);
    });

    // Re-populates both identity managers' FAISS indices from whatever's
    // already in the database, so a restart doesn't forget previously
    // captured faces/plates. Non-fatal -- logged, not thrown.
    if (!features_.hydrateKnownIdentities()) {
        std::cerr << "[WARNING] Failed to fully hydrate known identities from database." << std::endl;
    }

    // Alarm channels must be registered BEFORE controller_.init() below --
    // its camera-readiness pass can dispatch an alarm on a verification
    // failure for any camera already present in a loaded config, and that
    // must not fire into an empty channel list.
    AlarmDispatcher::getInstance().registerChannel("voice",
        [this](const AlarmEvent& e) { voiceAlarm_.onAlarm(e); });
    AlarmDispatcher::getInstance().registerChannel("beep",
        [this](const AlarmEvent& e) { beepAlarm_.onAlarm(e); });
    AlarmDispatcher::getInstance().registerChannel("cloud",
        [this](const AlarmEvent& e) { cloudClient_.onAlarm(e); });

    if (!controller_.init()) {
        throw std::runtime_error("Surveillance controller initialization failed.");
    }
}

void systemInterface::run() {
    controller_.run();
}

FeatureConfigManager& systemInterface::features() {
    return features_;
}
