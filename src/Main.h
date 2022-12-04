#pragma once

#include <filesystem>
#include <future>
#include <mutex>
#include <ncurses.h>
#include <signal.h>
#include <stdio.h>

#include "FileHandle.h"
#include "Input.h"
#include "View.h"
#include "Worker.h"
#include "search.h"

struct Main {
    Channel<Command> m_chan;
    Channel<std::function<void(void)>> m_task_chan;

    std::stop_source m_file_task_stop_source;
    std::promise<void> m_file_task_promise;

    std::mutex m_nc_mutex;

    FileHandle m_model;
    View m_view;

    InputThread m_input;
    WorkerThread m_taskmaster;

    bool m_highlight_active;

    enum class CaselessSearchMode {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    CaselessSearchMode m_caseless_mode;

    std::string m_command_str_buffer;
    size_t m_command_start_pos;
    size_t m_command_cursor_pos;

    std::vector<View::Highlight> m_highlight_offsets;
    std::string m_last_search_pattern;

    size_t m_half_page_size;
    size_t m_page_size;

    Main(std::filesystem::directory_entry file_de, FILE *tty)
        : m_model(FileHandle::initialize(std::move(file_de))),
          m_view(View::create(&m_nc_mutex, &m_model, tty)),
          m_input(&m_nc_mutex, &m_chan, tty), m_taskmaster(&m_task_chan),
          m_highlight_active(true),
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
};
