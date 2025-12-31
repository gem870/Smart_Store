#include "Scanner.hpp"


Scanner::Scanner() {}
Scanner::~Scanner() {}

void Scanner::setEdgeThresholds(int low, int high) { cannyLow_ = low; cannyHigh_ = high; }
void Scanner::setContourMinArea(double area) { minContourArea_ = area; }
void Scanner::setOutputSize(int w, int h) { outWidth_ = w; outHeight_ = h; }
void Scanner::setSharpening(double amount) { sharpenAmount_ = std::clamp(amount, 0.0, 2.0); }

int Scanner::getCannyLow() const { return cannyLow_; }
int Scanner::getCannyHigh() const { return cannyHigh_; }
double Scanner::getMinContourArea() const { return minContourArea_; }
int Scanner::getOutputWidth() const { return outWidth_; }
int Scanner::getOutputHeight() const { return outHeight_; }
double Scanner::getSharpening() const { return sharpenAmount_; }

void Scanner::run(const std::string& mode, const std::string& input, const std::string& output) {
    if (mode == "file") {
        auto result = scanFromFile(input, output);
        if (!result.success) {
            std::cerr << "Scan failed: " << result.message << std::endl;
        } else {
            std::cout << "Scan complete: " << result.type << std::endl;
        }
    } else if (mode == "cam") {
        int camIdx = std::stoi(input);
        auto result = scanFromCamera(camIdx, output);
        if (!result.success) {
            std::cerr << "Scan failed: " << result.message << std::endl;
        } else {
            std::cout << "Scan complete: " << result.type << std::endl;
        }
    } else {
        std::cerr << "Unknown mode: " << mode << std::endl;
    }
}

ScanResult Scanner::scanFromCamera(int cameraIndex, const std::string& outPath) {
    ScanResult res;
    cv::VideoCapture cap(cameraIndex);
    if (!cap.isOpened()) {
        res.message = "Could not open camera index " + std::to_string(cameraIndex);
        std::cerr << res.message << std::endl;
        return res;
    }

    cv::Mat frame;
    if (!cap.read(frame) || frame.empty()) {
        res.message = "Failed to read frame from camera";
        std::cerr << res.message << std::endl;
        cap.release();
        return res;
    }

    res = detectAndExtract(frame);
    if (res.success) {
        if (!cv::imwrite(outPath, res.processed)) {
            res.success = false;
            res.message = "Failed to write output image: " + outPath;
            std::cerr << res.message << std::endl;
        } else {
            std::cout << "Saved scan to " << outPath << std::endl;
        }
    }

    cap.release();
    return res;
}

ScanResult Scanner::scanFromFile(const std::string& imagePath, const std::string& outPath) {
    ScanResult res;
    cv::Mat img = cv::imread(imagePath);
    if (img.empty()) {
        res.message = "Failed to load image: " + imagePath;
        std::cerr << res.message << std::endl;
        return res;
    }

    res = detectAndExtract(img);
    if (res.success) {
        if (!cv::imwrite(outPath, res.processed)) {
            res.success = false;
            res.message = "Failed to write output image: " + outPath;
            std::cerr << res.message << std::endl;
        } else {
            std::cout << "Saved scan to " << outPath << std::endl;
        }
    }
    return res;
}

ScanResult Scanner::detectAndExtract(const cv::Mat& input) {
    ScanResult res;
    res.original = input;

    cv::Mat gray;
    cv::cvtColor(input, gray, cv::COLOR_BGR2GRAY);
    cv::Mat blur;
    cv::GaussianBlur(gray, blur, cv::Size(5,5), 0);
    cv::Mat edges;
    cv::Canny(blur, edges, cannyLow_, cannyHigh_);

    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double bestArea = 0.0;
    std::vector<cv::Point> best;
    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < minContourArea_) continue;

        std::vector<cv::Point> approx;
        double peri = cv::arcLength(c, true);
        cv::approxPolyDP(c, approx, 0.02 * peri, true);

        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            if (area > bestArea) {
                bestArea = area;
                best = approx;
            }
        }
    }

    if (best.empty()) {
        res.success = false;
        res.message = "No suitable document/ID contour found";
        std::cerr << res.message << std::endl;
        return res;
    }

    cv::Mat warped = fourPointWarp(input, best);
    cv::Mat enhanced = enhance(warped);

    res.processed = enhanced;
    res.type = "document/id";
    res.success = true;
    res.message = "Scan successful";
    return res;
}

bool Scanner::detectQuadrilateral(const cv::Mat& edges, std::vector<cv::Point>& quad) {
    std::vector<std::vector<cv::Point>> contours;
    cv::findContours(edges.clone(), contours, cv::RETR_EXTERNAL, cv::CHAIN_APPROX_SIMPLE);

    double bestArea = 0.0;
    std::vector<cv::Point> best;

    for (const auto& c : contours) {
        double area = cv::contourArea(c);
        if (area < minContourArea_) continue;

        std::vector<cv::Point> approx;
        double peri = cv::arcLength(c, true);
        cv::approxPolyDP(c, approx, 0.02 * peri, true);

        if (approx.size() == 4 && cv::isContourConvex(approx)) {
            if (area > bestArea) {
                bestArea = area;
                best = approx;
            }
        }
    }

    if (!best.empty()) {
        quad = best;
        return true;
    }
    return false;
}

cv::Mat Scanner::fourPointWarp(const cv::Mat& img, const std::vector<cv::Point>& quad) {
    std::vector<cv::Point2f> src;
    for (auto& p : quad) src.emplace_back(p.x, p.y);

    std::vector<cv::Point2f> dst{
        {0.f, 0.f},
        {static_cast<float>(outWidth_-1), 0.f},
        {static_cast<float>(outWidth_-1), static_cast<float>(outHeight_-1)},
        {0.f, static_cast<float>(outHeight_-1)}
    };

    cv::Mat M = cv::getPerspectiveTransform(src, dst);
    cv::Mat warped;
    cv::warpPerspective(img, warped, M, cv::Size(outWidth_, outHeight_));
    return warped;
}

cv::Mat Scanner::enhance(const cv::Mat& img) {
    cv::Mat out;
    img.copyTo(out);

    cv::Mat ycrcb;
    cv::cvtColor(out, ycrcb, cv::COLOR_BGR2YCrCb);
    std::vector<cv::Mat> channels;
    cv::split(ycrcb, channels);
    cv::equalizeHist(channels[0], channels[0]);
    cv::merge(channels, ycrcb);
    cv::cvtColor(ycrcb, out, cv::COLOR_YCrCb2BGR);

    if (sharpenAmount_ > 0.0) {
        cv::Mat blur;
        cv::GaussianBlur(out, blur, cv::Size(0,0), 2.0);
        cv::addWeighted(out, 1.0 + sharpenAmount_, blur, -sharpenAmount_, 0, out);
    }

    return out;
}
