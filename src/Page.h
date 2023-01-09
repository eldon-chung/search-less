#pragma once

#include <deque>
#include <iterator>
#include <optional>
#include <stddef.h>
#include <string_view>
#include <vector>

#include "ContentHandle.h"

/*
Invalidated when the screen width or wrap mode changes.
Otherwise, the Page struct is persistent across all
other calls you might make to it.
*/
struct Page {

    struct PageLine {
        size_t start;
        size_t end;
    };

    std::deque<PageLine> m_lines;
    // eventually remove this
    size_t m_global_offset;

    // Invariants
    size_t m_width;
    size_t m_height;
    bool m_wrap_lines;

  private:
    static PageLine from_string_view(const char *base_addr,
                                     std::string_view sv) {
        return {(size_t)(sv.data() - base_addr),
                (size_t)(sv.data() - base_addr) + sv.size()};
    };

    static std::string_view from_page_line(const char *base_addr,
                                           PageLine page_line) {
        return {base_addr + page_line.start, base_addr + page_line.end};
    };

    static size_t round_to_width_offset(std::string_view contents,
                                        size_t offset, size_t width) {
        if (offset == 0) {
            return 0;
        }

        if (offset >= contents.size()) {
            throw std::out_of_range("Page: attempting to index into something"
                                    "out of content size.\n");
        }

        // if the previous character is a newl, we stay
        if (contents[offset - 1] == '\n') {
            return offset;
        }

        std::string_view prior_contents = contents.substr(0, offset);
        size_t last_newl = prior_contents.find_last_of("\n");
        size_t last_line_offset =
            (last_newl == std::string::npos) ? 0 : last_newl + 1;

        return (offset - last_line_offset) / width * width + last_line_offset;
    }

  public:
    static Page get_page_at_byte_offset(std::string_view contents,
                                        size_t offset, size_t height,
                                        size_t width, bool wrap_lines) {

        auto break_into_wrapped_lines =
            [](const char *base_addr, size_t width,
               std::string_view content) -> std::deque<PageLine> {
            std::deque<PageLine> to_return;
            while (true) {
                to_return.push_back(
                    from_string_view(base_addr, content.substr(0, width)));
                if (content.size() < width) {
                    break;
                }
                content = content.substr(width);
            }

            return to_return;
        };

        // note: need to handle the case where we're given an offset and can
        // scroll further back up

        offset = round_to_width_offset(contents, offset, width);

        const char *base_addr = contents.data();
        std::string_view curr_content = contents.substr(offset);
        std::deque<PageLine> lines;
        while (!curr_content.empty() && lines.size() < height) {
            size_t next_newl = curr_content.find_first_of("\n");
            if (next_newl == std::string::npos) {
                // if we can't find it take it to the end
                next_newl = curr_content.size() - 1;
            }
            if (wrap_lines) {
                // break the current line into multiple string views
                // and push_back all over them into into the lines collection
                auto broken_lines = break_into_wrapped_lines(
                    base_addr, width, curr_content.substr(0, next_newl));
                std::move(broken_lines.begin(), broken_lines.end(),
                          std::back_inserter(lines));
            } else {
                // just push the entire thing into lines
                lines.push_back(from_string_view(
                    base_addr, curr_content.substr(0, next_newl)));
            }
            // skip over the newl
            curr_content = curr_content.substr(next_newl + 1);
        }

        Page initial_page = {std::move(lines), offset, width, height,
                             wrap_lines};

        while (initial_page.get_num_lines() < height &&
               initial_page.has_prev()) {
            initial_page.scroll_up(contents);
        }

        return initial_page;
    }

    std::string_view get_nth_line(std::string_view contents,
                                  size_t index) const {
        return from_page_line(contents.data(), m_lines[index]);
    }

    size_t get_num_lines() const {
        return m_lines.size();
    }

    void scroll_down(std::string_view contents) {
        if (!has_next(contents)) {
            return;
        }

        // depending on whether we wrap we search different substrings for \n
        size_t substr_len = (m_wrap_lines) ? m_width : std::string::npos;

        // get the offset based on the last line
        // add 1 to skip over the potential newl
        size_t next_starting_pos = get_end_offset() + 1;

        std::string_view remaining_contents =
            contents.substr(next_starting_pos, substr_len);

        size_t newl_pos = remaining_contents.find_first_of("\n");
        if (newl_pos != std::string::npos) {
            remaining_contents = remaining_contents.substr(0, newl_pos);
        }

        m_lines.pop_front();
        m_lines.push_back(
            from_string_view(contents.data(), remaining_contents));

        m_global_offset = m_lines.front().start;
    }

    void scroll_up(std::string_view contents) {
        if (!has_prev()) {
            return;
        }
        // we need to maintain the invariant that if we're not
        // at the start of some line, it's the case that we're already wrapped
        // so the only time we need to aggressively search backwards is
        // when we're leaving a line

        if (!m_wrap_lines) {

            // without line wrapping the prior char is newl
            std::string_view prior_contents =
                contents.substr(0, m_global_offset - 1);

            // search backwards for the next '\n'
            size_t last_idx = prior_contents.find_last_of("\n");
            if (last_idx != std::string::npos) {
                prior_contents = prior_contents.substr(last_idx + 1);
            }
            // assert(m_lines.size() <= m_height);
            if (m_lines.size() == m_height) {
                m_lines.pop_front();
            }
            m_lines.push_back(
                from_string_view(contents.data(), prior_contents));
            // do we need to make sure prior contents.data() is correct
            // to update global offset properly
            m_global_offset = m_lines.front().start;
            return;
        }

        // in the line wrap case
        std::string_view prior_contents = contents.substr(0, m_global_offset);

        if (prior_contents.back() != '\n') {
            std::string_view prior_line =
                contents.substr(m_global_offset - m_width, m_width);

            if (m_lines.size() == m_height) {
                m_lines.pop_back();
            }
            m_lines.push_front(from_string_view(contents.data(), prior_line));
            m_global_offset -= m_width;
        } else {
            prior_contents.remove_suffix(1);
            size_t newl_idx = prior_contents.find_last_of("\n");
            if (newl_idx != std::string::npos) {
                prior_contents = prior_contents.substr(newl_idx + 1);
            }

            // special case I guess
            if (prior_contents.empty()) {
                // assert(m_lines.size() <= m_height);
                if (m_lines.size() == m_height) {
                    m_lines.pop_back();
                }
                m_lines.push_front(
                    from_string_view(contents.data(), prior_contents));
                m_global_offset -= 1;
            } else {
                size_t rounded_wrapped_line =
                    ((prior_contents.size() - 1) / m_width) * m_width;
                prior_contents =
                    prior_contents.substr(rounded_wrapped_line, m_width);

                // assert(m_lines.size() <= m_height);
                if (m_lines.size() == m_height) {
                    m_lines.pop_back();
                }
                m_lines.push_front(
                    from_string_view(contents.data(), prior_contents));
                m_global_offset = m_lines.front().start;
            }
        }
    }

    // special case: always valid regardless
    size_t get_begin_offset() const {
        return m_global_offset;
    }

    size_t get_end_offset() const {
        return m_lines.back().end;
    }

    bool has_prev() const {
        return m_global_offset != 0;
    }

    bool has_next(std::string_view contents) const {
        if (m_lines.empty()) {
            return false;
        }
        // see if there are any more bytes that are
        // beyond our current line
        return m_lines.back().end + 1 < contents.size();
    }

    auto cbegin() const {
        return m_lines.cbegin();
    }

    auto cend() const {
        return m_lines.cend();
    }
};
