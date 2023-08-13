#pragma once

#include <mutex>
#include <shared_mutex>
#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>

struct ContentGuard {
    std::shared_lock<std::shared_mutex> lock;
    std::string_view contents;
};

class ContentHandle {
  protected:
    std::string_view m_contents;
    mutable std::shared_mutex m_mutex;

  public:
    ContentHandle() {
    }
    // mark dtor as virtual
    virtual ~ContentHandle() {
        if (!m_contents.data()) {
            munmap((void *)m_contents.data(), m_contents.size());
        }
    }
    ContentHandle(ContentHandle const &) = delete;
    ContentHandle(ContentHandle &&) = delete;
    ContentHandle &operator=(ContentHandle const &) = delete;
    ContentHandle &operator=(ContentHandle &&) = delete;

    ContentGuard get_contents() const {
        std::shared_lock lock(m_mutex);
        return {std::move(lock), m_contents};
    }

    size_t size() const {
        std::shared_lock lock(m_mutex);
        return m_contents.size();
    }

    // implemented by the inheriting classes
    virtual bool read_more() = 0;
    virtual bool read_to_eof() = 0;
    virtual bool has_changed() const = 0;
    virtual std::string_view get_path() const = 0;
};
