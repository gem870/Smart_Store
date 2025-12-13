
#ifndef FACE_RECOGNITION_HPP
#define FACE_RECOGNITION_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <optional>
#include <interface/BaseMicroservice.hpp>
#include <err_log/Logger.hpp>

/**
 * @brief Error codes for face recognition operations.
 */
enum class FaceError {
    None = 0,
    NotInitialized,
    InvalidCascadePath,
    CascadeLoadFailed,
    EmptyFrame,
    UnsupportedDepth,
    DetectionError,
    TrackingError,
    InvalidParameter
};

/**
 * @brief Detection result wrapper with error info.
 */
struct FaceDetectionResult {
    FaceError error = FaceError::None;
    std::string message;
    std::vector<cv::Rect> faces;

    /** Convenience predicate */
    bool ok() const { return error == FaceError::None; }
};

/**
 * @brief Tracking record for a single face ID.
 */
struct FaceTrack {
    int id;
    cv::Rect bbox;
    std::chrono::steady_clock::time_point lastSeen;
};

/**
 * @class FaceRecognition
 * @brief Detects and tracks faces in frames. Safe, robust, and configurable.
 *
 * Typical use:
 *  - call init(cascadePath)
 *  - for each frame: detectFaces(frame) → trackFaces(detections)
 */
class FaceRecognition : public BaseMicroservice {
public:
    FaceRecognition();
    ~FaceRecognition();

    FaceRecognition(const FaceRecognition&) = delete;
    FaceRecognition& operator=(const FaceRecognition&) = delete;
    FaceRecognition(FaceRecognition&&) noexcept;
    FaceRecognition& operator=(FaceRecognition&&) noexcept;

    /**
     * @brief Initialize classifier.
     * @param cascadePath Path to Haar cascade XML (e.g., haarcascade_frontalface_default.xml).
     * @return FaceError::None on success, otherwise an error code.
     */
    FaceError init(const std::string& cascadePath);

    /**
     * @brief Detect faces in a frame with robust validation.
     * @param frame Input BGR image (CV_8UC3 recommended).
     * @return FaceDetectionResult with faces or error.
     */
    FaceDetectionResult detectFaces(const cv::Mat& frame);

    /**
     * @brief Track faces across frames using simple IOU matching and timeouts.
     * @param detections Vector of face boxes from detectFaces().
     * @return Current active tracks (IDs + boxes).
     */
    std::vector<FaceTrack> trackFaces(const std::vector<cv::Rect>& detections);

    /**
     * @brief Draw annotations for detections.
     * @param frame In/out frame (annotated).
     * @param faces Rectangles to draw.
     * @param color Box color.
     */
    void drawDetections(cv::Mat& frame, const std::vector<cv::Rect>& faces,
                        const cv::Scalar& color = {0, 255, 0});

    /**
     * @brief Draw track IDs on frame.
     * @param frame In/out frame.
     * @param tracks Track list returned by trackFaces().
     */
    void drawTracks(cv::Mat& frame, const std::vector<FaceTrack>& tracks) const;


    /**
     * @brief Reset tracker state (clears tracked IDs and counters).
     */
    void resetTracking();

    // --- Configuration (validated setters) ---

    /**
     * @brief Set scale factor for detection.
     * @param value Must be between 1.01 and 2.5.
     */
    FaceError setScaleFactor(double value);

    /**
     * @brief Set minimum neighbors for detection.
     * @param value Must be between 0 and 10.
     */
    FaceError setMinNeighbors(int value);

    /**
     * @brief Set minimum face size for detection.
     * @param s Must have positive width and height.
     */
    FaceError setMinFaceSize(const cv::Size& s);

    /**
     * @brief Set maximum track age in milliseconds.
     * @param ms Must be between 100 and 60000.
     */
    FaceError setMaxTrackAgeMs(uint64_t ms);

    /**
     * @brief Set IOU match threshold for tracking.
     * @param t Must be between 0.0 and 0.95.
     */
    FaceError setIouMatchThreshold(double t);

    // --- Configuration getters ---

    /**
     * @brief Get current scale factor for detection.
     * @return Scale factor value.
     */
    double getScaleFactor() const;

    /**
     * @brief Get current minimum neighbors parameter.
     * @return Minimum neighbors value.
     */
    int getMinNeighbors() const;

    /**
     * @brief Get current minimum face size.
     * @return Minimum face size as cv::Size.
     */
    cv::Size getMinFaceSize() const;

    /**
     * @brief Get maximum track age in milliseconds.
     * @return Track age limit in ms.
     */
    uint64_t getMaxTrackAgeMs() const;

    /**
     * @brief Get IOU match threshold for tracking.
     * @return IOU threshold value.
     */
    double getIouMatchThreshold() const;

    /**
     * @brief Reset all configuration parameters to defaults.
     */
    void resetConfig();

    /**
     * @brief Run a monitoring loop for restricted areas.
     *        Opens webcam, detects and tracks faces, and displays annotated frames.
     * @param cascadePath Path to Haar cascade XML file.
     */
    void runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath = "");

    /**
     * @brief Access current active tracks.
     * @return Map of track IDs to FaceTrack objects.
     */
    const std::unordered_map<int, FaceTrack>& getTracks() const;

    /**
     * @brief Add a new track manually.
     * @param id Track ID.
     * @param track Track data.
     */
    void addTrack(int id, const FaceTrack& track);

    /**
     * @brief Remove a track by ID.
     * @param id Track ID to remove.
     */
    void removeTrack(int id);

private:
    // Classifier
    cv::CascadeClassifier faceCascade_;
    bool initialized_ = false;

    // Detection params
    double scaleFactor_ = 1.1;
    int minNeighbors_ = 3;
    cv::Size minFaceSize_ {30, 30};

    // Tracking
    int nextID_ = 0;
    std::unordered_map<int, FaceTrack> tracks_;
    uint64_t maxTrackAgeMs_ = 2000;  ///< Drop tracks not seen in this window
    double iouThreshold_ = 0.30;     ///< Match threshold

    // Helpers
    static double iou(const cv::Rect& a, const cv::Rect& b);
    void pruneStaleTracks();
    void matchDetections(const std::vector<cv::Rect>& detections);
};

#endif // FACE_RECOGNITION_HPP
