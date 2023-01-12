#pragma once

#include <algorithm>
#include <cassert>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

#include <curses.h>
#include <ncurses.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include "ContentHandle.h"
#include "Cursor.h"
#include "Page.h"

inline std::string_view strip_trailing_rn(std::string_view str) {
    size_t last_non_newline_char = str.find_last_not_of("\r\n");
    if (last_non_newline_char == std::string_view::npos) {
        return str.substr(0, 0);
    } else {
        return str.substr(0, last_non_newline_char + 1);
    }
}

// Serves as the driver for the entire view. For now let's keep it at a simple
//  thing that just holds a text_window, and given the state that needs to be
//  rendered drives the entire rendering logic
struct View {

    enum class ColorPair {
        MAIN_RESULT = 0,
        SIDE_RESULT = 8,
    };

    std::mutex *m_nc_mutex;
    WINDOW *m_main_window_ptr;
    WINDOW *m_command_window_ptr;
    size_t m_main_window_height;
    size_t m_main_window_width;

    ContentHandle *m_content_handle;
    bool m_wrap_lines;

    std::string m_status;
    std::string m_command;

    Page m_page;

    static View create(std::mutex *nc_mutex, ContentHandle *content_handle,
                       FILE *tty) {
        std::scoped_lock lock(*nc_mutex);
        newterm(getenv("TERM"), stdout, tty);
        start_color();
        use_default_colors();
        noecho();
        // raw();
        cbreak();
        curs_set(0);
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);

        // initialise some colours
        // init_pair((short)ColorPair::MAIN_RESULT, COLOR_WHITE, COLOR_BLACK);
        init_pair((short)ColorPair::SIDE_RESULT, -1, COLOR_RED);

        // Get the screen height and width
        int height, width;
        getmaxyx(stdscr, height, width);
        // Construct the view with the main screen, and passing in height and
        // width

        WINDOW *command_window_ptr = newwin(1, width, height - 1, 0);
        if (command_window_ptr == NULL) {
            fprintf(stderr, "View: could not create new window!\n");
            exit(1);
        }
        wresize(stdscr, height - 1, width);

