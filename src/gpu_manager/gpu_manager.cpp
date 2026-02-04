#include "gpu_manager.hpp"
#include <opencv2/core.hpp>
#include <opencv2/dnn.hpp>
#include <iostream>

// Static member definitions
bool GPUManager::available_   = false;
bool GPUManager::useGPU_      = false;
bool GPUManager::initialized_ = false;

void GPUManager::init() {
    if (initialized_) return; // avoid redundant detection
    initialized_ = true;

    try {
        cv::dnn::Net testNet;

        // Attempt to set CUDA backend
        testNet.setPreferableBackend(cv::dnn::DNN_BACKEND_CUDA);
        testNet.setPreferableTarget(cv::dnn::DNN_TARGET_CUDA);

        available_ = true;
        std::cout << "[GPUManager] CUDA GPU detected and available.\n";
    } catch (const cv::Exception& e) {
        available_ = false;
        std::cerr << "[GPUManager] No CUDA GPU detected, falling back to CPU. "
                  << "Reason: " << e.what() << "\n";
    } catch (...) {
        available_ = false;
        std::cerr << "[GPUManager] GPU initialization failed due to unknown error. "
                  << "Falling back to CPU.\n";
    }
}

bool GPUManager::isAvailable() {
    return available_;
}

bool GPUManager::useGPU() {
    return useGPU_ && available_;
}

void GPUManager::setUseGPU(bool enable) {
    if (enable && !available_) {
        std::cerr << "[GPUManager] GPU requested but not available. Falling back to CPU.\n";
        useGPU_ = false;
    } else {
        useGPU_ = enable;
        std::cout << "[GPUManager] GPU usage set to "
                  << (useGPU_ ? "ENABLED" : "DISABLED") << ".\n";
    }
}
