#pragma once

#include <functional>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

std::vector<size_t> basic_search_all(std::string_view model,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset);

size_t basic_search_first(std::string_view model, std::string_view pattern,
                          size_t beginning_offset, size_t ending_offset);

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset);
