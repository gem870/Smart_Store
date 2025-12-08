#ifndef MOTION_DETECTION_HPP
#define MOTION_DETECTION_HPP

#include <opencv2/opencv.hpp>
#include <vector>
#include <string>
#include <interface/BaseMicroservice.hpp>

struct MotionRegion {
    cv::Rect bbox;
    double intensity;
};

class MotionDetection : public BaseMicroservice {
public:
    MotionDetection(double threshold = 25.0, int minArea = 500);

    std::vector<MotionRegion> detectMotion(const cv::Mat& frame);
    void drawMotion(cv::Mat& frame, const std::vector<MotionRegion>& regions);
    void reset();

    /**
     * @brief Run a live motion monitor using webcam
     */
    void runMotionMonitor();

private:
    cv::Mat prevGray_;
    double diffThreshold_;
    int minArea_;
};

#endif // MOTION_DETECTION_HPP
