#pragma once

#include <stddef.h>

#include <assert.h>

#include <deque>
#include <iterator>
#include <optional>
#include <string_view>
#include <vector>

#include "ContentHandle.h"

/*
Invalidated when the screen width or wrap mode changes.
Otherwise, the Page struct is persistent across all
other calls you might make to it.
*/
struct Page {

    /**
     * A PageLine is basically a string view,
     * but it stores the offsets relative to the base address
     * of the contents string view.
     * It also stores two additional offsets that
     * tell you where the true line begins and ends
     * (the true ending either points at a newline, or at EOF)
     */
    struct PageLine {
        size_t m_line_start;
        size_t m_line_end;

        // never mutate these
        size_t m_view_start;
        size_t m_view_end;

        static PageLine get_rounded_page_line(std::string_view containing_line,
                                              const char *base_addr,
                                              size_t width, size_t offset) {
            size_t line_start = (size_t)(containing_line.data() - base_addr);
            size_t line_end = line_start + containing_line.size();

            size_t chunk_idx = (offset - line_start) / width;
            size_t view_start =
                std::min(line_end, line_start + width * chunk_idx);
            size_t view_end = std::min(line_end, view_start + width);

            return {line_start, line_end, view_start, view_end};
        }

        size_t length() const {
            return m_view_end - m_view_start;
        }

        size_t start() const {
            return m_view_start;
        }

        size_t end() const {
            return m_view_end;
        }

        bool has_right() const {
            return m_view_end != m_line_end;
        }

        bool has_left() const {
            return m_view_start != m_line_start;
        }

        bool empty() const {
            return m_view_start == m_view_end;
        }

        bool full_length() const {
            return m_line_end - m_line_start;
        }
    };

    std::deque<PageLine> m_lines;
    // eventually remove this
    size_t m_global_offset;
    size_t m_chunk_idx;

    // Invariants
    size_t m_width;
    size_t m_height;
    bool m_wrap_lines;

  private:
    // keeping some of the PageLine related algorithms
    // outside of the PageLine struct because I really just
    // want it as a slightly nicer string view that we should operate on
    // which has no notion of scrolling or moving

    PageLine move_right(PageLine page_line) {
        page_line.m_view_start = page_line.m_view_end;
        page_line.m_view_end =
            std::min(page_line.m_line_end, page_line.m_view_start + m_width);

        return page_line;
    }

    PageLine move_left(PageLine page_line) {
        page_line.m_view_end = page_line.m_view_start;

        if (page_line.m_line_start + m_width < page_line.m_view_start) {
            page_line.m_view_start -= m_width;
        } else {
            page_line.m_view_start = page_line.m_line_start;
        }

        return page_line;
    }

    size_t get_chunk_idx(PageLine const &page_line, size_t width) {
        // we add one to the chunk idx if we can move left
        // but we're pointing at the end right now
        return page_line.length() / width +
               (page_line.empty() && page_line.has_left());
    }

    static std::string_view from_page_line(const char *base_addr,
                                           PageLine page_line) {
        return {base_addr + page_line.start(), base_addr + page_line.end()};
    };

    static std::string_view get_sv_containing_offset(std::string_view contents,
                                                     size_t offset) {
        // we're going to explicitly allow for the case that offset ==
        // contents.size()
        if (offset > contents.size()) {
            throw std::out_of_range("Page: attempting to index into something"
                                    "out of content size.\n");
        }

        const char *base_addr = contents.data();

        if (offset == 0) {
            size_t next_newl = contents.find('\n');
            if (next_newl == std::string::npos) {
                next_newl = contents.size();
            }
            return contents.substr(0, next_newl);
        }

        // find the newline before us
        size_t starting_pos = contents.rfind('\n', offset - 1);
        if (starting_pos == std::string::npos) {
            starting_pos = 0;
        } else {
            ++starting_pos;
        }

        contents = contents.substr(starting_pos);

        size_t newl_pos = contents.find('\n');
        if (newl_pos == std::string::npos) {
            newl_pos = contents.size();
        }

        return contents.substr(0, newl_pos);
    }

