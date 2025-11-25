#pragma once
#include <string>
#include "message.hpp"

class BaseMicroservice {
public:
    virtual ~BaseMicroservice() = default;  // inline definition, no cpp needed

    // Pure virtual interface
    virtual void sendMessage(const std::string& payload,
                             const std::string& recipientID) = 0;

    virtual void receiveMessage(const Message& msg) = 0;

    virtual bool writeToFile(const std::string& filename,
                             const std::string& content) = 0;

    virtual std::string readFromFile(const std::string& filename) = 0;
};