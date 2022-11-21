#pragma once

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

struct Main {
    Channel<Command> m_chan;
    Channel<std::function<void(void)>> m_task_chan;

    std::stop_source m_file_task_stop_source;
    std::promise<void> m_file_task_promise;

    std::mutex m_nc_mutex;

    Model m_model;
    View m_view;

    InputThread m_input;
    WorkerThread m_taskmaster;

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode m_caseless_mode;

    std::string m_command_str_buffer;
    uint16_t m_command_cursor_pos;

    Main(std::filesystem::directory_entry file_de)
        : m_model(Model::initialize(std::move(file_de))),
          m_view(View::initialize(&m_nc_mutex, &m_model)),
          m_input(&m_nc_mutex, &m_chan), m_taskmaster(&m_task_chan),
          m_caseless_mode(CaselessSearchMode::SENSITIVE) {
        register_for_sigwinch_channel(&m_chan);

        auto read_line_offsets_tasks = [&]() -> void {
            compute_line_offsets(m_file_task_stop_source.get_token(), &m_chan,
                                 m_file_task_promise, m_model.get_contents(),
                                 0);
        };

        m_view.display_page_at({});
        m_view.display_status();
        // schedule a line offset computation
        m_task_chan.push(std::move(read_line_offsets_tasks));
    }

    ~Main() {
        m_chan.close();
        m_task_chan.close();
        m_file_task_stop_source.request_stop();
    }
    Main(Main const &other) = delete;
    Main(Main &&other) = delete;
    Main &operator=(Main const &other) = delete;
    Main &operator=(Main &&other) = delete;

