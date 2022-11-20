#include "search.h"
#include <cstring>

static std::string tolower(std::string_view s) {
    std::string out;
    out.resize(s.size());
    std::transform(s.begin(), s.end(), out.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return out;
}

template <size_t N> void tolowerN(const char *s, char *out) {
    for (size_t i = 0; i < N; ++i) {
        char c = s[i];
        c += (s[i] >= 'A' && s[i] <= 'Z') * ('a' - 'A');
        *(out++) = c;
    }
}

std::vector<size_t> basic_search_all(std::string_view file_contents,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset,
                                     bool caseless /* = false */) {

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
                          size_t ending_offset, bool caseless /* = false */) {
    std::string_view file_contents_substr = file_contents.substr(
        beginning_offset, ending_offset - beginning_offset);
    if (file_contents_substr.length() < pattern.length()) {
        return ending_offset;
    }
    if (pattern.length() == 0) {
        return beginning_offset;
    }
    if (caseless) {
        std::string lower_pattern = tolower(pattern);
        char *lower_pattern_ptr = const_cast<char *>(lower_pattern.data());
        std::string lower_file =
            tolower(file_contents_substr.substr(0, 4096 + pattern.length()));
        char *lower_file_ptr = const_cast<char *>(lower_file.data());
        char *cur_lower_file = lower_file_ptr;
        while (beginning_offset < ending_offset) {
            if (memcmp(cur_lower_file, lower_pattern_ptr, pattern.length()) ==
                0) {
                return beginning_offset;
            }
            ++beginning_offset;
            ++cur_lower_file;
            if (cur_lower_file - lower_file_ptr == 4096) {
                std::copy(cur_lower_file, cur_lower_file + pattern.length(),
                          lower_file_ptr);
                cur_lower_file = lower_file_ptr;
                if (file_contents_substr.length() >= 4096 + pattern.length()) {
                    tolowerN<4096>(file_contents_substr.data() +
                                       pattern.length(),
                                   cur_lower_file + pattern.length());
                } else {
                    std::string_view file_contents_substr_substr =
                        file_contents_substr.substr(pattern.length(), 4096);
                    std::transform(
                        file_contents_substr_substr.begin(),
                        file_contents_substr_substr.end(),
                        cur_lower_file + pattern.length(),
                        [](unsigned char c) { return std::tolower(c); });
                }
            }
        }
        return ending_offset;
    } else {
        size_t pos = file_contents_substr.find(pattern);
        if (pos == std::string::npos) {
            return ending_offset;
        } else {
            return pos + beginning_offset;
        }
    }
}

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless /* = false */) {

    size_t result = file_contents.rfind(pattern, ending_offset - 1);
    if (result == std::string::npos || result < beginning_offset) {
        result = ending_offset;
    }

    return result;
}
