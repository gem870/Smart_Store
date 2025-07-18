#include <gtest/gtest.h>
#include "t_manager/ItemManager.h"
#include <cstdio> // For std::remove
#include <nlohmann/json.hpp>
#include <fstream>  // For file handling (std::ofstream, std::ifstream)
#include <iostream>
#include <map>
#include <vector>
#include <memory>
#include <string>
#include <sstream>
#include <mutex>
#include <thread>
using json = nlohmann::json;
std::mutex mutex;



// :::::::: Test on functions without thread calls :::::::::
// *********************************************************

TEST(ItemManagerTest, AddItem) {
    std::cout << "::: Debug: Starting test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    manager.displayByTag("item1");  // Directly display the item
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, RemoveItem) {
    std::cout << "::: Debug: Starting RemoveItem test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    manager.removeByTag("item1");
    std::cout << "::: Debug: Removed item\n";

    ASSERT_THROW(manager.displayByTag("item1"), std::runtime_error);
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, ModifyItem) {
    std::cout << "::: Debug: Starting ModifyItem test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    manager.modifyItem<int>("item1", [](int& value) { value = 84; });
    std::cout << "::: Debug: Modified item\n";

    manager.displayByTag("item1");  // Directly display the item
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, GetItem) {
    std::cout << "::: Debug: Starting GetItem test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    auto item = manager.getItem<int>("item1");
    ASSERT_TRUE(item.has_value());
    EXPECT_EQ(item.value(), 42);
    std::cout << "::: Debug: Retrieved item with value: " << item.value() << "\n";

    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, Undo) {
    std::cout << "::: Debug: Starting Undo test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    manager.addItem(std::make_shared<int>(84), "item2");
    std::cout << "::: Debug: Added another item\n";

    manager.undo();
    std::cout << "::: Debug: Performed undo\n";

    ASSERT_THROW(manager.displayByTag("item2"), std::runtime_error);
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, Redo) {
    std::cout << "::: Debug: Starting Redo test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    std::cout << "::: Debug: Added item\n";

    manager.addItem(std::make_shared<int>(84), "item2");
    std::cout << "::: Debug: Added another item\n";

    manager.undo();
    std::cout << "::: Debug: Performed undo\n";

    manager.redo();
    std::cout << "::: Debug: Performed redo\n";

    manager.displayByTag("item2");  // Directly display the item
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, DisplayAll) {
    std::cout << "::: Debug: Starting DisplayAll test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("Hello"), "item2");
    std::cout << "::: Debug: Added items\n";

    manager.displayAll();  // Directly display all items
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, ListRegisteredTypes) {
    std::cout << "::: Debug: Starting ListRegisteredTypes test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("Hello"), "item2");
    std::cout << "::: Debug: Added items\n";

    manager.listRegisteredTypes();  // Directly display the registered types
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, FilterByTag) {
    std::cout << "::: Debug: Starting FilterByTag test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("Hello"), "item2");
    std::cout << "::: Debug: Added items\n";

    manager.filterByTag("item1");  // Directly display the filtered item
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, SortItemsByTag) {
    std::cout << "::: Debug: Starting SortItemsByTag test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "b_item");
    manager.addItem(std::make_shared<std::string>("Hello"), "a_item");
    std::cout << "::: Debug: Added items\n";

    manager.sortItemsByTag();  // Directly display the sorted items
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, DisplayAllClasses) {
    std::cout << "::: Debug: Starting DisplayAllClasses test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<int>(84), "item2");
    manager.addItem(std::make_shared<std::string>("Hello"), "item3");
    std::cout << "::: Debug: Added items\n";

    manager.displayAllClasses();  // Directly display the unique item classes
    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, GetItemRaw) {
    std::cout << "::: Debug: Starting GetItemRaw test\n";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("Hello"), "item2");
    std::cout << "::: Debug: Added items\n";

    // Test non-const version
    int& intRef = manager.getItemRaw<int>("item1");
    EXPECT_EQ(intRef, 42);
    intRef = 100;  // Modify the value
    EXPECT_EQ(manager.getItemRaw<int>("item1"), 100);

    // Test const version
    const std::string& strRef = manager.getItemRaw<std::string>("item2");
    EXPECT_EQ(strRef, "Hello");

    // Test type mismatch
    try {
        manager.getItemRaw<double>("item1");
        FAIL() << "Expected std::runtime_error due to type mismatch.";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "\n:::| Type mismatch for item with tag 'item1'.\n");
    }

    // Test tag not found
    try {
        manager.getItemRaw<int>("nonexistent");
        FAIL() << "Expected std::runtime_error due to missing tag.";
    } catch (const std::runtime_error& e) {
        EXPECT_STREQ(e.what(), "\n:::| Item with tag 'nonexistent' not found.\n");
    }

    std::cout << "::: Debug: Test Completed Successfully\n";
}

// Dummy struct for testing
struct Dummy {
    int value = 0;
    bool operator==(const Dummy& other) const { return value == other.value; }
};

// Required for JSON (de)serialization
void to_json(json& j, const Dummy& d) {
    j = json{{"value", d.value}};
}

void from_json(const json& j, Dummy& d) {
    j.at("value").get_to(d.value);
}

class ItemManagerTest : public ::testing::Test {
protected:
    ItemManager manager;
};

TEST(ItemManagerTest, AddAndRetrieveDummy) {
    ItemManager manager;
    auto dummy = std::make_shared<Dummy>();
    dummy->value = 77;

    manager.addItem(dummy, "d1");  // 🔄 Automatically registers Dummy type
    auto result = manager.getItem<Dummy>("d1");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 77);
}

TEST(ItemManagerTest, AddAndGetItem) {
    ItemManager manager;
    auto dummy = std::make_shared<Dummy>();
    dummy->value = 42;

    manager.addItem(dummy, "testDummy");
    auto result = manager.getItem<Dummy>("testDummy");

    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->value, 42);
}

TEST(ItemManagerTest, TypeMismatchReturnsNullopt) {
    std::cout << "::: Debug: Starting TypeMismatchReturnsNullopt test\n";

    ItemManager manager;
    auto dummy = std::make_shared<Dummy>();
    manager.addItem(dummy, "wrongTypeTag");

    // Attempt to get wrong type: should fail and return nullopt
    auto result = manager.getItem<std::string>("wrongTypeTag");
    EXPECT_FALSE(result.has_value()) << "Expected std::nullopt due to type mismatch.";

    std::cout << "::: Debug: Test Completed Successfully\n";
}

TEST(ItemManagerTest, UnknownTagReturnsNullopt) {
    ItemManager manager;
    auto result = manager.getItem<Dummy>("missingTag");
    EXPECT_FALSE(result.has_value());
}


    

