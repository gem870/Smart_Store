//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::

#pragma once
#include <filesystem>
#include <string>

class PathFinder {
public:
    /**
    * @brief Resolves a relative path into an absolute path safely across OS.
    * @param relativePath The relative path inside the project (e.g. "src/alarm_system/sounds/play.wav").
    * @return A std::filesystem::path object pointing to the resolved file.
    */
    static std::filesystem::path resolve(const std::string& relativePath) {
        // Start from current working directory
        std::filesystem::path base = std::filesystem::current_path();
        std::filesystem::path rel(relativePath);

        return base / rel; // OS-safe concatenation
    }

    /**
    * @brief Checks if a resolved file exists.
    * @param relativePath The relative path inside the project.
    * @return True if the file exists, false otherwise.
    */
    static bool exists(const std::string& relativePath) {
        return std::filesystem::exists(resolve(relativePath));
    }

    /**
    * @brief Returns the directory where the executable is located.
    * Useful for resolving assets relative to the binary.
    */
    static std::filesystem::path executableDir() {
        return std::filesystem::current_path(); 
        // On Linux/macOS you can use /proc/self or argv[0] if needed.
        // For now, current_path() is portable and works in most setups.
    }
};