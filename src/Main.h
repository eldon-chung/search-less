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
    constexpr static size_t npos = std::string::npos;

    Channel<Command> m_chan;

    std::stop_source m_file_task_stop_source;
    std::promise<void> m_file_task_promise;

    std::mutex m_nc_mutex;

    std::unique_ptr<ContentHandle> m_content_handle;
    View m_view;

    InputThread m_input;

    bool m_highlight_active;

    enum class SearchCase {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    SearchCase m_search_case;
    std::string m_search_pattern;
    size_t m_last_known_search_result;
    std::future<std::optional<size_t>> m_search_result;
    std::stop_source m_search_stop;
    WorkerThread m_search_worker;

    std::string m_status_str_buffer;
    std::string m_command_str_buffer;
    size_t m_command_cursor_pos;

    std::vector<std::vector<View::Highlight>> m_highlight_offsets;

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
          m_highlight_active(false), m_search_case(SearchCase::SENSITIVE),
          m_search_pattern(), m_search_worker(), m_following_eof(false),
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
