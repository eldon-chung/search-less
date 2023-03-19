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
    // TODO: change this for regex

    if (!m_search_result) {
        return;
    }

    // search all of the occurences of the pattern
    // that are visible on the screen right now.

    Page page = m_view.current_page();
    size_t main_result_offset = m_search_result->offset();
    size_t main_result_length = m_search_result->length();

    m_highlight_offsets.clear();

    std::vector<View::Highlight> line_highlights;
    for (size_t idx = 0; idx < page.get_num_lines(); ++idx) {
        auto page_line =
            page.get_nth_line(m_content_handle->get_contents(), idx);
        size_t line_base_offset = page.get_nth_offset(idx);

        // this is already relative to our visual line
        auto line_offsets = basic_search_all(
            page_line, m_search_result->pattern(), 0, page_line.size(),
            m_search_case != RegularSearch::Case::INSENSITIVE);
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
    } else if (m_search_result || m_content_handle->get_path().empty()) {
        m_view.display_command(":", 1);
    } else {
        m_view.display_status(m_content_handle->get_path());
    }
}

bool Main::run_main() {

    if (m_chan.empty() && (m_search_state || m_following_eof)) {
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
        m_task_chan.close();
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
        if (m_content_handle->has_changed()) {
            m_content_handle->read_to_eof();
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
        if (m_search_case == SearchState::Case::INSENSITIVE) {
            m_search_case = SearchState::Case::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = SearchState::Case::INSENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search enabled");
        }
        break;
    }
    case Command::TOGGLE_CONDITIONALLY_CASELESS: {
        if (m_search_case == SearchState::Case::CONDITIONALLY_SENSITIVE) {
            m_search_case = SearchState::Case::SENSITIVE;
            m_view.display_status(command.payload_str +
                                  ": Caseless search disabled");
        } else {
            m_search_case = SearchState::Case::CONDITIONALLY_SENSITIVE;
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
        if (!m_search_result) {
            set_status("No previous search pattern.");
            break;
        }

        std::string_view contents = m_content_handle->get_contents();
        if (contents.empty()) {
            break;
        }

        std::string search_pattern = m_search_result->pattern();
        size_t end = m_view.get_starting_offset();
        if (m_search_result->has_position()) {
            end = std::max(end, m_search_result->offset());
        }

        m_search_state.reset();
        m_search_state = std::make_unique<RegularSearch>(
            std::move(search_pattern), RegularSearch::Mode::PREV, end,
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
        if (!m_search_result) {
            set_status("No previous search pattern.");
            break;
        }

        // we are guaranteed they will be initialised
        std::string search_pattern = m_search_result->pattern();
        size_t start = m_view.get_starting_offset();
        if (m_search_result->has_position()) {
            start = std::min(m_search_result->offset() +
                                 m_search_result->pattern().size(),
                             start);
        }

        m_search_state.reset();
        m_search_state = std::make_unique<RegularSearch>(
            std::move(search_pattern), RegularSearch::Mode::NEXT, start,
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

        m_search_state.reset();
        m_search_state = std::make_unique<RegularSearch>(
            std::move(search_pattern), RegularSearch::Mode::NEXT, start,
            m_search_case, std::max((size_t)1, command.payload_num));
        m_search_state->schedule();

        // clear out the old search result
        m_search_result = {};

        m_view.display_status("searching...");
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
        if (!m_search_result) {
            set_status("No previous search pattern.");
            break;
        }

        m_highlight_active = !m_highlight_active;

        display_page();
        break;
    }
    case Command::SEARCH_CLEAR: {
        m_search_result = {};
        set_command("", 0);
        m_highlight_active = false;
        set_status("Search cleared.");

        display_page();
        break;
    }
    case Command::INTERRUPT: {
        if (m_following_eof) {
            m_following_eof = false;
            {
                std::scoped_lock lock(m_nc_mutex);
                ungetch(69420);
            }
        }
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

void Main::handle_search_success() {
    // prepare the search result
    m_search_result = m_search_state->get_result();

    // in the case we need to move the view
    if (m_search_result->found_offset >= m_view.get_ending_offset() ||
        m_search_result->found_offset < m_view.get_starting_offset()) {
        m_view.move_to_byte_offset(m_search_state->found_position());
    }

    // clean up the search state
    m_search_state.reset();

    // turn highlighting on
    m_highlight_active = true;

    // render the new page
    display_page();
    m_view.display_status("found result");
}

void Main::run_search() {
    // process search results here

    // run search for a round
    m_search_state->run(m_content_handle->get_contents());

    // return if it's done
    if (m_search_state->is_done()) {
        handle_search_success();
        return;
    }

    if (m_search_state->can_search_more()) {
        m_search_state->schedule();
        return;
    }

    // if it's not done and needs more, and we can load content
    if (m_search_state->needs_more(m_content_handle->size()) &&
        m_content_handle->has_changed()) {
        m_content_handle->read_more();
        m_search_state->schedule();
        return;
    }

    // else at this point no more running can be done
    if (m_search_state->has_position()) {
        handle_search_success();
        return;
    }

    // else we don't have anything to show for it
    m_view.display_status("Pattern not found");
    m_search_result = m_search_state->get_result();

    m_search_state.reset();
}

void Main::run_follow_eof() {
    if (m_content_handle->has_changed()) {
        m_content_handle->read_to_eof();
    }
    m_view.move_to_end();
    display_page();
    m_view.display_status("Waiting for data... (interrupt to abort)");
}

void Main::run() {
    while (true) {
        if (run_main()) {
            break;
        }
        if (m_search_state) {
            run_search();
        }
        if (m_following_eof) {
            run_follow_eof();
        }
    }
}