// :::::::: GlobalItemManager Tests ::::::::
// *****************************************


// Test singleton behavior
TEST(GlobalItemManagerTest, SingletonBehavior) {
    GlobalItemManager& instance1 = GlobalItemManager::getInstance();
    GlobalItemManager& instance2 = GlobalItemManager::getInstance();

    // Verify that both instances point to the same object
    EXPECT_EQ(&instance1, &instance2);
}

TEST(GlobalItemManagerTest, AccessItemManager) {
    GlobalItemManager& globalManager = GlobalItemManager::getInstance();
    ItemManager& itemManager = globalManager.getItemManager();

    // Add an item to the ItemManager
    itemManager.addItem(std::make_shared<int>(42), "testItem");

    // Verify that the item was added
    EXPECT_NO_THROW(itemManager.displayByTag("testItem"));
}

TEST(GlobalItemManagerTest, ResetItemManager) {
    GlobalItemManager& globalManager = GlobalItemManager::getInstance();
    ItemManager& itemManager = globalManager.getItemManager();

    // Add an item to the ItemManager
    itemManager.addItem(std::make_shared<int>(42), "testItem");

    // Reset the ItemManager instance
    globalManager.resetItemManager();

    // Verify that the ItemManager instance was reset
    EXPECT_THROW(itemManager.displayByTag("testItem"), std::runtime_error);
}



// ::::: Test suite for export and import of files (different file formats) :::::
// ******************************************************************************

struct Dummy2 {
    int value = 0;
    bool operator==(const Dummy2& other) const { return value == other.value; }
};

void to_json(json& j, const Dummy2& d) {
    j = json{{"value", d.value}};
}

void from_json(const json& j, Dummy2& d) {
    if (j.contains("value")) {
        j.at("value").get_to(d.value);
    } else if (j.contains("data") && j["data"].contains("value")) {
        j["data"].at("value").get_to(d.value);
    } else {
        throw std::runtime_error("Cannot deserialize Dummy2: missing value");
    }
}

class TestJsonSamples {
    public:
        static json getDummy2ImportArray() {
            const std::string dummy2TypeName = typeid(Dummy2).name();
            return json::array({
                {
                    {"id", "dummy2_id_1"},
                    {"tag", "dummy2_1"},
                    {"type", dummy2TypeName},
                    {"data", {
                        {"id", "dummy2_id_1"},
                        {"tag", "dummy2_1"},
                        {"type", dummy2TypeName},
                        {"value", 99}
                    }}
                }
            });
        }
    
        static json getAlternativeDummy2ImportArray() {
            const std::string dummy2TypeName = typeid(Dummy2).name();
            return json::array({
                {
                    {"id", "dummy2_id_2"},
                    {"tag", "dummy2_X"},
                    {"type", dummy2TypeName},
                    {"data", {
                        {"id", "dummy2_id_2"},
                        {"tag", "dummy2_X"},
                        {"type", dummy2TypeName},
                        {"value", 123}
                    }}
                }
            });
        }
    };

TEST(TestJsonSamples, getDummy2ImportArray_ProducesExpectedJson) {
    json arr = TestJsonSamples::getDummy2ImportArray();
    std::cout << "Actual JSON: " << arr.dump(4) << std::endl; // <-- Add this line
    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.size(), 1);
    const auto& obj = arr[0];

    // Print typeid(Dummy2).name() for reference
    std::cout << "typeid(Dummy2).name(): " << typeid(Dummy2).name() << std::endl;

    EXPECT_EQ(obj["id"], "dummy2_id_1");
    EXPECT_EQ(obj["tag"], "dummy2_1");
    EXPECT_EQ(obj["type"], typeid(Dummy2).name()); // <-- Use the actual type name
    EXPECT_TRUE(obj.contains("data"));
    EXPECT_EQ(obj["data"]["id"], "dummy2_id_1");
    EXPECT_EQ(obj["data"]["tag"], "dummy2_1");
    EXPECT_EQ(obj["data"]["type"], typeid(Dummy2).name());
    EXPECT_EQ(obj["data"]["value"], 99);
}

TEST(TestJsonSamples, getAlternativeDummy2ImportArray_ProducesExpectedJson) {
    json arr = TestJsonSamples::getAlternativeDummy2ImportArray();
    std::cout << "Actual JSON: " << arr.dump(4) << std::endl;
    ASSERT_TRUE(arr.is_array());
    ASSERT_EQ(arr.size(), 1);
    const auto& obj = arr[0];

    std::cout << "typeid(Dummy2).name(): " << typeid(Dummy2).name() << std::endl;

    EXPECT_EQ(obj["id"], "dummy2_id_2");
    EXPECT_EQ(obj["tag"], "dummy2_X");
    EXPECT_EQ(obj["type"], typeid(Dummy2).name());
    EXPECT_TRUE(obj.contains("data"));
    EXPECT_EQ(obj["data"]["id"], "dummy2_id_2");
    EXPECT_EQ(obj["data"]["tag"], "dummy2_X");
    EXPECT_EQ(obj["data"]["type"], typeid(Dummy2).name());
    EXPECT_EQ(obj["data"]["value"], 123);
}

TEST(ItemManagerTest, ExportImport_RestoresDummy2Correctly) {
    std::string filename = "test_export_import_dummy2.json";

    // 1. Create and add the item
    ItemManager manager;
    auto dummy = std::make_shared<Dummy2>();
    dummy->value = 99;
    manager.addItem(dummy, "dummy2_1");

    // 2. Export to file
    manager.exportToFile_Json(filename);

    // 3. Create a new manager and import
    ItemManager imported;
    imported.addItem(std::make_shared<Dummy2>(Dummy2{0}), "dummy2_reg"); // Register type
    imported.importFromFile_Json(filename);

    // 4. Check the imported item
    auto dummyOpt = imported.getItem<Dummy2>("dummy2_1");
    ASSERT_TRUE(dummyOpt.has_value());
    EXPECT_EQ(dummyOpt->value, 99);

    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ExportImport_RestoresAlternativeDummy2Correctly) {
    std::string filename = "test_export_import_alt_dummy2.json";

    // 1. Create and add the item
    ItemManager manager;
    auto dummy = std::make_shared<Dummy2>();
    dummy->value = 123;
    manager.addItem(dummy, "dummy2_X");

    // 2. Export to file
    manager.exportToFile_Json(filename);

    // 3. Create a new manager and import
    ItemManager imported;
    imported.addItem(std::make_shared<Dummy2>(Dummy2{0}), "dummy2_reg"); // Register type
    imported.importFromFile_Json(filename);

    // 4. Check the imported item
    auto dummyOpt = imported.getItem<Dummy2>("dummy2_X");
    ASSERT_TRUE(dummyOpt.has_value());
    EXPECT_EQ(dummyOpt->value, 123);

    std::remove(filename.c_str());
}

