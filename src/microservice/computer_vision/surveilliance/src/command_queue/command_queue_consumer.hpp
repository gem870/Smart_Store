#pragma once

#include "config_file/config.hpp"

#include <nlohmann/json.hpp>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>

/**
 * @class CommandQueueConsumer
 * @brief Drains the JSON-Lines command queue the ItemManager-side
 * Surveillance/ItemWrapper/ItemManager passthrough layer writes to
 * (see Surveillance.hpp's own doc comment and commandQueuePath()) and
 * applies each command to FeatureConfigManager (and, for DataBaseManager
 * passthroughs, features().getDBManager()) -- the "someone reads and
 * applies this" half that class was always written to expect but never
 * had, since ItemManagerProject and this app are separate processes with
 * no in-process call path between them.
 *
 * Runs on its own thread, same interruptible-sleep shape as
 * FeatureConfigManager::fileWatchLoop() -- polls commandQueuePath() every
 * few seconds rather than blocking on a filesystem-change notification,
 * since this queue can go quiet for long stretches and a missed OS-level
 * file-change event would otherwise mean a command sits unapplied
 * indefinitely.
 *
 * Query-style commands (getAllTrackedFaces, getEventById, ...) are applied
 * here (the underlying FeatureConfigManager/DataBaseManager call really
 * happens) and the real result is written back via writeResponse() to
 * Surveillance::responsesPath(), keyed by the requestId the original
 * Surveillance-side call returned -- see Surveillance::drainResponses()'s
 * own doc comment for how ItemManager reads these back. Every other
 * (mutating) command stays fire-and-forget, no response written.
 */
class CommandQueueConsumer {
public:
    explicit CommandQueueConsumer(FeatureConfigManager& features);
    ~CommandQueueConsumer();

    CommandQueueConsumer(const CommandQueueConsumer&) = delete;
    CommandQueueConsumer& operator=(const CommandQueueConsumer&) = delete;

private:
    void workerLoop();

    // Reads every currently-queued line, applies each in order, then
    // truncates the file -- see Surveillance::writeCommand()'s doc comment:
    // "single writer... reader is responsible for truncating/rotating this
    // file as it drains it." A command appended in the exact window between
    // the read and the truncate is lost; acceptable for this fire-and-forget
    // local queue, same as every other best-effort file-based channel in
    // this codebase (no delivery guarantees are documented or relied on
    // anywhere this queue is written to).
    void drainQueue();

    // One command's worth of dispatch -- looks up `command` in a table of
    // {FeatureConfigManager/DataBaseManager call} lambdas built once and
    // applies `params`. Logs and returns (never throws out of here) on an
    // unknown command name or a params shape that doesn't match what the
    // command needs, so one malformed line can't stop the rest of the batch
    // or crash this thread. requestId is only used by query-style branches
    // (to call writeResponse()) -- mutating branches ignore it.
    void dispatch(const std::string& requestId, const std::string& command, const nlohmann::json& params);

    // Appends {"requestId", "command", "result", "timestamp"} to
    // Surveillance::responsesPath() -- the reverse-direction counterpart to
    // Surveillance::writeCommand(), same single-writer/no-locking reasoning.
    void writeResponse(const std::string& requestId, const std::string& command, const nlohmann::json& result);

    FeatureConfigManager& features_;

    std::thread thread_;
    std::atomic<bool> running_{true};
    std::mutex cvMtx_;
    std::condition_variable cv_;
};
