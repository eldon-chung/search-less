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
        SEARCH,
    };
    Type type = INVALID;
    std::string payload;
};
