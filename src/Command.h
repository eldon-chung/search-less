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
        SEARCH_START,
        SEARCH_QUIT,
        SEARCH_EXEC,
        SEARCH_NEXT,
        SEARCH_PREV,
        RESIZE,
        DISPLAY_COMMAND,
        TOGGLE_CASELESS,
        TOGGLE_CONDITIONALLY_CASELESS,
        BUFFER_CURS_POS,
    };
    Type type = INVALID;
    std::string payload = "";
};