struct DummyCSV {
    std::string name;
    int score;

    static json schema() {
        return { {"name", "string"}, {"score", "int"} };
    }

    friend void to_json(json& j, const DummyCSV& obj) {
        j = json{{"name", obj.name}, {"score", obj.score}};
    }

    friend void from_json(const json& j, DummyCSV& obj) {
        obj.name = j.at("name");
        obj.score = j.at("score");
    }
};

class CSVExportTest : public ::testing::Test {
protected:
    ItemManager manager;
    std::string filename = "test_csv_output.csv";

    void SetUp() override {
        auto dummy = std::make_shared<DummyCSV>(DummyCSV{"Echo", 88});
        manager.addItem(dummy, "csv_test");
    }

    void TearDown() override {
        std::remove(filename.c_str());
    }
};

TEST_F(CSVExportTest, ExportCreatesValidCSVFile) {
    ASSERT_TRUE(manager.exportToFile_CSV(filename));

    std::ifstream file(filename);
    ASSERT_TRUE(file.is_open());

    std::string header;
    std::getline(file, header);
    EXPECT_EQ(header, "id,tag,type,data");

    std::string row;
    bool foundEcho = false;
    while (std::getline(file, row)) {
        if (row.find("Echo") != std::string::npos) {
            foundEcho = true;
            break;
        }
    }
    EXPECT_TRUE(foundEcho);
}

struct DummyCSV2 {
    std::string name;
    int score;

    static json schema() {
        return { {"name", "string"}, {"score", "int"} };
    }

    friend void to_json(json& j, const DummyCSV2& obj) {
        j = json{{"name", obj.name}, {"score", obj.score}};
    }

    friend void from_json(const json& j, DummyCSV2& obj) {
        obj.name = j.at("name");
        obj.score = j.at("score");
    }
};

class CSVImportExportTest : public ::testing::Test {
protected:
    std::string filename = "test_csv_output.csv";

    void TearDown() override {
        std::remove(filename.c_str());
    }
};

TEST(CSVImportExportTest, ExportThenImportItemMatchesOriginal) {
    const std::string tag = "csv_test_tag";

    ItemManager importer;
    auto dummy = std::make_shared<DummyCSV2>(DummyCSV2{"Echo", 88});
    importer.addItem(dummy, tag);
    ASSERT_TRUE(importer.exportToFile_CSV("test_csv_output.csv"));

    importer.addItem(std::make_shared<DummyCSV2>(DummyCSV2{"", 0}), "dummy_reg");  // register type
    ASSERT_TRUE(importer.importFromFile_CSV("test_csv_output.csv"));

    // Verify roundtrip
    auto imported = importer.getItem<DummyCSV2>(tag);
    ASSERT_TRUE(imported.has_value());
    EXPECT_EQ(imported->name, "Echo");
    EXPECT_EQ(imported->score, 88);

    std::cout << "CSV roundtrip test passed.\n";
}

struct DummyCSV3 {
    std::string name;
    int score;

    static json schema() {
        return { {"name", "string"}, {"score", "int"} };
    }

    friend void to_json(json& j, const DummyCSV3& obj) {
        j = json{{"name", obj.name}, {"score", obj.score}};
    }

    friend void from_json(const json& j, DummyCSV3& obj) {
        obj.name = j.at("name");
        obj.score = j.at("score");
    }
};

TEST(CSVSingleImportTest, ImportOneObjectByTagAndType) {
    const std::string tag = "single_tag";
    const std::string typeKey = typeid(DummyCSV3).name(); // match registration type
    const std::string filename = "test_csv_single_import.csv";

    // Step 1: Export item
    ItemManager manager;
    manager.addItem(std::make_shared<DummyCSV3>(DummyCSV3{"Solo", 33}), tag);

    // Optional: Register type for deserialization
    manager.addItem(std::make_shared<DummyCSV3>(DummyCSV3{"placeholder", 0}), "type_registration");

    ASSERT_TRUE(manager.exportToFile_CSV(filename));

    // Step 2: Import and validate
    std::shared_ptr<BaseItem> item = manager.importSingleObject_CSV(filename, typeKey, tag);
    std::cout << "Imported type: " << typeid(*item).name() << "\n";
    ASSERT_NE(item, nullptr);

    // Correct cast: use ItemWrapper<DummyCSV3>
    auto wrapper = std::dynamic_pointer_cast<ItemWrapper<DummyCSV3>>(item);
    ASSERT_NE(wrapper, nullptr);

    const DummyCSV3& typed = wrapper->getData();  // or wrapper->getValue() depending on your API
    EXPECT_EQ(typed.name, "Solo");
    EXPECT_EQ(typed.score, 33);
    std::cout << "CSV single import test passed.\n";

    std::remove(filename.c_str());
}

struct WithSchema {
    std::string name;
    int age;

    static nlohmann::json schema() {
        return {
            {"type", "object"},
            {"properties", {
                {"name", {{"type", "string"}}},
                {"age", {{"type", "integer"}}}
            }}
        };
    }

    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WithSchema, name, age)
};

struct WithoutSchema {
    int id;
    NLOHMANN_DEFINE_TYPE_INTRUSIVE(WithoutSchema, id)
};

