#pragma once

#include <cassert>
#include <curses.h>
#include <mutex>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

#include <sys/ioctl.h>
#include <unistd.h>

#include "Model.h"

// Serves as the driver for the entire view. For now let's keep it at a simple
//  thing that just holds a text_window, and given the state that needs to be
//  rendered drives the entire rendering logic
class View {
  public:
    struct DisplayableLineIt {
        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        Model::LineIt m_line_it;
        size_t m_screen_width;
        size_t m_global_offset; // byte position of start of line in m_contents
                                // of model
                                // TODO: make it an iterator haha

        DisplayableLineIt(Model::LineIt line_it, size_t screen_width,
                          size_t global_offset)
            : m_line_it(std::move(line_it)), m_screen_width(screen_width),
              m_global_offset(global_offset) {
        }

        DisplayableLineIt(Model::LineIt line_it, size_t screen_width)
            : m_line_it(std::move(line_it)), m_screen_width(screen_width),
              m_global_offset(m_line_it.m_offset) {
        }

        bool operator==(const DisplayableLineIt &other) const {
            return (m_global_offset == other.m_global_offset);
        }

        size_t relative_line_offset() const {
            assert(m_global_offset >= m_line_it.m_offset);
            return m_global_offset - m_line_it.m_offset;
        }

        std::string_view operator*() const {
            return m_line_it->substr(relative_line_offset());
        }

        struct Cursed {
            std::string_view tmp;
            std::string_view *operator->() {
                return &tmp;
            }
        };
        Cursed operator->() const {
            return {**this};
        }

        DisplayableLineIt &operator++() {
            if (relative_line_offset() + m_screen_width < m_line_it->length()) {
                m_global_offset += m_screen_width;
            } else {
                // I might be off by 1
                m_global_offset +=
                    (m_line_it->length() - relative_line_offset());
                ++m_line_it;
            }

            return *this;
        }

        DisplayableLineIt operator++(int) {
            DisplayableLineIt to_return = *this;
            ++(*this);
            return to_return;
        }

        DisplayableLineIt &operator--() {
            // to tell if you've rewinched we should also check if
            if (relative_line_offset() >= m_screen_width) {
                fprintf(stderr, "staying on same model line; relative %zu\n",
                        relative_line_offset());
                m_global_offset -= m_screen_width;
            } else if (relative_line_offset() == 0) {
                // if not you need to move back by 1 line and start figuring
                // out what the correct truncation is. but how do you do this
                // without repeating chars?
                --m_line_it;
                assert(m_line_it->length() >= 1);
                size_t num_skips = (m_line_it->length() - 1) / m_screen_width;
                size_t new_offset = num_skips * m_screen_width;

                m_global_offset =
                    (m_global_offset - m_line_it->length() + new_offset);
            } else {
                m_global_offset = m_line_it.m_offset;
            }
            return *this;
        }

        DisplayableLineIt operator--(int) {
            DisplayableLineIt to_return = *this;
            --(*this);
            return to_return;
        }
    };

    // we need to learn how to winch
    std::mutex *nc_mutex;
    WINDOW *m_main_window_ptr;
    WINDOW *m_command_window_ptr;
    DisplayableLineIt m_cursor;

    const Model *m_model;

  private:
    View(std::mutex *nc_mutex, WINDOW *main_window_ptr,
         WINDOW *command_window_ptr, const Model *model,
         DisplayableLineIt cursor)
        : nc_mutex(nc_mutex), m_main_window_ptr(main_window_ptr),
          m_command_window_ptr(command_window_ptr), m_cursor(std::move(cursor)),
          m_model(model) {
    }

