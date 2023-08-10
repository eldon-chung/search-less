#include "search.h"

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <string>

namespace {

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

std::vector<std::pair<size_t, size_t>> chunks(std::string_view file_contents,
                                              size_t beginning_offset,
                                              size_t ending_offset,
                                              size_t min_chunk_size) {
    std::vector<std::pair<size_t, size_t>> out;
    while (beginning_offset != ending_offset) {
        size_t approx_cur_chunk_end =
            std::min(beginning_offset + min_chunk_size, ending_offset);
        if (approx_cur_chunk_end == ending_offset) {
            out.push_back({beginning_offset, ending_offset});
            break;
        }
        size_t cur_chunk_end =
            file_contents.find_first_of('\n', approx_cur_chunk_end);
        if (cur_chunk_end == std::string_view::npos) {
            cur_chunk_end = ending_offset;
        } else {
            cur_chunk_end = std::min(cur_chunk_end + 1, ending_offset);
        }

        out.push_back({beginning_offset, cur_chunk_end});
        beginning_offset = cur_chunk_end;
    }
    return out;
}

std::pair<std::string, std::map<size_t, size_t>>
flip_by_lines(std::string_view str) {
    std::pair<std::string, std::map<size_t, size_t>> out;
    auto &[out_str, out_map] = out;

    out_str.reserve(str.length());

    while (!str.empty()) {
        size_t last_newline = str.find_last_of('\n');
        if (last_newline == std::string_view::npos) {
            out_map.insert({out_str.size(), 0});
            out_str.append(str);
            break;
        } else {
            out_map.insert({out_str.size(), last_newline + 1});
            out_str.append(str.substr(last_newline + 1));
            out_str.push_back('\n');
            str = str.substr(0, last_newline);
        }
    }

    return out;
}

} // namespace

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

    // split into line-aligned 4MB chunks
    for (auto [chunk_start, chunk_end] : chunks(
             file_contents, beginning_offset, ending_offset, 4 * 1024 * 1024)) {
        std::optional<std::pair<size_t, size_t>> ret = pcre2::match(
            re, file_contents.substr(chunk_start, chunk_end - chunk_start));
        if (ret) {
            return ret->first + chunk_start;
        }
    }
    return std::string_view::npos;
}

size_t regex_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless /* =false */) {
    assert(caseless == false);
    pcre2::Code re = pcre2::compile(pattern);

    // split into line-aligned 4MB chunks
    auto ch =
        chunks(file_contents, beginning_offset, ending_offset, 4 * 1024 * 1024);
    for (auto it = ch.rbegin(); it != ch.rend(); ++it) {
        auto [chunk_start, chunk_end] = *it;
        std::optional<std::pair<size_t, size_t>> ret = pcre2::match(
            re, file_contents.substr(chunk_start, chunk_end - chunk_start));
        if (ret) {
            // Found first matching chunk, now flip that chunk by lines and then
            // search on it
            auto [flipped_chunk, flip_map] = flip_by_lines(
                file_contents.substr(chunk_start, chunk_end - chunk_start));

            std::optional<std::pair<size_t, size_t>> actual_ret =
                pcre2::match(re, flipped_chunk);

            assert(actual_ret);

            size_t line_offset_of_match =
                flipped_chunk.find_last_of('\n', actual_ret->first);
            if (line_offset_of_match == std::string_view::npos) {
                line_offset_of_match = 0;
            } else {
                ++line_offset_of_match;
            }
            size_t original_line_offset = flip_map[line_offset_of_match];
            return actual_ret->first - line_offset_of_match +
                   original_line_offset + chunk_start;
        }
    }
    return std::string_view::npos;
}
