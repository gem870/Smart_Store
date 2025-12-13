

#include "MotionDetection.hpp"
#include <iostream>

// Constructor with defaults
MotionDetection::MotionDetection() {
    diffThreshold_     = 25.0;
    minArea_           = 500;
    crowdThreshold_    = 5;
    loiterSeconds_     = 10;
    leftBehindSeconds_ = 15;
    enableTracking_    = true;
}

// --- Config API ---
void MotionDetection::setConfig(double diffThreshold,
                                int minArea,
                                std::size_t crowdThreshold,
                                int loiterSeconds,
                                int leftBehindSeconds,
                                bool enableTracking) {
    diffThreshold_     = diffThreshold;
    minArea_           = minArea;
    crowdThreshold_    = crowdThreshold;
    loiterSeconds_     = loiterSeconds;
    leftBehindSeconds_ = leftBehindSeconds;
    enableTracking_    = enableTracking;
}

void MotionDetection::resetConfig() {
    diffThreshold_     = 25.0;
    minArea_           = 500;
    crowdThreshold_    = 5;
    loiterSeconds_     = 10;
    leftBehindSeconds_ = 15;
    enableTracking_    = true;
}

void MotionDetection::raiseAlert(const std::string& msg) {
    std::cout << "ALERT: " << msg << std::endl;
    LOG_CONTEXT(LogLevel::WARNING, msg, {});
    if (alertCallback_) alertCallback_(msg);
}

