#pragma once

#include <algorithm>
#include <filesystem>
#include <functional>
#include <future>
#include <mutex>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>
#include <stop_token>
#include <string>
#include <utility>
#include <vector>

#include "Channel.h"
#include "Command.h"
#include "FileHandle.h"
#include "Input.h"
#include "PipeHandle.h"
#include "View.h"
#include "Worker.h"
#include "search.h"

template <typename T> struct Main {
    Channel<Command> m_chan;
    Channel<std::function<void(void)>> m_task_chan;

    std::stop_source m_file_task_stop_source;
    std::promise<void> m_file_task_promise;

    std::mutex m_nc_mutex;

    T m_model;
    View<T> m_view;

    InputThread m_input;
    WorkerThread m_taskmaster;

    bool m_highlight_active;

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode m_caseless_mode;

    std::string m_status_str_buffer;
    std::string m_command_str_buffer;
    size_t m_command_cursor_pos;

    std::vector<typename View<T>::Highlight> m_highlight_offsets;
    std::string m_last_search_pattern;

    size_t m_half_page_size;
    size_t m_page_size;

    Main(std::string path, int fd, FILE *tty, std::string history_filename,
         int history_maxsize)
        : m_model(T::initialize(path, fd)),
          m_view(View<T>::create(&m_nc_mutex, &m_model, tty)),
          m_input(&m_nc_mutex, &m_chan, tty, std::move(history_filename),
                  history_maxsize),
          m_taskmaster(&m_task_chan), m_highlight_active(true),
          m_caseless_mode(CaselessSearchMode::SENSITIVE) {

        register_for_sigwinch_channel(&m_chan);
        auto read_line_offsets_tasks = [&]() -> void {
            compute_line_offsets(m_file_task_stop_source.get_token(), &m_chan,
                                 m_file_task_promise, m_model.get_contents(),
                                 0);
        };

        display_page();
        display_command_or_status();
        // schedule a line offset computation
        m_task_chan.push(std::move(read_line_offsets_tasks));

        m_half_page_size = std::max((size_t)1, m_view.m_main_window_height / 2);
        m_page_size = std::max((size_t)1, m_view.m_main_window_height);
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
