#pragma once
#ifndef ALARMSYSTEM_HPP 
#define ALARMSYSTEM_HPP


#include <opencv2/core.hpp>
#include <string>
#include <opencv2/highgui.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/objdetect.hpp>
#include <iostream>
#include "interface/BaseMicroservice.hpp"
#include <err_log/Logger.hpp>


/**
 * @brief QR Code Scanner using OpenCV's QRCodeDetector.
 *
 * Provides configuration setters/getters, default config, and reset functionality.
 * Supports scanning QR codes from both image files and live camera input.
 */
class QRCodeScanner : public BaseMicroservice {
public:
    /**
     * @brief Construct a new QRCodeScanner object with default configuration.
     *
     * Default configuration:
     * - cameraIndex = 0 (default webcam)
     * - previewEnabled = true (show preview window)
     */
    QRCodeScanner();

    /**
     * @brief Destroy the QRCodeScanner object.
     */
    ~QRCodeScanner();

    // --- Config setters ---

    /**
     * @brief Set the camera index to use for scanning.
     *
     * @param index Camera device index (0 = default webcam).
     */
    void setCameraIndex(int index);

    /**
     * @brief Enable or disable preview window during camera scanning.
     *
     * @param enabled True to show preview window, false to disable.
     */
    void setPreviewEnabled(bool enabled);

    // --- Config getters ---

    /**
     * @brief Get the current camera index.
     *
     * @return int Camera device index.
     */
    int getCameraIndex() const;

    /**
     * @brief Check if preview window is enabled.
     *
     * @return true if preview is enabled, false otherwise.
     */
    bool isPreviewEnabled() const;

    // --- Config reset ---

    /**
     * @brief Reset configuration to default values.
     *
     * Default values:
     * - cameraIndex = 0
     * - previewEnabled = true
     */
    void resetConfig();

    // --- QR code scanning ---

    /**
     * @brief Scan a QR code from an image file.
     *
     * @param imagePath Path to the image containing a QR code.
     * @return Decoded QR code string if found, empty string otherwise.
     */
    std::string scanFromFile(const std::string& imagePath);

    /**
     * @brief Scan a QR code using live camera input.
     *
     * Uses the configured camera index and preview setting.
     *
     * @return Decoded QR code string if found, empty string otherwise.
     */
    std::string scanFromCamera();

private:
    int cameraIndex_;       ///< Camera device index
    bool previewEnabled_;   ///< Show preview window
};

#endif // ALARMSYSTEM_HPP