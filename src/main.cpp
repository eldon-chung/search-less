#include <filesystem>
#include <ncurses.h>
#include <stdio.h>

#include "Model.h"
#include "View.h"

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "missing filename.\n");
        exit(1);
    }

    // try to open the file
    std::filesystem::directory_entry read_file(argv[1]);

    if (read_file.is_directory()) {
        fprintf(stderr, "%s is a directory.\n", argv[1]);
        exit(1);
    } else if (!read_file.is_regular_file()) {
        fprintf(stderr,
                "%s is not a regular file. We don't support opening "
                "non-regular files.\n",
                argv[1]);
        exit(1);
    }

    Model model = Model::initialize(std::move(read_file));
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

    return 0;
}
