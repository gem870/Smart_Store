#ifndef MOTION_DETECTION_HPP
#define MOTION_DETECTION_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <functional>
#include <map>
#include <chrono>
#include <interface/BaseMicroservice.hpp>
#include <err_log/Logger.hpp>

/**
 * @struct MotionRegion
 * @brief Represents a detected region of motion or a person.
 *
 * Each region contains:
 * - Bounding box (cv::Rect) around the detected area.
 * - Intensity (double) representing motion strength or area size.
 * - Track ID (int) for optional tracking continuity.
 */
struct MotionRegion {
    cv::Rect bbox;      ///< Bounding box of the motion/person region.
    double intensity;   ///< Motion intensity or area size.
    int trackId;        ///< Tracking ID (if tracking enabled).
};

/**
 * @struct Zone
 * @brief Defines a spatial zone in the monitored frame.
 *
 * Zones can be marked as restricted to trigger intrusion alerts.
 */
struct Zone {
    std::string name;   ///< Human-readable name of the zone.
    cv::Rect roi;       ///< Region of interest (ROI) rectangle.
    bool restricted = false; ///< Flag indicating if zone is restricted.
};

/**
 * @class MotionDetection
 * @brief Provides motion and behaviour detection for surveillance video streams.
 *
 * This class combines frame differencing, contour analysis, and person detection
 * (via HOG) to identify motion regions. It also supports behaviour analysis
 * (fighting, running, loitering, crowd formation, intrusion, left-behind objects, falls).
 *
 * Features:
 * - Configurable thresholds for sensitivity and behaviour detection.
 * - Zone management for restricted areas.
 * - Event callbacks for motion and alerts.
 * - Unified monitor loop for live camera feeds.
 */
class MotionDetection : public BaseMicroservice {
public:
    /**
     * @brief Default constructor with baked-in configuration defaults.
     */
    MotionDetection();

    // --- Configuration API ---

    /**
     * @brief Set detection and behaviour configuration parameters.
     * @param diffThreshold Pixel intensity difference threshold for motion detection.
     * @param minArea Minimum contour area to consider as motion.
     * @param crowdThreshold Number of regions to trigger crowd formation alert.
     * @param loiterSeconds Seconds threshold for loitering detection.
     * @param leftBehindSeconds Seconds threshold for left-behind object detection.
     * @param enableTracking Enable/disable tracking IDs.
     */
    void setConfig(double diffThreshold,
                   int minArea,
                   std::size_t crowdThreshold,
                   int loiterSeconds,
                   int leftBehindSeconds,
                   bool enableTracking);

    /**
     * @brief Reset configuration to default values.
     */
    void resetConfig();

    // --- Core motion detection ---

    /**
     * @brief Detect motion and people in a frame.
     * @param frame Input video frame (BGR).
     * @return Vector of MotionRegion objects.
     */
    std::vector<MotionRegion> detectMotion(const cv::Mat& frame);

    /**
     * @brief Draw motion/person bounding boxes and zones on a frame.
     * @param frame Input/output frame to annotate.
     * @param regions Motion/person regions to draw.
     */
    void drawMotion(cv::Mat& frame, const std::vector<MotionRegion>& regions);

    /**
     * @brief Reset internal state (previous frame, maps, tracking IDs).
     */
    void reset();

    // --- Unified monitor ---

    /**
     * @brief Run a unified monitor loop on a camera feed.
     * @param cameraIndex Index of the camera (default 0).
     *
     * Opens a resizable window, continuously processes frames,
     * detects motion/behaviours, and raises alerts.
     */
    void runUnifiedMonitor(int cameraIndex = 0);

    // --- Zones ---

    /**
     * @brief Add a zone to the monitored frame.
     * @param zone Zone definition (name, ROI, restricted flag).
     */
    void addZone(const Zone& zone);

    /**
     * @brief Clear all zones.
     */
    void clearZones();

    // --- Event callbacks ---

    /**
     * @brief Set callback invoked when motion is detected.
     * @param cb Function receiving vector of MotionRegion.
     */
    void setMotionCallback(std::function<void(const std::vector<MotionRegion>&)> cb);

    /**
     * @brief Set callback invoked when an alert is raised.
     * @param cb Function receiving alert message string.
     */
    void setAlertCallback(std::function<void(const std::string&)> cb);

    /// Setters for individual config parameters
    void setDiffThreshold(double diffThreshold);
    void setMinArea(int minArea);
    void setCrowdThreshold(std::size_t crowdThreshold);
    void setLoiterSeconds(int loiterSeconds);
    void setLeftBehindSeconds(int leftBehindSeconds);

    // --- Config getters ---
    double getDiffThreshold() const;       ///< Get motion difference threshold.
    int getMinArea() const;                ///< Get minimum contour area.
    std::size_t getCrowdThreshold() const; ///< Get crowd threshold.
    int getLoiterSeconds() const;          ///< Get loitering threshold (seconds).
    int getLeftBehindSeconds() const;      ///< Get left-behind object threshold (seconds).
    bool isTrackingEnabled() const;        ///< Check if tracking IDs are enabled.

    // --- Callback presence ---
    bool hasMotionCallback() const;        ///< Check if motion callback is set.

    // --- Behaviour detectors ---
    bool detectFighting(const std::vector<MotionRegion>& regions, const cv::Mat& frame);
    bool detectRunning(const std::vector<MotionRegion>& regions, const cv::Mat& frame);
    bool detectLoitering(const std::vector<MotionRegion>& regions, double timeThreshold);
    bool detectCrowdFormation(const std::vector<MotionRegion>& regions, std::size_t crowdThreshold);
    bool detectIntrusion(const std::vector<MotionRegion>& regions);
    bool detectObjectLeftBehind(const std::vector<MotionRegion>& regions, double timeThreshold);
    bool detectFall(const std::vector<MotionRegion>& regions);

private:
    cv::Mat prevGray_; ///< Previous grayscale frame for differencing.

    // Config values
    double diffThreshold_;
    int minArea_;
    std::size_t crowdThreshold_;
    int loiterSeconds_;
    int leftBehindSeconds_;
    bool enableTracking_;

    // Callbacks
    std::function<void(const std::vector<MotionRegion>&)> motionCallback_;
    std::function<void(const std::string&)> alertCallback_;

    // Behaviour maps
    std::map<int, std::chrono::steady_clock::time_point> loiteringMap_;
    std::map<int, std::chrono::steady_clock::time_point> objectMap_;
    std::vector<Zone> zones_;

    int nextTrackId_ = 1; ///< Next tracking ID counter.

    /**
     * @brief Raise an alert message and invoke alert callback.
     * @param msg Alert message string.
     */
    void raiseAlert(const std::string& msg);
};

#endif // MOTION_DETECTION_HPP
