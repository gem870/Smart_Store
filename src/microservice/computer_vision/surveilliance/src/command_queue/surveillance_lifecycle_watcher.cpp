#include "surveillance_lifecycle_watcher.hpp"
#include "surveillance/surveillance_controller.hpp"
#include "../../../err_log/Logger.hpp"

#include <cstdlib>
#include <filesystem>
#include <system_error>

namespace {

// Mirrors Surveillance::sentinelPath() exactly -- both processes must agree
// on this path without either one depending on the other's headers. Same
// reasoning already applied to CommandQueueConsumer's own mirrored
// commandQueuePath().
std::filesystem::path sentinelPath() {
    return std::filesystem::temp_directory_path() / "SmartSurveillance" / "surveillance.run";
}

} // namespace

SurveillanceLifecycleWatcher::SurveillanceLifecycleWatcher(SurveillanceController& controller)
    : controller_(controller) {
    thread_ = std::thread(&SurveillanceLifecycleWatcher::workerLoop, this);
}

SurveillanceLifecycleWatcher::~SurveillanceLifecycleWatcher() {
    running_.store(false);
    cv_.notify_all();
    if (thread_.joinable()) {
        thread_.join();
    }
}

void SurveillanceLifecycleWatcher::workerLoop() {
    while (running_.load()) {
        pollOnce();

        std::unique_lock<std::mutex> lock(cvMtx_);
        cv_.wait_for(lock, std::chrono::seconds(3), [this] { return !running_.load(); });
    }
}

void SurveillanceLifecycleWatcher::pollOnce() {
    std::error_code ec;
    const bool present = std::filesystem::exists(sentinelPath(), ec);

    if (present) {
        everObservedPresent_ = true;
        return;
    }

    if (everObservedPresent_) {
        LOG_CONTEXT(LogLevel::INFO,
                    "SurveillanceLifecycleWatcher: sentinel removed after being observed present -- requesting graceful shutdown.",
                    {});
        controller_.requestExternalStop();
        everObservedPresent_ = false;

        // Give mainDisplayLoop() a bounded window to notice
        // externalStopRequested_ and exit through its own same-thread
        // stop() call first -- the clean path, no cross-thread races,
        // exactly what ESC already does. If the whole process exits on its
        // own within this window, ~SurveillanceLifecycleWatcher() (running
        // as part of that normal teardown) sets running_ false and notifies
        // cv_, which wakes the wait below immediately -- no delay added to
        // the already-working case.
        //
        // If it DOESN'T wake early: confirmed against a real spawned
        // instance (no interactive HighGUI message pump being serviced)
        // that cv::waitKey()'s nominal ~10ms timeout is not reliably
        // honored, and mainDisplayLoop() can sit past it indefinitely --
        // exactly the deployment shape a programmatically-spawned station
        // process has. Directly calling controller_.stop() from this
        // thread instead was considered and rejected: reconcileCameraWorkers()
        // (called from mainDisplayLoop() once a second, still on the main
        // thread in this stuck case) has no running_ guard of its own, so a
        // stop() landing between two of its ticks can re-spawn workers that
        // stop() just tore down. Forcing real process termination sidesteps
        // that race entirely, and is strictly better than the pre-existing
        // ESC-only shutdown path's own failure mode ("nothing short of a
        // hard process kill ended a session" -- see run()'s own comment) --
        // this makes that happen automatically instead of needing a human
        // to notice and kill it. std::_Exit(), not std::exit(): skips
        // racing every other still-live thread's destructors from this one.
        std::unique_lock<std::mutex> lock(cvMtx_);
        if (cv_.wait_for(lock, std::chrono::seconds(5), [this] { return !running_.load(); })) {
            return;
        }
        lock.unlock();

        LOG_CONTEXT(LogLevel::WARNING,
                    "SurveillanceLifecycleWatcher: engine did not exit within the grace period after a stop request -- forcing process termination.",
                    {});
        std::_Exit(0);
    }
}
