#include "Main.h"

#include <charconv>
#include <fcntl.h>
#include <optional>
#include <span>
#include <stdlib.h>
#include <string.h>
#include <string_view>
#include <unistd.h>

#include "search.h"

// is this good? the member fields now sort of behave like
// "scoped globals" in this way. perhaps we should make
// this a static method that takes in params
void Main::update_screen_highlight_offsets() {

    if (m_last_search_pattern.empty()) {
        return;
    }

    // search all of the occurences of the pattern
    // that are visible on the screen right now.
    if (m_view.get_starting_offset() >= m_content_handle->size()) {
        m_highlight_offsets.clear();
        return;
    }

    auto result_offsets = basic_search_all(
        m_content_handle->get_contents(), m_last_search_pattern,
        m_view.get_starting_offset(), m_view.get_ending_offset(),
        m_caseless_mode != CaselessSearchMode::SENSITIVE);

    // clear our highlight offsets
    m_highlight_offsets.clear();
    m_highlight_offsets.reserve(result_offsets.size());

    for (size_t offset : result_offsets) {
        m_highlight_offsets.push_back({offset, m_last_search_pattern.length()});
    }
}

void Main::display_page() {
    if (m_highlight_active) {
        update_screen_highlight_offsets();
        m_view.display_page_at(m_highlight_offsets);
    } else {
        m_view.display_page_at({});
    }
}

void Main::display_command_or_status() {
    if (!m_command_str_buffer.empty()) {
        m_view.display_command(m_command_str_buffer, m_command_cursor_pos);
    } else if (!m_status_str_buffer.empty()) {
        m_view.display_status(m_status_str_buffer);
    } else if (!m_last_search_pattern.empty() ||
               m_content_handle->get_path().empty()) {
        m_view.display_command(":", 1);
    } else {
        m_view.display_status(m_content_handle->get_path());
    }
}

void Main::run() {
    std::optional<Command> prev_command;
    while (true) {
        if (m_time_commands && prev_command) {
            fprintf(stderr, "Time taken for command %d: %ld ns\n",
                    prev_command->type,
                    std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - prev_command->start)
                        .count());
        }

        Command command = m_chan.pop().value();
        prev_command = command;
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

            std::string_view contents = m_content_handle->get_contents();
            if (contents.empty()) {
                break;
            }

            // prepare the final value that we should be moving to

            // size_t end_of_file_offset = contents.size();
            for (size_t i = 0; i < std::max((size_t)1, command.payload_num);
                 ++i) {

                Page const &page = m_view.const_current_page();
                size_t curr_line_match, prev_match;
                std::string_view curr_line =
                    page.get_nth_line(m_content_handle->get_contents(), 0);

                size_t curr_line_offset = page.get_begin_offset();
                size_t curr_line_end = curr_line_offset + curr_line.size();

                curr_line_match = basic_search_first(
                    contents, m_last_search_pattern, curr_line_offset,
                    curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                prev_match = basic_search_last(
                    contents, m_last_search_pattern, 0, curr_line_offset,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (prev_match == curr_line_offset &&
                    curr_line_match != curr_line_end) {
                    // we still have the one on the current line
                    // don't move the screen
                    set_status("(END)");
                    break;
                } else if (prev_match == curr_line_offset &&
                           curr_line_match == curr_line_end) {
                    // we have none
                    // don't move the screen
                    set_status("Pattern not found");
                } else {
                    // we have something
                    m_view.move_to_byte_offset(prev_match);
                    if (!page.has_prev()) {
                        break;
                    }
                }
            }
            // what happens if there wasnt a match?
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

            // for now let's just eagerly load as much
            // as possible
            if (m_content_handle->has_changed()) {
                size_t curr_offset = m_view.get_starting_offset();
                m_content_handle->read_to_eof();
                m_view.move_to_byte_offset(curr_offset);
            }

            std::string_view contents = m_content_handle->get_contents();
            if (contents.empty()) {
                break;
            }

            // we are guaranteed they will be initialised
            size_t end_of_file_offset = contents.size();
            for (size_t i = 0; i < std::max((size_t)1, command.payload_num);
                 ++i) {

                Page const &page = m_view.const_current_page();

                size_t curr_line_match, next_match;
                std::string_view curr_line =
                    page.get_nth_line(m_content_handle->get_contents(), 0);

                size_t curr_line_offset = page.get_begin_offset();
                size_t curr_line_end = curr_line_offset + curr_line.size();

                curr_line_match = basic_search_first(
                    contents, m_last_search_pattern, curr_line_offset,
                    curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                next_match = basic_search_first(
                    contents, m_last_search_pattern, curr_line_end,
                    end_of_file_offset,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (next_match == end_of_file_offset &&
                    curr_line_match != curr_line_end) {
                    set_status("(END)");
                    break;
                } else if (next_match == end_of_file_offset &&
                           curr_line_match == curr_line_end) {
                    set_status("Pattern not found");
                    break;
                } else {
                    m_view.move_to_byte_offset(next_match);
                    if (!page.has_next(m_content_handle->get_contents())) {
                        break;
                    }
                }
            }
            display_page();
            break;
        }

        case Command::SEARCH_EXEC: {
            set_command("", 0);
            set_status("");
            m_highlight_active = true;

            // for now let's just eagerly load as much
            // as possible
            if (m_content_handle->has_changed()) {
                size_t curr_offset = m_view.get_starting_offset();
                m_content_handle->read_to_eof();
                m_view.move_to_byte_offset(curr_offset);
            }

            std::string_view contents = m_content_handle->get_contents();

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

            if (match == m_content_handle->size() ||
                match == std::string::npos) {
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
            // m_model.update_line_idxs(command.payload_nums);
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
    FILE *tty = isatty(STDIN_FILENO) ? stdin : fopen("/dev/tty", "r");

    // TODO: proper cmdline arg parsing
    std::string filename = "";
    int fd = -1;
    bool time_commands = false;
    for (char *arg : std::span<char *>(argv + 1, argv + argc)) {
        using namespace std::string_literals;
        if (arg == "--time-commands"s) {
            time_commands = true;
            continue;
        } else {
            // try to open the file
            filename = arg;
            fd = open(arg, O_RDONLY);
            if (fd == -1) {
                fprintf(stderr, "%s: %s\n", arg, strerror(errno));
                return 1;
            }
        }
    }

    if (fd != -1) {
        // We already have a file
    } else if (!isatty(STDIN_FILENO)) {
        // Use stdin
        fd = STDIN_FILENO;
    } else {
        fprintf(stderr, "Missing filename\n");
        return 1;
    }

    struct stat statbuf;
    if (fstat(fd, &statbuf) == -1) {
        fprintf(stderr, "%s: fstat: %s\n", argv[1], strerror(errno));
        return 1;
    }

    if (S_ISREG(statbuf.st_mode)) {
        Main main{std::move(filename), tty, std::move(history_filename),
                  history_maxsize, time_commands};
        main.run();
        return 0;
    } else {
        Main main{fd, tty, std::move(history_filename), history_maxsize,
                  time_commands};
        main.run();
        return 0;
    }
}