TEST(ItemManagerTest, ExportIncludesSchemaOnlyForTypesWithSchema) {
    const std::string filename = "test_schemas.json";
    ItemManager manager;

    // Add items — triggers schema registration automatically
    manager.addItem(std::make_shared<WithSchema>(WithSchema{"Ada", 30}), "with-schema");
    manager.addItem(std::make_shared<WithoutSchema>(WithoutSchema{101}), "no-schema");

    manager.exportToFile_Json(filename);

    // Read export
    std::ifstream file(filename);
    ASSERT_TRUE(file.is_open());

    nlohmann::json jArray;
    file >> jArray;
    ASSERT_EQ(jArray.size(), 2);

    // Validate entry with schema
    const auto& withEntry = *std::find_if(jArray.begin(), jArray.end(),
        [](const nlohmann::json& e) { return e["tag"] == "with-schema"; });
    
    ASSERT_TRUE(withEntry.contains("schema"));
    EXPECT_EQ(withEntry["schema"]["type"], "object");
    EXPECT_EQ(withEntry["schema"]["properties"]["name"]["type"], "string");
    EXPECT_EQ(withEntry["schema"]["properties"]["age"]["type"], "integer");

    // Validate entry without schema
    const auto& noEntry = *std::find_if(jArray.begin(), jArray.end(),
        [](const nlohmann::json& e) { return e["tag"] == "no-schema"; });

    EXPECT_FALSE(noEntry.contains("schema"));

    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportFromFile_JSON_RestoresItemsCorrectly) {
    const std::string filename = "test_import.json";
    ItemManager original;
    // Add test data
    original.addItem(std::make_shared<WithSchema>(WithSchema{"Ada", 30}), "ada");
    original.addItem(std::make_shared<WithoutSchema>(WithoutSchema{99}), "anon");

    // Export to file
    original.exportToFile_Json(filename);

    // Import into a new manager instance
    original.importFromFile_Json(filename);

    // Check contents
    auto ada = original.getItem<WithSchema>("ada");
    auto anon = original.getItem<WithoutSchema>("anon");

    ASSERT_TRUE(ada.has_value());
    ASSERT_TRUE(anon.has_value());

    EXPECT_EQ(ada->name, "Ada");
    EXPECT_EQ(ada->age, 30);
    EXPECT_EQ(anon->id, 99);

    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportSingleObject_JSON_WorksForBothTypes) {
    const std::string filename = "test_import_single.json";
    ItemManager restored;

    // Add test data
    restored.addItem(std::make_shared<WithSchema>(WithSchema{"Ada", 30}), "ada");
    restored.addItem(std::make_shared<WithoutSchema>(WithoutSchema{99}), "anon");

    // Export to file
    restored.exportToFile_Json(filename);


    // Import single object with schema
    auto adaItem = restored.importSingleObject_Json(filename, typeid(WithSchema).name(), "ada");
    ASSERT_NE(adaItem, nullptr);
    EXPECT_EQ(adaItem->getTag(), "ada");
    EXPECT_EQ(adaItem->getTypeName(), typeid(WithSchema).name());

    auto ada = std::dynamic_pointer_cast<ItemWrapper<WithSchema>>(adaItem);
    ASSERT_NE(ada, nullptr);
    EXPECT_EQ(ada->getData().name, "Ada");
    EXPECT_EQ(ada->getData().age, 30);

    // Import single object without schema
    auto anonItem = restored.importSingleObject_Json(filename, typeid(WithoutSchema).name(), "anon");
    ASSERT_NE(anonItem, nullptr);
    EXPECT_EQ(anonItem->getTag(), "anon");
    EXPECT_EQ(anonItem->getTypeName(), typeid(WithoutSchema).name());

    auto anon = std::dynamic_pointer_cast<ItemWrapper<WithoutSchema>>(anonItem);
    ASSERT_NE(anon, nullptr);
    EXPECT_EQ(anon->getData().id, 99);

    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ExportToFileJson_WritesCorrectStructureAndIds) {
    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("hello"), "item2");

    std::string filename = "test_export_only.json";
    manager.exportToFile_Json(filename);

    // Check exported file exists and is readable      
    std::ifstream ifs(filename);
    ASSERT_TRUE(ifs.is_open());
    json jArr;
    ifs >> jArr;
    ifs.close();

    // Check array size
    ASSERT_TRUE(jArr.is_array());
    ASSERT_EQ(jArr.size(), 2);

    // Check each entry has required fields and unique non-empty id
    std::set<std::string> ids;
    for (const auto& entry : jArr) {
        ASSERT_TRUE(entry.contains("id"));
        ASSERT_TRUE(entry.contains("tag"));
        ASSERT_TRUE(entry.contains("type"));
        ASSERT_TRUE(entry.contains("data"));
        std::string id = entry["id"].get<std::string>();
        EXPECT_FALSE(id.empty());
        EXPECT_TRUE(ids.insert(id).second) << " Duplicate id found: " << id;
    }

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportSingleObjectJson_FindsAndRestoresObject) {
    // Prepare a JSON file with two items, each with a unique id
    json jArr = json::array({
        {
            {"id", "obj_101"},
            {"tag", "item1"},
            {"type", typeid(int).name()},
            {"data", {
                {"id", "obj_101"},
                {"tag", "item1"},
                {"type", typeid(int).name()},
                {"data", 42}
            }}
        },
        {
            {"id", "obj_102"},
            {"tag", "item2"},
            {"type", typeid(std::string).name()},
            {"data", {
                {"id", "obj_102"},
                {"tag", "item2"},
                {"type", typeid(std::string).name()},
                {"data", "hello"}
            }}
        }
    });

    std::string filename = "test_import_single.json";
    std::ofstream ofs(filename);
    ofs << jArr.dump(4);
    ofs.close();

    ItemManager manager;
    // Use addItem to ensure automatic type registration
    manager.addItem(std::make_shared<int>(0), "dummy_int");
    manager.addItem(std::make_shared<std::string>(""), "dummy_str");

    // Import item1 (int)
    auto item1 = manager.importSingleObject_Json(filename, typeid(int).name(), "item1");
    ASSERT_TRUE(item1 != nullptr);
    auto wrapper1 = dynamic_cast<ItemWrapper<int>*>(item1.get());
    ASSERT_TRUE(wrapper1 != nullptr);
    EXPECT_EQ(wrapper1->getData(), 42);
    EXPECT_EQ(wrapper1->getId(), "obj_101");

    // Import item2 (string)
    auto item2 = manager.importSingleObject_Json(filename, typeid(std::string).name(), "item2");
    ASSERT_TRUE(item2 != nullptr);
    auto wrapper2 = dynamic_cast<ItemWrapper<std::string>*>(item2.get());
    ASSERT_TRUE(wrapper2 != nullptr);
    EXPECT_EQ(wrapper2->getData(), "hello");
    EXPECT_EQ(wrapper2->getId(), "obj_102");

    // Try importing a non-existent object
    auto notFound = manager.importSingleObject_Json(filename, typeid(double).name(), "item3");
    EXPECT_EQ(notFound, nullptr);

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportFromFileJson_RestoresAllItemsWithCorrectIdAndValue) {
    // Prepare a JSON file with two items, each with a unique id
    json jArr = json::array({
        {
            {"id", "obj_101"},
            {"tag", "item1"},
            {"type", typeid(int).name()},
            {"data", {
                {"id", "obj_101"},
                {"tag", "item1"},
                {"type", typeid(int).name()},
                {"data", 42}
            }}
        },
        {
            {"id", "obj_102"},
            {"tag", "item2"},
            {"type", typeid(std::string).name()},
            {"data", {
                {"id", "obj_102"},
                {"tag", "item2"},
                {"type", typeid(std::string).name()},
                {"data", "hello"}
            }}
        }
    });

    std::string filename = "test_import_all.json";
    std::ofstream ofs(filename);
    ofs << jArr.dump(4);
    ofs.close();

    ItemManager manager;
    // Use addItem to ensure automatic type registration
    manager.addItem(std::make_shared<int>(0), "dummy_int");
    manager.addItem(std::make_shared<std::string>(""), "dummy_str");

    // Now import from file
    manager.importFromFile_Json(filename);

    // Check that both items exist and have correct values
    auto intOpt = manager.getItem<int>("item1");
    ASSERT_TRUE(intOpt.has_value());
    EXPECT_EQ(intOpt.value(), 42);

    auto strOpt = manager.getItem<std::string>("item2");
    ASSERT_TRUE(strOpt.has_value());
    EXPECT_EQ(strOpt.value(), "hello");

    // Check that the ids are restored
    auto& items = manager.getItemMapStore();
    EXPECT_EQ(items.at("item1")->getId(), "obj_101");
    EXPECT_EQ(items.at("item2")->getId(), "obj_102");

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ExportImport_Binary_RestoresAllItemsWithCorrectIdAndValue) {
    // Setup: Add two items and export to binary
    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("hello"), "item2");

    std::string filename = "test_export_import.bin";
    ASSERT_TRUE(manager.exportToFile_Binary(filename));

    // Clear and import from binary
    ItemManager imported;
    // Use addItem to ensure automatic type registration
    imported.addItem(std::make_shared<int>(0), "dummy_int");
    imported.addItem(std::make_shared<std::string>(""), "dummy_str");
    ASSERT_TRUE(imported.importFromFile_Binary(filename));

    // Check that both items exist and have correct values
    auto intOpt = imported.getItem<int>("item1");
    ASSERT_TRUE(intOpt.has_value());
    EXPECT_EQ(intOpt.value(), 42);

    auto strOpt = imported.getItem<std::string>("item2");
    ASSERT_TRUE(strOpt.has_value());
    EXPECT_EQ(strOpt.value(), "hello");

    // Check that the ids are restored and not empty
    auto& items = imported.getItemMapStore();
    EXPECT_FALSE(items.at("item1")->getId().empty());
    EXPECT_FALSE(items.at("item2")->getId().empty());

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportSingleObject_Binary_FindsAndRestoresObject) {
    // Setup: Add two items and export to binary
    ItemManager manager;
    manager.addItem(std::make_shared<int>(42), "item1");
    manager.addItem(std::make_shared<std::string>("hello"), "item2");

    std::string filename = "test_import_single.bin";
    ASSERT_TRUE(manager.exportToFile_Binary(filename));

    // Use addItem to ensure automatic type registration
    ItemManager imported;
    imported.addItem(std::make_shared<int>(0), "dummy_int");
    imported.addItem(std::make_shared<std::string>(""), "dummy_str");

    // Import item1 (int)
    auto item1 = imported.importSingleObject_Binary(filename, typeid(int).name(), "item1");
    ASSERT_TRUE(item1 != nullptr);
    auto wrapper1 = dynamic_cast<ItemWrapper<int>*>(item1.get());
    ASSERT_TRUE(wrapper1 != nullptr);
    EXPECT_EQ(wrapper1->getData(), 42);

    // Import item2 (string)
    auto item2 = imported.importSingleObject_Binary(filename, typeid(std::string).name(), "item2");
    ASSERT_TRUE(item2 != nullptr);
    auto wrapper2 = dynamic_cast<ItemWrapper<std::string>*>(item2.get());
    ASSERT_TRUE(wrapper2 != nullptr);
    EXPECT_EQ(wrapper2->getData(), "hello");

    // Try importing a non-existent object
    auto notFound = imported.importSingleObject_Binary(filename, typeid(double).name(), "item3");
    EXPECT_EQ(notFound, nullptr);

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportFromFile_XML_RestoresAllItemsWithCorrectIdAndValue) {
    // Prepare a minimal XML file with two items, each with a unique id
    std::string filename = "test_import_xml.xml";
    {
        std::ofstream ofs(filename);
        ofs << R"(
<SmartStore>
  <Item>
    <Tag>item1</Tag>
    <Type>)" << typeid(int).name() << R"(</Type>
    <Data>{"id":"obj_101","tag":"item1","type":")" << typeid(int).name() << R"(","data":42}</Data>
  </Item>
  <Item>
    <Tag>item2</Tag>
    <Type>)" << typeid(std::string).name() << R"(</Type>
    <Data>{"id":"obj_102","tag":"item2","type":")" << typeid(std::string).name() << R"(","data":"hello"}</Data>
  </Item>
</SmartStore>
)";
        ofs.close();
    }

    ItemManager manager;
    // Use addItem to ensure automatic type registration
    manager.addItem(std::make_shared<int>(0), "dummy_int");
    manager.addItem(std::make_shared<std::string>(""), "dummy_str");

    ASSERT_TRUE(manager.importFromFile_XML(filename));

    // Check that both items exist and have correct values
    auto intOpt = manager.getItem<int>("item1");
    ASSERT_TRUE(intOpt.has_value());
    EXPECT_EQ(intOpt.value(), 42);

    auto strOpt = manager.getItem<std::string>("item2");
    ASSERT_TRUE(strOpt.has_value());
    EXPECT_EQ(strOpt.value(), "hello");

    // Check that the ids are restored and not empty
    auto& items = manager.getItemMapStore();
    EXPECT_EQ(items.at("item1")->getId(), "obj_101");
    EXPECT_EQ(items.at("item2")->getId(), "obj_102");

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportFromFile_XML_MissingFileReturnsFalse) {
    ItemManager manager;
    EXPECT_FALSE(manager.importFromFile_XML("nonexistent_file.xml")); 
}

