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
    View view = View::initialize();

    Model::LineIt cursor = model.get_line_at_byte_offset(0);
    view.display_page_at(cursor, {});
    view.display_status("Hello, this is a status");

    // wait for some input
    while (true) {
        int ch = getch();
        // the quit key
        switch (ch) {
        case 'q':
            break;
        case 'j':
        case KEY_DOWN:
            if (cursor != model.get_last_line()) {
                ++cursor;
            }
            view.display_page_at(cursor, {});
            break;
        case 'k':
        case KEY_UP:
            if (cursor != model.get_nth_line(0)) {
                --cursor;
            }
            view.display_page_at(cursor, {});
            break;
        case 'g':
            cursor = model.get_line_at_byte_offset(0);
            view.display_page_at(cursor, {});
            break;
        case 'G':
            model.read_to_eof();
            cursor = model.get_line_at_byte_offset(model.length());
            view.display_page_at(cursor, {});
            break;
        }
        if (ch == 'q') {
            break;
        }
    }

    return 0;
}
