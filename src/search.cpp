#include "search.h"
#include <memory>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>

#include <algorithm>
#include <optional>
#include <string>

void tolower(const char *s, const char *end, char *out) {
    std::transform(s, end, out, [](char c) {
        return c + (c >= 'A' && c <= 'Z') * ('a' - 'A');
    });
}

void approx_tolower(const char *s, const char *end, char *out) {
    std::transform(s, end, out, [](char c) { return c & 0x1f; });
}

void tolower(std::string_view in, char *out) {
    tolower(in.data(), in.data() + in.length(), out);
}

void approx_tolower(std::string_view in, char *out) {
    approx_tolower(in.data(), in.data() + in.length(), out);
}

size_t basic_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset, bool caseless /* = false */) {

    assert(!pattern.empty());

    std::string_view file_contents_substr = file_contents.substr(
        beginning_offset, ending_offset - beginning_offset);
    if (file_contents_substr.length() < pattern.length()) {
        return std::string::npos;
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

        return std::string::npos;
    } else {
        size_t pos = file_contents_substr.find(pattern);
        if (pos == std::string::npos) {
            return std::string::npos;
        } else {
            return pos + beginning_offset;
        }
    }
}

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless /* = false */) {
    assert(caseless == false);
    std::string_view sub_contents = file_contents.substr(
        beginning_offset, ending_offset - beginning_offset);

    size_t result = sub_contents.rfind(pattern);
    if (result == std::string::npos) {
        return result;
    } else {
        return beginning_offset + result;
    }
}

namespace pcre2 {
using Code =
    std::unique_ptr<pcre2_code,
                    decltype([](pcre2_code *re) { pcre2_code_free(re); })>;
Code compile(std::string_view pattern) {
    int errornumber;
    PCRE2_SIZE erroroffset;
    return Code{pcre2_compile((PCRE2_SPTR8)pattern.data(),
                              (PCRE2_SIZE)pattern.size(),
                              0,            /* default options */
                              &errornumber, /* for error number */
                              &erroroffset, /* for error offset */
                              NULL)};       /* use default compile context */
}

std::optional<std::pair<size_t, size_t>> match(const Code &code,
                                               std::string_view subject) {
    pcre2_match_data *match_data =
        pcre2_match_data_create_from_pattern(code.get(), NULL);

    int rc = pcre2_match(code.get(),                  /* the compiled pattern */
                         (PCRE2_SPTR8)subject.data(), /* the subject string */
                         subject.length(), /* the length of the subject */
                         0,          /* start at offset 0 in the subject */
                         0,          /* default options */
                         match_data, /* block for storing the result */
                         NULL);      /* use default match context */

    if (rc < 0) {
        return std::nullopt;
    }
    PCRE2_SIZE *ovector = pcre2_get_ovector_pointer(match_data);
    auto ret = std::make_pair(ovector[0], ovector[1]);
    pcre2_match_data_free(match_data);
    return ret;
}

} // namespace pcre2

size_t regex_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset, bool caseless /* =false */) {
    assert(caseless == false);
    pcre2::Code re = pcre2::compile(pattern);

    std::optional<std::pair<size_t, size_t>> ret = pcre2::match(
        re, file_contents.substr(beginning_offset,
                                 ending_offset - beginning_offset));
    if (!ret) {
        return std::string_view::npos;
    }
    return ret->first + beginning_offset;
}

size_t regex_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless /* =false */) {
    assert(caseless == false);
    return std::string_view::npos;
}
