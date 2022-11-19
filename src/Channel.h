#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>

template <typename T> struct Channel {
    std::queue<T> que;
    std::mutex mut;
    std::condition_variable cond;

    void push(T v) {
        {
            std::scoped_lock lock(mut);
            que.push(std::move(v));
        }
        cond.notify_one();
    }

    T pop() {
        std::unique_lock lock(mut);
        cond.wait(lock, [this]() { return !que.empty(); });
        T top = std::move(que.front());
        que.pop();
        return top;
    }
};
