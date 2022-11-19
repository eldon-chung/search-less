#pragma once

#include <functional>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#include "Model.h"

std::vector<size_t> basic_search_all(const Model &model,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset);

size_t basic_search_first(const Model &model, std::string_view pattern,
                          size_t beginning_offset, size_t ending_offset);