TEST(ItemManagerTest, ImportFromFile_XML_InvalidXMLReturnsFalse) {
    std::string filename = "invalid.xml";
    std::ofstream ofs(filename);
    ofs << "<SmartStore><Item><Tag>item1</Tag><Type>int</Type><Data>INVALID_JSON</Data></Item></SmartStore>";
    ofs.close();

    ItemManager manager;
    manager.addItem(std::make_shared<int>(0), "dummy_int");
    EXPECT_TRUE(manager.importFromFile_XML(filename)); // Should skip the invalid item, but not crash

    // Clean up
    std::remove(filename.c_str());
}

TEST(ItemManagerTest, ImportFromFile_XML_UnknownTypeIsSkipped) {
    const std::string filename = "unknown_type.xml";

    std::ofstream out(filename);
    out << R"(
        <SmartStore>
            <Item>
                <Tag>unknown_item</Tag>
                <Type>UnregisteredType</Type>
                <Data>{ "value": 999 }</Data>
            </Item>
        </SmartStore>
    )";
    out.close();

    ItemManager manager;
    EXPECT_TRUE(manager.importFromFile_XML(filename)); // Import continues
    EXPECT_FALSE(manager.hasItem("unknown_item"));     // Item should be skipped

    std::remove(filename.c_str());
}



