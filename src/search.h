#pragma once

#include <assert.h>
#include <stddef.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

template <typename ForwardSearcher>
std::optional<std::vector<size_t>>
search_all(ForwardSearcher forward_searcher, std::string_view file_contents,
           std::string_view pattern, size_t beginning_offset,
           size_t ending_offset, bool caseless, std::stop_token stop) {
    std::vector<size_t> out;
    while (true) {
        std::optional<size_t> offset =
            forward_searcher(file_contents, pattern, beginning_offset,
                             ending_offset, caseless, stop);
        if (!offset) {
            return std::nullopt;
        }
        if (*offset == std::string::npos) {
            break;
        }
        out.push_back(*offset);
        // i think this is flawed
        beginning_offset = *offset + pattern.size();
    }
    return out;
}

template <typename ForwardSearcher>
std::optional<size_t>
search_forward_n(ForwardSearcher forward_searcher, size_t num_repeats,
                 std::string_view file_contents, std::string_view pattern,
                 size_t beginning_offset, size_t ending_offset, bool caseless,
                 std::stop_token stop) {
    size_t latest_hit = std::string::npos;
    for (size_t i = 0; i < num_repeats; ++i) {
        std::optional<size_t> result =
            forward_searcher(file_contents, pattern, beginning_offset,
                             ending_offset, caseless, stop);
        if (!result) {
            return std::nullopt;
        }
        if (*result == std::string::npos) {
            break;
        }
        latest_hit = *result;
        beginning_offset = *result + 1;
    }
    return latest_hit;
}

template <typename BackwardSearcher>
std::optional<size_t>
search_backward_n(BackwardSearcher backward_searcher, size_t num_repeats,
                  std::string_view file_contents, std::string_view pattern,
                  size_t beginning_offset, size_t ending_offset, bool caseless,
                  std::stop_token stop) {
    size_t latest_hit = std::string::npos;
    for (size_t i = 0; i < num_repeats; ++i) {
        std::optional<size_t> result =
            backward_searcher(file_contents, pattern, beginning_offset,
                              ending_offset, caseless, stop);
        if (!result) {
            return std::nullopt;
        }
        if (*result == std::string::npos) {
            break;
        }
        latest_hit = *result;
        ending_offset = *result;
    }
    return latest_hit;
}

std::optional<size_t> basic_search_first(std::string_view file_contents,
                                         std::string_view pattern,
                                         size_t beginning_offset,
                                         size_t ending_offset, bool caseless,
                                         std::stop_token stop);

std::optional<size_t> basic_search_last(std::string_view file_contents,
                                        std::string_view pattern,
                                        size_t beginning_offset,
                                        size_t ending_offset, bool caseless,
                                        std::stop_token stop);

std::optional<size_t> regex_search_first(std::string_view file_contents,
                                         std::string_view pattern,
                                         size_t beginning_offset,
                                         size_t ending_offset, bool caseless,
                                         std::stop_token stop);

std::optional<size_t> regex_search_last(std::string_view file_contents,
                                        std::string_view pattern,
                                        size_t beginning_offset,
                                        size_t ending_offset, bool caseless,
                                        std::stop_token stop);