        return View(nc_mutex, content_handle, command_window_ptr, height,
                    width);
    }

  private:
    View(std::mutex *nc_mutex, ContentHandle *content_handle,
         WINDOW *command_window_ptr, int height, int width)
        : m_nc_mutex(nc_mutex), m_main_window_ptr(stdscr),
          m_command_window_ptr(command_window_ptr),
          m_main_window_height((size_t)(height - 1)),
          m_main_window_width((size_t)width), m_content_handle(content_handle),
          m_wrap_lines(true),
          m_page(Page::get_page_at_byte_offset(
              m_content_handle->get_contents(), 0, m_main_window_height,
              m_main_window_width, m_wrap_lines))

    {
    }

  public:
    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&) = delete;
    View &operator=(View &&) = delete;
    ~View() {
        std::scoped_lock lock(*m_nc_mutex);
        delwin(m_main_window_ptr);
        delwin(m_command_window_ptr);
        endwin(); // here's how you finish up ncurses mode
    }

    Page current_page() const {
        return m_page;
    }

    Page const &const_current_page() const {
        return m_page;
    }

    void scroll_up(size_t num_scrolls = 1) {
        while (num_scrolls-- > 0 && m_page.has_prev()) {
            m_page.scroll_up(m_content_handle->get_contents());
        }
    }

    void scroll_down(size_t num_scrolls = 1) {
        while (num_scrolls-- > 0) {
            if (m_page.has_next(m_content_handle->get_contents())) {
                m_page.scroll_down(m_content_handle->get_contents());
            } else if (m_content_handle->has_changed()) {
                size_t offset = m_page.get_begin_offset();
                m_content_handle->read_more();
                move_to_byte_offset(offset);
                m_page.scroll_down(m_content_handle->get_contents());
            } else {
                break;
            }
        }
    }

    void move_to_top() {
        move_to_byte_offset(0);
    }

    void move_to_end() {
        if (m_content_handle->get_contents().empty()) {
            return;
        }
        move_to_byte_offset(m_content_handle->get_contents().length() - 1);
    }

    void move_to_byte_offset(size_t offset) {
        m_page = Page::get_page_at_byte_offset(
            m_content_handle->get_contents(), offset, m_main_window_height,
            m_main_window_width, m_wrap_lines);
    }

    size_t get_starting_offset() const {
        return m_page.get_begin_offset();
    }

    size_t get_ending_offset() const {
        return m_page.get_end_offset();
    }

    struct Highlight {
        enum class Type { Main, Side };

        size_t m_offset;
        size_t m_length;
        Type m_type;

        size_t begin_offset() const {
            return m_offset;
        }
        size_t end_offset() const {
            return m_offset + m_length;
        }
        size_t length() const {
            return m_length;
        }
        Type type() const {
            return m_type;
        }
    };

    void display_page_at(std::vector<std::vector<Highlight>> highlight_list) {

        std::scoped_lock lock(*m_nc_mutex);

        werase(m_main_window_ptr);

        Page page = current_page();
        // assert(highlight_list.size() == page.get_num_lines());

        for (size_t row_idx = 0; row_idx < m_main_window_height; ++row_idx) {
            if (row_idx < page.get_num_lines()) {
                std::string_view curr_line = page.get_nth_line(
                    m_content_handle->get_contents(), row_idx);
                mvwaddnstr(m_main_window_ptr, row_idx, 0, curr_line.data(),
                           std::min(curr_line.size(), m_main_window_width));
            } else {
                mvwaddnstr(m_main_window_ptr, row_idx, 0, "~", 1);
            }
        }

        wstandend(m_main_window_ptr);

        // split the highlights based on row
        auto put_highlights = [](Highlight const &highlight, size_t row_idx,
                                 size_t m_main_window_width,
                                 WINDOW *m_main_window_ptr) {
            size_t actual_length =
                std::min(highlight.length(),
                         m_main_window_width - highlight.begin_offset());
            attr_t attr = (highlight.type() == Highlight::Type::Main)
                              ? WA_STANDOUT
                              : WA_NORMAL;
            using enum ColorPair;
            short colour = (short)((highlight.type() == Highlight::Type::Main)
                                       ? MAIN_RESULT
                                       : SIDE_RESULT);

            mvwchgat(m_main_window_ptr, row_idx, highlight.begin_offset(),
                     actual_length, attr, colour, 0);
        };

        for (size_t row_idx = 0; row_idx < highlight_list.size(); ++row_idx) {
            for (auto const &highlight : highlight_list[row_idx]) {
                put_highlights(highlight, row_idx, m_main_window_width,
                               m_main_window_ptr);
            }
        }

        wrefresh(m_main_window_ptr);
    }

    void display_command(std::string_view command, size_t cursor_pos) {
        if (cursor_pos >= m_main_window_width) {
            size_t half_width = (m_main_window_width + 1) / 2;
            size_t adjusted_cursor_pos =
                (cursor_pos - half_width) % half_width + half_width;
            command.remove_prefix(cursor_pos - adjusted_cursor_pos);
            cursor_pos = adjusted_cursor_pos;
        }

        std::scoped_lock lock(*m_nc_mutex);
        werase(m_command_window_ptr);
        wattrset(m_command_window_ptr, WA_NORMAL);
        mvwaddnstr(m_command_window_ptr, 0, 0, command.data(),
                   (int)std::min(command.length(), m_main_window_width));

        if (cursor_pos < std::numeric_limits<int>::max()) {
            mvwchgat(m_command_window_ptr, 0, (int)cursor_pos, (int)1,
                     WA_STANDOUT, 0, NULL);
        }
        wrefresh(m_command_window_ptr);
    }

    void display_status(std::string_view status) {
        std::scoped_lock lock(*m_nc_mutex);
        werase(m_command_window_ptr);
        wattrset(m_command_window_ptr, WA_STANDOUT);
        mvwaddnstr(m_command_window_ptr, 0, 0, status.data(),
                   (int)std::min(status.length(), m_main_window_width));
        wrefresh(m_command_window_ptr);
    }

    void handle_resize() {
        std::scoped_lock lock(*m_nc_mutex);
        endwin();
        refresh();

        wresize(m_main_window_ptr, LINES - 1, COLS);

        m_main_window_height = (size_t)(LINES - 1);
        m_main_window_width = (size_t)COLS;

        delwin(m_command_window_ptr);

        m_command_window_ptr = newwin(1, COLS, LINES - 1, 0);
        if (m_command_window_ptr == NULL) {
            fprintf(stderr, "failed to make new command window\n");
            exit(1);
        }

        wclear(m_main_window_ptr);
        wclear(m_command_window_ptr);

        size_t curr_offset = m_page.get_begin_offset();
        m_page = Page::get_page_at_byte_offset(
            m_content_handle->get_contents(), curr_offset, m_main_window_height,
            m_main_window_width, m_wrap_lines);
    }
};
