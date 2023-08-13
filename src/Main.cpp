#include "Main.h"

#include <charconv>
#include <chrono>
#include <fcntl.h>
#include <optional>
#include <span>
#include <stdlib.h>
#include <string.h>
#include <string_view>
#include <unistd.h>

#include <iostream>

#include "Timer.h"
#include "search.h"

// is this good? the member fields now sort of behave like
// "scoped globals" in this way. perhaps we should make
// this a static method that takes in params
void Main::update_screen_highlight_offsets() {
    auto content_guard = m_content_handle->get_contents();
    Page page = m_view.current_page();

    // clear our highlight offsets
    m_highlight_offsets.clear();

    std::vector<View::Highlight> line_highlights;
    for (size_t idx = 0; idx < page.get_num_lines(); ++idx) {
        auto page_line = page.get_nth_line(content_guard.contents, idx);
        size_t line_base_offset = page.get_nth_offset(idx);

        // this is already relative to our visual line
        std::vector<size_t> line_offsets = *search_all(
            regex_search_first, page_line, m_search_pattern, 0,
            page_line.size(), m_search_case != SearchCase::SENSITIVE,
            std::stop_token());
        line_highlights.clear();
        line_highlights.reserve(line_offsets.size());

        for (size_t offset : line_offsets) {
            using enum View::Highlight::Type;
            if (offset + line_base_offset == m_last_known_search_result) {
                line_highlights.push_back(
                    {offset, m_search_pattern.length(), Main});
            } else {
                line_highlights.push_back(
                    {offset, m_search_pattern.length(), Side});
            }
        }

        m_highlight_offsets.push_back(std::move(line_highlights));
    }
    assert(m_highlight_offsets.size() == page.get_num_lines());
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
    } else if (m_last_known_search_result != npos ||
               m_content_handle->get_path().empty()) {
        m_view.display_command(":", 1);
    } else {
        m_view.display_status(m_content_handle->get_path());
    }
}

