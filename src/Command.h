#pragma once

#include <string>

struct Command {
    enum Type {
        INVALID,
        QUIT,
        VIEW_DOWN,
        VIEW_UP,
        VIEW_BOF,
        VIEW_EOF,
    };
    Type type = INVALID;
    std::string payload;
};
