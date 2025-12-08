#pragma once
#include <string>
#include "message.hpp"

class BaseMicroservice {
public:
    virtual ~BaseMicroservice() = default;  // inline definition, no cpp needed
};