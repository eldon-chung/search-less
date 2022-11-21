#include "search.h"
#include <cstring>

void tolower(const char *s, const char *end, char *out) {
    std::transform(s, end, out, [](char c) {
        return c + (c >= 'A' && c <= 'Z') * ('a' - 'A');
    });
}

void approx_tolower(const char *s, const char *end, char *out) {
    std::transform(s, end, out, [](char c) { return (c & 0x1f) | 0x40; });
}

void tolower(std::string_view in, char *out) {
    tolower(in.data(), in.data() + in.length(), out);
}

void approx_tolower(std::string_view in, char *out) {
    approx_tolower(in.data(), in.data() + in.length(), out);
}

std::vector<size_t> basic_search_all(std::string_view file_contents,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset,
                                     bool caseless /* = false */) {
    std::vector<size_t> out;
    while (true) {
        size_t offset = basic_search_first(
            file_contents, pattern, beginning_offset, ending_offset, caseless);
        if (offset == ending_offset) {
            break;
        }
        out.push_back(offset);
        beginning_offset = offset + 1;
    }
    return out;
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
        std::string lower_pattern;
        lower_pattern.resize(pattern.length());
        tolower(pattern.data(), pattern.data() + pattern.length(),
                lower_pattern.data());
        std::string approx_lower_pattern;
        approx_lower_pattern.resize(pattern.length());
        approx_tolower(pattern.data(), pattern.data() + pattern.length(),
                       approx_lower_pattern.data());
        const char *lower_pattern_ptr = lower_pattern.data();
        const char *approx_lower_pattern_ptr = approx_lower_pattern.data();

        const char *file_contents_ptr = file_contents.data() + beginning_offset;

        std::string approx_lower_file_buf;
        approx_lower_file_buf.resize(4096 + pattern.length());
        approx_tolower(file_contents_substr.substr(0, 4096 + pattern.length()),
                       approx_lower_file_buf.data());
        char *approx_lower_file_ptr =
            const_cast<char *>(approx_lower_file_buf.data());
        char *cur_approx_lower_file_ptr = approx_lower_file_ptr;

        std::string lower_file_buf;
        lower_file_buf.resize(4096 + pattern.length());
        char *lower_file_ptr = const_cast<char *>(lower_file_buf.data());

        while (true) {
            if (ending_offset - beginning_offset < 4096 + pattern.length()) {
                break;
            }
            for (size_t iters = 0; iters < 4096; ++iters) {
                if (memcmp(cur_approx_lower_file_ptr, approx_lower_pattern_ptr,
                           pattern.length()) == 0) {
                    tolower(file_contents_ptr,
                            file_contents_ptr + pattern.length(),
                            lower_file_ptr);
                    if (memcmp(lower_file_ptr, lower_pattern_ptr,
                               pattern.length()) == 0) {
                        return beginning_offset;
                    }
                }
                ++beginning_offset;
                ++cur_approx_lower_file_ptr;
                ++file_contents_ptr;
            }
            std::copy(cur_approx_lower_file_ptr,
                      cur_approx_lower_file_ptr + pattern.length(),
                      approx_lower_file_ptr);
            cur_approx_lower_file_ptr = approx_lower_file_ptr;
            approx_tolower(file_contents_ptr + pattern.length(),
                           file_contents_ptr + pattern.length() + 4096,
                           approx_lower_file_ptr + pattern.length());
        }

        // handle stragglers
        tolower(file_contents_ptr,
                file_contents_ptr + (ending_offset - beginning_offset),
                lower_file_ptr);
        while (beginning_offset < ending_offset) {
            if (memcmp(lower_file_ptr, lower_pattern_ptr, pattern.length()) ==
                0) {
                return beginning_offset;
            }
            ++beginning_offset;
            ++lower_file_ptr;
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
    if (result == std::string::npos || result < beginning_offset ||
        result >= ending_offset) {
        result = ending_offset;
    }

    return result;
}
