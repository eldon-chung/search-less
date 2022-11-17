#pragma once

#include <cassert>
#include <curses.h>
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
    Model const *m_model;
    WINDOW *m_main_window_ptr;
    WINDOW *m_command_window_ptr;

  private:
    View(Model const *model, WINDOW *main_window_ptr,
         WINDOW *command_window_ptr)
        : m_model(model), m_main_window_ptr(main_window_ptr),
          m_command_window_ptr(command_window_ptr) {
    }

  public:
    View(View const &) = delete;
    View &operator=(View const &) = delete;
    View(View &&) = delete;
    View &operator=(View &&) = delete;
    ~View() {
    }

    static View initialize(Model *model) {
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
        return View(model, stdscr, command_window_ptr);
    }

    // just some toy thing
    void print_to_main(int ch) {
        waddch(m_main_window_ptr, (unsigned int)ch);
    }

    void print_to_command_window(int ch) {
        waddch(m_command_window_ptr, (unsigned int)ch);
    }

    // Calls render on the relevant view elements
    void render_main() {
        wrefresh(m_main_window_ptr);
    }

    void render_sub() {
        wrefresh(m_command_window_ptr);
    }

    // void update_state() {
    //     m_text_widget.update_state();
    // }
};