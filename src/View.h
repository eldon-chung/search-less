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
        raw();
        curs_set(0);
        keypad(stdscr, TRUE);
        nodelay(stdscr, TRUE);

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
          m_wrap_lines(true), m_page(Page::get_page_at_byte_offset(
                                  m_content_handle, 0, m_main_window_height,
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

    void scroll_up(size_t num_scrolls = 1) {
        while (num_scrolls-- > 0 && m_page.has_prev()) {
            m_page.scroll_up();
        }
    }

    void scroll_down(size_t num_scrolls = 1) {
        while (num_scrolls-- > 0 && m_page.has_next()) {
            m_page.scroll_down();
        }
    }

    void move_to_top() {
        move_to_byte_offset(0);
    }

    void move_to_end() {
        move_to_byte_offset(m_content_handle->get_contents().length());
    }

    void move_to_byte_offset(size_t offset) {
        m_page = Page::get_page_at_byte_offset(
            m_content_handle, offset, m_main_window_height, m_main_window_width,
            m_wrap_lines);
    }

    size_t get_starting_offset() const {
        return m_page.get_begin_offset();
    }

    size_t get_ending_offset() const {
        return m_page.get_end_offset();
    }

    struct Highlight {
        size_t offset;
        size_t length;
        size_t get_begin_offset() const {
            return offset;
        }
        size_t get_end_offset() const {
            return offset + length;
        }
    };

    void display_page_at(std::vector<Highlight> highlight_list) {

        std::scoped_lock lock(*m_nc_mutex);

        werase(m_main_window_ptr);

        Page page = current_page();

        for (size_t row_idx = 0; row_idx < m_main_window_height; ++row_idx) {
            if (row_idx < page.get_num_lines()) {
                std::string_view curr_line = page[row_idx];
                mvwaddnstr(m_main_window_ptr, row_idx, 0, curr_line.data(),
                           std::min(curr_line.size(), m_main_window_width));
            } else {
                mvwaddnstr(m_main_window_ptr, row_idx, 0, "~", 1);
            }
        }

        wstandend(m_main_window_ptr);

        // split the highlights based on row
        auto highlight_it = highlight_list.cbegin();
        const char *base_addr = m_content_handle->get_contents().data();

        for (size_t row_idx = 0; row_idx < m_main_window_height &&
                                 highlight_it != highlight_list.cend();
             ++row_idx) {
            std::string_view curr_line = page[row_idx];
            size_t starting_offset = (size_t)(curr_line.data() - base_addr);
            size_t ending_offset =
                (size_t)(curr_line.data() + curr_line.size() - base_addr);

            ending_offset =
                std::min(ending_offset, starting_offset + m_main_window_width);

            while (highlight_it != highlight_list.cend() &&
                   highlight_it->offset >= starting_offset &&
                   highlight_it->offset < starting_offset + ending_offset) {

                size_t highlight_len = std::min(
                    highlight_it->length,
                    starting_offset + ending_offset - highlight_it->offset);
                mvwchgat(m_main_window_ptr, row_idx,
                         highlight_it->offset - starting_offset, highlight_len,
                         WA_STANDOUT, 0, 0);
                ++highlight_it;
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
            m_content_handle, curr_offset, m_main_window_height,
            m_main_window_width, m_wrap_lines);
    }
};
