
#include <fstream>
#include <sstream>
#include <chrono>
#include <queue>
#include <string>

#include <err_log/Logger.hpp>
#include "network_agent.hpp"

#include <asio.hpp>
#include <asio/ts/internet.hpp>
#include <asio/ts/buffer.hpp>


#include <nlohmann/json.hpp>

// Initialization
void NetworkAgent::initialize(const std::string& id, const std::string& type, const std::string& path) {
    objectID   = id;
    objectType = type;
    basePath   = path;

    autoSync      = false;
    alertOnChange = false;
    peerDiscovery = false;

    lastPayloadSize = 0;
}

// Messaging
void NetworkAgent::sendMessage(const std::string& payload, const std::string& recipientID) {
    Message msg{recipientID, payload, std::chrono::system_clock::now()};
    outbox.push(msg);
    lastPayloadSize = payload.size();
    lastPayloadType = "text";
    lastCommTime    = msg.timestamp;
    processoutbox();
}

void NetworkAgent::receiveMessage(const Message& msg) {
    inbox.push(msg);

    while (!inbox.empty()) {
        Message m = inbox.front();
        inbox.pop();

        Logger::LOG_CONTEXT(LogLevel::INFO,
            "\nIncoming Message:\n{\nReceived from: " + m.senderID +
            "\n message: " + m.payload +
            "\nTime stamp: " + std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    m.timestamp.time_since_epoch()
                ).count()
            ) + "\n}\n"
        , {});
    }
}

void NetworkAgent::processoutbox() {
    if (outbox.empty()) {
        Logger::LOG_CONTEXT(LogLevel::ERR, "Outbox is empty, nothing to send.", {});
        return;
    }

    while (!outbox.empty()) {
        Message msg = outbox.front();
        outbox.pop();
        sendToServer(msg);
        Logger::LOG_CONTEXT(LogLevel::INFO,
            "\nIncoming Message:\n{\nReceived from: " + msg.senderID +
            "\n message: " + msg.payload +
            "\nTime stamp: " + std::to_string(
                std::chrono::duration_cast<std::chrono::seconds>(
                    msg.timestamp.time_since_epoch()
                ).count()
            ) + "\n}\n"
        , {});
    }
}

// File I/O
bool NetworkAgent::writeToFile(const std::string& filename, const std::string& content) {
    std::ofstream file(basePath + "/" + filename);
    if (!file.is_open()) {
        LOG_CONTEXT(LogLevel::ERR, "Failed to open file.", false);
        return false;
    }
    file << content;
    LOG_CONTEXT(LogLevel::ERR, "Successfully written to file", ErrorCode::FLAG_TRUE);
    return true;
}

std::string NetworkAgent::readFromFile(const std::string& filename) {
    std::ifstream file(basePath + "/" + filename);
    if (!file.is_open()) return "";
    return std::string((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
}

bool NetworkAgent::fileExists(const std::string& filename) {
    std::ifstream file(basePath + "/" + filename);
    return file.good();
}

// Networking (stubbed out, no web/HTTP)
void NetworkAgent::sendToServer(Message msg) {
    try {
        asio::io_context ioc;

        asio::ip::tcp::resolver resolver(ioc);
        asio::ip::tcp::socket socket(ioc);

        auto endpoints = resolver.resolve("127.0.0.1", "5000");
        asio::connect(socket, endpoints);

        // Serialize message to JSON
        nlohmann::json j;
        j["senderID"]  = msg.senderID;
        j["payload"]   = msg.payload;
        j["timestamp"] = std::chrono::duration_cast<std::chrono::seconds>(
            msg.timestamp.time_since_epoch()
        ).count();

        std::string body = j.dump(4);

        // Send JSON string
        asio::write(socket, asio::buffer(body));

        Logger::log_base(LogLevel::INFO, "Sent message to server:\n" + body);

        // Optional: read response
        char reply[1024];
        asio::error_code ec;
        size_t len = socket.read_some(asio::buffer(reply), ec);

        if (!ec) {
            std::string response(reply, len);
            Logger::log_base(LogLevel::INFO, "Server replied:\n" + response);
        } else {
            Logger::log_base(LogLevel::ERR, "Read error: " + ec.message());
        }

        socket.close();
    }
    catch (const std::exception& e) {
        Logger::log_base(LogLevel::ERR, "Asio sendToServer error: " + std::string(e.what()));
    }
}


void NetworkAgent::uploadFile(const std::string& filepath) {
    // Removed HTTP upload logic.
    Logger::log_base(LogLevel::INFO,
        "Stubbed uploadFile: would upload file -> " + filepath);
}

void NetworkAgent::downloadFile(const std::string& filename) {
    // Removed HTTP download logic.
    Logger::log_base(LogLevel::INFO,
        "Stubbed downloadFile: would download file -> " + filename);
}