  public:
    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&) = delete;
    View &operator=(View &&) = delete;
    ~View() {
        std::scoped_lock lock(*nc_mutex);
        delwin(m_main_window_ptr);
        delwin(m_command_window_ptr);
        endwin(); // here's how you finish up ncurses mode
    }

    DisplayableLineIt begin() const {
        int height, width;
        getmaxyx(stdscr, height, width);
        return {m_model->begin(), (size_t)width};
    }
    DisplayableLineIt end() const {
        int height, width;
        getmaxyx(stdscr, height, width);
        return {m_model->end(), (size_t)width};
    }

    static View initialize(std::mutex *nc_mutex, const Model *model) {
        std::scoped_lock lock(*nc_mutex);
        initscr();
        start_color();
        use_default_colors();
        noecho();
        raw();
        curs_set(0);
        keypad(stdscr, TRUE);

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

        auto model_line_it = model->get_line_at_byte_offset(0);
        auto cursor =
            DisplayableLineIt(std::move(model_line_it), (size_t)width);

        return View(nc_mutex, stdscr, command_window_ptr, model,
                    std::move(cursor));
    }

    DisplayableLineIt get_line_at(Model::LineIt line) {
        int height, width;
        getmaxyx(stdscr, height, width);
        return DisplayableLineIt(line, (size_t)width);
    }

    DisplayableLineIt get_line_at_byte_offset(size_t offset) {
        DisplayableLineIt it =
            get_line_at(m_model->get_line_at_byte_offset(offset));

        int height, width;
        getmaxyx(stdscr, height, width);

        while (it.m_global_offset + (size_t)width < offset) {
            ++it;
        }
        return it;
    }

    // just some toy thing
    void print_to_main(int ch) {
        std::scoped_lock lock(*nc_mutex);
        waddch(m_main_window_ptr, (unsigned int)ch);
    }

    void print_to_command_window(int ch) {
        std::scoped_lock lock(*nc_mutex);
        waddch(m_command_window_ptr, (unsigned int)ch);
    }

    // Calls render on the relevant view elements
    void render_main() {
        std::scoped_lock lock(*nc_mutex);
        wrefresh(m_main_window_ptr);
    }

    void render_sub() {
        std::scoped_lock lock(*nc_mutex);
        wrefresh(m_command_window_ptr);
    }

    struct Highlights {
        Model::LineIt line;
        size_t start_col;
        size_t len;
    };

    void scroll_up() {
        if (m_cursor != begin()) {
            --m_cursor;
        }
        // --m_cursor;
    }

    void scroll_down() {
        ++m_cursor;
        if (m_cursor == end()) {
            --m_cursor;
        }
    }

    void move_to_top() {
        m_cursor = begin();
    }

    void move_to_end() {
        if (end() != begin()) {
            m_cursor = end();
            --m_cursor;
        } else {
            m_cursor = begin();
        }
    }

    void move_to_byte_offset(size_t offset) {
        m_cursor = get_line_at_byte_offset(offset);
    }

    size_t get_starting_offset() {
        return m_cursor.m_global_offset;
    }

    void display_page_at(const std::vector<Highlights> &) {

        auto strip_r = [](std::string_view str) -> std::string {
            std::string display_string(str);
            if (display_string.back() == '\n') {
                display_string.pop_back();
            }

            if (display_string.back() == '\r') {
                display_string.pop_back();
            }

            return display_string;
        };

        std::scoped_lock lock(*nc_mutex);
        int height, width;
        getmaxyx(m_main_window_ptr, height, width);

        werase(m_main_window_ptr);

        auto page_lines_it = m_cursor;
        for (int display_row = 0; display_row < height;) {
            if (page_lines_it != end()) {
                std::string display_string = strip_r(*page_lines_it);
                if (!display_string.empty() ||
                    page_lines_it.relative_line_offset() == 0) {
                    mvwaddnstr(m_main_window_ptr, display_row, 0,
                               display_string.c_str(), display_string.length());
                    display_row++;
                }
                ++page_lines_it;
            } else {
                mvwaddnstr(m_main_window_ptr, display_row, 0, "~", 1);
                display_row++;
            }
        }
        wrefresh(m_main_window_ptr);
        wrefresh(m_command_window_ptr);
    }

    void display_command(std::string_view command) {
        std::scoped_lock lock(*nc_mutex);
        werase(m_command_window_ptr);
        wattrset(m_command_window_ptr, WA_NORMAL);
        mvwaddnstr(m_command_window_ptr, 0, 0, command.data(),
                   command.length());
        wrefresh(m_command_window_ptr);
    }
    void display_status(std::string_view status) {
        std::scoped_lock lock(*nc_mutex);
        werase(m_command_window_ptr);
        wattrset(m_command_window_ptr, WA_STANDOUT);
        mvwaddnstr(m_command_window_ptr, 0, 0, status.data(), status.length());
        wrefresh(m_command_window_ptr);
    }

    void handle_resize() {

        endwin();
        refresh();

        wresize(m_main_window_ptr, LINES - 1, COLS);

        delwin(m_command_window_ptr);

        m_command_window_ptr = newwin(1, COLS, LINES - 1, 0);
        if (m_command_window_ptr == NULL) {
            fprintf(stderr, "failed to make new command window\n");
            exit(1);
        }

        wclear(m_main_window_ptr);
        wclear(m_command_window_ptr);

        // update cursor
        m_cursor.m_screen_width = (size_t)COLS;
    }

    // void update_state() {
    //     m_text_widget.update_state();
    // }
};
