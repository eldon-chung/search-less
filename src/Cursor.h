#pragma once

#include <assert.h>

#include "ContentHandle.h"

struct Cursor {
    // stores the file line that is independent of view
    // cursor is a position that always sits on this line
    // the end of LineIt is at m_content_handle->size() with width 0
    struct LineIt {
        ContentHandle *m_content_handle;
        size_t m_offset;
        // the width needs to be inclusive of the '\n' if it exists
        size_t m_width;

        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        [[nodiscard]] bool operator==(LineIt const &) const = default;

        LineIt(ContentHandle *content_handle, size_t offset)
            : m_content_handle(content_handle), m_offset(offset) {
            auto content_guard = m_content_handle->get_contents();
            std::string_view contents = content_guard.contents;
            size_t next_pos = contents.find_first_of("\n", m_offset);
            if (next_pos == std::string_view::npos) {
                m_width = m_content_handle->size() - m_offset;
            } else {
                m_width = next_pos - m_offset + 1;
            }
        }

        std::string_view get_line() const {
            return m_content_handle->get_contents().contents.substr(m_offset,
                                                                    m_width);
        }

        size_t line_begin_offset() const {
            return m_offset;
        }

        size_t line_end_offset() const {
            // should be the index of "\n" if it exists
            if (m_width == 0) {
                // special case at EOF
                return m_offset;
            }
            return m_offset + m_width - 1;
        }

        size_t size() const {
            return m_width;
        }

        LineIt &operator++() {
            // note: eventually need to update this to read more of the current
            // file if required?
            size_t next_line_end_pos =
                m_content_handle->get_contents().contents.find_first_of(
                    "\n", m_offset + m_width);
            // move offset up by width
            m_offset += m_width;
            if (next_line_end_pos == std::string_view::npos) {
                // update width to include whatever is remaining
                m_width =
                    m_content_handle->get_contents().contents.size() - m_offset;
            } else {
                // update width to include the new line
                m_width = next_line_end_pos - m_offset + 1;
            }
            return *this;
        }

        LineIt operator++(int) {
            LineIt line = *this;
            ++*this;
            return line;
        }

        LineIt &operator--() {
            // don't scroll up on the very first line
            if (m_offset == 0) {
                return *this;
            }

            assert(m_offset >= 1);
            size_t prev_line_end_pos = m_content_handle->get_contents()
                                           .contents.substr(0, m_offset - 1)
                                           .find_last_of("\n");

            if (prev_line_end_pos == std::string_view::npos) {
                // we're on the 1st line scrolling back to the 0th line
                m_width = m_offset;
                m_offset = 0;
            } else {
                // note: should we be subtracting by 1?
                m_width = m_offset - prev_line_end_pos - 1;
                m_offset = prev_line_end_pos + 1;
            }
            return *this;
        }

        LineIt operator--(int) {
            LineIt line = *this;
            --*this;
            return line;
        }

        bool has_next() const {
            // make sure width is 0 if and only if we're at the end
            assert(!(m_width == 0 ^ m_offset == m_content_handle->size()));
            return !(m_offset == m_content_handle->size());
        }

        bool has_prev() const {
            return m_offset > 0;
        }
    };

    ContentHandle *m_content_handle;
    LineIt m_cur_line;
    // only points to '\n' if that line is of length 1
    size_t m_offset;

    // TODO: if non printable ascii is encountered, make sure to count them
    // as longer <HEX> sequences e.g. with window width of 8, "abcdefgh"
    // fits on one line, but
    // "<00><00>" only 2 null bytes fit in the same space.

    static Cursor get_cursor_at_byte_offset(ContentHandle *content_handle,
                                            size_t offset) {
        return {content_handle, LineIt(content_handle, offset), offset};
    }

    [[nodiscard]] size_t get_offset() const {
        return m_offset;
    }

