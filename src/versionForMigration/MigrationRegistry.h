#pragma once
#ifndef MIGRATION_REGISTRY_H
#define MIGRATION_REGISTRY_H

#include <functional>
#include <string>
#include <map>
#include <unordered_map>
#include <vector>
#include <iostream>
#include <nlohmann/json.hpp>

// :::Forward declaration of MigrationRegistry class
// :::This class is responsible for managing migrations of items in the system.
// :::It allows registering versions, migration functions, and upgrading items to the latest version.
// **************************************************************************************************
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

class MigrationRegistry {
    public:
        ~MigrationRegistry() {
            latestVersions.clear();
            migrations.clear();
            logs.clear();
        }
    
        using MigrationFn = std::function<nlohmann::json(const nlohmann::json&)>;
        static constexpr int kMaxMigrationDepth = 10;
    
        void registerVersion(const std::string& typeName, int latest);
        void registerMigration(const std::string& typeName, int fromVersion, MigrationFn fn);
        int getLatestVersion(const std::string& typeName) const;
        nlohmann::json upgradeToLatest(const std::string& typeName, int currentVersion, const nlohmann::json& data) const;
    
        // Log API
        void printMigrationLog() const;
        void clearMigrationLog();
    
    private:
        std::unordered_map<std::string, int> latestVersions;
        std::unordered_map<std::string, std::map<int, MigrationFn>> migrations;
        mutable std::vector<std::string> logs;
    };
    
    #endif // MIGRATION_REGISTRY_H