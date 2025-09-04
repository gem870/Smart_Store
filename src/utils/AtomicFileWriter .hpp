
//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::

#pragma once
#include <fstream>
#include <string>
#include <filesystem>
#include <system_error>

//::::: AtomicFileWriter class
//****************************

class AtomicFileWriter {
public:

    // Writes a string atomically to a file
    static bool writeAtomically(const std::string& targetFilename, const std::string& content) {
        std::string tempFilename = targetFilename + ".tmp";

        std::ofstream out(tempFilename, std::ios::trunc);
        if (!out.is_open()) return false;

        out << content;
        out.close();

        std::error_code ec;
        std::filesystem::rename(tempFilename, targetFilename, ec);
        return !ec;
    }

    // Writes binary data atomically to a file
   static bool writeAtomicallyBinary(const std::string& targetFilename, const std::vector<uint8_t>& binaryData) {
        std::string tempFilename = targetFilename + ".tmp";

        std::ofstream out(tempFilename, std::ios::binary);
        if (!out.is_open()) return false;

        out.write(reinterpret_cast<const char*>(binaryData.data()), binaryData.size());
        out.close();

        std::filesystem::rename(tempFilename, targetFilename);
        return true;
    }

};
