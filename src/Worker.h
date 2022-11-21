#pragma once

#include <assert.h>
#include <cctype>
#include <fcntl.h>
#include <future>
#include <mutex>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <stop_token>
#include <thread>

#include "Channel.h"
#include "Command.h"

struct WorkerThread {
    Channel<std::function<void(void)>> *task_chan;
    std::thread t;

    WorkerThread(Channel<std::function<void(void)>> *task_chan)
        : task_chan(task_chan), t(&WorkerThread::start, this) {
    }

    WorkerThread(const WorkerThread &) = delete;
    WorkerThread &operator=(const WorkerThread &) = delete;
    WorkerThread(WorkerThread &&) = delete;
    WorkerThread &operator=(WorkerThread &&) = delete;
    ~WorkerThread() {
        t.join();
    }

    void start() {
        while (true) {
            auto maybe_task = task_chan->pop();
            if (maybe_task.has_value()) {
                maybe_task.value()();
            } else {
                break;
            }
        };
    }
};

void compute_line_offsets(std::stop_token stop, Channel<Command> *chan,
                          std::promise<void> &promise,
                          std::string_view contents, size_t starting_offset);
