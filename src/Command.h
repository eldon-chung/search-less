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
        SEARCH_NEXT,
        RESIZE,
        DISPLAY_COMMAND,
        TOGGLE_CASELESS,
        TOGGLE_CONDITIONALLY_CASELESS,
    };
    Type type = INVALID;
    std::string payload = "";
};
