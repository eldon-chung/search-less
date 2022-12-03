#pragma once

#include <iterator>
#include <string_view>

#include "Cursor.h"

// Easily invalidated! Do not store long term!
struct Page {
    // Note that these LineIt iterators are easily invalidated, do not store
    // long term!
    struct LineIt {
        Cursor m_cursor;
        size_t m_width;
        bool m_wrap_lines;

        std::string_view get_contents() const {
            if (m_wrap_lines) {
                return m_cursor.get_contents().substr(0, m_width);
            } else {
                return m_cursor.get_contents();
            }
        }

        size_t get_begin_offset() const {
            return m_cursor.get_offset();
        }
        size_t get_end_offset() const {
            return m_cursor.get_offset() + get_contents().length();
        }

        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        LineIt &operator++() {
            m_cursor = m_cursor.next_line(m_width, m_wrap_lines);
            return *this;
        }
        LineIt operator++(int) {
            LineIt line = *this;
            ++*this;
            return line;
        }
        LineIt &operator--() {
            m_cursor = m_cursor.prev_line(m_width, m_wrap_lines);
            return *this;
        }
        LineIt operator--(int) {
            LineIt line = *this;
            --*this;
            return line;
        }

        bool has_next() const {
            LineIt peek = *this;
            return *this != ++peek;
        }

        bool has_prev() const {
            LineIt peek = *this;
            return *this != --peek;
        }

        struct Cursed {
            std::string_view cursed;
            std::string_view operator*() const {
                return cursed;
            }
        };
        std::string_view operator*() const {
            return get_contents();
        }
        Cursed operator->() const {
            return Cursed{**this};
        }

        bool operator==(LineIt const &) const = default;
    };

    LineIt m_begin;
    LineIt m_end;

    static Page get_page_at_byte_offset(FileHandle const *model, size_t offset,
                                        size_t height, size_t width,
                                        bool wrap_lines) {
        Cursor start_cursor = Cursor::get_cursor_at_byte_offset(model, offset);
        if (wrap_lines) {
            start_cursor = start_cursor.round_to_wrapped_line(width);
        }
        LineIt start_line{start_cursor, width, wrap_lines};
        // Each loop, we go down N rows from m_cursor until we reach the end. if
        // we reach the end, move m_cursor up that many line and try again. We
        // need to try again since sometimes going up a line doesn't actually
        // result in more reLinendered lines.
        while (true) {
            LineIt end_line = start_line;
            size_t i = 0;
            for (; i < height; ++i) {
                if (end_line.get_begin_offset() == model->length()) {
                    break;
                }
                ++end_line;
            }
            if (i == height) {
                return {start_line, end_line};
            }

            for (; i < height; ++i) {
                if (start_line.get_begin_offset() == 0) {
                    return {start_line, end_line};
                }
                --start_line;
            }
            continue;
        }
    }

    LineIt begin() const {
        return m_begin;
    }
    LineIt end() const {
        return m_end;
    }
};
