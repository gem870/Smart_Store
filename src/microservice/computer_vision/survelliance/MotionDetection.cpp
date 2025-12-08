#include "MotionDetection.hpp"
#include <iostream>

MotionDetection::MotionDetection(double threshold, int minArea)
    : diffThreshold_(threshold), minArea_(minArea) {}

std::vector<MotionRegion> MotionDetection::detectMotion(const cv::Mat& frame) {
    std::vector<MotionRegion> regions;

    if (frame.empty()) {
        std::cerr << "Warning: Empty frame passed to detectMotion." << std::endl;
        return regions;
    }

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
    cv::dilate(thresh, thresh, cv::Mat(), cv::Point(-1,-1), 2);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(thresh, contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    for (const auto& contour : contours) {
        if (cv::contourArea(contour) < minArea_) continue;
        cv::Rect bbox = cv::boundingRect(contour);
        regions.push_back({bbox, cv::contourArea(contour)});
    }

    prevGray_ = gray.clone();
    return regions;
}

void MotionDetection::drawMotion(cv::Mat& frame, const std::vector<MotionRegion>& regions) {
    if (frame.empty()) return;

    for (const auto& r : regions) {
        cv::rectangle(frame, r.bbox, cv::Scalar(0, 0, 255), 2);
        cv::putText(frame, "Motion", {r.bbox.x, r.bbox.y - 5},
                    cv::FONT_HERSHEY_SIMPLEX, 0.5, {0, 0, 255}, 2);
    }
}

void MotionDetection::reset() {
    prevGray_.release();
}

void MotionDetection::runMotionMonitor() {
    cv::VideoCapture cap(0);
    if (!cap.isOpened()) {
        std::cerr << "Error: Could not open camera." << std::endl;
        return;
    }

    cv::Mat frame;
    try {
        while (cap.read(frame)) {
            auto regions = detectMotion(frame);

            if (!regions.empty()) {
                std::cout << "ALERT: Motion detected in " << regions.size() << " regions." << std::endl;
            }

            drawMotion(frame, regions);

            cv::imshow("Motion Monitor", frame);
            if (cv::waitKey(1) == 27) break; // ESC to quit
        }
    } catch (const cv::Exception& e) {
        std::cerr << "OpenCV error: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "Unknown error occurred during motion monitoring." << std::endl;
    }

    cap.release();              //  free camera
    cv::destroyAllWindows();    //  close windows
}
