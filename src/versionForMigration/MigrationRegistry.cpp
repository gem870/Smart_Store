#include "MigrationRegistry.h"

// ::::| MigrationRegistry class for managing item migrations
// **********************************************************


constexpr int MigrationRegistry::kMaxMigrationDepth;

void MigrationRegistry::registerVersion(const std::string& typeName, int latest) {
    latestVersions[typeName] = latest;

    if (latestVersions.size() > kMaxMigrationDepth) {
        auto it = latestVersions.begin();
        std::advance(it, latestVersions.size() - kMaxMigrationDepth);
        latestVersions.erase(latestVersions.begin(), it);
    }
}

void MigrationRegistry::registerMigration(const std::string& typeName, int fromVersion, MigrationFn fn) {
    auto& chain = migrations[typeName];
    chain[fromVersion] = std::move(fn);

    while (static_cast<int>(chain.size()) > kMaxMigrationDepth) {
        chain.erase(chain.begin());
    }
}

int MigrationRegistry::getLatestVersion(const std::string& typeName) const {
    auto it = latestVersions.find(typeName);
    return (it != latestVersions.end()) ? it->second : 1;
}

nlohmann::json MigrationRegistry::upgradeToLatest(const std::string& typeName, int currentVersion, const nlohmann::json& data) const {
    auto upgraded = data;
    int latest = getLatestVersion(typeName);
    auto it = migrations.find(typeName);
    if (it == migrations.end()) return upgraded;

    int depth = 0;
    while (currentVersion < latest) {
        auto fnIt = it->second.find(currentVersion);
        if (fnIt == it->second.end()) break;
        upgraded = fnIt->second(upgraded);

        logs.push_back("[MIGRATION] Type: " + typeName + " | v" + std::to_string(currentVersion) + " -> v" + std::to_string(currentVersion + 1));

        currentVersion++;
        if (++depth > kMaxMigrationDepth) break;
    }
    return upgraded;
}

void MigrationRegistry::printMigrationLog() const {
    for (const auto& entry : logs) {
        std::cout << entry << std::endl;
    }
}

void MigrationRegistry::clearMigrationLog() {
    logs.clear();
}
