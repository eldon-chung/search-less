#include "search.h"

std::vector<size_t> basic_search_all(std::string_view file_contents,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset) {

    auto bm_searcher =
        std::boyer_moore_searcher(pattern.begin(), pattern.end());
    std::vector<size_t> result_offsets;

    auto it = std::search(file_contents.begin() + beginning_offset,
                          file_contents.begin() + ending_offset, bm_searcher);

    while (it != file_contents.begin() + ending_offset) {
        result_offsets.push_back((size_t)(it - file_contents.begin()));

        beginning_offset =
            ((size_t)(it - file_contents.begin()) + pattern.length());

        it = std::search(file_contents.begin() + beginning_offset,
                         file_contents.begin() + ending_offset, bm_searcher);
    }

    return result_offsets;
}

size_t basic_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset) {

    auto it =
        std::search(file_contents.begin() + beginning_offset,
                    file_contents.begin() + ending_offset,
                    std::boyer_moore_searcher(pattern.begin(), pattern.end()));

    return (size_t)(it - file_contents.begin());
}
