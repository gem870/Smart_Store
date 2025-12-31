
#include "FaceRecognition.hpp"
#include <stdexcept>
#include <iostream>

// --- Lifecycle ---
FaceRecognition::FaceRecognition() {
        // Configure parameters
    setScaleFactor(1.1);
    setMinNeighbors(3);
    setMinFaceSize({30, 30});
    setMaxTrackAgeMs(3000);
    setIouMatchThreshold(0.35);
}
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

        double scaleFactor = scaleFactor_;
        int minNeighbors = minNeighbors_;
        cv::Size minSize = minFaceSize_;

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

// --- Improved drawTracks (only one definition now) ---
void FaceRecognition::drawTracks(cv::Mat& frame, const std::vector<FaceTrack>& tracks) const {
    if (frame.empty()) return;

    for (const auto& t : tracks) {
        cv::Scalar boxColor(0, 255, 0); // green outline
        cv::Scalar textBgColor(0, 255, 0);
        cv::Scalar textColor(255, 255, 255);

        // Semi-transparent fill
        cv::Mat overlay;
        frame.copyTo(overlay);
        cv::rectangle(overlay, t.bbox, boxColor, cv::FILLED);
        cv::addWeighted(overlay, 0.25, frame, 0.75, 0, frame);

        // Bold outline
        cv::rectangle(frame, t.bbox, boxColor, 3);

        // Label with background
        std::string label = "ID: " + std::to_string(t.id);
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.6, 2, &baseline);

        cv::Rect bgRect(t.bbox.x, t.bbox.y - textSize.height - 8,
                        textSize.width + 8, textSize.height + 8);
        cv::rectangle(frame, bgRect, textBgColor, cv::FILLED);

        cv::putText(frame, label, {t.bbox.x + 4, t.bbox.y - 4},
                    cv::FONT_HERSHEY_SIMPLEX, 0.6, textColor, 2);
    }
}

void FaceRecognition::resetTracking() {
    tracks_.clear();
    nextID_ = 0;
}

// --- Configuration (setters/getters) ---
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

double FaceRecognition::getScaleFactor() const { return scaleFactor_; }
int FaceRecognition::getMinNeighbors() const { return minNeighbors_; }
cv::Size FaceRecognition::getMinFaceSize() const { return minFaceSize_; }
uint64_t FaceRecognition::getMaxTrackAgeMs() const { return maxTrackAgeMs_; }
double FaceRecognition::getIouMatchThreshold() const { return iouThreshold_; }

void FaceRecognition::resetConfig() {
    scaleFactor_   = 1.1;
    minNeighbors_  = 3;
    minFaceSize_   = cv::Size(30,30);
    maxTrackAgeMs_ = 2000;
    iouThreshold_  = 0.30;
}

// --- Helpers ---
double FaceRecognition::iou(const cv::Rect& a, const cv::Rect& b) {
    // Coordinates of intersection rectangle
    const int x1 = std::max(a.x, b.x);
    const int y1 = std::max(a.y, b.y);
    const int x2 = std::min(a.x + a.width, b.x + b.width);
    const int y2 = std::min(a.y + a.height, b.y + b.height);

    // Intersection dimensions
    const int interW = std::max(0, x2 - x1);
    const int interH = std::max(0, y2 - y1);
    const double interArea = static_cast<double>(interW) * interH;

    // Areas of the two rectangles
    const double areaA = static_cast<double>(a.width) * a.height;
    const double areaB = static_cast<double>(b.width) * b.height;

    // Union area
    const double unionArea = areaA + areaB - interArea;
    if (unionArea <= 0.0) return 0.0;

    // Intersection over Union
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
    // Unmatched tracks are pruned by age in pruneStaleTracks()
}

// --- Restricted Area Monitor ---
void FaceRecognition::runRestrictedAreaMonitor(int cameraIndex, const std::string& cascadePath) {
    std::string path = cascadePath.empty() ? 
        "C:/Users/PC/Desktop/Smart_Store/lib/opencv/haarcascade_frontalface_default.xml" 
        : cascadePath;

    // Initialize classifier
    if (init(path) != FaceError::None) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to initialize face cascade from: " + path, 
                             std::make_exception_ptr(std::runtime_error("Cascade init failed")));
    }



    // Open webcam
    cv::VideoCapture cap(cameraIndex);
    if (!cap.isOpened()) {
        LOG_CONTEXT(LogLevel::ERR, "Could not open camera index " + std::to_string(cameraIndex), 
                                    std::make_exception_ptr(std::runtime_error("Camera open failed")));
    }

    // Make window resizable
    cv::namedWindow("Restricted Area Monitor", cv::WINDOW_NORMAL);
    cv::resizeWindow("Restricted Area Monitor", 800, 600);

    cv::Mat frame;
    while (cap.read(frame)) {
        auto res = detectFaces(frame);
        if (!res.ok()) {
            std::cerr << "Detection error: " << res.message << std::endl;
            LOG_CONTEXT(LogLevel::ERR, "Face detection error: " + res.message, {});
            continue;
        }

        auto tracks = trackFaces(res.faces);

        drawDetections(frame, res.faces);
        drawTracks(frame, tracks);

        // Resize frame to fit window size
        cv::Mat display;
        cv::resize(frame, display, cv::Size(800, 600));
        cv::imshow("Restricted Area Monitor", display);

        if (cv::waitKey(1) == 27) break; // ESC to quit
    }

    cap.release();
    cv::destroyAllWindows();
}

// --- Track management ---
const std::unordered_map<int, FaceTrack>& FaceRecognition::getTracks() const {
    return tracks_;
}

void FaceRecognition::addTrack(int id, const FaceTrack& track) {
    tracks_[id] = track;
}

void FaceRecognition::removeTrack(int id) {
    tracks_.erase(id);
}
