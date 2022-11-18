#include <ncurses.h>
#include <stdio.h>

#include "Model.h"
#include "View.h"

int main(int argc, char **argv) {

    Model model;
    View view = View::initialize(&model);
    int num_pressed = 0;

    wprintw(stdscr, "hello world! press any key to quit.");

    // wait for some input
    while (true) {
        int ch = getch();
        // the quit key
        if (ch == 'q') {
            break;
        }
        if (num_pressed % 2) {
            view.print_to_main((char)ch);
            view.render_main();
        } else {
            view.print_to_command_window((char)ch);
            view.render_sub();
        }
        num_pressed += 1;
    }

    endwin(); // here's how you finish up ncurses mode
    return 0;
}