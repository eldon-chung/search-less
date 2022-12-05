#pragma once

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <assert.h>
#include <fcntl.h>
#include <future>
#include <stdio.h>
#include <stop_token>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>

#include "Channel.h"

class FileHandle {
    // A model is a collection of lines, and possibly known or unknown line
    // numbers, computed lazily / asynchronously
    std::filesystem::directory_entry m_de;
    std::string_view m_contents;
    std::vector<size_t> m_line_idxs;
    size_t m_num_processed_bytes;
    // should we be using string view?
    int m_fd;

    FileHandle(std::filesystem::directory_entry de, std::string_view contents,
               int fd)
        : m_de(std::move(de)), m_contents(std::move(contents)),
          m_num_processed_bytes(0), m_fd(fd) {
    }

  public:
    static FileHandle initialize(std::filesystem::directory_entry de) {
        int fd = open(de.path().c_str(), O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "error opening file. %s\n", strerror(errno));
            exit(1);
        }

        return initialize(std::move(de), fd);
    }

    static FileHandle initialize(std::filesystem::directory_entry de, int fd) {
        // stat the file
        struct stat statbuf;
        fstat(fd, &statbuf);

        // note: we might want MAP_SHARED with somethimg about msync
        char *contents_ptr = (char *)mmap(NULL, (size_t)statbuf.st_size,
                                          PROT_READ, MAP_PRIVATE, fd, 0);

        std::string_view contents{contents_ptr, (size_t)statbuf.st_size};
        return FileHandle(std::move(de), contents, fd);
    }
    FileHandle(FileHandle const &) = delete;
    FileHandle &operator=(FileHandle const &) = delete;
    FileHandle(FileHandle &&other)
        : m_de(std::move(other.m_de)), m_contents(std::move(other.m_contents)),
          m_line_idxs(std::move(other.m_line_idxs)),
          m_num_processed_bytes(std::move(other.m_num_processed_bytes)),
          m_fd(std::exchange(other.m_fd, -1)) {
    }
    FileHandle &operator=(FileHandle &&other) {
        FileHandle temp{std::move(other)};
        using std::swap;
        swap(*this, temp);
        return *this;
    }
    ~FileHandle() {

        if (m_fd == -1) {
            return;
        }
        munmap((void *)m_contents.data(), m_contents.size());
    }

    struct LineIt {
        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        const FileHandle *m_model;
        size_t m_offset; // byte position of start of line
                         // TODO: make it an iterator haha
        size_t m_length;
        bool operator==(const FileHandle::LineIt &other) const {
            return m_offset == other.m_offset && m_length == other.m_length;
        }
        std::string_view operator*() const {
            return m_model->get_contents().substr(m_offset, m_length);
        }
        struct Cursed {
            std::string_view tmp;
            std::string_view *operator->() {
                return &tmp;
            }
        };
        Cursed operator->() const {
            return {**this};
        }

        FileHandle::LineIt &operator++() {
            if (m_length == 0) {
                throw std::runtime_error("tried to go past past last line.\n");
            }

            std::string_view remaining_contents =
                m_model->get_contents().substr(m_offset + m_length);
            size_t next_length = remaining_contents.find_first_of('\n');
            if (next_length == std::string::npos) {
                next_length = remaining_contents.length();
            } else {
                next_length++;
            }

            m_offset += m_length;
            m_length = next_length;
            return *this;
        }
        FileHandle::LineIt operator++(int) {
            FileHandle::LineIt to_return = *this;
            ++(*this);
            return to_return;
        }

        FileHandle::LineIt &operator--() {
            if (m_offset == 0) {
                throw std::runtime_error("Tried to go behind first line.\n");
            }

            std::string_view front_contents =
                m_model->get_contents().substr(0, m_offset - 1);
            size_t prev_offset = front_contents.find_last_of('\n');
            if (prev_offset == std::string::npos) {
                prev_offset = 0;
            } else {
                prev_offset++;
            }

            m_offset = prev_offset;
            m_length = front_contents.length() - prev_offset + 1;

            return *this;
        }
        FileHandle::LineIt operator--(int) {
            FileHandle::LineIt to_return = *this;
            --(*this);
            return to_return;
        }
        size_t line_begin_offset() const {
            return m_offset;
        }
        size_t line_end_offset() const {
            return m_offset + m_length;
        }
        FileHandle const *get_model() const {
            return m_model;
        }
    };

    FileHandle::LineIt begin() {
        return get_nth_line(0);
    }
    FileHandle::LineIt end() {
        return {this, m_contents.size(), 0};
    }

    FileHandle::LineIt get_nth_line(size_t line_idx) {
        // get the left and right bounds
        size_t first_line_length = m_contents.find_first_of('\n');
        if (first_line_length == std::string::npos) {
            first_line_length = m_contents.length();
        } else {
            first_line_length++;
        }

        FileHandle::LineIt to_return{this, 0, first_line_length};

        // bounds checking?
        while (line_idx-- > 0) {
            to_return++;
        }

        return to_return;
    }

    FileHandle::LineIt get_last_line() {
        // get the left and right bounds
        return get_line_at_byte_offset(m_contents.size() - 1);
    }

    FileHandle::LineIt get_line_at_byte_offset(size_t byte_offset) {
        if (byte_offset > m_contents.size()) {
            throw std::runtime_error(
                "byte_offset is larger than content length.\n");
        }
        // do it the slow way first;
        // TODO: optimize?
        std::string_view left_half = m_contents.substr(0, byte_offset);
        std::string_view right_half = m_contents.substr(byte_offset);

        size_t left_pos = left_half.find_last_of('\n');
        if (left_pos == std::string::npos) {
            left_pos = 0;
        } else {
            left_pos++;
        }

        size_t right_length = right_half.find_first_of('\n');
        if (right_length == std::string::npos) {
            right_length = right_half.length();
        } else {
            right_length++;
        }

        return {this, left_pos, (left_half.length() - left_pos) + right_length};
    }

    std::string_view get_contents() const {
        return m_contents;
    }

    ssize_t num_byte_diff() const {
        struct stat statbuf;
        fstat(m_fd, &statbuf);

        return (ssize_t)statbuf.st_size - (ssize_t)m_contents.length();
    }

    bool has_changed() const {
        return num_byte_diff() != 0;
    }

    size_t get_num_processed_bytes() const {
        return m_num_processed_bytes;
    }

    ssize_t read_to_eof() {
        // stat the file
        struct stat statbuf;
        fstat(m_fd, &statbuf);

        ssize_t size_diff =
            (ssize_t)statbuf.st_size - (ssize_t)m_contents.length();

        if (size_diff <= 0) {
            return size_diff;
        }

        munmap((void *)m_contents.data(), m_contents.size());
        char *contents_ptr = (char *)mmap(NULL, (size_t)statbuf.st_size,
                                          PROT_READ, MAP_PRIVATE, m_fd, 0);
        m_contents = std::string_view{contents_ptr, (size_t)statbuf.st_size};

        return size_diff;
    }

    size_t length() const {
        return m_contents.length();
    };

    std::string relative_path() const {
        return m_de.path().relative_path().string();
    }

    void update_line_idxs(const std::vector<size_t> &offsets) {
        assert(offsets.size() >= 1);
        m_num_processed_bytes = offsets.back();
        auto it = std::lower_bound(offsets.begin(), offsets.end(),
                                   m_num_processed_bytes);
        m_line_idxs.insert(m_line_idxs.end(), it, --offsets.end());
    }

    int get_fd() const {
        return m_fd;
    }
};
