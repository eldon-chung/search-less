#include <filesystem>
#include <mutex>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>

#include "Input.h"
#include "Model.h"
#include "View.h"
#include "search.h"

static uint16_t from_payload(const std::string &payload) {
    assert(payload.size() == 2);
    uint16_t first_half = (uint16_t)payload[0];
    first_half |= (uint16_t)payload[1] << 8;

    return first_half;
}

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
    view.display_status();

    InputThread input(&nc_mutex, &chan);

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode caseless_mode = CaselessSearchMode::SENSITIVE;

    std::string command_str_buffer;
    uint16_t command_cursor_pos;

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
        case Command::SEARCH_START: {
            command_str_buffer = command.payload;
            // we expect the next incoming command to trigger the redraw
            break;
        }
        case Command::SEARCH_QUIT: {
            command_str_buffer = ":";
            view.display_command(command_str_buffer);
            break;
        }
        case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                     // definitely called
            fprintf(stderr, "search-next executing on [%s]\n",
                    command_str_buffer.c_str());
            std::string search_pattern{command_str_buffer.begin() + 1,
                                       command_str_buffer.end()};

            if (view.begin() == view.end()) {
                break;
            }

            view.scroll_down();

            size_t first_match = basic_search_first(
                model.get_contents(), search_pattern,
                view.get_starting_offset(), model.length(),
                caseless_mode != CaselessSearchMode::SENSITIVE);

            if (first_match == model.length() ||
                first_match == std::string::npos) {
                // this needs to change depending on whether there was already a
                // search being done
                view.scroll_up();
                view.display_status("(END)");
            } else {
                view.move_to_byte_offset(first_match);
                auto result_offsets = basic_search_all(
                    model.get_contents(), search_pattern,
                    view.get_starting_offset(), view.get_ending_offset());
                std::vector<View::Highlights> highlight_list;
                highlight_list.reserve(result_offsets.size());
                for (size_t offset : result_offsets) {
                    highlight_list.push_back({offset, search_pattern.length()});
                    fprintf(stderr, "global offset %zu\n", offset);
                }
                view.display_page_at(highlight_list);
                view.display_command(":");
            }
            break;
        }
        case Command::SEARCH_EXEC: {
            fprintf(stderr, "search executing on [%s]\n",
                    command_str_buffer.c_str());
            std::string search_pattern{command_str_buffer.begin() + 1,
                                       command_str_buffer.end()};

            if (view.begin() == view.end()) {
                break;
            }

            size_t first_match = basic_search_first(
                model.get_contents(), search_pattern,
                view.get_starting_offset(), model.length(),
                caseless_mode != CaselessSearchMode::SENSITIVE);

            if (first_match == model.length() ||
                first_match == std::string::npos) {
                // this needs to change depending on whether there was already a
                // search being done
                view.display_status("Pattern not found");
                view.display_page_at({});
                break;
            } else {
                view.move_to_byte_offset(first_match);
                auto result_offsets = basic_search_all(
                    model.get_contents(), search_pattern,
                    view.get_starting_offset(), view.get_ending_offset());
                std::vector<View::Highlights> highlight_list;
                highlight_list.reserve(result_offsets.size());
                for (size_t offset : result_offsets) {
                    highlight_list.push_back({offset, search_pattern.length()});
                    fprintf(stderr, "global offset %zu\n", offset);
                }
                view.display_page_at(highlight_list);
                view.display_command(":");
            }
            break;
        }
        case Command::BUFFER_CURS_POS: {
            command_cursor_pos = from_payload(command.payload);
            fprintf(stderr, "printing command buffer: %s\n",
                    command_str_buffer.c_str());
            view.display_command(command_str_buffer, command_cursor_pos);
            break;
        }
            // case Command::SEARCH: {
            // if (view.begin() == view.end()) {
            //     break;
            // }
            // size_t first_match = basic_search_first(
            //     model.get_contents(), command.payload,
            //     view.get_starting_offset(), model.length(),
            //     caseless_mode != CaselessSearchMode::SENSITIVE);
            // if (first_match == model.length() ||
            //     first_match == std::string::npos) {
            //     // view.move_to_end();
            //     view.display_status("Pattern not found");
            //     break;
            // } else {
            //     view.move_to_byte_offset(first_match);
            //     auto result_offsets = basic_search_all(
            //         model.get_contents(), command.payload,
            //         view.get_starting_offset(),
            //         view.get_ending_offset());
            //     // std::vector<size_t> result_offsets = {first_match};
            //     std::vector<View::Highlights> highlight_list;
            //     highlight_list.reserve(result_offsets.size());
            //     for (size_t offset : result_offsets) {
            //         highlight_list.push_back(
            //             {offset, command.payload.length()});
            //         fprintf(stderr, "global offset %zu\n", offset);
            //     }
            //     view.display_page_at(highlight_list);
            // }
            // break;
            // }
        }
        if (command.type == Command::QUIT) {
            break;
        }
    }

    return 0;
}
