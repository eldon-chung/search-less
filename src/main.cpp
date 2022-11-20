#include <filesystem>
#include <mutex>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>

#include "Input.h"
#include "Model.h"
#include "View.h"
#include "search.h"

int main(int argc, char **argv) {
    Channel<Command> chan;
    register_for_sigwinch_channel(&chan);

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

    // View::DisplayableLineIt cursor =
    //     view.get_line_at(model.get_line_at_byte_offset(0));
    view.display_page_at({});
    view.display_status("Hello, this is a status");

    InputThread input(&nc_mutex, &chan);

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode caseless_mode = CaselessSearchMode::SENSITIVE;

    while (true) {
        Command command = chan.pop();
        switch (command.type) {
        case Command::INVALID:
            view.display_status("Invalid key pressed: " + command.payload);
            break;
        case Command::RESIZE:
            view.handle_resize();
            view.display_page_at({});
            view.display_status("handle resize called");
            break;
        case Command::QUIT:
            break;
        case Command::VIEW_DOWN:
            view.scroll_down();
            view.display_page_at({});
            break;
        case Command::VIEW_UP:
            view.scroll_up();
            view.display_page_at({});
            break;
        case Command::VIEW_BOF:
            view.move_to_top();
            view.display_page_at({});
            break;
        case Command::VIEW_EOF:
            model.read_to_eof();
            view.move_to_end();
            view.display_page_at({});
            break;
        case Command::DISPLAY_COMMAND: {
            view.display_command(command.payload);
            break;
        }
        case Command::TOGGLE_CASELESS: {
            if (caseless_mode == CaselessSearchMode::INSENSITIVE) {
                caseless_mode = CaselessSearchMode::SENSITIVE;
                view.display_status(command.payload +
                                    ": Caseless search disabled");
            } else {
                caseless_mode = CaselessSearchMode::INSENSITIVE;
                view.display_status(command.payload +
                                    ": Caseless search enabled");
            }
            break;
        }
        case Command::TOGGLE_CONDITIONALLY_CASELESS: {
            if (caseless_mode == CaselessSearchMode::CONDITIONALLY_SENSITIVE) {
                caseless_mode = CaselessSearchMode::SENSITIVE;
                view.display_status(command.payload +
                                    ": Caseless search disabled");
            } else {
                caseless_mode = CaselessSearchMode::CONDITIONALLY_SENSITIVE;
                view.display_status(
                    command.payload +
                    ": Conditionally caseless search enabled (case is "
                    "ignored if pattern only contains lowercase)");
            }
            break;
        }
        case Command::SEARCH_NEXT: {
            if (view.begin() == view.end()) {
                break;
            }
            // ++cursor;
            view.scroll_down();
            [[fallthrough]];
        }
        case Command::SEARCH: {
            if (view.begin() == view.end()) {
                break;
            }
            size_t first_match = basic_search_first(
                model.get_contents(), command.payload,
                view.get_starting_offset(), model.length(),
                caseless_mode != CaselessSearchMode::SENSITIVE);
            if (first_match == model.length() ||
                first_match == std::string::npos) {
                view.move_to_end();
            } else {
                view.move_to_byte_offset(first_match);
            }
            view.display_page_at({});
            break;
        }
        }
        if (command.type == Command::QUIT) {
            break;
        }
    }

    return 0;
}
