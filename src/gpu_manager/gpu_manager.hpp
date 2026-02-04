#pragma once
#include <opencv2/dnn.hpp>

/**
 * @class GPUManager
 * @brief Centralized utility for detecting and enabling GPU acceleration (CUDA) in OpenCV DNN.
 *
 * This class provides a static interface to:
 *  - Detect whether a CUDA-capable GPU is available at runtime.
 *  - Enable or disable GPU usage globally across the framework.
 *  - Query whether GPU acceleration is currently active.
 *
 * Design notes:
 *  - Detection is idempotent: calling init() multiple times will only perform detection once.
 *  - All members are static, so GPUManager acts as a global singleton.
 *  - Intended to be called at framework startup, but safe to call from any service.
 */
class GPUManager {
public:
    /**
     * @brief Detect GPU availability.
     *
     * Attempts to configure OpenCV's DNN backend to use CUDA. If successful,
     * sets the availability flag to true. If CUDA is not available or initialization fails,
     * falls back to CPU and sets availability to false.
     *
     * This method is idempotent: repeated calls will not re-run detection.
     */
    static void init();

    /**
     * @brief Check if a CUDA-capable GPU is available.
     * @return true if CUDA GPU is available, false otherwise.
     */
    static bool isAvailable();

    /**
     * @brief Check if GPU usage is enabled and available.
     *
     * This returns true only if:
     *  - A CUDA GPU was detected (isAvailable() == true), AND
     *  - GPU usage has been explicitly enabled via setUseGPU(true).
     *
     * @return true if GPU acceleration is active, false otherwise.
     */
    static bool useGPU();

    /**
     * @brief Enable or disable GPU usage globally.
     *
     * If enable == true but no GPU is available, logs a warning and forces CPU fallback.
     * Otherwise, sets the global flag accordingly.
     *
     * @param enable Whether to request GPU usage.
     */
    static void setUseGPU(bool enable);

private:
    /// Whether CUDA GPU is available on this system
    static bool available_;

    /// Whether GPU usage has been requested by the framework
    static bool useGPU_;

    /// Whether init() has already run (prevents redundant detection)
    static bool initialized_;
};
