#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>

class SurveillanceController;

/**
 * @class SurveillanceLifecycleWatcher
 * @brief Polls Surveillance::sentinelPath() (mirrored locally, same
 * reasoning as CommandQueueConsumer's own mirrored commandQueuePath() --
 * both processes must agree on the path without either depending on the
 * other's headers) and drives a graceful shutdown of the local
 * SurveillanceController when the sentinel transitions from present to
 * absent.
 *
 * Only a present -> absent transition triggers a stop, and only once the
 * sentinel has actually been observed present at least one time. This is
 * what keeps a manually/dev-launched instance (no sentinel ever written for
 * it) completely unaffected by this feature -- absence alone, with no prior
 * presence, is never treated as a stop request.
 *
 * Same interruptible-sleep shape as CommandQueueConsumer/
 * FeatureConfigManager::fileWatchLoop() -- its own thread, polls every few
 * seconds rather than blocking on a filesystem-change notification.
 */
class SurveillanceLifecycleWatcher {
public:
    explicit SurveillanceLifecycleWatcher(SurveillanceController& controller);
    ~SurveillanceLifecycleWatcher();

    SurveillanceLifecycleWatcher(const SurveillanceLifecycleWatcher&) = delete;
    SurveillanceLifecycleWatcher& operator=(const SurveillanceLifecycleWatcher&) = delete;

private:
    void workerLoop();
    void pollOnce();

    SurveillanceController& controller_;

    // Starts false; only ever set true once the sentinel has been seen
    // present. Deliberately not reset once the graceful stop has been
    // triggered -- this watcher's job is done for this process's lifetime
    // after that (the process is on its way out).
    bool everObservedPresent_ = false;

    std::thread thread_;
    std::atomic<bool> running_{true};
    std::mutex cvMtx_;
    std::condition_variable cv_;
};
