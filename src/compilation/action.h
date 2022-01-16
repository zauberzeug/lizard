#pragma once

#include <memory>

class Action;
using Action_ptr = std::shared_ptr<Action>;

class Action {
public:
    virtual ~Action();
    virtual bool run() = 0;
};