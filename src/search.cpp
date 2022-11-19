#include "search.h"

std::vector<size_t> basic_search_all(const Model &model,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset) {

    const std::string_view contents = model.get_contents();
    auto bm_searcher =
        std::boyer_moore_searcher(pattern.begin(), pattern.end());
    std::vector<size_t> result_offsets;

    auto it = std::search(contents.begin() + beginning_offset,
                          contents.begin() + ending_offset, bm_searcher);

    while (it != contents.begin() + ending_offset) {
        result_offsets.push_back(it - contents.begin());

        beginning_offset = (it - contents.begin() + pattern.length());

        it = std::search(contents.begin() + beginning_offset,
                         contents.begin() + ending_offset, bm_searcher);
    }

    return result_offsets;
}

size_t basic_search_first(const Model &model, std::string_view pattern,
                          size_t beginning_offset, size_t ending_offset) {
    const std::string_view contents = model.get_contents();

    auto it = std::search(
        contents.begin() + beginning_offset, contents.begin() + ending_offset,
        std::boyer_moore_searcher(pattern.begin(), pattern.end()));

    return it - contents.begin();
}
