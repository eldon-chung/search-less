#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>

template <typename T> struct Channel {
    bool closed = false;
    std::queue<T> que;
    // for signal handlers to send into channel
    // we need something "lock free" but i'm too lazy to copy my lecture notes
    // from last semester
    // state: OR of the following:
    //   - 0x1: intent to set
    //   - 0x2: setting complete, ready to read
    std::atomic<char> sig_que_state;
    T sig_que;

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
        if (sig_que_state.fetch_or(0x1) & 0x1) {
            // We didn't get the intent to set, return
            return;
        }
        // We were the ones to set the intent, go ahead and set it now.
        sig_que = std::move(v);
        sig_que_state.fetch_or(0x2); // Indicate that it's been set
        cond.notify_one();
    }

    std::optional<T> pop() {
        std::unique_lock lock(mut);
        cond.wait(lock, [this]() {
            return !que.empty() || (sig_que_state & 0x2) || closed;
        });

        if (sig_que_state & 0x2) {
            T val = std::move(sig_que);
            sig_que_state = 0;
            return val;
        }

        if (!que.empty()) {
            T top = std::move(que.front());
            que.pop();
            return top;
        }

        // queue is empty, so channel is closed
        return std::nullopt;
    }

    void close() {
        {
            std::scoped_lock lock(mut);
            closed = true;
        }
        cond.notify_one();
    }
};
