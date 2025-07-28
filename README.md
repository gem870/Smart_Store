
# 🛍️ Smart_Store

![Smart_Store Banner](assets/smartImage.png)

<p align="center">
  <b>A High-Performance, Modern C++ Item Manager for Inventory, Asset Tracking, and Data Persistence.</b>
</p>

---

## Overview

<b>Smart_Store</b> is a modular, extensible, and high-performance C++ framework for managing, serializing, deserializing, and organizing complex item data across multiple formats. Perfect for inventory systems, asset management, data editors, and any application needing structured, taggable, and type-safe storage of arbitrary objects.

### Key Features
- **Undo / Redo History** — Safe state rollback with snapshots
- **Versioned Schema Migration** — Upgrade legacy data automatically
- **Multi-Format Import/Export** — JSON, XML, CSV, Binary
- **Dynamic Type Registration** — Add custom types with zero boilerplate
- **Tag-Based Lookup** — Fast, flexible item access
- **Safe Deserialization** — Registered handlers for type safety
- **Extensive Logging** — Color-coded, timestamped debug output
- **Thread-Safe API** — Concurrent access and modification

---

## Feature Matrix

| Feature                | Status | Description                              |
|------------------------|--------|------------------------------------------|
| Undo / Redo            | ✅     | Safe state rollback through snapshots    |
| JSON Import/Export     | ✅     | Schema versioning & upgrade support      |
| CSV Import/Export      | ✅     | Standard data format compatibility       |
| XML Import/Export      | ✅     | Human-readable, structured format        |
| Binary Import/Export   | ✅     | Compact, efficient persistence           |
| Schema Upgrades        | ✅     | Future-proof with migration strategies   |
| Dynamic Types          | ✅     | Register custom object types easily      |

---

## Technologies

- **C++20** — Modern language features
- **nlohmann::json** — Fast, flexible JSON serialization
- **TinyXML2** — Lightweight XML support
- **Smart Pointers & RAII** — Memory safety
- **Modern STL** — `std::map`, `std::optional`, `std::shared_ptr`
- **Type-Safe Deserialization Registry**
- **Custom Logging Utility** — ANSI color support

---

## Directory Structure
![file structure](https://github.com/gem870/Smart_Store/blob/main/assets/file%20structure.PNG)

---

## Quick Example

```cpp
#include "t_manager/ItemManager.h"

int main() {
    ItemManager manager;

    manager.addItem(std::make_shared<int>(42), "item1");
    manager.displayByTag("item1");

    manager.removeByTag("item1");
    manager.displayByTag("item1");

    manager.importFromFile_JSON("inventory.json");
    manager.exportToFile_CSV("backup.csv");

    manager.undo();
    manager.redo();

    return 0;
}
```

---

| Format | Import | Export |
|--------|--------|--------|
| JSON   | ✅     | ✅     |
| CSV    | ✅     | ✅     |
| XML    | ✅     | ✅     |
| Binary | ✅     | ✅     |

---

## Why Choose Smart_Store?

- **Modern & Maintainable:** Clean, idiomatic C++20 codebase
- **Zero Boilerplate:** Automatic type registration—just use `addItem`
- **No Inheritance Required:** Works with any object type
- **Data Safety:** Undo history ensures you never lose a state
- **Robust Migration:** Built-in schema upgrades for legacy data
- **Multi-Format Ready:** Seamless transitions between formats

---

## Installation

```bash
git clone https://github.com/gem870/Smart_Store.git
cd Smart_Store
mkdir build
cd build
cmake ..
cmake --build .
./TestApp
```

---

## License

MIT License. See [LICENSE](LICENSE) for details.

---

## Contributing

We welcome contributions from the community! Whether it's fixing bugs, suggesting new features, improving documentation, or refactoring code, your help is appreciated.

### How to Contribute

1. Fork the repository
2. Create a branch for your feature or bugfix
3. Commit your changes with clear messages
4. Open a Pull Request (PR) with a detailed description
5. Ensure your contribution aligns with the project's guidelines

---

## Resources

- [Code of Conduct](https://github.com/gem870/Smart_Store/blob/main/CODE_OF_CONDUCT.md)
- [Contribution Guidelines](https://github.com/gem870/Smart_Store/blob/main/CONTRIBUTING.md)
- [Project Wiki](https://github.com/gem870/Smart_Store/wiki)

---

## Contact

**Author:** Emmanuel Chibuike Victor  
**Email:** ve48381@gmail.com  
[LinkedIn](https://linkedin.com/in/chibuike-emmanuel-b8b29b269/)  
[Portfolio](https://emmanuelvictor-portfolio.vercel.app)



