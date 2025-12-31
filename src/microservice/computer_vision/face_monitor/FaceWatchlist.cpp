

#include "FaceWatchlist.hpp"
#include <iostream>
#include <stdexcept>


// Define the network in one TU only
namespace {
    cv::dnn::Net faceNet;
    bool initialized = false;
}

FaceWatchlist::FaceWatchlist() : similarityThreshold_(DEFAULT_THRESHOLD),
                                 cascadePath_("C:/Users/PC/Desktop/Smart_Store/lib/opencv/haarcascade_frontalface_default.xml") {
    try {
        if (!initialized) {
            faceNet = cv::dnn::readNetFromTorch("C:/Users/PC/Desktop/Smart_Store/lib/opencv/models/openface.nn4.small2.v1.t7");
            initialized = true;
        }
        if (!faceCascade_.load(cascadePath_)) {
            LOG_CONTEXT(LogLevel::ERR, "", std::make_exception_ptr(
                           std::runtime_error("Failed to load Haar cascade")));
        }
    } catch (const std::exception& e) {
        LOG_CONTEXT(LogLevel::ERR, "", 
            std::make_exception_ptr(std::runtime_error(std::string("FaceWatchlist init error: ") + e.what())));
        // Keep class constructible even if model fails; usage can be guarded later.
    }
}

// Threshold controls
double FaceWatchlist::getThreshold() const { return similarityThreshold_; }
void   FaceWatchlist::setThreshold(double newThreshold) { similarityThreshold_ = newThreshold; }
void   FaceWatchlist::resetThreshold() { similarityThreshold_ = DEFAULT_THRESHOLD; }

// Cascade path controls
void FaceWatchlist::setCascadePath(const std::string& path) {
    cascadePath_ = path;
    if (!faceCascade_.load(cascadePath_)) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to load cascade from " + cascadePath_, {});
    }
}
std::string FaceWatchlist::getCascadePath() const { return cascadePath_; }

// Watchlist operations
void FaceWatchlist::loadKnownFaces(const std::vector<std::string>& filePaths) {
    int idCounter = static_cast<int>(knownFaces_.size());
    for (const auto& path : filePaths) {
        cv::Mat img = cv::imread(path);
        if (img.empty()) {
            LOG_CONTEXT(LogLevel::WARNING, "Could not read image from " + path, {});
            continue;
        }
        cv::Mat embedding = computeEmbedding(img);
        knownFaces_.push_back({idCounter++, path, embedding});
    }
}

bool FaceWatchlist::checkForKnownFace(const cv::Mat& faceROI) {
    if (faceROI.empty()) return false;

    cv::Mat embedding = computeEmbedding(faceROI);
    if (embedding.empty()) return false;

    for (const auto& kf : knownFaces_) {
        double sim = cosineSimilarity(embedding, kf.embedding);
        if (sim > similarityThreshold_) {
            LOG_CONTEXT(LogLevel::INFO, "ALERT: Face found! Matches " + kf.name, {});
            // Trigger alert mechanism here as needed or network callback
            return true;
        }
    }
    return false;
}

// Watchlist management
size_t FaceWatchlist::getKnownFaceCount() const { return knownFaces_.size(); }
void   FaceWatchlist::resetKnownFaces() { knownFaces_.clear(); }

// Internal helpers
cv::Mat FaceWatchlist::computeEmbedding(const cv::Mat& faceROI) {
    cv::Mat resized;
    cv::resize(faceROI, resized, cv::Size(96, 96));
    cv::Mat blob = cv::dnn::blobFromImage(
        resized, 1.0/255.0, cv::Size(96,96), cv::Scalar(0,0,0), true, false
    );
    if (initialized) {
        faceNet.setInput(blob);
        return faceNet.forward().clone();
    }
    return {};
}

double FaceWatchlist::cosineSimilarity(const cv::Mat& a, const cv::Mat& b) {
    if (a.empty() || b.empty()) return 0.0;
    double dot   = a.dot(b);
    double normA = cv::norm(a);
    double normB = cv::norm(b);
    if (normA == 0.0 || normB == 0.0) return 0.0;
    return dot / (normA * normB);
}

// Parameterized camera monitor
void FaceWatchlist::monitorCamera(const std::string& cascadePath) {
    // Decide which cascade path to use
    std::string pathToUse = cascadePath.empty()
        ? "models/haarcascade_frontalface_default.xml"   // system default
        : cascadePath;

    if (!faceCascade_.load(pathToUse)) {
        LOG_CONTEXT(LogLevel::ERR, "" + pathToUse, 
            std::make_exception_ptr(std::runtime_error("Could not load cascade from " + pathToUse)));
    }

    cv::VideoCapture cap(0); // open default camera
    if (!cap.isOpened()) {
        LOG_CONTEXT(LogLevel::ERR, "Could not open camera.", 
            std::make_exception_ptr(std::runtime_error("Could not open camera")));
    }

    cv::Mat frame;
    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        // Detect faces using cascade
        std::vector<cv::Rect> faces;
        faceCascade_.detectMultiScale(frame, faces);

        for (const auto& face : faces) {
            cv::Mat faceROI = frame(face);
            checkForKnownFace(faceROI); // reuse existing function
        }

        cv::imshow("Restricted Area Monitor", frame);
        if (cv::waitKey(30) >= 0) break; // press any key to exit
    }

    
    cap.release();              // release camera resource
    cv::destroyAllWindows();    // close all OpenCV windows
}


