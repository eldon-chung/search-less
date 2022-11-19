#pragma once

#include <algorithm>
#include <filesystem>
#include <iterator>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <assert.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>

class Model {
    // A model is a collection of lines, and possibly known or unknown line
    // numbers, computed lazily / asynchronously
    std::filesystem::directory_entry m_de;
    std::string_view m_contents;
    std::vector<size_t> m_line_offsets;
    // should we be using string view?
    int m_fd;

    Model(std::filesystem::directory_entry de, std::string_view contents,
          std::vector<size_t> line_lengths, int fd)
        : m_de(std::move(de)), m_contents(std::move(contents)),
          m_line_offsets(std::move(line_lengths)), m_fd(fd) {
    }

  public:
    static Model initialize(std::filesystem::directory_entry de) {
        // there's the mmap way.
        // we're guaranteed that on call this is a valid directory entry that
        // refers to a regular file

        // get the file descriptor
        int fd = open(de.path().c_str(), O_RDONLY);
        if (fd == -1) {
            fprintf(stderr, "Error opening file. %s\n", strerror(errno));
            exit(1);
        }

        // stat the file
        struct stat statbuf;
        fstat(fd, &statbuf);

        // note: we might want MAP_SHARED with somethimg about msync
        char *contents_ptr = (char *)mmap(NULL, (size_t)statbuf.st_size,
                                          PROT_READ, MAP_PRIVATE, fd, 0);

        std::string_view contents{contents_ptr, (size_t)statbuf.st_size};

        std::vector<size_t> line_lengths;
        std::string_view remaining_contents = contents;
        while (!remaining_contents.empty()) {
            size_t first_idx = remaining_contents.find_first_of("\n");
            if (first_idx == std::string::npos) {
                // if no more newlines, we know the remaining contents is the
                // last line length
                line_lengths.push_back(remaining_contents.size());
                break;
            }

            // line is of length first_idx + 1 (we need to include the newline
            // itself)
            line_lengths.push_back(first_idx + 1);
            // remove the prefix along with the newline
            remaining_contents.remove_prefix(first_idx + 1);
        }

        // we need to make the array cumulative instead
        for (size_t idx = 1; idx < line_lengths.size(); idx++) {
            line_lengths[idx] += line_lengths[idx - 1];
        }

        return Model(std::move(de), std::move(contents),
                     std::move(line_lengths), fd);
    }
    Model(Model const &) = delete;
    Model &operator=(Model const &) = delete;
    Model(Model &&) = delete;
    Model &operator=(Model &&) = delete;
    ~Model() {
        munmap((void *)m_contents.data(), m_contents.size());
    }

    size_t num_lines() const {
        return m_line_offsets.size();
    }
    struct LineIt {
        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        const Model *m_model;
        size_t m_offset; // byte position of start of line
                         // TODO: make it an iterator haha
        size_t m_length;

        bool operator==(const LineIt &other) const {
            return m_offset == other.m_offset && m_length == other.m_length;
        }

        std::string_view operator*() const {
            return m_model->get_contents().substr(m_offset, m_length);
        }

        LineIt end() const {
            return {m_model, m_model->get_contents().size(), 0};
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

        LineIt &operator++() {
            if (m_length == 0) {
                throw std::runtime_error("tried to go past past last line.\n");
            }

            std::string_view remaining_contents =
                m_model->get_contents().substr(m_offset + m_length);
            size_t next_length = remaining_contents.find_first_of("\n");
            if (next_length == std::string::npos) {
                next_length = remaining_contents.length();
            } else {
                next_length++;
            }

            m_offset += m_length;
            m_length = next_length;
            return *this;
        }

        LineIt operator++(int) {
            LineIt to_return = *this;
            ++(*this);
            return to_return;
        }

        LineIt &operator--() {

            throw std::runtime_error("Tried to go behind first line.\n");

            std::string_view front_contents =
                m_model->get_contents().substr(0, m_offset - 1);
            size_t prev_offset = front_contents.find_last_of("\n");
            if (prev_offset == std::string::npos) {
                prev_offset = front_contents.length();
            } else {
                prev_offset++;
            }

            m_offset = prev_offset;
            m_length = front_contents.length() - m_offset + 1;
            return *this;
        }

        LineIt operator--(int) {
            LineIt to_return = *this;
            --(*this);
            return to_return;
        }

        size_t line_end_offset() const {
            return m_offset + m_length;
        }
    };
    LineIt get_nth_line(size_t line_idx) const {
        // get the left and right bounds
        size_t first_line_length = m_contents.find_first_of("\n");
        if (first_line_length == std::string::npos) {
            first_line_length++;
        } else {
            first_line_length = m_contents.length();
        }

        LineIt to_return{this, 0, first_line_length};

        while (line_idx-- > 0) {
            to_return++;
        }

        return to_return;
    }

    LineIt get_line_at_byte_offset(size_t byte_offset) const {
        if (byte_offset > m_contents.size()) {
            throw std::runtime_error(
                "byte_offset is larger than content length.\n");
        }
        // do it the slow way first;
        std::string_view left_half = m_contents.substr(0, byte_offset);
        std::string_view right_half = m_contents.substr(byte_offset);

        size_t left_pos = left_half.find_last_of("\n");
        if (left_pos == std::string::npos) {
            left_pos = 0;
        } else {
            left_pos++;
        }

        size_t right_length = right_half.find_first_of("\n");
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

    void update_line_offsets(const std::vector<size_t> &offsets) {
        for (size_t offset : offsets) {
            m_line_offsets.push_back(offset);
        }
    }
};