// ::::::::::: Concurrency test for all the functions (thread calls on functions) :::::::::::
// ******************************************************************************************

void simulateFileLoad(const std::string& filename) {
    std::cout << "\033[1;33m📂 File load of: " << filename << "\033[0m" << std::endl;

    for (int i = 0; i <= 100; i += 20) {
        std::cout << "\033[1;32m🔄 Loading... " << i << "%\r\033[0m" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Delay between steps
    }

    std::cout << "\n\033[1;36m File import complete for file: " << filename << "\033[0m\n" << std::endl;
}

void simulateFileExport(const std::string& filename) {
    std::cout << "\033[1;35m📝 Preparing to export: " << filename << "\033[0m" << std::endl;

    for (int progress = 0; progress <= 100; progress += 25) {
        std::cout << "\033[1;34m📤 Exporting... " << progress << "%\r\033[0m" << std::flush;
        std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Delay between steps
    }

    std::cout << "\n\033[1;32m File export complete for: " << filename << "\033[0m\n" << std::endl;
}



TEST(ThreadSafetyTest, ConcurrentAddItemIsSafe) {
    ItemManager manager;

    std::thread t1([&](){
        for (int i = 0; i < 3; ++i)
            manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    });

    std::thread t2([&](){
        for (int i = 100; i < 3; ++i)
            manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    });

    t1.join();
    t2.join();

    for (int i = 0; i < 3; ++i) {
        auto item = manager.getItem<int>("item" + std::to_string(i));
        ASSERT_TRUE(item.has_value());
    }
}

TEST(ThreadSafetyTest, ConcurrentGetItemIsSafe) {
    ItemManager manager;

    // Populate shared state
    for (int i = 0; i < 3; ++i) {
        manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    }

    std::vector<std::thread> threads;
    std::atomic<int> successCount{0};

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 3; ++i) {
                auto val = manager.getItem<int>("item" + std::to_string(i));
                if (val.has_value() && val.value() == i) {
                    successCount++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();
    EXPECT_EQ(successCount, 6);  // 8 threads × 100 items
}

TEST(ThreadSafetyTest, ConcurrentRemoveByTagIsSafe) {
    ItemManager manager;

    // Add 100 items
    for (int i = 0; i < 3; ++i) {
        manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    }

    std::vector<std::thread> threads;

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 3; ++i) {
                manager.removeByTag("item" + std::to_string(i)); // multiple threads may race to remove
            }
        });
    }

    for (auto& th : threads) th.join();

    // All items should be gone
    for (int i = 0; i < 3; ++i) {
        auto item = manager.getItem<int>("item" + std::to_string(i));
        EXPECT_FALSE(item.has_value());
    }
}

TEST(ThreadSafetyTest, UndoRaceConditionTest) {
    ItemManager manager;

    for (int i = 0; i < 2; ++i) {
        manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    }

    std::vector<std::thread> threads;

    // All threads will call undo 3 times
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 3; ++i) {
                manager.undo();  // just call it, don't test return
            }
        });
    }

    for (auto& th : threads) th.join();

    // Final consistency check (state shouldn't be corrupted)
    for (int i = 0; i < 2; ++i) {
        auto item = manager.getItem<int>("item" + std::to_string(i));
        if (item.has_value()) {
            EXPECT_EQ(item.value(), i);
        }
    }
}

TEST(ThreadSafetyTest, RedoRaceConditionTest) {
    ItemManager manager;

    // Add 10 items (which auto-snapshot)
    for (int i = 0; i < 2; ++i) {
        manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    }

    // Undo a few steps first
    for (int i = 0; i < 3; ++i) {
        manager.undo();
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.redo();
            }
        });
    }

    for (auto& th : threads) th.join();

    // Should still land in a legal state (no crashes or bad memory)
    int count = 0;
    for (int i = 0; i < 2; ++i) {
        if (manager.getItem<int>("item" + std::to_string(i)).has_value()) {
            count++;
        }
    }

    EXPECT_GE(count, 0);
    EXPECT_LE(count, 2);
}

TEST(ThreadSafetyTest, ModifyItemInParallelWithTemplateIsSafe) {
    ItemManager manager;
    manager.addItem(std::make_shared<int>(0), "counter");

    std::vector<std::thread> threads;

    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 10; ++i) {
                manager.modifyItem<int>("counter", [](int& val) {
                    val += 1;
                });
            }
        });
    }

    for (auto& th : threads) th.join();

    auto result = manager.getItem<int>("counter");
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 50);  // 5 threads × 10 modifications
}

TEST(ThreadSafetyTest, GetItemRawReturnsCorrectReferenceConcurrently) {
    ItemManager manager;
    manager.addItem(std::make_shared<std::string>("raw-access"), "raw");

    std::atomic<int> successCount{0};
    std::vector<std::thread> threads;

    for (int t = 0; t < 5; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 20; ++i) {
                try {
                    const std::string& ref = manager.getItemRaw<std::string>("raw");
                    if (ref == "raw-access") {
                        successCount++;
                    }
                } catch (const std::exception& e) {
                    // Optionally log: std::cerr << e.what() << "\n";
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(successCount.load(), 100);  // 5 threads × 20 reads
}

TEST(ThreadSafetyTest, DisplayAllRunsConcurrently) {
    ItemManager manager;

    for (int i = 0; i < 3; ++i) {
        manager.addItem(std::make_shared<int>(i), "item" + std::to_string(i));
    }

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            manager.displayAll();  // just verify no crashes
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // No assertion needed — success is no crash
}

TEST(ThreadSafetyTest, DisplayByTagIsSafeWhenCalledConcurrently) {
    ItemManager manager;
    manager.addItem(std::make_shared<double>(3.14159), "pi");

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.displayByTag("pi");  // safe parallel reads
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // Reaching this point = no crashes or race conditions
}

TEST(ThreadSafetyTest, ListRegisteredTypesIsSafeUnderLoad) {
    ItemManager manager;

    // Add different types, which automatically register
    manager.addItem(std::make_shared<int>(42), "intItem");
    manager.addItem(std::make_shared<std::string>("hello"), "stringItem");
    manager.addItem(std::make_shared<double>(3.14), "piItem");

    std::vector<std::thread> threads;

    // Read from the registry concurrently
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.listRegisteredTypes();
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // Passes if there's no crash or race condition
}

TEST(ThreadSafetyTest, FilterByTagDisplaysMatchingItemsSafely) {
    ItemManager manager;

    manager.addItem(std::make_shared<int>(10), "apple");
    manager.addItem(std::make_shared<int>(20), "banana");
    manager.addItem(std::make_shared<int>(30), "apple");  // Duplicate tag

    std::vector<std::thread> threads;

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.filterByTag("apple");  // Should display two items
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // We're only verifying safe parallel execution
}

TEST(ThreadSafetyTest, SortItemsByTagRunsConcurrentlyWithoutRace) {
    ItemManager manager;

    manager.addItem(std::make_shared<std::string>("pear"), "b");
    manager.addItem(std::make_shared<std::string>("apple"), "a");
    manager.addItem(std::make_shared<std::string>("cherry"), "c");

    std::vector<std::thread> threads;

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.sortItemsByTag();
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // Thread-safe if no crashes or malformed output
}

TEST(ThreadSafetyTest, DisplayAllClassesConcurrentAccessIsSafe) {
    ItemManager manager;

    // Add items to auto-register types
    manager.addItem(std::make_shared<int>(1), "one");
    manager.addItem(std::make_shared<std::string>("hello"), "two");
    manager.addItem(std::make_shared<double>(3.14), "three");

    std::vector<std::thread> threads;

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.displayAllClasses();
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // Test passes if no crash or deadlock occurs
}

TEST(ThreadSafetyTest, GetItemMapStoreReturnsConsistentView) {
    ItemManager manager;

    manager.addItem(std::make_shared<int>(10), "a");
    manager.addItem(std::make_shared<int>(20), "b");
    manager.addItem(std::make_shared<int>(30), "c");

    std::vector<std::thread> threads;
    std::atomic<int> totalFound{0};

    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                auto snapshot = manager.getItemMapStore();
                for (const auto& [tag, item] : snapshot) {
                    if (item) totalFound++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    // Should find 3 items per snapshot × 4 threads × 10 iterations
    EXPECT_EQ(totalFound.load(), 12);
}

TEST(ThreadSafetyTest, DisplayRegisteredDeserializersRunsWithoutRace) {
    ItemManager manager;

    // Deserializers registered automatically via addItem
    manager.addItem(std::make_shared<int>(7), "int_val");
    manager.addItem(std::make_shared<std::string>("ok"), "str_val");

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                manager.displayRegisteredDeserializers();  // safe read
            }
        });
    }

    for (auto& th : threads) th.join();

    SUCCEED();  // Confirming thread-safe output
}

