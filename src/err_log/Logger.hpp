#pragma once
#include <iostream>
#include <string>
#include <ctime>


// ::::| Logger class for logging messages with different levels and colors
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::



enum class LogLevel { INFO, WARNING, ERR, DEBUG };
enum class LogColor { RESET, RED, GREEN, YELLOW, BLUE, MAGENTA, CYAN, WHITE };

class Logger {
public:

    static void log(LogLevel level, const std::string& message) {
        std::string prefix = getPrefix(level);
        std::string timestamp = getTimestamp();
        std::cout << getColorCode(LogColor::CYAN) + "[" + getColorCode(LogColor::RESET) << timestamp << getColorCode(LogColor::CYAN) + "]" + getColorCode(LogColor::RESET) << prefix << " " + getColorCode(LogColor::CYAN) << message << getColorCode(LogColor::RESET) << std::endl;
    }

    static std::string getStamp() {
         return getColorCode(LogColor::CYAN) + "["  + getColorCode(LogColor::RESET) + getTimestamp() + getColorCode(LogColor::CYAN) + "] " + getColorCode(LogColor::RESET);
    }
    
    // Returns a color code for terminal output
    static std::string getColorCode(LogColor color) {
        switch (color) {
            case LogColor::RED: return "\033[1;31m";
            case LogColor::GREEN: return "\033[1;32m";
            case LogColor::YELLOW: return "\033[1;33m";
            case LogColor::BLUE: return "\033[1;34m";
            case LogColor::MAGENTA: return "\033[1;35m";
            case LogColor::CYAN: return "\033[1;36m";
            case LogColor::WHITE: return "\033[1;37m";
            default: return "\033[0m"; // No color
        }
    }

private:
    // Returns the current timestamp in a formatted string
    static std::string getTimestamp() {
        std::time_t now = std::time(nullptr);
        char buf[20];
        std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
        return std::string(buf);
    }

    // Returns a prefix string based on the log level
    static std::string getPrefix(LogLevel level) {
        switch (level) {
            case LogLevel::INFO: return  getColorCode(LogColor::GREEN) + " :::| INFO:" + getColorCode(LogColor::RESET);
            case LogLevel::WARNING: return getColorCode(LogColor::RED) +" :::| WARNING:"  + getColorCode(LogColor::RESET);
            case LogLevel::ERR: return getColorCode(LogColor::RED) + " :::| ERROR:"  + getColorCode(LogColor::RESET);
            case LogLevel::DEBUG: return getColorCode(LogColor::MAGENTA) + " :::| DEBUG:" + getColorCode(LogColor::RESET);
        }
        return "LOG";
    }

};
