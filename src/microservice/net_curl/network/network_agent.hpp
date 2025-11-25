#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <fstream>
#include "message.hpp"
#include <iostream> // For std::cout/cerr in public functions (optional)
#include "interface/BaseMicroservice.hpp"

class NetworkAgent : public BaseMicroservice {
public:
    // Identity
    std::string objectID;
    std::string objectType;
    std::string owner;

    // Connection Profile
    std::string ipAddress;
    std::string macAddress;
    std::string wifiSSID;
    std::string bluetoothID;
    std::string networkStatus;

    // Security
    std::string authToken;
    std::string encryptionKey;
    std::string accessLevel;

    // Communication Queues
    std::queue<Message> inbox;
    std::queue<Message> outbox;
    std::mutex inboxMutex;
    std::mutex outboxMutex;

    // Protocol & Endpoint
    std::string protocol;
    std::string endpoint;

    // Payload Metadata
    size_t lastPayloadSize = 0; // Default initializer
    std::string lastPayloadType;
    std::chrono::system_clock::time_point lastCommTime;

    // Behavioral Flags
    bool autoSync = false; // Default initializer
    bool alertOnChange = false; // Default initializer
    bool peerDiscovery = false; // Default initializer

    // File Handling
    std::string basePath;

    // Initialization
    void initialize(const std::string& id, const std::string& type, const std::string& path);

    // Messaging
    void sendMessage(const std::string& payload, const std::string& recipientID) override;
    void receiveMessage(const Message& msg) override;
    void processoutbox();

    // File I/O
    bool writeToFile(const std::string& filename, const std::string& content) override;
    std::string readFromFile(const std::string& filename) override;
    bool fileExists(const std::string& filename);

    // Networking (stubbed for now)
    void sendToServer(Message msg);
    void uploadFile(const std::string& filepath);   
    void downloadFile(const std::string& filename); 
    // Encryption / Decryption
    std::string encryptMessage(const std::string& plainText);
    std::string decryptMessage(const std::string& cipherText);
};