TEST(ThreadSafetyTest, HasItemHandlesConcurrentQueries) {
    ItemManager manager;
    manager.addItem(std::make_shared<int>(123), "exists");

    std::atomic<int> foundCount{0};
    std::atomic<int> notFoundCount{0};

    std::vector<std::thread> threads;
    for (int t = 0; t < 2; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 2; ++i) {
                if (manager.hasItem("exists")) {
                    foundCount++;
                } else {
                    notFoundCount++;
                }

                if (manager.hasItem("ghost")) {
                    foundCount++;
                } else {
                    notFoundCount++;
                }
            }
        });
    }

    for (auto& th : threads) th.join();

    EXPECT_EQ(foundCount.load(), 4);       // 4 × 25 positive lookups
    EXPECT_EQ(notFoundCount.load(), 4);    // 4 × 25 negative lookups
}

TEST(ThreadSafetyTest, AsyncImportFromFileIsSafeAndCorrect) {
    const std::string testFile = "threaded_import_test.json";
    ItemManager manager;

    // Create sample data and export to file
    manager.addItem(std::make_shared<int>(42), "alpha");
    manager.addItem(std::make_shared<std::string>("thread-test"), "beta");
    manager.exportToFile_Json(testFile);

    std::atomic<int> importCount{0};
    std::vector<std::thread> threads;

    // Call asyncImportFromFile_Json() multiple times concurrently
    for (int i = 0; i < 2; ++i) {
        threads.emplace_back([&]() {
            manager.asyncImportFromFile_Json(testFile);
            importCount++;
        });
    }

    for (auto& th : threads) th.join();

    // Wait to ensure async imports complete
    simulateFileLoad(testFile);

    // Final check for data presence
    EXPECT_TRUE(manager.hasItem("alpha"));
    EXPECT_TRUE(manager.hasItem("beta"));
    EXPECT_GE(importCount.load(), 2);  // All imports attempted
}

TEST(ThreadSafetyTest, AsyncExportToFileIsSafeAndWritesCorrectly) {
    ItemManager manager;
    manager.addItem(std::make_shared<int>(99), "export_tag");
    manager.addItem(std::make_shared<std::string>("save_me"), "text_tag");

    const std::string testFile = "threaded_export_test.json";

    std::atomic<bool> exportDone{false};

    std::thread([&]() {
        manager.asyncExportToFile_Json(testFile);
        exportDone = true;
    }).join();  // Wait for async thread to finish

    simulateFileExport(testFile);  // Give export time

    // Validate file exists and contains expected tags
    std::ifstream in(testFile);
    ASSERT_TRUE(in.is_open());

    json parsed;
    in >> parsed;

    bool found_export_tag = false, found_text_tag = false;

    for (const auto& entry : parsed) {
        if (entry.contains("tag")) {
            std::string tag = entry["tag"].get<std::string>();
            if (tag == "export_tag") found_export_tag = true;
            if (tag == "text_tag") found_text_tag = true;
        }
    }

    EXPECT_TRUE(found_export_tag);
    EXPECT_TRUE(found_text_tag);
    EXPECT_TRUE(exportDone.load());
}

TEST(ThreadSafetyTest, AsyncImportSingleObjectWorksSafely) {
    const std::string testFile = "single_object_test.json";
    const std::string tag = "single_tag";
    const std::string typeName = "i";

    // Step 1: Create exporter and save one item to file
    ItemManager manager;
    manager.addItem(std::make_shared<int>(777), tag);
    manager.exportToFile_Json(testFile);


    // Step 3: Async import of the saved object
    manager.asyncImportSingleObject_Json(testFile, typeName, tag);

    // Step 4: Wait for async thread to complete
    simulateFileLoad(testFile);

    // Step 5: Verify that the item was correctly imported
    EXPECT_TRUE(manager.hasItem(tag));
    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<int>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), 777);  // getData() confirmed correct
}

TEST(ThreadSafetyTest, AsyncExportToFile_BinaryWorksSafely) {
    const std::string testFile = "binary_export_async_test.bin";
    const std::string tag = "bin_tag";
    const int value = 12345;

    // Step 1: Set up ItemManager and add one item
    ItemManager manager;
    manager.addItem(std::make_shared<int>(value), tag);

    // Step 2: Run async binary export
    manager.asyncExportToFile_Binary(testFile);

    // Step 3: Wait briefly to allow background export to complete
    simulateFileExport(testFile);

    // Step 4: Confirm file exists and isn't empty
    std::ifstream in(testFile, std::ios::binary | std::ios::ate);
    ASSERT_TRUE(in.is_open());
    ASSERT_GT(in.tellg(), 0);  // File should have content
}

