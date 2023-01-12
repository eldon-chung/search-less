#include "Main.h"

#include <charconv>
#include <fcntl.h>
#include <optional>
#include <span>
#include <stdlib.h>
#include <string.h>
#include <string_view>
#include <unistd.h>

#include <iostream>

#include "search.h"

// is this good? the member fields now sort of behave like
// "scoped globals" in this way. perhaps we should make
// this a static method that takes in params
void Main::update_screen_highlight_offsets() {

    if (!m_search_result.has_pattern()) {
        return;
    }

    // search all of the occurences of the pattern
    // that are visible on the screen right now.

    Page page = m_view.current_page();
    size_t main_result_offset = m_search_result.offset();
    size_t main_result_length = m_search_result.length();

    m_highlight_offsets.clear();

    std::vector<View::Highlight> line_highlights;
    for (size_t idx = 0; idx < page.get_num_lines(); ++idx) {
        auto page_line =
            page.get_nth_line(m_content_handle->get_contents(), idx);
        size_t line_base_offset = page.get_nth_offset(idx);

        // this is already relative to our visual line
        auto line_offsets = basic_search_all(
            page_line, m_search_result.pattern(), 0, page_line.size(),
            m_search_case != Search::Case::INSENSITIVE);

        line_highlights.clear();
        line_highlights.reserve(line_offsets.size());

        for (size_t offset : line_offsets) {
            using enum View::Highlight::Type;
            if (offset + line_base_offset == main_result_offset) {
                line_highlights.push_back({offset, main_result_length, Main});
            } else {
                line_highlights.push_back({offset, main_result_length, Side});
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
    } else if (m_search_result.has_result() ||
               m_content_handle->get_path().empty()) {
        m_view.display_command(":", 1);
    } else {
        m_view.display_status(m_content_handle->get_path());
    }
}

bool Main::run_main() {

    if (m_chan.empty() && m_search_state.has_value()) {
        // if there isn't a command and there is an ongoing search
        // we can just continue the search
        return false;
    }

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
        if (m_search_case == Search::Case::INSENSITIVE) {
            m_search_case = Search::Case::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = Search::Case::INSENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search enabled");
        }
        break;
    }
    case Command::TOGGLE_CONDITIONALLY_CASELESS: {
        if (m_search_case == Search::Case::CONDITIONALLY_SENSITIVE) {
            m_search_case = Search::Case::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = Search::Case::CONDITIONALLY_SENSITIVE;
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
        if (!m_search_result.has_pattern()) {
            set_status("No previous search pattern.");
            break;
        }

        std::string_view contents = m_content_handle->get_contents();
        if (contents.empty()) {
            break;
        }

        std::string search_pattern = std::string(m_search_result.pattern());
        size_t start = 0;
        size_t end = m_view.get_starting_offset();
        if (m_search_result.has_result()) {
            end = std::max(end, m_search_result.offset());
        }

        m_search_state =
            Search(std::move(search_pattern), start, end, Search::Mode::PREV,
                   m_search_case, std::max((size_t)1, command.payload_num));

        m_search_state->schedule();
        break;
    }

    case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                 // definitely called
        set_command("", 0);
        set_status("");
        m_highlight_active = true;

        // for now if search pattern is empty, we just break
        if (!m_search_result.has_pattern()) {
            set_status("No previous search pattern.");
            break;
        }

        // we are guaranteed they will be initialised

        std::string search_pattern = std::string(m_search_result.pattern());
        size_t start;
        if (!m_search_result.has_result()) {
            start = m_view.get_starting_offset();
        } else {
            start = m_search_result.offset() + m_search_result.pattern().size();
        }
        size_t end = std::string::npos;

        m_search_state =
            Search(std::move(search_pattern), start, end, Search::Mode::NEXT,
                   m_search_case, std::max((size_t)1, command.payload_num));

        m_search_state->schedule();
        break;
    }

    case Command::SEARCH_EXEC: {
        set_command("", 0);
        set_status("");
        m_highlight_active = true;

        std::string search_pattern = command.payload_str;
        size_t start = m_view.get_starting_offset();
        size_t end = std::string::npos;

        m_search_state =
            Search(std::move(search_pattern), start, end, Search::Mode::NEXT,
                   m_search_case, std::max((size_t)1, command.payload_num));

        m_search_state->schedule();

        m_view.display_status("searching...");
        break;
    }
    case Command::UPDATE_LINE_IDXS: {
        // m_model.update_line_idxs(command.payload_nums);
        break;
    }
    case Command::TOGGLE_HIGHLIGHTING: {
        if (!m_search_result.has_pattern()) {
            set_status("No previous search pattern.");
            break;
        }

        m_highlight_active = !m_highlight_active;

        display_page();
        break;
    }
    case Command::SEARCH_CLEAR: {
        m_search_result.clear();
        set_command("", 0);
        m_highlight_active = false;
        set_status("Search cleared.");

        display_page();
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

void Main::run_search() {
    // process search results here

    // if it isn't done we run it for one round
    if (!m_search_state->is_done()) {
        m_search_state->run(m_content_handle->get_contents());
    }

    // if it still hasn't completed we go do something else
    if (!m_search_state->is_done()) {
        return;
    }

    // otherwise at this moment it's done
    // and we need to process the current state
    assert(m_search_state->num_iter() >= 1);

    if (m_search_state->has_result() && m_search_state->num_iter() == 1) {
    }

    // case 1: we can run more
    // if our current iteration was successful
    if (m_search_state->has_result() && m_search_state->num_iter() >= 2) {
        --(m_search_state->num_iter());
        m_search_state->schedule();
        return;
    }

    // case 2: we can run more
    // if our current iteration was unsuccessful
    // because we needed more
    if (m_search_state->need_more() && m_content_handle->has_changed()) {
        m_content_handle->read_more();
        m_search_state->give_more();
        m_search_state->schedule();
        return;
    }

    // case 3: we can't run more (and we can't give more)

    if (m_search_state->has_result()) {
        if (m_search_state->num_iter() >= 2) {
            // subtract iterations by 1, schedule again

        } else if () {
            m_view.move_to_byte_offset(m_search_state->result());
            m_search_result = {m_search_state->pattern(),
                               m_search_state->result()};
            m_highlight_active = true;
            display_page();
            m_search_state = std::nullopt;
            m_view.display_status("found result");
        }
    } else if (m_search_state->need_more() && m_content_handle->has_changed()) {
        // we hit EOF and we can provide more
        m_content_handle->read_more();
        m_search_state->give_more();
        m_search_state->schedule();

    } else {
        // else it needs more and we can't provide or
        // we hit BOF
        m_view.display_status("Pattern not found");
        m_search_result = {m_search_state->pattern(), std::string::npos};
        m_search_state = std::nullopt;
    }
}

void Main::run() {
    while (true) {
        if (run_main()) {
            break;
        }
        if (m_search_state.has_value()) {
            run_search();
        }
    }
}
