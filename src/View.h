#pragma once

#include <cassert>
#include <curses.h>
#include <mutex>
#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <utility>

#include "Model.h"

// Serves as the driver for the entire view. For now let's keep it at a simple
//  thing that just holds a text_window, and given the state that needs to be
//  rendered drives the entire rendering logic
class View {
    // we need to learn how to winch
    std::mutex *nc_mutex;
    WINDOW *m_main_window_ptr;
    WINDOW *m_command_window_ptr;

  private:
    View(std::mutex *nc_mutex, WINDOW *main_window_ptr,
         WINDOW *command_window_ptr)
        : nc_mutex(nc_mutex), m_main_window_ptr(main_window_ptr),
          m_command_window_ptr(command_window_ptr) {
    }

    struct DisplayableLineIt {
        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        Model::LineIt m_line_it;
        size_t m_screen_width;
        size_t m_line_offset;   // starting position in m_line_it
        size_t m_global_offset; // byte position of start of line in m_contents
                                // of model
                                // TODO: make it an iterator haha
        size_t m_length;

        DisplayableLineIt(Model::LineIt line_it, size_t screen_width,
                          size_t m_line_offset, size_t global_offset,
                          size_t m_length)
            : m_line_it(std::move(line_it)), m_screen_width(screen_width),
              m_line_offset(m_line_offset), m_global_offset(global_offset),
              m_length(m_length) {
        }

        DisplayableLineIt(Model::LineIt line_it, size_t screen_width)
            : m_line_it(std::move(line_it)), m_screen_width(screen_width),
              m_line_offset(0), m_global_offset(m_line_it.m_offset),
              m_length(std::min(m_screen_width, m_line_it->length())) {
        }

        bool operator==(const DisplayableLineIt &other) const {
            return (m_screen_width == other.m_screen_width) &&
                   (m_line_offset == other.m_line_offset) &&
                   (m_global_offset == other.m_global_offset) &&
                   (m_length == other.m_length);
        }

        std::string_view operator*() const {
            return m_line_it->substr(m_line_offset, m_screen_width);
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
            if (m_line_offset + m_screen_width < m_line_it->length()) {
                m_line_offset += m_screen_width;
                m_global_offset += m_screen_width;
                m_length = m_screen_width;
            } else {
                // I might be off by 1
                m_global_offset += (m_line_it->length() - m_line_offset);
                ++m_line_it;
                m_line_offset = 0;
                m_length = std::min(m_line_it->length(), m_screen_width);
            }

            return *this;
        }

        DisplayableLineIt operator++(int) {
            DisplayableLineIt to_return = *this;
            ++(*this);
            return to_return;
        }

        DisplayableLineIt &operator--() {
            if (m_line_offset >= m_screen_width) {
                m_line_offset -= m_screen_width;
                m_global_offset -= m_screen_width;
                m_length = m_screen_width;
            } else {
                // if not you need to move back by 1 line and start figuring
                // out what the correct truncation is. but how do you do this
                // without repeating chars?
                --m_line_it;
                assert(m_line_it->length() >= 1);
                size_t num_skips = (m_line_it->length() - 1) / m_screen_width;
                m_line_offset = num_skips * m_screen_width;
                // do we actually ever need this? perhaps for something else
                m_global_offset =
                    (m_global_offset - m_line_it->length() + m_line_offset);

                if (m_line_it->length() % m_screen_width == 0) {
                    m_length = m_screen_width;
                } else {
                    m_length = m_line_it->length() % m_screen_width;
                }
            }
            return *this;
        }

        DisplayableLineIt operator--(int) {
            DisplayableLineIt to_return = *this;
            --(*this);
            return to_return;
        }

        DisplayableLineIt end() const {
            return {m_line_it.end(), m_screen_width, 0,
                    m_line_it.end().line_end_offset(), 0};
        }
    };

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

    static View initialize(std::mutex *nc_mutex) {
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
        return View(nc_mutex, stdscr, command_window_ptr);
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

    void display_page_at(Model::LineIt line, const std::vector<Highlights> &) {
        std::scoped_lock lock(*nc_mutex);
        int height, width;
        getmaxyx(m_main_window_ptr, height, width);

        werase(m_main_window_ptr);

        DisplayableLineIt displayable_line_it{line, (size_t)width};
        for (int display_row = 0; display_row < height; display_row++) {
            if (displayable_line_it != displayable_line_it.end()) {
                mvwaddnstr(m_main_window_ptr, display_row, 0,
                           displayable_line_it->data(),
                           displayable_line_it->length());
                ++displayable_line_it;
            } else {
                mvwaddnstr(m_main_window_ptr, display_row, 0, "~", 1);
            }
        }
        wrefresh(m_main_window_ptr);
        wrefresh(m_command_window_ptr);
    }
    void display_command(std::string_view command) {
        std::scoped_lock lock(*nc_mutex);
        werase(m_command_window_ptr);
        mvwaddnstr(m_command_window_ptr, 0, 0, command.data(),
                   command.length());
        wrefresh(m_command_window_ptr);
    }
    void display_status(std::string_view status) {
        std::scoped_lock lock(*nc_mutex);
        werase(m_command_window_ptr);
        mvwaddnstr(m_command_window_ptr, 0, 0, status.data(), status.length());
        wattrset(m_command_window_ptr, WA_STANDOUT);
        wrefresh(m_command_window_ptr);
    }

    // void update_state() {
    //     m_text_widget.update_state();
    // }
};
