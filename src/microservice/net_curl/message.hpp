#pragma once
#include <string>
#include <queue>
#include <mutex>
#include <chrono>
#include <fstream>
#include <iostream>

struct Message {
    std::string senderID;
    std::string payload;
    std::chrono::system_clock::time_point timestamp;
};


