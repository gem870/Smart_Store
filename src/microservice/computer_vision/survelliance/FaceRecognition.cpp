#include "FaceRecognition.hpp"
#include <stdexcept>
#include <iostream>



// --- Lifecycle ---
FaceRecognition::FaceRecognition() = default;
FaceRecognition::~FaceRecognition() = default;

FaceRecognition::FaceRecognition(FaceRecognition&& other) noexcept {
    faceCascade_ = std::move(other.faceCascade_);
    initialized_ = other.initialized_;
    scaleFactor_ = other.scaleFactor_;
    minNeighbors_ = other.minNeighbors_;
    minFaceSize_ = other.minFaceSize_;
    nextID_      = other.nextID_;
    tracks_      = std::move(other.tracks_);
    maxTrackAgeMs_ = other.maxTrackAgeMs_;
    iouThreshold_  = other.iouThreshold_;
    other.initialized_ = false;
    other.tracks_.clear();
}

FaceRecognition& FaceRecognition::operator=(FaceRecognition&& other) noexcept {
    if (this == &other) return *this;
    faceCascade_ = std::move(other.faceCascade_);
    initialized_ = other.initialized_;
    scaleFactor_ = other.scaleFactor_;
    minNeighbors_ = other.minNeighbors_;
    minFaceSize_ = other.minFaceSize_;
    nextID_      = other.nextID_;
    tracks_      = std::move(other.tracks_);
    maxTrackAgeMs_ = other.maxTrackAgeMs_;
    iouThreshold_  = other.iouThreshold_;
    other.initialized_ = false;
    other.tracks_.clear();
    return *this;
}

// --- Initialization ---

FaceError FaceRecognition::init(const std::string& cascadePath) {
    if (cascadePath.empty()) {
        initialized_ = false;
        return FaceError::InvalidCascadePath;
    }
    try {
        if (!faceCascade_.load(cascadePath)) {
            initialized_ = false;
            return FaceError::CascadeLoadFailed;
        }
    } catch (const cv::Exception&) {
        initialized_ = false;
        return FaceError::CascadeLoadFailed;
    }
    initialized_ = true;
    return FaceError::None;
}

// --- Detection ---

FaceDetectionResult FaceRecognition::detectFaces(const cv::Mat& frame) {
    FaceDetectionResult res;

    if (!initialized_) {
        res.error = FaceError::NotInitialized;
        res.message = "Classifier not initialized. Call init(cascadePath) first.";
        return res;
    }
    if (frame.empty()) {
        res.error = FaceError::EmptyFrame;
        res.message = "Input frame is empty.";
        return res;
    }
    if (frame.depth() != CV_8U) {
        res.error = FaceError::UnsupportedDepth;
        res.message = "Unsupported image depth. Expected 8-bit (CV_8U).";
        return res;
    }

    try {
        cv::Mat gray;
        if (frame.channels() == 3) {
            cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
        } else if (frame.channels() == 1) {
            gray = frame;
        } else {
            res.error = FaceError::UnsupportedDepth;
            res.message = "Unsupported channel count. Expected 1 or 3 channels.";
            return res;
        }

        cv::equalizeHist(gray, gray);

        double scaleFactor;
        int minNeighbors;
        cv::Size minSize;
        {
            scaleFactor = scaleFactor_;
            minNeighbors = minNeighbors_;
            minSize = minFaceSize_;
        }

        faceCascade_.detectMultiScale(
            gray, res.faces, scaleFactor, minNeighbors, 0, minSize
        );

        res.error = FaceError::None;
        res.message.clear();
        return res;
    } catch (const cv::Exception& e) {
        res.error = FaceError::DetectionError;
        res.message = std::string("OpenCV detection error: ") + e.what();
        res.faces.clear();
        return res;
    } catch (...) {
        res.error = FaceError::DetectionError;
        res.message = "Unknown error during detection.";
        res.faces.clear();
        return res;
    }
}

// --- Tracking ---

std::vector<FaceTrack> FaceRecognition::trackFaces(const std::vector<cv::Rect>& detections) {
    try {
        pruneStaleTracks();
        matchDetections(detections);

        std::vector<FaceTrack> out;
        out.reserve(tracks_.size());
        for (auto& [id, track] : tracks_) {
            out.push_back(track);
        }
        return out;
    } catch (...) {
        // Fail-safe: return current tracks without modification
        std::vector<FaceTrack> out;
        out.reserve(tracks_.size());
        for (auto& [id, track] : tracks_) out.push_back(track);
        return out;
    }
}

void FaceRecognition::drawDetections(cv::Mat& frame, const std::vector<cv::Rect>& faces,
                                     const cv::Scalar& color) {
    if (frame.empty()) return;
    for (const auto& r : faces) {
        cv::rectangle(frame, r, color, 2);
    }
}

