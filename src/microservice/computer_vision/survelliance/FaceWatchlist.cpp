#include "FaceWatchlist.hpp"
#include <opencv2/dnn.hpp>
#include <iostream>

static cv::dnn::Net faceNet;

FaceWatchlist::FaceWatchlist(double threshold) : similarityThreshold_(threshold) {
    try {
        faceNet = cv::dnn::readNetFromTorch("models/openface.nn4.small2.v1.t7");
    } catch (...) {
        std::cerr << "Error: Could not load face embedding model." << std::endl;
    }
}

void FaceWatchlist::loadKnownFaces(const std::vector<std::string>& filePaths) {
    int idCounter = 0;
    for (const auto& path : filePaths) {
        cv::Mat img = cv::imread(path);
        if (img.empty()) continue;

        cv::Mat embedding = computeEmbedding(img);
        knownFaces_.push_back({idCounter++, path, embedding});
    }
}

bool FaceWatchlist::checkForKnownFace(const cv::Mat& faceROI) {
    cv::Mat embedding = computeEmbedding(faceROI);

    for (const auto& kf : knownFaces_) {
        double sim = cosineSimilarity(embedding, kf.embedding);
        if (sim > similarityThreshold_) {
            std::cout << "ALERT: Face found! Matches " << kf.name << std::endl;
            return true;
        }
    }
    return false;
}

cv::Mat FaceWatchlist::computeEmbedding(const cv::Mat& faceROI) {
    cv::Mat blob = cv::dnn::blobFromImage(faceROI, 1.0/255.0, cv::Size(96,96),
                                          cv::Scalar(0,0,0), true, false);
    faceNet.setInput(blob);
    return faceNet.forward().clone();
}

double FaceWatchlist::cosineSimilarity(const cv::Mat& a, const cv::Mat& b) {
    double dot = a.dot(b);
    double normA = cv::norm(a);
    double normB = cv::norm(b);
    return dot / (normA * normB);
}