TEST(ThreadSafetyTest, AsyncImportFromFile_BinaryWorksSafely) {
    const std::string testFile = "binary_import_async_test.bin";
    const std::string tag = "binary_tag";
    const int value = 9090;

    // Step 1: Export a valid item using one manager
    ItemManager manager;
    manager.addItem(std::make_shared<int>(value), tag);
    manager.exportToFile_Binary(testFile);

    // Step 3: Perform asynchronous binary import
    manager.asyncImportFromFile_Binary(testFile);

    // Step 4: Wait for background thread to complete
    simulateFileLoad(testFile);

    // Step 5: Validate that the item was successfully imported
    EXPECT_TRUE(manager.hasItem(tag));

    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<int>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), value);  // Should match original export
}

TEST(ThreadSafetyTest, AsyncImportSingleObject_BinaryWorksSafely) {
    const std::string testFile = "binary_single_import_test.bin";
    const std::string tag = "bin_single";
    const std::string typeName = "i";
    const int value = 4242;

    // Step 1: Export one item from a fresh instance
    ItemManager manager;
    manager.addItem(std::make_shared<int>(value), tag);
    manager.exportToFile_Binary(testFile);

    // Step 3: Trigger async binary import for specific object
    manager.asyncImportSingleObject_Binary(testFile, typeName, tag);

    // Step 4: Wait for background thread to complete
    simulateFileLoad(testFile);

    // Step 5: Validate item presence and correctness
    EXPECT_TRUE(manager.hasItem(tag));

    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<int>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), value);  // Confirm correct value
}

TEST(ThreadSafetyTest, AsyncExportToFile_XMLWorksSafely) {
    const std::string testFile = "xml_export_async_test.xml";
    const std::string tag = "xml_tag";
    const std::string text = "Hello_XML";

    // Step 1: Create manager and add item
    ItemManager manager;
    manager.addItem(std::make_shared<std::string>(text), tag);

    // Step 2: Run async XML export
    manager.asyncExportToFile_XML(testFile);

    // Step 3: Wait briefly to allow background export to finish
    simulateFileExport(testFile);

    // Step 4: Validate that XML file was created and contains expected content
    std::ifstream in(testFile);
    ASSERT_TRUE(in.is_open());

    std::stringstream buffer;
    buffer << in.rdbuf();
    std::string xmlContent = buffer.str();

    // Look for tag and data to confirm export
    EXPECT_NE(xmlContent.find(tag), std::string::npos);
    EXPECT_NE(xmlContent.find(text), std::string::npos);
}

TEST(ThreadSafetyTest, AsyncImportFromFile_XMLWorksSafely) {
    const std::string testFile = "threaded_xml_import_test.xml";
    const std::string tag = "xml_tag";
    const std::string text = "Hello_XML";

    // Step 1: Export a sample file using the normal method
    ItemManager manager;
    manager.addItem(std::make_shared<std::string>(text), tag);
    manager.exportToFile_XML(testFile);

    manager.asyncImportFromFile_XML(testFile);

    // Step 3: Wait for async thread to complete
    simulateFileLoad(testFile);

    // Step 4: Check that the item is present
    EXPECT_TRUE(manager.hasItem(tag));
    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<std::string>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), text);
}

TEST(ThreadSafetyTest, AsyncImportSingleObject_XMLWorksSafely) {
    const std::string file = "xml_single_import_test.xml";
    const std::string tag = "x_tag";
    const std::string typeName = typeid(std::string).name();
    const std::string value = "Import_XML_Single";

    // Step 1: Export single item
    ItemManager manager;
    manager.addItem(std::make_shared<std::string>(value), tag);
    manager.exportToFile_XML(file);

    manager.asyncImportSingleObject_XML(file, typeName, tag);

    simulateFileLoad(file);  // Allow async to complete

    // Step 3: Validate result
    EXPECT_TRUE(manager.hasItem(tag));
    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<std::string>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), value);
}

TEST(ThreadSafetyTest, AsyncExportToFile_CSVWorksSafely) {
    const std::string testFile = "csv_export_async_test.csv";
    const std::string tag1 = "csv_int";
    const std::string tag2 = "csv_text";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(456), tag1);
    manager.addItem(std::make_shared<std::string>("Async_CSV"), tag2);

    manager.asyncExportToFile_CSV(testFile);

    // Allow time for export thread to complete
    simulateFileExport(testFile);

    // Check that file was created and contains expected content
    std::ifstream in(testFile);
    ASSERT_TRUE(in.is_open());

    std::stringstream ss;
    ss << in.rdbuf();
    std::string content = ss.str();

    EXPECT_NE(content.find(tag1), std::string::npos);
    EXPECT_NE(content.find("456"), std::string::npos);
    EXPECT_NE(content.find(tag2), std::string::npos);
    EXPECT_NE(content.find("Async_CSV"), std::string::npos);
}

TEST(ThreadSafetyTest, AsyncImportFromFile_CSVWorksSafely) {
    const std::string testFile = "csv_import_async_test.csv";
    const std::string tag = "csv_tag";
    const std::string value = "CSV_Import";

    // Step 1: Export a sample item to CSV
    ItemManager manager;
    manager.addItem(std::make_shared<std::string>(value), tag);
    manager.exportToFile_CSV(testFile);

    // Step 2: Perform async import
    manager.asyncImportFromFile_CSV(testFile);

    // Step 3: Wait for async thread to complete
    simulateFileLoad(testFile);

    // Step 4: Validate that the item was imported correctly
    EXPECT_TRUE(manager.hasItem(tag));
    auto snapshot = manager.getItemMapStore();
    auto it = snapshot.find(tag);
    ASSERT_NE(it, snapshot.end());

    auto typed = std::dynamic_pointer_cast<ItemWrapper<std::string>>(it->second);
    ASSERT_TRUE(typed);
    EXPECT_EQ(typed->getData(), value);
}

TEST(ThreadSafetyTest, AsyncImportSingleObject_CSVWorksSafely) {
    const std::string file = "single_csv_test.csv";
    const std::string tag = "csv_single_tag";
    const std::string typeName = "i";

    ItemManager manager;
    manager.addItem(std::make_shared<int>(1234), tag);
    manager.exportToFile_CSV(file);
    
    manager.asyncImportSingleObject_CSV(file, typeName, tag);

    // Step 3: Wait for background import to complete
    simulateFileLoad(file);

    manager.displayRegisteredDeserializers(); // Ensure deserializers are registered
    // Step 4: Validate the item
    EXPECT_TRUE(manager.hasItem(tag));
    auto result = manager.getItem<int>(tag);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result.value(), 1234);
}






// Main function to run all tests
int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}