bool Main::run_main() {
    if (m_search_result.valid() &&
        m_search_result.wait_for(std::chrono::nanoseconds{0}) ==
            std::future_status::ready) {
        std::optional<size_t> result = m_search_result.get();
        if (!result) {
            return false;
        }
        if (*result == m_content_handle->size() ||
            *result == std::string::npos) {
            // this needs to change depending on whether there was
            // already a search being done
            set_status("Pattern not found");
        } else {
            m_last_known_search_result = *result;
            m_view.move_to_byte_offset(*result);
        }
        display_page();
        /* m_chan.push(Command{Command::QUIT}); */
        /* m_input.t.detach(); */
        return false;
    }

    if (m_chan.empty() && m_following_eof) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return false;
    }

    if (m_time_commands && prev_command) {
        fprintf(stderr, "Time taken for command %d: %ld ns\n",
                prev_command->type,
                std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - prev_command->start)
                    .count());
    }

    Command command;

    if (m_search_result.valid()) {
        std::optional<Command> c = m_chan.try_pop();
        if (c) {
            command = *c;
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return false;
        }
    } else {
        command = m_chan.pop().value();
    }
    prev_command = command;

    if (m_following_eof && command.type != Command::INTERRUPT) {
        return false;
    }

    switch (command.type) {
    case Command::INVALID:
        m_view.display_status("Invalid key pressed: " + command.payload_str);
        break;
    case Command::RESIZE:
        m_view.handle_resize();
        m_half_page_size = std::max((size_t)1, m_view.m_main_window_height / 2);
        m_page_size = std::max((size_t)1, m_view.m_main_window_height);
        display_page();
        break;
    case Command::QUIT:
        m_chan.close();
        m_file_task_stop_source.request_stop();
        return true;
    case Command::VIEW_LEFT:
        m_view.scroll_left(std::max(command.payload_num, (size_t)1));
        display_page();
        break;
    case Command::VIEW_RIGHT:
        m_view.scroll_right(std::max(command.payload_num, (size_t)1));
        display_page();
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
        m_search_stop.request_stop();
        if (m_content_handle->has_changed()) {
            // If the pipe is fast (it refills within 10ms), then read more.
            // However have a hard cutoff of 1s so it doesn't hang for
            // dripping pipes.
            auto start = std::chrono::steady_clock::now();
            while (m_content_handle->read_to_eof() &&
                   std::chrono::steady_clock::now() - start <
                       std::chrono::seconds(1)) {
                m_view.move_to_end();
                display_page();
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
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
    case Command::TOGGLE_LONG_LINES: {
        m_view.toggle_wrap_lines();
        display_page();
        break;
    }
    case Command::DISPLAY_STATUS: {
        set_status(command.payload_str);
        break;
    }
    case Command::TOGGLE_CASELESS: {
        set_command("", 0);
        if (m_search_case == SearchCase::INSENSITIVE) {
            m_search_case = SearchCase::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = SearchCase::INSENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search enabled");
        }
        break;
    }
    case Command::TOGGLE_CONDITIONALLY_CASELESS: {
        if (m_search_case == SearchCase::CONDITIONALLY_SENSITIVE) {
            m_search_case = SearchCase::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = SearchCase::CONDITIONALLY_SENSITIVE;
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
        if (m_search_pattern.empty()) {
            set_status("No previous search pattern.");
            break;
        }

        auto content_guard = m_content_handle->get_contents();
        std::string_view contents = content_guard.contents;
        if (contents.empty()) {
            break;
        }

        std::string search_pattern = m_search_pattern;
        size_t end;
        if (m_last_known_search_result == npos ||
            m_last_known_search_result < m_view.get_starting_offset() ||
            m_last_known_search_result >= m_view.get_ending_offset()) {
            end = m_view.get_starting_offset();
        } else {
            end = m_last_known_search_result;
        }

        std::tie(m_search_result, m_search_stop) =
            m_search_worker.spawn([=](std::stop_token stop) {
                return search_backward_n(
                    regex_search_last, std::max((size_t)1, command.payload_num),
                    contents, search_pattern, 0, end,
                    m_search_case != SearchCase::SENSITIVE, stop);
            });
        break;
    }

    case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                 // definitely called
        set_command("", 0);
        set_status("");
        m_highlight_active = true;

        // for now if search pattern is empty, we just break
        if (m_search_pattern.empty()) {
            set_status("No previous search pattern.");
            break;
        }

        // we are guaranteed they will be initialised
        auto content_guard = m_content_handle->get_contents();
        std::string_view contents = content_guard.contents;
        if (contents.empty()) {
            break;
        }

        std::string search_pattern = m_search_pattern;
        size_t start;
        if (m_last_known_search_result == npos ||
            m_last_known_search_result < m_view.get_starting_offset() ||
            m_last_known_search_result >= m_view.get_ending_offset()) {
            start = m_view.get_starting_offset();
        } else {
            start = m_last_known_search_result + 1;
        }

        std::tie(m_search_result, m_search_stop) =
            m_search_worker.spawn([=](std::stop_token stop) {
                return search_forward_n(
                    regex_search_first,
                    std::max((size_t)1, command.payload_num), contents,
                    search_pattern, start, m_content_handle->size(),
                    m_search_case != SearchCase::SENSITIVE, stop);
            });
        break;
    }

    case Command::SEARCH_EXEC: {
        set_command("", 0);
        set_status("");
        m_highlight_active = true;

        auto content_guard = m_content_handle->get_contents();
        std::string_view contents = content_guard.contents;
        if (contents.empty()) {
            break;
        }

        std::string search_pattern = command.payload_str;
        m_search_pattern = search_pattern;
        m_last_known_search_result = npos;
        size_t start = m_view.get_starting_offset();

        std::tie(m_search_result, m_search_stop) =
            m_search_worker.spawn([=](std::stop_token stop) {
                return search_forward_n(
                    regex_search_first,
                    std::max((size_t)1, command.payload_num), contents,
                    search_pattern, start, m_content_handle->size(),
                    m_search_case != SearchCase::SENSITIVE, stop);
            });
        break;
    }
    case Command::UPDATE_LINE_IDXS: {
        // m_model.update_line_idxs(command.payload_nums);
        break;
    }
    case Command::FOLLOW_EOF: {
        m_following_eof = true;
        break;
    }
    case Command::TOGGLE_HIGHLIGHTING: {
        if (m_search_pattern.empty()) {
            set_status("No previous search pattern.");
            break;
        }

        m_highlight_active = !m_highlight_active;

        display_page();
        break;
    }
    case Command::SEARCH_CLEAR: {
        m_search_pattern = "";
        m_last_known_search_result = npos;
        m_search_result = std::future<std::optional<size_t>>();
        set_command("", 0);
        m_highlight_active = false;
        set_status("Search cleared.");

        display_page();
        break;
    }
    case Command::INTERRUPT: {
        if (m_following_eof) {
            m_following_eof = false;
        }
        m_search_result = std::future<std::optional<size_t>>();
        set_command("", 0);
        set_status("");
        break;
    }
    }

    if (command.type == Command::QUIT) {
        return true;
    } else {
        return false;
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

    /* Timer timer; */

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

void Main::run_follow_eof() {
    if (m_content_handle->has_changed()) {
        // Cancel existing search so this doesn't hang
        m_search_stop.request_stop();
        m_content_handle->read_to_eof();
    }
    m_view.move_to_end();
    display_page();
    m_view.display_status("Waiting for data... (interrupt to abort) " +
                          std::to_string(rand()));
}

void Main::run() {
    /* m_chan.push(Command{Command::SEARCH_EXEC, "123123", {}, 0}); */
    while (true) {
        if (run_main()) {
            break;
        }
        /* if (m_search_state) { */
        /*     run_search(); */
        /* } */
        if (m_following_eof) {
            run_follow_eof();
        }
    }
}