// --- Motion + Person detection ---
std::vector<MotionRegion> MotionDetection::detectMotion(const cv::Mat& frame) {
    std::vector<MotionRegion> regions;
    if (frame.empty()) return regions;

    cv::Mat gray;
    cv::cvtColor(frame, gray, cv::COLOR_BGR2GRAY);
    cv::GaussianBlur(gray, gray, cv::Size(21, 21), 0);

    if (prevGray_.empty()) {
        prevGray_ = gray.clone();
        return regions;
    }

    cv::Mat diff, thresh;
    cv::absdiff(prevGray_, gray, diff);
    cv::threshold(diff, thresh, diffThreshold_, 255, cv::THRESH_BINARY);

    cv::Mat kernel = cv::getStructuringElement(cv::MORPH_RECT, cv::Size(5, 5));
    cv::morphologyEx(thresh, thresh, cv::MORPH_CLOSE, kernel, cv::Point(-1,-1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    // Collect all contour points above minArea
    std::vector<cv::Point> allPoints;
    for (const auto& contour : contours) {
        double area = cv::contourArea(contour);
        if (area < minArea_) continue;
        allPoints.insert(allPoints.end(), contour.begin(), contour.end());
    }

    // Fuse into one convex hull box if motion exists
    if (!allPoints.empty()) {
        std::vector<cv::Point> hull;
        cv::convexHull(allPoints, hull);
        cv::Rect fullBox = cv::boundingRect(hull);
        regions.push_back({fullBox, static_cast<double>(fullBox.area()), -1});
    }

    // --- Person detection (HOG) ---
    cv::HOGDescriptor hog;
    hog.setSVMDetector(cv::HOGDescriptor::getDefaultPeopleDetector());
    std::vector<cv::Rect> detections;
    hog.detectMultiScale(frame, detections);

    for (auto& d : detections) {
        regions.push_back({d, static_cast<double>(d.area()), -1});
    }

    prevGray_ = gray.clone();
    return regions;
}

// --- Drawing ---
void MotionDetection::drawMotion(cv::Mat& frame, const std::vector<MotionRegion>& regions) {
    for (const auto& r : regions) {
        // Choose color dynamically (here: red for all motion)
        cv::Scalar boxColor(0, 0, 255);

        // Draw semi-transparent filled rectangle
        cv::Mat overlay;
        frame.copyTo(overlay);
        cv::rectangle(overlay, r.bbox, boxColor, cv::FILLED);
        double alpha = 0.3; // transparency factor
        cv::addWeighted(overlay, alpha, frame, 1 - alpha, 0, frame);

        // Draw thicker outline
        cv::rectangle(frame, r.bbox, boxColor, 3);

        // Label with background
        std::string label = "MotionPerson";
        int baseline = 0;
        cv::Size textSize = cv::getTextSize(label, cv::FONT_HERSHEY_SIMPLEX, 0.5, 1, &baseline);
        cv::Rect bgRect(r.bbox.x, r.bbox.y - textSize.height - 6,
                        textSize.width + 6, textSize.height + 6);
        cv::rectangle(frame, bgRect, boxColor, cv::FILLED);
        cv::putText(frame, label, {r.bbox.x + 3, r.bbox.y - 3},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, cv::Scalar(255,255,255), 1);
    }

    // Draw zones as before
    for (const auto& z : zones_) {
        cv::Scalar color = z.restricted ? cv::Scalar(0, 0, 200) : cv::Scalar(0, 200, 0);
        cv::rectangle(frame, z.roi, color, 2);
        cv::putText(frame, z.name, {z.roi.x, z.roi.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, color, 2);
    }
}


// --- Reset ---
void MotionDetection::reset() {
    prevGray_.release();
    loiteringMap_.clear();
    objectMap_.clear();
    nextTrackId_ = 1;
}

// --- Callbacks ---
void MotionDetection::setMotionCallback(std::function<void(const std::vector<MotionRegion>&)> cb) {
    motionCallback_ = cb;
}
void MotionDetection::setAlertCallback(std::function<void(const std::string&)> cb) {
    alertCallback_ = cb;
}
void MotionDetection::setDiffThreshold(double diffThreshold) { diffThreshold_ = diffThreshold; }
void MotionDetection::setMinArea(int minArea) { minArea_ = minArea; }
void MotionDetection::setCrowdThreshold(std::size_t crowdThreshold) { crowdThreshold_ = crowdThreshold; }
void MotionDetection::setLoiterSeconds(int loiterSeconds) { loiterSeconds_ = loiterSeconds; }
void MotionDetection::setLeftBehindSeconds(int leftBehindSeconds) { leftBehindSeconds_ = leftBehindSeconds; }

// --- Getters ---
double MotionDetection::getDiffThreshold() const { return diffThreshold_; }
int MotionDetection::getMinArea() const { return minArea_; }
std::size_t MotionDetection::getCrowdThreshold() const { return crowdThreshold_; }
int MotionDetection::getLoiterSeconds() const { return loiterSeconds_; }
int MotionDetection::getLeftBehindSeconds() const { return leftBehindSeconds_; }
bool MotionDetection::isTrackingEnabled() const { return enableTracking_; }
bool MotionDetection::hasMotionCallback() const { return static_cast<bool>(motionCallback_); }

// --- Behaviour detectors ---
bool MotionDetection::detectFighting(const std::vector<MotionRegion>& regions, const cv::Mat& frame) {
    (void)frame;
    for (size_t i = 0; i < regions.size(); ++i) {
        for (size_t j = i + 1; j < regions.size(); ++j) {
            if ((regions[i].bbox & regions[j].bbox).area() > 0 &&
                regions[i].intensity > 1000 && regions[j].intensity > 1000) {
                return true;
            }
        }
    }
    return false;
}
bool MotionDetection::detectRunning(const std::vector<MotionRegion>& regions, const cv::Mat& frame) {
    (void)frame;
    for (const auto& r : regions) {
        if (r.intensity > 2000) return true;
    }
    return false;
}
bool MotionDetection::detectLoitering(const std::vector<MotionRegion>& regions, double timeThreshold) {
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < regions.size(); ++i) {
        int id = static_cast<int>(i);
        if (loiteringMap_.find(id) == loiteringMap_.end()) {
            loiteringMap_[id] = now;
        } else {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - loiteringMap_[id]).count();
            if (duration > timeThreshold) return true;
        }
    }
    return false;
}
bool MotionDetection::detectCrowdFormation(const std::vector<MotionRegion>& regions, std::size_t crowdThreshold) {
    return regions.size() >= crowdThreshold;
}
bool MotionDetection::detectIntrusion(const std::vector<MotionRegion>& regions) {
    for (const auto& z : zones_) {
        if (!z.restricted) continue;
        for (const auto& r : regions) {
            if ((r.bbox & z.roi).area() > 0) return true;
        }
    }
    return false;
}

bool MotionDetection::detectObjectLeftBehind(const std::vector<MotionRegion>& regions, double timeThreshold) {
    auto now = std::chrono::steady_clock::now();
    for (size_t i = 0; i < regions.size(); ++i) {
        int id = static_cast<int>(i);
        if (objectMap_.find(id) == objectMap_.end()) {
            objectMap_[id] = now;
        } else {
            auto duration = std::chrono::duration_cast<std::chrono::seconds>(now - objectMap_[id]).count();
            if (duration > timeThreshold) return true;
        }
    }
    return false;
}

bool MotionDetection::detectFall(const std::vector<MotionRegion>& regions) {
    for (const auto& r : regions) {
        // Simple heuristic: if width > height, assume collapse/fall
        if (r.bbox.height < r.bbox.width) return true;
    }
    return false;
}

// --- Unified monitor with persistence ---
// --- Unified monitor with persistence ---
void MotionDetection::runUnifiedMonitor(int cameraIndex) {
    cv::VideoCapture cap(cameraIndex);
    if (!cap.isOpened()) {
        LOG_CONTEXT(LogLevel::ERR, "Could not open camera index " + std::to_string(cameraIndex), 
                                    std::make_exception_ptr(std::runtime_error("Camera open failed")));
    }

    // Make window resizable
    cv::namedWindow("Unified Motion & Behaviour Monitor", cv::WINDOW_NORMAL);
    cv::resizeWindow("Unified Motion & Behaviour Monitor", 800, 600);

    cv::Mat frame;
    std::vector<MotionRegion> lastRegions;
    auto lastTime = std::chrono::steady_clock::now();

    try {
        while (cap.read(frame)) {
            auto regions = detectMotion(frame);

            if (!regions.empty()) {
                lastRegions = regions;
                lastTime = std::chrono::steady_clock::now();

                if (motionCallback_) motionCallback_(regions);
                raiseAlert("Motion detected in " + std::to_string(regions.size()) + " regions.");

                if (detectFighting(regions, frame)) 
                    raiseAlert("Abnormal behaviour - Fighting!");
                if (detectRunning(regions, frame)) 
                    raiseAlert("Abnormal behaviour - Running!");
                if (detectLoitering(regions, loiterSeconds_)) 
                    raiseAlert("Abnormal behaviour - Loitering!");
                if (detectCrowdFormation(regions, crowdThreshold_)) 
                    raiseAlert("Abnormal behaviour - Crowd formation!");
                if (detectIntrusion(regions)) 
                    raiseAlert("Abnormal behaviour - Intrusion in restricted zone!");
                if (detectObjectLeftBehind(regions, leftBehindSeconds_)) 
                    raiseAlert("Abnormal behaviour - Object left behind!");
                if (detectFall(regions)) 
                    raiseAlert("Abnormal behaviour - Fall detected!");
            } else {
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - lastTime).count() < 3) {
                    regions = lastRegions;
                }
            }

            // Draw bounding boxes and zones
            drawMotion(frame, regions);

            // Resize frame to fit window size
            cv::Mat display;
            cv::resize(frame, display, cv::Size(800, 600));
            cv::imshow("Unified Motion & Behaviour Monitor", display);

            if (cv::waitKey(1) == 27) break; // ESC to quit
        }
    } catch (const cv::Exception& e) {
        LOG_CONTEXT(LogLevel::ERR, "OpenCV error: " + std::string(e.what()), {});
    } catch (...) {
        LOG_CONTEXT(LogLevel::ERR, "Unknown error occurred during monitoring.", {});
    }

    cap.release();
    cv::destroyAllWindows();
}


// --- Zone management ---
void MotionDetection::addZone(const Zone& zone) {
    zones_.push_back(zone);
}

void MotionDetection::clearZones() {
    zones_.clear();
}
