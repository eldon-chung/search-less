#pragma once

#include <algorithm>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <ncurses.h>
#include <optional>
#include <signal.h>
#include <stdio.h>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "Channel.h"
#include "Command.h"
#include "Input.h"
#include "View.h"
#include "Worker.h"
#include "search.h"

#include "ContentHandle.h"
#include "FileHandle.h"
#include "PipeHandle.h"

struct Main {
    Channel<Command> m_chan;
    Channel<std::function<void(void)>> m_task_chan;

    std::stop_source m_file_task_stop_source;
    std::promise<void> m_file_task_promise;

    std::mutex m_nc_mutex;

    std::unique_ptr<ContentHandle> m_content_handle;
    View m_view;

    InputThread m_input;
    WorkerThread m_taskmaster;

    bool m_highlight_active;

    RegularSearch::Case m_search_case;

    std::string m_status_str_buffer;
    std::string m_command_str_buffer;
    size_t m_command_cursor_pos;

    std::unique_ptr<RegularSearch> m_search_state;
    std::vector<std::vector<View::Highlight>> m_highlight_offsets;
    // this is to help highlight stuff on screen so it persists beyond
    // the search state (which is only valid until the job is done)
    std::optional<SearchResult> m_search_result;

    std::optional<Command> prev_command;

    bool m_following_eof;

    size_t m_half_page_size;
    size_t m_page_size;
    bool m_time_commands;

    Main(ContentHandle *content_ptr, FILE *tty, std::string history_filename,
         int history_maxsize, bool time_commands)
        : m_content_handle(content_ptr),
          m_view(View::create(&m_nc_mutex, m_content_handle.get(), tty)),
          m_input(&m_nc_mutex, &m_chan, tty, std::move(history_filename),
                  history_maxsize),
          m_taskmaster(&m_task_chan), m_highlight_active(true),
          m_search_case(RegularSearch::Case::SENSITIVE),
          m_search_result({"", std::string::npos}), m_following_eof(false),
          m_time_commands(time_commands) {
        register_signal_handlers(&m_chan);

        display_page();
        display_command_or_status();

        m_half_page_size = std::max((size_t)1, m_view.m_main_window_height / 2);
        m_page_size = std::max((size_t)1, m_view.m_main_window_height);
    }

  public:
    Main(std::string path, FILE *tty, std::string history_filename,
         int history_maxsize, bool time_commands)
        : Main(new FileHandle(std::move(path)), tty, history_filename,
               history_maxsize, time_commands) {
    }

    Main(int fd, FILE *tty, std::string history_filename, int history_maxsize,
         bool time_commands)
        : Main(new PipeHandle(fd), tty, history_filename, history_maxsize,
               time_commands) {
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

    void run();

  private:
    void update_screen_highlight_offsets();

    void display_page();
    void display_command_or_status();

    void run_search();
    bool run_main();
    void run_follow_eof();
    void handle_search_success();

    void set_command(std::string command, size_t cursor_pos) {
        m_command_str_buffer = std::move(command);
        m_command_cursor_pos = cursor_pos;
        display_command_or_status();
    }
    void set_status(std::string status) {
        m_status_str_buffer = std::move(status);
        display_command_or_status();
    }
};
