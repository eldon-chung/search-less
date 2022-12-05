#pragma once

#include <cassert>
#include <mutex>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

#include <curses.h>
#include <ncurses.h>

#include <sys/ioctl.h>
#include <unistd.h>

#include "Cursor.h"
#include "FileHandle.h"
#include "Page.h"
#include "PipeHandle.h"

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
template <typename T> struct View {
    std::mutex *m_nc_mutex;
    WINDOW *m_main_window_ptr;
    WINDOW *m_command_window_ptr;
    size_t m_main_window_height;
    size_t m_main_window_width;

    T *m_model;
    size_t m_offset;
    bool m_wrap_lines;

    std::string m_status;
    std::string m_command;

    static View create(std::mutex *nc_mutex, T *model, FILE *tty) {
        return View(nc_mutex, model, tty);
    }

  private:
    View(std::mutex *nc_mutex, T *model, FILE *tty) {
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

        // The actual ctor
        m_nc_mutex = nc_mutex;
        m_main_window_ptr = stdscr;
        m_command_window_ptr = command_window_ptr;
        m_main_window_height = (size_t)(height - 1);
        m_main_window_width = (size_t)width;

        m_model = model;
        m_wrap_lines = true;
        m_offset = 0;
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

    Page<T> current_page() const {
        return Page<T>::get_page_at_byte_offset(
            m_model, m_offset, m_main_window_height, m_main_window_width,
            m_wrap_lines);
    }

    void scroll_up(size_t num_scrolls = 1) {
        Page<T> page = current_page();
        typename Page<T>::LineIt line = page.begin();
        while (num_scrolls-- > 0 && line.has_prev()) {
            --line;
        }
        m_offset = line.m_cursor.get_offset();
    }

    void scroll_down(size_t num_scrolls = 1) {
        Page<T> page = current_page();
        typename Page<T>::LineIt line = page.begin();
        while (num_scrolls-- > 0 && line.has_next()) {
            ++line;
        }
        m_offset = line.m_cursor.get_offset();
    }

    void move_to_top() {
        m_offset = 0;
    }

    void move_to_end() {
        m_offset = m_model->length();
    }

    void move_to_byte_offset(size_t offset) {
        Cursor cursor = Cursor<T>::get_cursor_at_byte_offset(m_model, offset);
        if (m_wrap_lines) {
            cursor = cursor.round_to_wrapped_line(m_main_window_width);
        }
        m_offset = cursor.get_offset();
    }

    size_t get_starting_offset() const {
        Page<T> page = current_page();
        return page.begin().get_begin_offset();
    }

    size_t get_ending_offset() const {
        Page<T> page = current_page();
        return page.end().get_begin_offset();
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

    void display_page_at(const std::vector<Highlight> &highlight_list) {
        auto place_attr_on_line = [](WINDOW *window_ptr, size_t display_row,
                                     size_t offset_of_row,
                                     Highlight highlight) {
            size_t starting_offset = std::max(highlight.offset, offset_of_row);
            size_t starting_col = starting_offset - offset_of_row;
            size_t length =
                highlight.offset + highlight.length - starting_offset;
            mvwchgat(window_ptr, (int)display_row, (int)starting_col,
                     (int)length, WA_STANDOUT, 0, NULL);
        };

        std::scoped_lock lock(*m_nc_mutex);

        werase(m_main_window_ptr);

        Page<T> page = current_page();

        auto page_lines_it = page.begin();
        for (size_t display_row = 0; display_row < m_main_window_height;
             ++display_row) {
            if (page_lines_it != page.end()) {
                std::string display_string(
                    strip_trailing_rn(page_lines_it.get_contents()));
                mvwaddstr(m_main_window_ptr, (int)display_row, 0,
                          display_string.c_str());
                ++page_lines_it;

            } else {
                mvwaddnstr(m_main_window_ptr, (int)display_row, 0, "~", 1);
            }
        }

        wstandend(m_main_window_ptr);

        page_lines_it = page.begin();
        auto highlight_it = highlight_list.begin();
        for (size_t display_row = 0;
             display_row < m_main_window_height && page_lines_it != page.end();
             ++display_row, ++page_lines_it) {
            while (highlight_it != highlight_list.end() &&
                   highlight_it->get_end_offset() <
                       page_lines_it.get_begin_offset()) {
                ++highlight_it;
            }
            for (auto cur_highlight_it = highlight_it;
                 cur_highlight_it != highlight_list.end(); ++cur_highlight_it) {
                if (cur_highlight_it->get_begin_offset() >=
                    page_lines_it.get_end_offset()) {
                    break;
                }
                place_attr_on_line(m_main_window_ptr, display_row,
                                   page_lines_it.get_begin_offset(),
                                   *cur_highlight_it);
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
    }
};