void FaceRecognition::drawTracks(cv::Mat& frame, const std::vector<FaceTrack>& tracks) const {
    if (frame.empty()) return;
    for (const auto& t : tracks) {
        cv::rectangle(frame, t.bbox, cv::Scalar(0, 255, 0), 2);
        cv::putText(frame, "ID: " + std::to_string(t.id),
                    {t.bbox.x, t.bbox.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, {255, 0, 0}, 2);
    }
}

void FaceRecognition::resetTracking() {
    tracks_.clear();
    nextID_ = 0;
}

// --- Configuration ---

FaceError FaceRecognition::setScaleFactor(double value) {
    if (value < 1.01 || value > 2.5) return FaceError::InvalidParameter;
    scaleFactor_ = value;
    return FaceError::None;
}

FaceError FaceRecognition::setMinNeighbors(int value) {
    if (value < 0 || value > 10) return FaceError::InvalidParameter;
    minNeighbors_ = value;
    return FaceError::None;
}

FaceError FaceRecognition::setMinFaceSize(const cv::Size& s) {
    if (s.width <= 0 || s.height <= 0) return FaceError::InvalidParameter;
    minFaceSize_ = s;
    return FaceError::None;
}

FaceError FaceRecognition::setMaxTrackAgeMs(uint64_t ms) {
    if (ms < 100 || ms > 60000) return FaceError::InvalidParameter;
    maxTrackAgeMs_ = ms;
    return FaceError::None;
}

FaceError FaceRecognition::setIouMatchThreshold(double t) {
    if (t < 0.0 || t > 0.95) return FaceError::InvalidParameter;
    iouThreshold_ = t;
    return FaceError::None;
}

// --- Helpers ---

double FaceRecognition::iou(const cv::Rect& a, const cv::Rect& b) {
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.width, b.x + b.width);
    const int y2 = std::min(a.y + a.height, b.y + b.height);

    const int interW = std::max(0, x2 - x1);
    const int interH = std::max(0, y2 - y1);
    const double interArea = static_cast<double>(interW) * interH;

    const double areaA = static_cast<double>(a.width) * a.height;
    const double areaB = static_cast<double>(b.width) * b.height;

    const double unionArea = areaA + areaB - interArea;
    if (unionArea <= 0.0) return 0.0;
    return interArea / unionArea;
}

void FaceRecognition::pruneStaleTracks() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = tracks_.begin(); it != tracks_.end(); ) {
        const auto ageMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - it->second.lastSeen).count();
        if (ageMs > static_cast<long long>(maxTrackAgeMs_)) {
            it = tracks_.erase(it);
        } else {
            ++it;
        }
    }
}

void FaceRecognition::matchDetections(const std::vector<cv::Rect>& detections) {
    const auto now = std::chrono::steady_clock::now();

    // Track availability flags
    std::unordered_map<int, bool> used;
    used.reserve(tracks_.size());
    for (const auto& [id, t] : tracks_) used[id] = false;

    // First pass: greedy match by highest IOU
    for (const auto& det : detections) {
        double bestIou = 0.0;
        int bestId = -1;

        for (const auto& [id, t] : tracks_) {
            if (used[id]) continue;
            const double i = iou(det, t.bbox);
            if (i > bestIou) {
                bestIou = i;
                bestId = id;
            }
        }

        if (bestId >= 0 && bestIou >= iouThreshold_) {
            auto& tr = tracks_.at(bestId);
            tr.bbox = det;
            tr.lastSeen = now;
            used[bestId] = true;
        } else {
            // Create new track
            FaceTrack tr;
            tr.id = nextID_++;
            tr.bbox = det;
            tr.lastSeen = now;
            tracks_.emplace(tr.id, tr);
            used[tr.id] = true;
        }
    }

    // No explicit action for unmatched tracks; they’ll be pruned by age.
}


void FaceRecognition::runRestrictedAreaMonitor(const std::string& cascadePath) {
    std::string path = cascadePath.empty() ? "C:/Users/PC/Desktop/Smart_Store/lib/opencv/haarcascade_frontalface_default.xml" : cascadePath;

    // Initialize classifier
    if (init(path) != FaceError::None) {
        std::cerr << "Failed to initialize face cascade from: " << path << std::endl;
        return;
    }

    // Configure parameters
    setScaleFactor(1.1);
    setMinNeighbors(3);
    setMinFaceSize({30, 30});
    setMaxTrackAgeMs(3000);
    setIouMatchThreshold(0.35);

    // Open webcam
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open camera." << std::endl;
        return;
    }

    cv::Mat frame;
    while (cap.read(frame)) {
        auto res = detectFaces(frame);
        if (!res.ok()) {
            std::cerr << "Detection error: " << res.message << std::endl;
            continue;
        }

        auto tracks = trackFaces(res.faces);

        drawDetections(frame, res.faces);
        drawTracks(frame, tracks);

        cv::imshow("Restricted Area Monitor", frame);
        if (cv::waitKey(1) == 27) break; // ESC to quit
    }

    cap.release();              // release the camera
    cv::destroyAllWindows();   // close all OpenCV windows
}







