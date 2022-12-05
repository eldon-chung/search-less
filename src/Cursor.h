#pragma once

#include "FileHandle.h"

template <typename T> struct Cursor {
    typename T::LineIt m_cur_line;
    size_t m_offset;

    // TODO: if non printable ascii is encountered, make sure to count them
    // as longer <HEX> sequences e.g. with window width of 8, "abcdefgh"
    // fits on one line, but
    // "<00><00>" only 2 null bytes fit in the same space.

    static Cursor<T> get_cursor_at_byte_offset(T *model, size_t offset) {
        return {model->get_line_at_byte_offset(offset), offset};
    }

    [[nodiscard]] size_t get_offset() const {
        return m_offset;
    }

    // Get the contents of the line from the cursor to the end of the line
    // (\n)
    [[nodiscard]] std::string_view get_contents() const {
        return m_cur_line->substr(m_offset - m_cur_line.line_begin_offset());
    }

    [[nodiscard]] Cursor<T> round_to_wrapped_line(size_t window_width) const {
        return {m_cur_line, m_cur_line.line_begin_offset() +
                                (m_offset - m_cur_line.line_begin_offset()) /
                                    window_width * window_width};
    }

    [[nodiscard]] Cursor<T> prev_wrapped_line(size_t window_width) const {
        if (m_offset == 0) {
            return *this;
        }
        if (m_offset == m_cur_line.line_begin_offset()) {
            typename T::LineIt prev_line = --(typename T::LineIt(m_cur_line));
            size_t new_offset =
                prev_line.line_begin_offset() +
                (prev_line->size() - 1) / window_width * window_width;
            return {prev_line, new_offset};
        } else {
            size_t new_offset = std::max(m_offset - window_width,
                                         m_cur_line.line_begin_offset());
            return {m_cur_line, new_offset};
        }
    }

    [[nodiscard]] Cursor<T> next_wrapped_line(size_t window_width) const {
        if (m_offset >= m_cur_line.get_model()->length()) {
            return *this;
        }
        size_t potential_next_offset = m_offset + window_width;
        if (potential_next_offset >= m_cur_line.line_end_offset()) {
            typename T::LineIt next_line = ++(typename T::LineIt(m_cur_line));
            return {next_line, next_line.line_begin_offset()};
        }

        bool rest_of_line_is_newlines =
            m_cur_line->substr(window_width).find_first_not_of("\r\n") ==
            std::string_view::npos;
        if (rest_of_line_is_newlines) {
            typename T::LineIt next_line = ++(typename T::LineIt(m_cur_line));
            return {next_line, next_line.line_begin_offset()};
        }

        return {m_cur_line, m_offset + window_width};
    }

    [[nodiscard]] Cursor<T> prev_full_line() const {
        if (m_cur_line.line_begin_offset() == 0) {
            return *this;
        }
        typename T::LineIt prev_line = --(typename T::LineIt(m_cur_line));
        return {prev_line, prev_line.line_begin_offset()};
    }

    [[nodiscard]] Cursor<T> next_full_line() const {
        if (m_cur_line.line_end_offset() == m_cur_line.get_model()->length()) {
            return *this;
        }
        typename T::LineIt next_line = ++(typename T::LineIt(m_cur_line));
        return {next_line, next_line.line_begin_offset()};
    }

    [[nodiscard]] Cursor<T> prev_line(size_t window_width, bool wrapped) const {
        if (wrapped) {
            return prev_wrapped_line(window_width);
        } else {
            return prev_full_line();
        }
    }

    [[nodiscard]] Cursor<T> next_line(size_t window_width, bool wrapped) const {
        if (wrapped) {
            return next_wrapped_line(window_width);
        } else {
            return next_full_line();
        }
    }

    [[nodiscard]] bool operator==(Cursor<T> const &) const = default;
};
