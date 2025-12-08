#ifndef FACE_WATCHLIST_HPP
#define FACE_WATCHLIST_HPP

#include <opencv2/opencv.hpp>
#include <string>
#include <vector>
#include <interface/BaseMicroservice.hpp>

struct KnownFace {
    int id;
    std::string name;
    cv::Mat embedding;
};

class FaceWatchlist : public BaseMicroservice {
public:
    FaceWatchlist(double threshold = 0.75);

    void loadKnownFaces(const std::vector<std::string>& filePaths);
    bool checkForKnownFace(const cv::Mat& faceROI);

private:
    std::vector<KnownFace> knownFaces_;
    double similarityThreshold_;

    cv::Mat computeEmbedding(const cv::Mat& faceROI);
    double cosineSimilarity(const cv::Mat& a, const cv::Mat& b);
};

#endif // FACE_WATCHLIST_HPP
