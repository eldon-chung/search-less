#include <filesystem>
#include <future>
#include <mutex>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>

#include "Input.h"
#include "Model.h"
#include "View.h"
#include "Worker.h"
#include "search.h"

// static uint16_t from_payload(const std::string &payload) {
//     assert(payload.size() == 2);
//     uint16_t first_half = (uint16_t)payload[0];
//     first_half |= (uint16_t)payload[1] << 8;

//     return first_half;
// }

int main(int argc, char **argv) {
    Channel<Command> chan;
    Channel<std::function<void(void)>> task_chan;

    std::stop_source file_task_stop_source;
    std::promise<void> file_task_promise;

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

    auto read_line_offsets_tasks = [&]() -> void {
        compute_line_offsets(file_task_stop_source.get_token(), &chan,
                             file_task_promise, model.get_contents(), 0);
    };

    // schedule a line offset computation
    task_chan.push(std::move(read_line_offsets_tasks));

    // View::DisplayableLineIt cursor =
    //     view.get_line_at(model.get_line_at_byte_offset(0));
    view.display_page_at({});
    view.display_status();

    InputThread input(&nc_mutex, &chan);
    WorkerThread taskmaster(&task_chan);

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode caseless_mode = CaselessSearchMode::SENSITIVE;

    std::string command_str_buffer;
    uint16_t command_cursor_pos;

    while (true) {
        Command command = chan.pop().value();

        fprintf(stderr, "command str %s, type %d\n",
                command.payload_str.c_str(), command.type);
        switch (command.type) {
        case Command::INVALID:
            view.display_status("Invalid key pressed: " + command.payload_str);
            break;
        case Command::RESIZE:
            view.handle_resize();
            view.display_page_at({});
            view.display_status("handle resize called");
            break;
        case Command::QUIT:
            chan.close();
            task_chan.close();
            file_task_stop_source.request_stop();
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
            if (model.has_changed()) {
                // need to kill task if running
                file_task_stop_source.request_stop();
                file_task_promise.get_future().wait();
                model.read_to_eof();

                // reset the stop source; schedule new task
                file_task_stop_source = std::stop_source();
                auto read_line_offsets_tasks = [&]() -> void {
                    compute_line_offsets(file_task_stop_source.get_token(),
                                         &chan, file_task_promise,
                                         model.get_contents(),
                                         model.get_num_processed_bytes());
                };
                task_chan.push(read_line_offsets_tasks);
            }
            view.move_to_end();
            view.display_page_at({});
            break;
        case Command::DISPLAY_COMMAND: {
            view.display_command(command.payload_str);
            break;
        }
        case Command::TOGGLE_CASELESS: {
            if (caseless_mode == CaselessSearchMode::INSENSITIVE) {
                caseless_mode = CaselessSearchMode::SENSITIVE;
                view.display_status(command.payload_str +
                                    ": Caseless search disabled");
            } else {
                caseless_mode = CaselessSearchMode::INSENSITIVE;
                view.display_status(command.payload_str +
                                    ": Caseless search enabled");
            }
            break;
        }
        case Command::TOGGLE_CONDITIONALLY_CASELESS: {
            if (caseless_mode == CaselessSearchMode::CONDITIONALLY_SENSITIVE) {
                caseless_mode = CaselessSearchMode::SENSITIVE;
                view.display_status(command.payload_str +
                                    ": Caseless search disabled");
            } else {
                caseless_mode = CaselessSearchMode::CONDITIONALLY_SENSITIVE;
                view.display_status(
                    command.payload_str +
                    ": Conditionally caseless search enabled (case is "
                    "ignored if pattern only contains lowercase)");
            }
            break;
        }
        case Command::SEARCH_START: {
            command_str_buffer = command.payload_str;
            // we expect the next incoming command to trigger the redraw
            break;
        }
        case Command::SEARCH_QUIT: {
            command_str_buffer = ":";
            view.display_command(command_str_buffer);
            break;
        }
        case Command::SEARCH_PREV: { // assume for now that search_exec was
                                     // definitely called

            command_str_buffer = command.payload_str;

            if (view.begin() == view.end()) {
                break;
            }

            std::string_view contents = model.get_contents();
            std::string search_pattern{command_str_buffer.begin() + 1,
                                       command_str_buffer.end()};
            size_t left_bound = 0;
            size_t right_bound = view.get_starting_offset();
            size_t curr_line_end = view.cursor().get_ending_offset();

            // if it doesnt exist on the current line, don't
            // it means it doesnt exist.
            size_t first_match = basic_search_first(
                contents, search_pattern, right_bound, curr_line_end,
                caseless_mode != CaselessSearchMode::SENSITIVE);

            // fprintf(stderr, "first_match %zu\n", first_match);
            // if (first_match == curr_line_end ||
            //     first_match == std::string::npos) {
            //     // this needs to change depending on whether there was
            //     // already a search being done
            //     view.display_status("Pattern not found");
            //     break;
            // }

            // dont bother scrolling up if we know what offset to start
            // from view.scroll_down();
            size_t last_match = basic_search_last(
                contents, search_pattern, left_bound, right_bound,
                caseless_mode != CaselessSearchMode::SENSITIVE);
            // fprintf(stderr, "last_match %zu\n", last_match);

            if (last_match == right_bound && first_match != curr_line_end) {
                // if there isnt another instance behind us, but there is one
                // instance on our line
                view.display_status("(TOP)");
            } else if (last_match == right_bound) {
                // there just isnt another instance
                view.display_status("Pattern not found");
            } else {
                view.move_to_byte_offset(last_match);
                auto result_offsets = basic_search_all(
                    model.get_contents(), search_pattern,
                    view.get_starting_offset(), view.get_ending_offset());
                std::vector<View::Highlights> highlight_list;
                highlight_list.reserve(result_offsets.size());
                for (size_t offset : result_offsets) {
                    highlight_list.push_back({offset, search_pattern.length()});
                    // fprintf(stderr, "global offset %zu\n", offset);
                }
                view.display_page_at(highlight_list);
                command_str_buffer = ":";
                view.display_command(":");
            }
            break;
        }
        case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                     // definitely called

            command_str_buffer = command.payload_str;
            // fprintf(stderr, "search-next executing on [%s]\n",
            // command_str_buffer.c_str());

            if (view.begin() == view.end()) {
                break;
            }

            std::string_view contents = model.get_contents();
            std::string search_pattern{command_str_buffer.begin() + 1,
                                       command_str_buffer.end()};
            size_t left_bound = view.get_starting_offset();
            size_t curr_line_end = view.cursor().get_ending_offset();
            size_t right_bound = contents.size();

            // if it doesnt exist on the current line, don't
            // it means it doesnt exist.
            size_t first_match = basic_search_first(
                contents, search_pattern, left_bound, curr_line_end,
                caseless_mode != CaselessSearchMode::SENSITIVE);
            if (first_match == curr_line_end ||
                first_match == std::string::npos) {
                // this needs to change depending on whether there was already a
                // search being done
                view.display_status("Pattern not found");
                break;
            }

            // dont bother scrolling down if we know what offset to start from
            // view.scroll_down();
            first_match = basic_search_first(
                contents, search_pattern, curr_line_end, right_bound,
                caseless_mode != CaselessSearchMode::SENSITIVE);

            if (first_match == model.length() ||
                first_match == std::string::npos) {
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
                    // fprintf(stderr, "global offset %zu\n", offset);
                }
                view.display_page_at(highlight_list);
                command_str_buffer = ":";
                view.display_command(":");
            }
            break;
        }
        case Command::SEARCH_EXEC: {
            // fprintf(stderr, "search executing on [%s]\n",
            // command_str_buffer.c_str());

            if (view.begin() == view.end()) {
                break;
            }

            std::string_view contents = model.get_contents();
            std::string search_pattern{command_str_buffer.begin() + 1,
                                       command_str_buffer.end()};
            size_t left_bound = view.get_starting_offset();
            size_t right_bound = contents.size();

            size_t first_match = basic_search_first(
                contents, search_pattern, left_bound, right_bound,
                caseless_mode != CaselessSearchMode::SENSITIVE);

            if (first_match == model.length() ||
                first_match == std::string::npos) {
                // this needs to change depending on whether there was
                // already a search being done
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
                command_str_buffer = ":";
                view.display_command(":");
            }
            break;
        }
        case Command::BUFFER_CURS_POS: {
            // command_cursor_pos = from_payload(command.payload_str);
            command_cursor_pos = (uint16_t)command.payload_nums.front();
            // fprintf(stderr, "printing command buffer: %s\n",
            //         command_str_buffer.c_str());
            view.display_command(command_str_buffer, command_cursor_pos);
            break;
        }
        case Command::UPDATE_LINE_IDXS: {
            model.update_line_idxs(command.payload_nums);
            view.display_status("computed line idxs");
            break;
        }
        }
        if (command.type == Command::QUIT) {
            break;
        }
    }

    return 0;
}
