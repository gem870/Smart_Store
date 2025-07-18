#pragma once
#ifndef ITEM_WRAPPER_H
#define ITEM_WRAPPER_H
#include <string>
#include <memory>
#include <nlohmann/json.hpp>



using json = nlohmann::json;

class BaseItem {
public:
    virtual ~BaseItem() = default;

    virtual void display() const = 0;

    virtual std::string getTypeName() const = 0;
    
    virtual json serialize() const = 0;

    virtual std::shared_ptr<BaseItem> clone() const = 0;
    
    virtual std::string getTag() const = 0;
    
    virtual nlohmann::json toJson() const = 0;

    // Require all derived classes to provide an id
    virtual std::string getId() const = 0;

    virtual void logId() const {
        std::cout << "::: [BaseItem] ID: " << getId() << std::endl;
    }

};

#endif // ITEM_WRAPPER_H