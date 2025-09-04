#pragma once
#include <iostream>
#include <string>
#include <err_log/Logger.hpp>



//     ::::::::::::::::::::::::::::::::::::::::::::
//     :: *  © 2025 Victor. All rights reserved. ::
//     :: *  Smart_Store Framework               ::
//     :: *  Licensed under the MIT License      ::
//     ::::::::::::::::::::::::::::::::::::::::::::

#define SMART_STORE_VERSION "1.0.0"
#define SMART_STORE_AUTHOR "Victor"
#define SMART_STORE_YEAR "2025"

const char* SMART_STORE_SIGNATURE = 
    "Smart_Store Framework\n"
    "Author:  " SMART_STORE_AUTHOR "\n"
    "Version: " SMART_STORE_VERSION "\n"
    "License: MIT\n"
    "© " SMART_STORE_YEAR " " SMART_STORE_AUTHOR ". All rights reserved.";



class Author {
public:
   
    static void getSignature() {
        std::cout << "\n";
        LOG_CONTEXT(LogLevel::INFO, 
            "\n:::::::| Smart_Store Author Signature |:::::::\n\n" +
                std::string(SMART_STORE_SIGNATURE) + 
                "\n\n---\n" + 
                 "Smart_Store was built by Victor to solve\n" +
                 "serialization challenges in C++. It introduces\n" +
                 "a metadata-driven instantiation model, multi-format\n" +
                 "serialization (JSON, XML, binary, and more to come),\n" +
                 "and undo/redo logic—designed for extensibility, performance,\n" +
                 "and architectural clarity.\n\n" +

                 "MIT:\n" +
                    "Permission is hereby granted, free of charge, to any person\n" +
                    "obtaining a copy of this software and associated documentation\n" +
                    "files (the \"Software\"), to deal in the Software without\n" +
                    "restriction, including without limitation the rights to use,\n" +
                    "copy, modify, merge, publish, distribute, sublicense, and/or\n" +
                    "sell copies of the Software, and to permit persons to whom\n" +
                    "the Software is furnished to do so, subject to the following\n" +
                    "conditions:\n\n" +
                    "The above copyright notice and this permission notice shall be\n" +
                    "included in all copies or substantial portions of the Software.\n\n" +
                    "THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND,\n" +
                    "EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES\n" +
                    "OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND\n" +
                    "NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT\n" +
                    "HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n" +
                    "LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR\n" +
                    "OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH\n" +
                    "THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE." +
                    "\n" +
                 "---" +
            "\n:::::::::::::::::::::::::::::::::::::::::::::::::::::::::\n", {});
    }
};    