    void run() {
        while (true) {
            Command command = m_chan.pop().value();

            switch (command.type) {
            case Command::INVALID:
                m_view.display_status("Invalid key pressed: " +
                                      command.payload_str);
                break;
            case Command::RESIZE:
                m_view.handle_resize();
                m_view.display_page_at({});
                m_view.display_status("handle resize called");
                break;
            case Command::QUIT:
                m_chan.close();
                m_task_chan.close();
                m_file_task_stop_source.request_stop();
                break;
            case Command::VIEW_DOWN:
                m_view.scroll_down();
                m_view.display_page_at({});
                break;
            case Command::VIEW_UP:
                m_view.scroll_up();
                m_view.display_page_at({});
                break;
            case Command::VIEW_BOF:
                m_view.move_to_top();
                m_view.display_page_at({});
                break;
            case Command::VIEW_EOF:
                if (m_model.has_changed()) {
                    // need to kill task if running
                    m_file_task_stop_source.request_stop();
                    m_file_task_promise.get_future().wait();
                    m_model.read_to_eof();

                    // reset the stop source; schedule new task
                    m_file_task_stop_source = std::stop_source();
                    auto read_line_offsets_tasks = [&]() -> void {
                        compute_line_offsets(
                            m_file_task_stop_source.get_token(), &m_chan,
                            m_file_task_promise, m_model.get_contents(),
                            m_model.get_num_processed_bytes());
                    };
                    m_task_chan.push(read_line_offsets_tasks);
                }
                m_view.move_to_end();
                m_view.display_page_at({});
                break;
            case Command::DISPLAY_COMMAND: {
                m_view.display_command(command.payload_str);
                break;
            }
            case Command::TOGGLE_CASELESS: {
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
                    m_caseless_mode =
                        CaselessSearchMode::CONDITIONALLY_SENSITIVE;
                    m_view.display_status(
                        command.payload_str +
                        ": Conditionally caseless search enabled (case is "
                        "ignored if pattern only contains lowercase)");
                }
                break;
            }
            case Command::SEARCH_START: {
                m_command_str_buffer = command.payload_str;
                // we expect the next incoming command to trigger the redraw
                break;
            }
            case Command::SEARCH_QUIT: {
                m_command_str_buffer = ":";
                m_view.display_command(m_command_str_buffer);
                break;
            }
            case Command::SEARCH_PREV: { // assume for now that search_exec was
                                         // definitely called

                m_command_str_buffer = command.payload_str;

                if (m_view.begin() == m_view.end()) {
                    break;
                }

                std::string_view contents = m_model.get_contents();
                std::string search_pattern{m_command_str_buffer.begin() + 1,
                                           m_command_str_buffer.end()};
                size_t left_bound = 0;
                size_t right_bound = m_view.get_starting_offset();
                size_t curr_line_end = m_view.cursor().get_ending_offset();

                // if it doesnt exist on the current line, don't
                // it means it doesnt exist.
                size_t first_match = basic_search_first(
                    contents, search_pattern, right_bound, curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                // dont bother scrolling up if we know what offset to start
                // from view.scroll_down();
                size_t last_match = basic_search_last(
                    contents, search_pattern, left_bound, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (last_match == right_bound && first_match != curr_line_end) {
                    // if there isnt another instance behind us, but there is
                    // one instance on our line
                    m_view.display_status("(TOP)");
                } else if (last_match == right_bound) {
                    // there just isnt another instance
                    m_view.display_status("Pattern not found");
                } else {
                    m_view.move_to_byte_offset(last_match);
                    auto result_offsets = basic_search_all(
                        m_model.get_contents(), search_pattern,
                        m_view.get_starting_offset(),
                        m_view.get_ending_offset(),
                        m_caseless_mode != CaselessSearchMode::SENSITIVE);
                    std::vector<View::Highlights> highlight_list;
                    highlight_list.reserve(result_offsets.size());
                    for (size_t offset : result_offsets) {
                        highlight_list.push_back(
                            {offset, search_pattern.length()});
                    }
                    m_view.display_page_at(highlight_list);
                    m_command_str_buffer = ":";
                    m_view.display_command(":");
                }
                break;
            }
            case Command::SEARCH_NEXT: { // assume for now that search_exec was
                                         // definitely called

                m_command_str_buffer = command.payload_str;
                if (m_view.begin() == m_view.end()) {
                    break;
                }

                std::string_view contents = m_model.get_contents();
                std::string search_pattern{m_command_str_buffer.begin() + 1,
                                           m_command_str_buffer.end()};
                size_t left_bound = m_view.get_starting_offset();
                size_t curr_line_end = m_view.cursor().get_ending_offset();
                size_t right_bound = contents.size();

                // if it doesnt exist on the current line, don't
                // it means it doesnt exist.
                size_t first_match = basic_search_first(
                    contents, search_pattern, left_bound, curr_line_end,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);
                if (first_match == curr_line_end ||
                    first_match == std::string::npos) {
                    // this needs to change depending on whether there was
                    // already a search being done
                    m_view.display_status("Pattern not found");
                    break;
                }

                // dont bother scrolling down if we know what offset to start
                // from view.scroll_down();
                first_match = basic_search_first(
                    contents, search_pattern, curr_line_end, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (first_match == m_model.length() ||
                    first_match == std::string::npos) {
                    m_view.display_status("(END)");
                } else {
                    m_view.move_to_byte_offset(first_match);
                    auto result_offsets = basic_search_all(
                        m_model.get_contents(), search_pattern,
                        m_view.get_starting_offset(),
                        m_view.get_ending_offset(),
                        m_caseless_mode != CaselessSearchMode::SENSITIVE);
                    std::vector<View::Highlights> highlight_list;
                    highlight_list.reserve(result_offsets.size());
                    for (size_t offset : result_offsets) {
                        highlight_list.push_back(
                            {offset, search_pattern.length()});
                    }
                    m_view.display_page_at(highlight_list);
                    m_command_str_buffer = ":";
                    m_view.display_command(":");
                }
                break;
            }
            case Command::SEARCH_EXEC: {
                if (m_view.begin() == m_view.end()) {
                    break;
                }

                std::string_view contents = m_model.get_contents();
                std::string search_pattern{m_command_str_buffer.begin() + 1,
                                           m_command_str_buffer.end()};
                size_t left_bound = m_view.get_starting_offset();
                size_t right_bound = contents.size();

                size_t first_match = basic_search_first(
                    contents, search_pattern, left_bound, right_bound,
                    m_caseless_mode != CaselessSearchMode::SENSITIVE);

                if (first_match == m_model.length() ||
                    first_match == std::string::npos) {
                    // this needs to change depending on whether there was
                    // already a search being done
                    m_view.display_status("Pattern not found");
                    m_view.display_page_at({});
                    break;
                } else {
                    m_view.move_to_byte_offset(first_match);
                    auto result_offsets = basic_search_all(
                        m_model.get_contents(), search_pattern,
                        m_view.get_starting_offset(),
                        m_view.get_ending_offset(),
                        m_caseless_mode != CaselessSearchMode::SENSITIVE);
                    std::vector<View::Highlights> highlight_list;
                    highlight_list.reserve(result_offsets.size());
                    for (size_t offset : result_offsets) {
                        highlight_list.push_back(
                            {offset, search_pattern.length()});
                    }
                    m_view.display_page_at(highlight_list);
                    m_command_str_buffer = ":";
                    m_view.display_command(":");
                }
                break;
            }
            case Command::BUFFER_CURS_POS: {
                m_command_cursor_pos = (uint16_t)command.payload_nums.front();
                m_view.display_command(m_command_str_buffer,
                                       m_command_cursor_pos);
                break;
            }
            case Command::UPDATE_LINE_IDXS: {
                m_model.update_line_idxs(command.payload_nums);
                m_view.display_status("computed line idxs");
                break;
            }
            }
            if (command.type == Command::QUIT) {
                break;
            }
        }
    }
};