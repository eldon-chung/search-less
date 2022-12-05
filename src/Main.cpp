#include "Main.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <charconv>
#include <optional>
#include <string_view>

#include "search.h"

// is this good? the member fields now sort of behave like
// "scoped globals" in this way. perhaps we should make
// this a static method that takes in params
template <typename T> void Main<T>::update_screen_highlight_offsets() {
    // search all of the occurences of the pattern
    // that are visible on the screen right now.
    auto result_offsets = basic_search_all(
        m_model.get_contents(), m_last_search_pattern,
        m_view.get_starting_offset(), m_view.get_ending_offset(),
        m_caseless_mode != CaselessSearchMode::SENSITIVE);

    // clear our highlight offsets
    m_highlight_offsets.clear();
    m_highlight_offsets.reserve(result_offsets.size());

    for (size_t offset : result_offsets) {
        m_highlight_offsets.push_back({offset, m_last_search_pattern.length()});
    }
}

template <typename T> void Main<T>::display_page() {
    if (m_highlight_active) {
        update_screen_highlight_offsets();
        m_view.display_page_at(m_highlight_offsets);
    } else {
        m_view.display_page_at({});
    }
}

template <typename T> void Main<T>::display_command_or_status() {
    if (!m_command_str_buffer.empty()) {
        m_view.display_command(m_command_str_buffer, m_command_cursor_pos);
    } else if (!m_status_str_buffer.empty()) {
        m_view.display_status(m_status_str_buffer);
    } else if (!m_last_search_pattern.empty()) {
        m_view.display_command(":", 1);
    } else {
        m_view.display_status(m_model.relative_path());
    }
}

