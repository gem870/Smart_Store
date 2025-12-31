#include "QRCodeScanner.hpp"
#include <opencv2/opencv.hpp>
#include <iostream>

QRCodeScanner::QRCodeScanner() {
    cameraIndex_ = 0;
    previewEnabled_ = true;
}

QRCodeScanner::~QRCodeScanner() {}

void QRCodeScanner::setCameraIndex(int index) { cameraIndex_ = index; }
void QRCodeScanner::setPreviewEnabled(bool enabled) { previewEnabled_ = enabled; }

int QRCodeScanner::getCameraIndex() const { return cameraIndex_; }
bool QRCodeScanner::isPreviewEnabled() const { return previewEnabled_; }

void QRCodeScanner::resetConfig() {
    cameraIndex_ = 0;
    previewEnabled_ = true;

}

std::string QRCodeScanner::scanFromFile(const std::string& imagePath) {
    cv::Mat img = cv::imread(imagePath);
    if (img.empty()) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to load image: " + imagePath, {});
        return "";
    }

    cv::QRCodeDetector qrDecoder;
    std::string data = qrDecoder.detectAndDecode(img);

    if (data.empty()) {
        LOG_CONTEXT(LogLevel::WARNING, "No QR code detected in file: " + imagePath, {});
    } else {
        LOG_CONTEXT(LogLevel::INFO, "QR Code detected: " + data, {});
    }
    return data;
}

std::string QRCodeScanner::scanFromCamera() {
    cv::VideoCapture cap(cameraIndex_);
    if (!cap.isOpened()) {
        LOG_CONTEXT(LogLevel::ERR, "Could not open camera index " + std::to_string(cameraIndex_), {});
        return "";
    }

    cv::QRCodeDetector qrDecoder;
    cv::Mat frame;
    std::string data;

    int sweepY = 0;
    int sweepDir = 5; // speed of sweep

    while (true) {
        cap >> frame;
        if (frame.empty()) break;

        // Define smaller central scan area (40% of frame size)
        int boxWidth  = frame.cols * 0.4;
        int boxHeight = frame.rows * 0.4;
        int boxX = (frame.cols - boxWidth) / 2;
        int boxY = (frame.rows - boxHeight) / 2;
        cv::Rect scanArea(boxX, boxY, boxWidth, boxHeight);

        // Decode only inside scan area
        cv::Mat roi = frame(scanArea);
        data = qrDecoder.detectAndDecode(roi);

        // Draw inverted scanner box (corners only)
        int thickness = 4;
        int segment = 40; // length of corner segments

        // Top-left corner
        cv::line(frame, cv::Point(scanArea.x, scanArea.y),
                 cv::Point(scanArea.x + segment, scanArea.y),
                 cv::Scalar(0,255,0), thickness);
        cv::line(frame, cv::Point(scanArea.x, scanArea.y),
                 cv::Point(scanArea.x, scanArea.y + segment),
                 cv::Scalar(0,255,0), thickness);

        // Top-right corner
        cv::line(frame, cv::Point(scanArea.x + scanArea.width, scanArea.y),
                 cv::Point(scanArea.x + scanArea.width - segment, scanArea.y),
                 cv::Scalar(0,255,0), thickness);
        cv::line(frame, cv::Point(scanArea.x + scanArea.width, scanArea.y),
                 cv::Point(scanArea.x + scanArea.width, scanArea.y + segment),
                 cv::Scalar(0,255,0), thickness);

        // Bottom-left corner
        cv::line(frame, cv::Point(scanArea.x, scanArea.y + scanArea.height),
                 cv::Point(scanArea.x + segment, scanArea.y + scanArea.height),
                 cv::Scalar(0,255,0), thickness);
        cv::line(frame, cv::Point(scanArea.x, scanArea.y + scanArea.height),
                 cv::Point(scanArea.x, scanArea.y + scanArea.height - segment),
                 cv::Scalar(0,255,0), thickness);

        // Bottom-right corner
        cv::line(frame, cv::Point(scanArea.x + scanArea.width, scanArea.y + scanArea.height),
                 cv::Point(scanArea.x + scanArea.width - segment, scanArea.y + scanArea.height),
                 cv::Scalar(0,255,0), thickness);
        cv::line(frame, cv::Point(scanArea.x + scanArea.width, scanArea.y + scanArea.height),
                 cv::Point(scanArea.x + scanArea.width, scanArea.y + scanArea.height - segment),
                 cv::Scalar(0,255,0), thickness);

        // If QR code detected, animate fade band inside the box
        if (!data.empty()) {
            cv::Mat overlay = frame.clone();

            sweepY += sweepDir;
            if (sweepY > scanArea.height - 30 || sweepY < 0) {
                sweepDir = -sweepDir;
                sweepY += sweepDir;
            }

            cv::Rect band(scanArea.x, scanArea.y + sweepY, scanArea.width, 30);
            cv::rectangle(overlay, band, cv::Scalar(0,255,0), cv::FILLED);

            cv::Mat mask = cv::Mat::zeros(frame.size(), CV_8UC1);
            cv::rectangle(mask, scanArea, cv::Scalar(255), cv::FILLED);

            cv::Mat bandMasked;
            overlay.copyTo(bandMasked, mask);

            cv::addWeighted(bandMasked, 0.4, frame, 0.6, 0, frame);

            cv::putText(frame, "QR Code Detected: " + data, cv::Point(30,30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,0), 2);
            LOG_CONTEXT(LogLevel::INFO, "QR Code detected: " + data, {}); 
        } else {
            cv::putText(frame, "Place QR Code inside the box...", cv::Point(30,30),
                        cv::FONT_HERSHEY_SIMPLEX, 0.8, cv::Scalar(0,255,255), 2);
        }

        if (previewEnabled_) {
            cv::imshow("QR Code Scanner", frame);
            if (cv::waitKey(1) == 27) break;
        } else {
            if (!data.empty()) break;
        }
    }

    cap.release();
    cv::destroyAllWindows();
    return data;
}