  public:
    static Page get_page_at_byte_offset(std::string_view contents,
                                        size_t offset, size_t height,
                                        size_t width, bool wrap_lines = true,
                                        bool auto_scroll_right = true) {

        const char *base_addr = contents.data();

        // get the string view containing our offset
        std::string_view containing_line =
            get_sv_containing_offset(contents, offset);

        size_t chunk_idx = 0;
        if (!wrap_lines && auto_scroll_right) {
            chunk_idx =
                (offset - (size_t)(containing_line.data() - base_addr)) / width;
        }

        // compute rounded offset and chunk_idx ourselves
        PageLine initial_line = PageLine::get_rounded_page_line(
            containing_line, base_addr, width, offset);

        Page initial_page = {{initial_line}, initial_line.start(),
                             chunk_idx,      width,
                             height,         wrap_lines};

        // now scroll down and up to fill out the remaining lines
        while (initial_page.get_num_lines() < height &&
               initial_page.has_next(contents)) {
            initial_page.scroll_down(contents);
        }

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

    size_t get_nth_offset(size_t index) const {
        return m_lines[index].start();
    }

    size_t get_num_lines() const {
        return m_lines.size();
    }

    void scroll_right() {
        // based on the way it's written right now there's no real need to
        // invoke has_right because it would just iterate on the list anyway

        // should we do some rounding
        // in case we move between lines
        for (auto &line : m_lines) {
            if (line.has_right()) {
                line = move_right(line);
            }
        }

        update_chunk_idx();
    }

    bool has_right() const {
        // might as well update chunk since we're
        // iterating on it anyway
        size_t new_chunk_idx = 0;
        bool has_right = false;
        for (auto const &line : m_lines) {
            has_right |= line.has_right();
        }

        return has_right;
    }

    void scroll_left() {
        // based on the way it's written right now there's no real need to
        // invoke has_right because it would just iterate on the list anyway

        // precondition: m_chunk_idx is updated
        for (auto &line : m_lines) {
            // assert the precondition
            assert(get_chunk_idx(line, m_width) <= m_chunk_idx);
            if (line.has_left()) {
                line = move_left(line);
            }
        }

        update_chunk_idx();
    }

    bool has_left() const {
        bool has_left = false;
        for (auto const &line : m_lines) {
            has_left |= line.has_left();
        }
        return has_left;
    }

    /**
     * Helps to bound the chunk idx when scrolling between lines
     */
    void update_chunk_idx() {
        size_t new_chunk_idx = 0;
        for (auto const &line : m_lines) {
            new_chunk_idx =
                std::max(new_chunk_idx, get_chunk_idx(line, m_width));
        }
        m_chunk_idx = new_chunk_idx;
    }

    void scroll_down(std::string_view contents) {
        // if our ending line is already at EOF
        if (!has_next(contents)) {
            return;
        }

        if (m_wrap_lines && m_lines.back().has_right()) {
            if (m_lines.size() == m_height) {
                m_lines.pop_front();
            }
            m_lines.push_back(move_right(m_lines.back()));
            update_chunk_idx();
            return;
        }

        const char *base_addr = contents.data();

        // skip over one (we're guaranteed that we're skipping over a newline)
        size_t next_starting_pos = get_end_offset() + 1;
        std::string_view containing_line =
            get_sv_containing_offset(contents, next_starting_pos);

        size_t relative_offset;
        if (m_wrap_lines) {
            relative_offset = 0;
        } else {
            relative_offset =
                std::min(containing_line.length() / m_width * m_width,
                         m_chunk_idx * m_width);
        }

        PageLine next_line = PageLine::get_rounded_page_line(
            containing_line, base_addr, m_width,
            next_starting_pos + relative_offset);

        if (m_lines.size() == m_height) {
            m_lines.pop_front();
        }
        m_lines.push_back(next_line);
        update_chunk_idx();
    }

    void scroll_up(std::string_view contents) {
        if (!has_prev()) {
            return;
        }

        if (m_wrap_lines && m_lines.front().has_left()) {
            PageLine prev_line = move_left(m_lines.front());
            if (m_lines.size() == m_height) {
                m_lines.pop_back();
            }
            m_lines.push_front(prev_line);
            update_chunk_idx();
            return;
        }

        const char *base_addr = contents.data();

        // else we're going to be starting on a new line
        assert(contents.back() == '\n');
        assert(get_begin_offset() >= 1);

        // skip over one
        size_t prev_starting_pos = get_begin_offset() - 1;
        std::string_view containing_line =
            get_sv_containing_offset(contents, prev_starting_pos);

        // update prev_starting_pos to point at the start of the line
        prev_starting_pos = (size_t)(containing_line.data() - base_addr);

        assert(!m_wrap_lines || (m_chunk_idx == 0));
        size_t relative_offset =
            std::min(containing_line.length() / m_width * m_width,
                     m_chunk_idx * m_width);

        PageLine prev_line = PageLine::get_rounded_page_line(
            containing_line, base_addr, m_width,
            prev_starting_pos + relative_offset);

        if (m_lines.size() == m_height) {
            m_lines.pop_back();
        }
        m_lines.push_front(prev_line);
        update_chunk_idx();
    }

    // special case: always valid regardless
    size_t get_begin_offset() const {
        return m_lines.front().start();
    }

    size_t get_end_offset() const {
        return m_lines.back().end();
    }

    bool has_prev() const {
        return get_begin_offset() != 0;
    }

    bool has_next(std::string_view contents) const {
        if (m_lines.empty()) {
            return false;
        }
        // see if there are any more bytes that are
        // beyond our current line
        return m_lines.back().end() + 1 < contents.size();
    }

    auto cbegin() const {
        return m_lines.cbegin();
    }

    auto cend() const {
        return m_lines.cend();
    }
};
