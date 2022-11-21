#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> struct Channel {
    std::queue<T> que;
    // for signal handlers to send into channel
    // we need something "lock free" but i'm too lazy to copy my lecture notes
    // from last semester
    std::atomic<T *> sig_que;
    std::mutex mut;
    std::condition_variable cond;

    void push(T v) {
        {
            std::scoped_lock lock(mut);
            que.push(std::move(v));
        }
        cond.notify_one();
    }

    void push_signal(T v) {
        thread_local static T val;
        if (sig_que == &val)
            return;
        val = std::move(v);
        sig_que = &val;
        cond.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock lock(mut);
        cond.wait(lock,
                  [this]() { return !que.empty() || sig_que != nullptr; });
        if (sig_que != nullptr) {
            T val = std::move(*sig_que);
            sig_que = nullptr;
            return val;
        }
        T top = std::move(que.front());
        que.pop();
        return top;
    }

    void close() {
    }
};