    // Get the contents of the line from the cursor to the end of the line
    // (\n)
    // NOTE: idk if we'll revive this method, but this method can't be
    // efficiently implemented with the content guard thingy
#if 0
    [[nodiscard]] std::string_view get_contents() const {
        return m_content_handle->get_contents().substr(m_offset);
    }
#endif

    [[nodiscard]] size_t operator-(const Cursor &other) const {
        return m_offset - other.get_offset();
    }

    [[nodiscard]] Cursor round_to_wrapped_line(size_t window_width) const {
        // question: should round_to_wrapped_line round up or down in a
        // newline case?
        // based on our usage it seems you should do the next line, not the
        // previous

        size_t line_offset = m_cur_line.line_begin_offset() +
                             ((m_offset - m_cur_line.line_begin_offset()) /
                              window_width * window_width);

        LineIt line = m_cur_line;
        if (line_offset == m_cur_line.line_end_offset() &&
            m_cur_line.size() > 1) {
            // if we are indexing into a '\n'
            line = ++(LineIt(line));
            line_offset = line.line_begin_offset();
        }

        return {m_content_handle, line, line_offset};
    }

    [[nodiscard]] Cursor prev_wrapped_line(size_t window_width) const {
        if (m_offset == 0) {
            return *this;
        }

        if (m_offset == m_cur_line.line_begin_offset()) {
            LineIt prev_line = --(LineIt(m_cur_line));

            size_t new_offset =
                prev_line.line_begin_offset() +
                (prev_line.size() - 1) / window_width * window_width;

            if (new_offset == prev_line.line_end_offset() &&
                new_offset >= window_width + prev_line.line_begin_offset()) {
                new_offset -= window_width;
            }
            return {m_content_handle, prev_line, new_offset};
        } else {
            size_t new_offset = std::max(m_offset - window_width,
                                         m_cur_line.line_begin_offset());
            return {m_content_handle, m_cur_line, new_offset};
        }
    }

    [[nodiscard]] Cursor next_wrapped_line(size_t window_width) const {
        size_t potential_next_offset = m_offset + window_width;

        // if adding window_width to offset still gives us non-newline chars
        if (potential_next_offset < m_cur_line.line_end_offset()) {
            return {m_content_handle, m_cur_line, potential_next_offset};
        }
        // else we need to advance the line
        LineIt next_line = ++LineIt(m_cur_line);
        return {m_content_handle, next_line, next_line.line_begin_offset()};
    }

    [[nodiscard]] Cursor prev_full_line() const {
        if (m_cur_line.line_begin_offset() == 0) {
            return *this;
        }
        LineIt prev_line = --LineIt(m_cur_line);
        return {m_content_handle, prev_line, prev_line.line_begin_offset()};
    }

    [[nodiscard]] Cursor next_full_line() const {
        if (m_cur_line.line_end_offset() == m_content_handle->size()) {
            return *this;
        }
        LineIt next_line = ++LineIt(m_cur_line);
        return {m_content_handle, next_line, next_line.line_begin_offset()};
    }

    [[nodiscard]] Cursor prev_line(size_t window_width, bool wrapped) const {
        if (wrapped) {
            return prev_wrapped_line(window_width);
        } else {
            return prev_full_line();
        }
    }

    [[nodiscard]] Cursor next_line(size_t window_width, bool wrapped) const {
        if (wrapped) {
            return next_wrapped_line(window_width);
        } else {
            return next_full_line();
        }
    }

    [[nodiscard]] bool has_next(size_t window_width, bool wrapped) const {
        Cursor next = next_line(window_width, wrapped);
        return *this != next;
        // return m_offset != m_content_handle->size();
    }

    [[nodiscard]] bool has_prev(size_t window_width, bool wrapped) const {
        Cursor prev = prev_line(window_width, wrapped);
        return *this != prev;
        // return m_offset != 0;
    }

    [[nodiscard]] bool operator==(Cursor const &) const = default;
};