template <typename T> void Main<T>::run() {

    while (true) {
        Command command = m_chan.pop().value();
        switch (command.type) {
        case Command::INVALID:
            m_view.display_status("Invalid key pressed: " +
                                  command.payload_str);
            break;
        case Command::RESIZE:
            m_view.handle_resize();
            m_half_page_size =
                std::max((size_t)1, m_view.m_main_window_height / 2);
            m_page_size = std::max((size_t)1, m_view.m_main_window_height);
            display_page();
            break;
        case Command::QUIT:
            m_chan.close();
            m_task_chan.close();
            m_file_task_stop_source.request_stop();
            break;
        case Command::VIEW_DOWN:
            m_view.scroll_down(std::max(command.payload_num, (size_t)1));
            display_page();
            break;
        case Command::VIEW_UP:
            m_view.scroll_up(std::max(command.payload_num, (size_t)1));
            display_page();
            break;
        case Command::VIEW_DOWN_HALF_PAGE:
            m_view.scroll_down(std::max((size_t)1, command.payload_num) *
                               m_half_page_size);
            display_page();
            break;
        case Command::VIEW_UP_HALF_PAGE:
            m_view.scroll_up(std::max((size_t)1, command.payload_num) *
                             m_half_page_size);
            display_page();
            break;
        case Command::VIEW_DOWN_PAGE:
            m_view.scroll_down(std::max((size_t)1, command.payload_num) *
                               m_page_size);
            display_page();
            break;
        case Command::VIEW_UP_PAGE:
            m_view.scroll_up(std::max((size_t)1, command.payload_num) *
                             m_page_size);
            display_page();
            break;
        case Command::SET_HALF_PAGE_SIZE:
            m_half_page_size = command.payload_num;
            break;
        case Command::SET_PAGE_SIZE:
            m_page_size = command.payload_num;
            break;
        case Command::VIEW_BOF:
            m_view.move_to_top();
            display_page();
            if (!m_status_str_buffer.empty()) {
                set_status("");
            }
            break;
        case Command::VIEW_EOF:
            if (m_model.has_changed()) {
                // need to kill task if running
                m_file_task_stop_source.request_stop();
                auto file_task_fut = m_file_task_promise.get_future();
                if (file_task_fut.valid()) {
                    file_task_fut.get();
                }
                m_model.read_to_eof();

                // reset the stop source and promise; schedule new task
                m_file_task_stop_source = std::stop_source();
                m_file_task_promise = std::promise<void>();
                auto read_line_offsets_tasks = [&]() -> void {
                    compute_line_offsets(m_file_task_stop_source.get_token(),
                                         &m_chan, m_file_task_promise,
                                         m_model.get_contents(),
                                         m_model.get_num_processed_bytes());
                };

                m_task_chan.push(read_line_offsets_tasks);
            }

            m_view.move_to_end();
            display_page();
            if (!m_status_str_buffer.empty()) {
                set_status("");
            }
            break;
        case Command::DISPLAY_COMMAND: {
            set_command(command.payload_str, command.payload_num);
            break;
        }
        case Command::DISPLAY_STATUS: {
            set_status(command.payload_str);
            break;
        }
        case Command::TOGGLE_CASELESS: {
            set_command("", 0);
            if (m_caseless_mode == CaselessSearchMode::INSENSITIVE) {
                m_caseless_mode = CaselessSearchMode::SENSITIVE;
                m_view.display_status(command.payload_str +
                                      ": Caseless search disabled");
            } else {
                m_caseless_mode = CaselessSearchMode::INSENSITIVE;
                m_view.display_status(command.payload_str +
                                      ": Caseless search enabled");
            }
            break;
        }
        case Command::TOGGLE_CONDITIONALLY_CASELESS: {
            if (m_caseless_mode ==
                CaselessSearchMode::CONDITIONALLY_SENSITIVE) {
                m_caseless_mode = CaselessSearchMode::SENSITIVE;
                m_view.display_status(command.payload_str +
                                      ": Caseless search disabled");
            } else {
                m_caseless_mode = CaselessSearchMode::CONDITIONALLY_SENSITIVE;
                m_view.display_status(
                    command.payload_str +
                    ": Conditionally caseless search enabled (case is "
                    "ignored if pattern only contains lowercase)");
            }
            break;
        }
        case Command::SEARCH_START: {
            // The loop is weird because command.payload_str is being mutated
            // in the loop body

            for (size_t i = 0; i < command.payload_str.size(); ++i) {
                std::string replacement;
                if (command.payload_str[i] < 32) {
                    replacement = "^";
                    replacement.push_back(command.payload_str[i] + 0x40);
                } else if (command.payload_str[i] == '\x7f') {
                    replacement = "^?";
                }
                if (!replacement.empty()) {
                    command.payload_str = command.payload_str.substr(0, i) +
                                          replacement +
                                          command.payload_str.substr(i + 1);
                    if (command.payload_num > i) {
                        command.payload_num++;
                    }
                }
            }

            set_command(command.payload_str, command.payload_num);
            set_status("");
            break;
        }
        case Command::SEARCH_QUIT: {
            set_command("", 0);
            set_status("");
            break;
        }

        case Command::SEARCH_PREV: { // assume for now that search_exec was
                                     // definitely called
            set_command("", 0);
            set_status("");
            m_highlight_active = true;

            // for now if search pattern is empty, we just break
            if (m_last_search_pattern.empty()) {
                break;
            }

            std::string_view contents = m_model.get_contents();
            // TODO: optimize?
            for (size_t i = 0; i < std::max((size_t)1, command.payload_num);
                 ++i) {
                size_t right_bound = m_view.get_starting_offset();
                size_t curr_line_end =
                    m_view.current_page().begin().get_end_offset();

                // figure out if there is one on our current line;
                size_t first_match = basic_search_first(
                    contents, m_last_search_pattern, right_bound, curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                // look for instances before us
                size_t last_match = basic_search_last(
                    contents, m_last_search_pattern, 0, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (last_match == right_bound && first_match != curr_line_end) {
                    // if there isnt another instance behind us, but there
                    // is one instance on our line
                    set_status("(TOP)");
                    break;
                } else if (last_match == right_bound) {
                    set_status("Pattern not found");
                    break;
                } else {
                    m_view.move_to_byte_offset(last_match);
                }
            }
            display_page();
            break;
        }

        case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                     // definitely called
            set_command("", 0);
            set_status("");
            m_highlight_active = true;

            // for now if search pattern is empty, we just break
            if (m_last_search_pattern.empty()) {
                break;
            }

            // use the most recent search pattern stored in
            // m_last_search_pattern
            std::string_view contents = m_model.get_contents();
            // TODO: optimize?
            // TODO: Should get nth match, not nth line containing matches
            // TODO: should display "search cursor" so that multiple matches
            // on the same line can be `n`d properly
            for (size_t i = 0; i < std::max((size_t)1, command.payload_num);
                 ++i) {
                size_t left_bound = m_view.get_starting_offset();
                size_t curr_line_end =
                    m_view.current_page().begin().get_end_offset();
                size_t right_bound = contents.size();

                size_t curr_line_match = basic_search_first(
                    contents, m_last_search_pattern, left_bound, curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                size_t next_match = basic_search_first(
                    contents, m_last_search_pattern, curr_line_end, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (next_match == right_bound &&
                    curr_line_match != curr_line_end) {
                    set_status("(END)");
                    break;
                } else {
                    m_view.move_to_byte_offset(next_match);
                }
            }
            display_page();
            break;
        }

        case Command::SEARCH_EXEC: {
            set_command("", 0);
            set_status("");
            m_highlight_active = true;

            std::string_view contents = m_model.get_contents();

            // update the search pattern
            m_last_search_pattern = std::string{command.payload_str.begin(),
                                                command.payload_str.end()};
            size_t left_bound = m_view.get_starting_offset();
            size_t right_bound = contents.size();

            size_t match = basic_search_first(
                contents, m_last_search_pattern, left_bound, right_bound,
                m_caseless_mode != CaselessSearchMode::SENSITIVE);
            // TODO: optimize
            for (size_t i = 1; i < command.payload_num; ++i) {
                size_t cur_match = basic_search_first(
                    contents, m_last_search_pattern, match, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);
                if (cur_match == right_bound) {
                    break;
                }
                match = cur_match;
            }

            if (match == m_model.length() || match == std::string::npos) {
                // this needs to change depending on whether there was
                // already a search being done
                set_status("Pattern not found");
                break;
            } else {
                m_view.move_to_byte_offset(match);
            }
            display_page();
            break;
        }
        case Command::UPDATE_LINE_IDXS: {
            m_model.update_line_idxs(command.payload_nums);
            break;
        }
        case Command::TOGGLE_HIGHLIGHTING: {
            if (m_last_search_pattern.empty()) {
                set_status("No previous search pattern.");
                break;
            }

            m_highlight_active = !m_highlight_active;

            display_page();
            break;
        }
        case Command::SEARCH_CLEAR: {
            m_last_search_pattern.clear();
            set_command("", 0);
            m_highlight_active = false;
            set_status("Search cleared.");

            display_page();
            break;
        }
        }
        if (command.type == Command::QUIT) {
            break;
        }
    }
}

int main(int argc, char **argv) {

    const char *history_filename_env = getenv("SEARCHLESSHISTFILE");
    std::string history_filename;
    if (history_filename_env && *history_filename_env) {
        history_filename = history_filename_env;
    } else {
        const char *home = getenv("HOME");
        if (home) {
            history_filename = std::string(home) + "/.searchlesshst";
        }
    }
    const char *history_maxsize_env = getenv("SEARCHLESSHISTSIZE");
    int history_maxsize = 100;
    if (history_maxsize_env && *history_maxsize_env) {
        std::from_chars(history_maxsize_env,
                        history_maxsize_env + strlen(history_maxsize_env),
                        history_maxsize);
    }

    if (argc >= 2) {
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
        FILE *tty = isatty(STDIN_FILENO) ? stdin : fopen("/dev/tty", "r");

        Main<FileHandle> main{read_file, tty, std::move(history_filename),
                              history_maxsize};
        main.run();
        return 0;
    }

    if (!isatty(0)) {
        int pipe_fd = dup(0);
        if (pipe_fd == -1) {
            fprintf(stderr, "Error duping stdin %s\n", strerror(pipe_fd));
            exit(1);
        }
        int tty_fd = open("/dev/tty", O_RDONLY);
        if (tty_fd == -1) {
            fprintf(stderr, "Error opening /dev/tty %s\n", strerror(pipe_fd));
            exit(1);
        }
        if (dup2(tty_fd, 0) == -1) {
            fprintf(stderr, "Error duping tty_fd to 0 %s\n", strerror(pipe_fd));
            exit(1);
        }
        FILE *tty = isatty(STDIN_FILENO) ? stdin : fopen("/dev/tty", "r");

        Main<PipeHandle> main(pipe_fd, tty, std::move(history_filename),
                              history_maxsize);
        main.run();
        return 0;
    }

    fprintf(stderr, "missing filename.\n");
    return 1;
}
