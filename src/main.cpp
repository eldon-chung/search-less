#include <filesystem>
#include <mutex>
#include <ncurses.h>
#include <stdio.h>

#include "Input.h"
#include "Model.h"
#include "View.h"
#include "search.h"

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

    std::mutex nc_mutex;

    Model model = Model::initialize(std::move(read_file));
    View view = View::initialize(&nc_mutex, &model);

    View::DisplayableLineIt cursor =
        view.get_line_at(model.get_line_at_byte_offset(0));
    view.display_page_at(cursor, {});
    view.display_status("Hello, this is a status");

    Channel<Command> chan;
    InputThread input(&nc_mutex, &chan);

    while (true) {
        Command command = chan.pop();
        switch (command.type) {
        case Command::INVALID:
            view.display_status("Invalid key pressed: " + command.payload);
            break;
        case Command::QUIT:
            break;
        case Command::VIEW_DOWN:
            ++cursor;
            if (cursor == view.end()) {
                --cursor;
            } else {
                view.display_page_at(cursor, {});
            }
            break;
        case Command::VIEW_UP:
            if (cursor != view.begin()) {
                --cursor;
            }
            view.display_page_at(cursor, {});
            break;
        case Command::VIEW_BOF:
            cursor = view.begin();
            view.display_page_at(cursor, {});
            break;
        case Command::VIEW_EOF:
            model.read_to_eof();
            if (view.end() != view.begin()) {
                cursor = view.end();
                --cursor;
            } else {
                cursor = view.begin();
            }
            view.display_page_at(cursor, {});
            break;
        case Command::SEARCH_NEXT: {
            if (view.begin() == view.end()) {
                break;
            }
            ++cursor;
            [[fallthrough]];
        }
        case Command::SEARCH: {
            if (view.begin() == view.end()) {
                break;
            }
            size_t first_match =
                basic_search_first(model.get_contents(), command.payload,
                                   cursor.m_global_offset, model.length());
            if (first_match == model.length()) {
                cursor = view.end();
                --cursor;
            } else {
                cursor = view.get_line_at_byte_offset(first_match);
            }
            view.display_page_at(cursor, {});
            break;
        }
        }
        if (command.type == Command::QUIT) {
            break;
        }
    }

    return 0;
}
