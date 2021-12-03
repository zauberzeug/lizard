#pragma once

class Action {
public:
    virtual ~Action();
    virtual bool run() = 0;
};