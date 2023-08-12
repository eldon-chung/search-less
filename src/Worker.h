#pragma once

#include <assert.h>
#include <cctype>
#include <fcntl.h>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <optional>
#include <poll.h>
#include <signal.h>
#include <stop_token>
#include <string_view>
#include <thread>

#include "Channel.h"

#if 0
// Sample usage:
int main() {
    // Make a worker thread
    WorkerThread worker;

    // Send jobs into it
    for (size_t i = 0; i < 10; ++i) {
        auto [fut, stop] = worker.spawn(
            [](int i, std::stop_token) {
                std::cout << i << std::endl;
                return i;
            },
            i);
        // Await the job
        auto job_result = fut.get();
        std::cout << "Job completed: " << *job_result << std::endl;
    }

    // You can cancel a hung/slow job with stop token
    {
        auto [fut, stop] = worker.spawn([](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });
        // This would hang, don't do this
        /* auto job_result = fut.get(); */
        stop.request_stop();
        // This is ok, expect to wait about a second since the worker thread
        // only checks stop_requested every second
        fut.get();
        std::cout << "Job cancelled" << std::endl;
    }

    {
        // We can send more jobs now that the previous one was cancelled
        auto [fut, stop] = worker.spawn(
            [](int i) {
                std::cout << i << std::endl;
                return i;
            },
            69);
        // Await the job
        auto job_result = fut.get();
        std::cout << "Job completed: " << job_result << std::endl;
    }

    {
        // You can also just send another job in without cancelling the existing
        // job, attempting to send a new job in automatically requests a stop
        // for the existing job.
        auto [fut1, stop1] = worker.spawn([](std::stop_token stop) {
            while (!stop.stop_requested()) {
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
        });

        // Cancels the hung job
        auto [fut2, stop2] = worker.spawn(
            [](int i) {
                std::cout << i << std::endl;
                return i;
            },
            69);

        fut1.get();
        std::cout << "Job cancelled" << std::endl;

        auto job_result2 = fut2.get();
        std::cout << "Job completed: " << job_result2 << std::endl;
    }
}
#endif
struct WorkerThread {
    Channel<std::function<void(void)>> task_chan;
    std::stop_source stop_current_task;
    std::thread t;

    WorkerThread() : task_chan(), t(&WorkerThread::run, this) {
    }

    WorkerThread(const WorkerThread &) = delete;
    WorkerThread &operator=(const WorkerThread &) = delete;
    WorkerThread(WorkerThread &&) = delete;
    WorkerThread &operator=(WorkerThread &&) = delete;
    ~WorkerThread() {
        task_chan.close();
        stop_current_task.request_stop();
        t.join();
    }

    template <class Function, class... Args>
    [[nodiscard]] std::pair<
        std::future<std::invoke_result_t<
            std::decay_t<Function>, std::decay_t<Args>..., std::stop_token>>,
        std::stop_source>
    spawn(Function &&f, Args &&...args) {
        // Stop any existing task
        stop_current_task.request_stop();

        using ResultType =
            std::invoke_result_t<std::decay_t<Function>, std::decay_t<Args>...,
                                 std::stop_token>;
        std::promise<ResultType> promise;
        std::future<ResultType> future = promise.get_future();
        stop_current_task = std::stop_source();
        std::stop_token token = stop_current_task.get_token();
        task_chan.push([promise = std::make_shared<std::promise<ResultType>>(
                            std::move(promise)),
                        f = std::forward<Function>(f),
                        args = std::make_tuple(std::forward<Args>(args)...,
                                               std::move(token))]() mutable {
            promise->set_value(std::apply(std::move(f), std::move(args)));
        });
        return {std::move(future), stop_current_task};
    }

  private:
    void run() {
        while (true) {
            auto maybe_task = task_chan.pop();
            if (maybe_task.has_value()) {
                maybe_task.value()();
            } else {
                break;
            }
        };
    }
};
