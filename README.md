# 🛍️ Smart_Store

![Smart_Store Banner](assets/smartImage.png)

> **A High-Performance, Modern C++ Item Manager for Inventory, Asset Tracking, and Data Persistence.**

---

##  Overview

**Smart_Store** is a modular, extensible, and high-performance C++ framework designed for managing, serializing, deserializing, and organizing complex item (Objects) data across multiple formats. This project is ideal for building inventory systems, asset management tools, data editors, and any application requiring structured, taggable, and type-safe storage of arbitrary objects.

It supports powerful features like:
-  **Undo / Redo History**
-  **Versioned Schema Migration**
-  **Multiple Import/Export Formats** (JSON, XML, CSV, Binary)
-  **Dynamic Item Type Registration**
-  **Tag-Based Lookup**
-  **Safe Deserialization via Registered Handlers**
-  **Extensive Logging for Debugging**
-  **Schema Versioning with Upgrade Path Support**
-  **Thread-Safe and non thread API for concurrent access and modifications.**

---

##  Features Breakdown
| Feature              | Status | Description                              |
|----------------------|--------|------------------------------------------|
| Undo / Redo          | ✅     | Safe state rollback through snapshots    |
| JSON Import/Export   | ✅     | Supports schema versioning & upgrade     |
| CSV Import/Export    | ✅     | Supports standard data formats           |
| XML Import/Export    | ✅     | Simple and human-readable format         |
| Binary Import/Export | ✅     | Compact and efficient persistence        |
| Schema Upgrades      | ✅     | Future-proof with migration strategies   |
| Dynamic Types        | ✅     | Register custom object types easily      |

---

##  Technologies Used

- **C++20**
- **nlohmann::json**
- **TinyXML2**
- **Custom Smart Pointers & RAII Utilities**
- **Modern STL (std::map, std::optional, std::shared_ptr)**
- **Type-safe Deserialization Registry**
- **Custom Logging Utility with ANSI Color Support**
- **std::shared_ptr (Smart Pointers for Memory Safety)**

---

##  Directory Structure
![file structure](https://github.com/gem870/Smart_Store/blob/main/assets/file%20structure.PNG)
#
## Example:

<pre> ```
#include "ItemManager.h"

    
int main(){
    ItemManager manager;

    manager.addItem(std::make_shared<int>(42), "item1");
    manager.displayByTag("item1");  // Directly display the item

    manager.removeByTag("item1");

    manager.displayByTag("item1");

    manager.importFromFile_JSON("inventory.json");
    manager.exportToFile_CSV("backup.csv");

    manager.undo();
    manager.redo();

    return 0;
}
        
 ``` </pre>



| Format | Import  | Export  |
| ------ | ------- | ------  |
| JSON   | ✅      | ✅     |
| CSV    | ✅      | ✅     |
| XML    | ✅      | ✅     |
| Binary | ✅      | ✅     |


## Why Smart_Store?
- **Highly Maintainable: Clean, modern C++ practices.**
- **Extendable: Automatic type registration without altering core logic.**
- **No inheritance of an object or boilaplate is required.**
- **Only the use of addItem function, every other implementation**
  **regards to file format, schema aware and more will be done automatically.**
- **Data-Safe: Undo history ensures you never lose a state.**
- **Robust Migration: Built-in support for upgrading legacy schemas.**
- **Multi-Format Ready: Seamless transitions between data formats.**


## Installation

<pre> ```bash git clone https://github.com/gem870/Smart_Store.git 
    cd Smart_Store
    mkdir build
    cd build
    cmake ..
    cmake --build .
    ./TestApp 
``` </pre>


## License
MIT License. See LICENSE for details.
#
## Contributing
We welcome contributions from the community!
Whether it's fixing bugs, suggesting new features, improving documentation, or refactoring code, your help is appreciated.

## How to Contribute
* Fork the repository.
* Create a branch for your feature or bugfix.
* Commit your changes with clear messages.
* Open a Pull Request (PR) with a detailed description of your changes.
* Ensure your contribution aligns with the project's guidelines.


## Important Resources
 Code of Conduct: https://github.com/gem870/Smart_Store/blob/main/CODE_OF_CONDUCT.md

 Contribution Guidelines: https://github.com/gem870/Smart_Store/blob/main/CONTRIBUTING.md

 Project Documentation (Wiki):https://github.com/gem870/Smart_Store/wiki



## Contact </br>
Author: Emmanuel Chibuike Victor</br></br>
Email: ve48381@gmail.com </br>
LinkedIn: linkedin.com/in/chibuike-emmanuel-b8b29b269/ </br>
Portfolio: emmanuelvictor-portfolio.vercel.app </br>



