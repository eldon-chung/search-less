#pragma once

#include <assert.h>
#include <stddef.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

template <typename ForwardSearcher>
std::vector<size_t>
search_all(ForwardSearcher forward_searcher, std::string_view file_contents,
           std::string_view pattern, size_t beginning_offset,
           size_t ending_offset, bool caseless = false) {
    std::vector<size_t> out;
    while (true) {
        size_t offset = forward_searcher(
            file_contents, pattern, beginning_offset, ending_offset, caseless);
        if (offset == std::string::npos) {
            break;
        }
        out.push_back(offset);
        // i think this is flawed
        beginning_offset = offset + pattern.size();
    }
    return out;
}

template <typename ForwardSearcher>
size_t search_forward_n(ForwardSearcher forward_searcher, size_t num_repeats,
                        std::string_view file_contents,
                        std::string_view pattern, size_t beginning_offset,
                        size_t ending_offset, bool caseless = false) {
    size_t latest_hit = std::string::npos;
    for (size_t i = 0; i < num_repeats; ++i) {
        size_t result = forward_searcher(
            file_contents, pattern, beginning_offset, ending_offset, caseless);
        if (result == std::string::npos) {
            break;
        }
        latest_hit = result;
        beginning_offset = result + 1;
    }
    return latest_hit;
}

template <typename BackwardSearcher>
size_t search_backward_n(BackwardSearcher backward_searcher, size_t num_repeats,
                         std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless = false) {
    size_t latest_hit = std::string::npos;
    for (size_t i = 0; i < num_repeats; ++i) {
        size_t result = backward_searcher(
            file_contents, pattern, beginning_offset, ending_offset, caseless);
        if (result == std::string::npos) {
            break;
        }
        latest_hit = result;
        ending_offset = result;
    }
    return latest_hit;
}

size_t basic_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset, bool caseless = false);

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless = false);

size_t regex_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset, bool caseless = false);

size_t regex_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless = false);
