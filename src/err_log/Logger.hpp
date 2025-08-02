#pragma once
#include <iostream>
#include <string>
#include <ctime>
#include <variant>
#define LOG_CONTEXT(level, message, hint) \
    Logger::log_with_context(level, message, hint, __FILE__, __LINE__, __func__)


// ::::| Logger class for logging messages with different levels and colors
// ::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::::

template<class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

template<class... Ts>
overloaded(Ts...) -> overloaded<Ts...>;



enum ErrorCode {
  // Resource-related errors
  ITEM_NOT_FOUND       = 1004, // Requested item or object does not exist
  CONFIG_MISSING       = 1005, // Critical configuration file or value not found
  FILE_LOAD_FAILED     = 1006, // File could not be opened or read
  MODULE_NOT_REGISTERED= 1007, // Referenced module is missing from system registry

  // Authentication / Access errors
  UNAUTHORIZED_ACCESS  = 2001, // User lacks permission for operation
  TOKEN_EXPIRED        = 2002, // Auth token has expired or is invalid
  USER_NOT_FOUND       = 2003, // Login attempted with unknown user ID

  // Network / Communication errors
  NETWORK_TIMEOUT      = 3001, // Network request exceeded allowed time
  CONNECTION_FAILED    = 3002, // Unable to connect to server or endpoint
  PROTOCOL_ERROR       = 3003, // Unexpected format or violation in communication protocol

  // Logic / Processing errors
  INVALID_INPUT        = 4001, // Provided data or arguments are malformed
  PROCESSING_FAILED    = 4002, // Operation failed during execution
  NULL_POINTER_DETECTED= 4003, // Unexpected null reference encountered

  // System / Internal errors
  OUT_OF_MEMORY        = 5001, // Memory allocation failed
  THREAD_DEADLOCK      = 5002, // Multiple threads locked and cannot proceed
  UNKNOWN_ERROR        = 5999  // A catch-all for unexpected internal issues
};


enum class LogLevel {
    INFO,
    WARNING, 
    ERR, 
    DEBUG 
};

enum class LogColor {
    RESET, 
    RED, 
    GREEN, 
    YELLOW, 
    BLUE, 
    MAGENTA, 
    CYAN, 
    WHITE 
};

class Logger {
public:
using ErrorHint = std::variant<std::monostate, std::nullptr_t, std::exception_ptr, int ,std::string, std::optional<std::string>>;

    static void log_base(LogLevel level, const std::string& message) {
        std::string prefix = getPrefix(level);
        std::string timestamp = getTimestamp();
        std::cout << getColorCode(LogColor::CYAN) + "[" + getColorCode(LogColor::RESET) << timestamp << getColorCode(LogColor::CYAN) + "]" + getColorCode(LogColor::RESET) << prefix << " " + getColorCode(LogColor::CYAN) << message << getColorCode(LogColor::RESET) << std::endl;
    }

   static void log_with_context(LogLevel level,
                             const std::string& message,
                             const ErrorHint& hint,
                             const char* file,
                             int line,
                             const char* function) {
    std::string context = std::string(file) +
                          " | Function: " + function +
                          " | Line: " + std::to_string(line);

    std::visit(overloaded{
        [&](std::monostate) {
            log_base(level, message + "  ");
        },
        [&](std::nullptr_t) {
            log_base(level, message + " — Null detected. " + context);
        },
        [&](std::exception_ptr eptr) {
            try {
                if (eptr) std::rethrow_exception(eptr);
            } catch (const std::exception& e) {
                throw std::runtime_error(getStamp() + " " + getPrefix(level) + " " + getColorCode(LogColor::CYAN) + 
                                    message + " " + std::string(e.what()) + "\n" + context + getColorCode(LogColor::RESET));
            }
        },
        [&](int code) {
            throw std::runtime_error(getStamp() + " " + getPrefix(level) + " " + getColorCode(LogColor::CYAN) + 
                                    message + " — Code: " + std::to_string(code) + "\n" + context + getColorCode(LogColor::RESET));
        },
        [&](const std::string& extra) {
            log_base(level, message + " — " + extra);
        },
        [&](const std::optional<std::string>& opt) {
            if (opt.has_value()) {
                log_base(level, message + " — " + opt.value());
            } else {
                log_base(level, message + " — Optional value missing. ");
            }
        }
    }, hint);
    }


    static std::string getStamp() {
         return getColorCode(LogColor::RED) + "\n["  + getColorCode(LogColor::RESET) + getTimestamp() + getColorCode(LogColor::RED) + "]" + getColorCode(LogColor::RESET);
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
