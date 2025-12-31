#pragma once
#include <opencv2/core.hpp>
#include <string>
#include <vector>
#include <opencv2/imgproc.hpp>
#include <opencv2/highgui.hpp>
#include <iostream>
#include <algorithm>
#include "interface/BaseMicroservice.hpp"

/**
 * @brief Result of a scanning operation.
 */
struct ScanResult {
    cv::Mat original;     ///< Original captured frame or image
    cv::Mat processed;    ///< Perspective-corrected and enhanced scan
    std::string type;     ///< Classification ("document" or "id")
    bool success{false};  ///< True if scan succeeded
    std::string message;  ///< Status or error message
};

/**
 * @brief Scanner class for detecting and scanning documents or ID cards
 *        using camera or image files.
 *
 * Provides configuration setters/getters, scanning pipeline, and an entry point
 * for camera or file-based scanning.
 */
class Scanner : public BaseMicroservice {
public:
    /**
     * @brief Construct a new Scanner object with default configuration.
     */
    Scanner();

    /**
     * @brief Destroy the Scanner object.
     */
    ~Scanner();

    // --- Configuration setters ---

    /**
     * @brief Set thresholds for Canny edge detection.
     * @param low Lower threshold.
     * @param high Upper threshold.
     */
    void setEdgeThresholds(int low, int high);

    /**
     * @brief Set minimum contour area to consider for document/ID detection.
     * @param area Minimum area in pixels.
     */
    void setContourMinArea(double area);

    /**
     * @brief Set output scan resolution.
     * @param width Desired width in pixels.
     * @param height Desired height in pixels.
     */
    void setOutputSize(int width, int height);

    /**
     * @brief Set sharpening intensity for image enhancement.
     * @param amount Sharpening factor (0.0–2.0).
     */
    void setSharpening(double amount);

    // --- Configuration getters ---

    /**
     * @brief Get lower Canny threshold.
     * @return int Lower threshold.
     */
    int getCannyLow() const;

    /**
     * @brief Get upper Canny threshold.
     * @return int Upper threshold.
     */
    int getCannyHigh() const;

    /**
     * @brief Get minimum contour area.
     * @return double Minimum area in pixels.
     */
    double getMinContourArea() const;

    /**
     * @brief Get output scan width.
     * @return int Width in pixels.
     */
    int getOutputWidth() const;

    /**
     * @brief Get output scan height.
     * @return int Height in pixels.
     */
    int getOutputHeight() const;

    /**
     * @brief Get sharpening intensity.
     * @return double Sharpening factor.
     */
    double getSharpening() const;

    /**
     * @brief Entry point to run scanning.
     *
     * @param mode "file" or "cam"
     * @param input File path (for "file") or camera index string (for "cam")
     * @param output Output file path to save the scan
     */
    void run(const std::string& mode, const std::string& input, const std::string& output);

private:
    /**
     * @brief Scan a document/ID using camera input.
     * @param cameraIndex Index of the camera device.
     * @param outPath Output file path.
     * @return ScanResult Result of the scan.
     */
    ScanResult scanFromCamera(int cameraIndex, const std::string& outPath);

    /**
     * @brief Scan a document/ID from an image file.
     * @param imagePath Path to the input image.
     * @param outPath Output file path.
     * @return ScanResult Result of the scan.
     */
    ScanResult scanFromFile(const std::string& imagePath, const std::string& outPath);

    /**
     * @brief Detect and extract document/ID region from an image.
     * @param input Input image.
     * @return ScanResult Result with processed scan.
     */
    ScanResult detectAndExtract(const cv::Mat& input);

    /**
     * @brief Detect quadrilateral contour representing document/ID.
     * @param edges Edge-detected image.
     * @param quad Output vector of four points.
     * @return true if quadrilateral found, false otherwise.
     */
    bool detectQuadrilateral(const cv::Mat& edges, std::vector<cv::Point>& quad);

    /**
     * @brief Apply perspective warp to normalize document/ID.
     * @param img Input image.
     * @param quad Quadrilateral points.
     * @return cv::Mat Warped image.
     */
    cv::Mat fourPointWarp(const cv::Mat& img, const std::vector<cv::Point>& quad);

    /**
     * @brief Enhance scanned image (contrast, sharpening).
     * @param img Input warped image.
     * @return cv::Mat Enhanced image.
     */
    cv::Mat enhance(const cv::Mat& img);

private:
    int cannyLow_{50};          ///< Lower threshold for Canny edge detection
    int cannyHigh_{150};        ///< Upper threshold for Canny edge detection
    double minContourArea_{20000.0}; ///< Minimum contour area to consider
    int outWidth_{1000};        ///< Output scan width
    int outHeight_{1400};       ///< Output scan height
    double sharpenAmount_{0.5}; ///< Sharpening intensity
};
