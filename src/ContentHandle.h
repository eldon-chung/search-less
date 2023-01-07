#pragma once

#include <string_view>

#include <fcntl.h>
#include <sys/mman.h>

class ContentHandle {

  protected:
    std::string_view m_contents;

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

    std::string_view get_contents() const {
        return m_contents;
    }

    size_t size() const {
        return m_contents.size();
    }

    // implemented by the inheriting classes
    virtual bool read_more() = 0;
    virtual bool read_to_eof() = 0;
    virtual bool has_changed() const = 0;
    virtual std::string_view get_path() const = 0;
};
