#pragma once

#include <chrono>
#include <string>
#include <vector>

struct Command {
    enum Type {
        INVALID,
        QUIT,
        VIEW_DOWN,
        VIEW_UP,
        VIEW_LEFT,
        VIEW_RIGHT,
        VIEW_DOWN_HALF_PAGE,
        VIEW_UP_HALF_PAGE,
        VIEW_DOWN_PAGE,
        VIEW_UP_PAGE,
        SET_HALF_PAGE_SIZE,
        SET_PAGE_SIZE,
        VIEW_BOF,
        VIEW_EOF,
        SEARCH_START,
        SEARCH_QUIT,
        SEARCH_EXEC,
        SEARCH_NEXT,
        SEARCH_PREV,
        RESIZE,
        DISPLAY_COMMAND,
        DISPLAY_STATUS,
        TOGGLE_CASELESS,
        TOGGLE_CONDITIONALLY_CASELESS,
        UPDATE_LINE_IDXS,
        SEARCH_CLEAR,
        TOGGLE_HIGHLIGHTING,
        INTERRUPT,
        FOLLOW_EOF,
        TOGGLE_LONG_LINES,
    };
    Type type;
    std::string payload_str;
    std::vector<size_t> payload_nums;
    size_t payload_num;
    std::chrono::time_point<std::chrono::steady_clock> start;

    Command(Type type = INVALID, std::string payload_str = "",
            std::vector<size_t> payload_nums = {}, size_t payload_num = 0,
            std::chrono::time_point<std::chrono::steady_clock> start =
                std::chrono::steady_clock::now())
        : type(type), payload_str(payload_str), payload_nums(payload_nums),
          payload_num(payload_num), start(start) {
    }
};
