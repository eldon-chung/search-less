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

Also invalidated when the underlying content under content handle
is remapped
*/
struct Page {
    std::deque<std::string_view> m_lines;
    const ContentHandle *m_content_handle;
    size_t m_global_offset;

    // Invariants
    size_t m_width;
    bool m_wrap_lines;

    static Page get_page_at_byte_offset(ContentHandle const *content_handle,
                                        size_t offset, size_t height,
                                        size_t width, bool wrap_lines) {

        auto break_into_wrapped_lines =
            [width](std::string_view content) -> std::vector<std::string_view> {
            std::vector<std::string_view> to_return;
            to_return.reserve(content.size() / width + 1);
            while (true) {
                to_return.push_back(content.substr(0, width));
                if (content.size() < width) {
                    break;
                }
                content = content.substr(width);
            }

            return to_return;
        };

        // note: need to handle the case where we're given an offset and can
        // scroll further back up

        std::deque<std::string_view> lines;
        // build the initial list of lines and bound the size of content
        // so that we dont end up taking forever to
        // search in case it's a huge document with no newls
        std::string_view curr_content = content_handle->get_contents().substr(
            offset, ((width + 1) * height));

        while (!curr_content.empty() && lines.size() < height) {
            size_t next_newl = curr_content.find_first_of("\n");

            if (wrap_lines) {
                // break the current line into multiple string views
                // and push_back all over them into into the lines collection
                auto broken_lines =
                    break_into_wrapped_lines(curr_content.substr(0, next_newl));
                std::move(broken_lines.begin(), broken_lines.end(),
                          std::back_inserter(lines));
            } else {
                // just push the entire thing into lines
                lines.push_back(curr_content.substr(0, next_newl));
            }
            // skip over the newl
            curr_content = curr_content.substr(next_newl + 1);
        }

        return {std::move(lines), content_handle, offset, (size_t)width,
                wrap_lines};
    }

    std::string_view operator[](size_t index) const {
        return m_lines[index];
    }

    size_t get_num_lines() const {
        return m_lines.size();
    }

    void scroll_down() {
        if (!has_next()) {
            return;
        }

        // depending on whether we wrap we search different substrings for \n
        size_t substr_len = (m_wrap_lines) ? m_width : std::string::npos;

        // get the offset based on the last line
        // add 1 to skip over the potential newl
        size_t next_starting_pos = get_end_offset() + 1;

        std::string_view remaining_contents =
            m_content_handle->get_contents().substr(next_starting_pos,
                                                    substr_len);

        size_t newl_pos = remaining_contents.find_first_of("\n");
        if (newl_pos != std::string::npos) {
            remaining_contents = remaining_contents.substr(0, newl_pos);
        }

        m_lines.pop_front();
        m_lines.push_back(remaining_contents);

        m_global_offset = (size_t)(m_lines.front().data() -
                                   m_content_handle->get_contents().data());
    }

    void scroll_up() {
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
                m_content_handle->get_contents().substr(0, m_global_offset - 1);

            // search backwards for the next '\n'
            size_t last_idx = prior_contents.find_last_of("\n");
            if (last_idx != std::string::npos) {
                prior_contents = prior_contents.substr(last_idx + 1);
            }

            m_lines.pop_back();
            m_lines.push_back(prior_contents);
            // do we need to make sure prior contents.data() is correct
            // to update global offset properly
            m_global_offset = (size_t)(m_lines.front().data() -
                                       m_content_handle->get_contents().data());
            return;
        }

        // in the line wrap case
        std::string_view prior_contents =
            m_content_handle->get_contents().substr(0, m_global_offset);

        if (prior_contents.back() != '\n') {
            // simple case where we just shift back by width
            std::string_view prior_line =
                m_content_handle->get_contents().substr(
                    m_global_offset - m_width, m_width);

            m_lines.pop_back();
            m_lines.push_back(prior_line);
            m_global_offset -= m_width;
        } else {
            prior_contents.remove_suffix(1);
            size_t newl_idx = prior_contents.find_last_of("\n");
            if (newl_idx != std::string::npos) {
                prior_contents = prior_contents.substr(newl_idx + 1);
            }

            // special case I guess
            if (prior_contents.empty()) {
                m_lines.pop_back();
                m_lines.push_front(prior_contents);
                m_global_offset -= 1;
            } else {
                size_t rounded_wrapped_line =
                    ((prior_contents.size() - 1) / m_width) * m_width;
                prior_contents =
                    prior_contents.substr(rounded_wrapped_line, m_width);

                m_lines.pop_back();
                m_lines.push_front(prior_contents);

                m_global_offset =
                    (size_t)(m_lines.front().data() -
                             m_content_handle->get_contents().data());
            }
        }
    }

    // special case: always valid regardless
    size_t get_begin_offset() const {
        return m_global_offset;
    }

    size_t get_end_offset() const {
        const char *last_addr = m_lines.back().data() + m_lines.back().size();
        return (size_t)(last_addr - m_content_handle->get_contents().data());
    }

    bool has_prev() const {
        return m_global_offset != 0;
    }

    bool has_next() const {
        if (m_lines.empty()) {
            return false;
        }
        // add 1 because we need to skip over a potential newl
        const char *maybe_next_addr =
            (m_lines.back().data() + m_lines.back().size() + 1);

        const char *end_addr = m_content_handle->get_contents().data() +
                               m_content_handle->get_contents().size();

        // see if there are any more bytes that are
        // beyond our current line
        return maybe_next_addr < end_addr;
    }

    auto cbegin() const {
        return m_lines.cbegin();
    }

    auto cend() const {
        return m_lines.cend();
    }
};
