#pragma once

#include <fcntl.h>
#include <filesystem>
#include <stddef.h>
#include <string_view>
#include <sys/ioctl.h>
#include <unistd.h>
#include <vector>

#include "FileHandle.h"

class PipeHandle {
    FileHandle m_file_handle;
    std::string m_path;
    int m_fd; // the pipe file des

  public:
    PipeHandle(std::string path, int fd, int temp_fd)
        : m_file_handle(FileHandle::initialize("", temp_fd)),
          m_path(std::move(path)), m_fd(fd) {
        read_to_eof_into_temp();
    }

    struct LineIt {
        using difference_type = size_t;
        using value_type = std::string_view;
        using pointer = void;
        using reference = std::string_view;
        using iterator_category = std::bidirectional_iterator_tag;

        static bool is_going_to_eof(FileHandle::LineIt const &line_it) {
            return (line_it->empty() || line_it->back() != '\n');
        }

        PipeHandle *m_pipe_handle;
        FileHandle *m_file_handle;
        FileHandle::LineIt m_file_line_it;
        bool m_going_into_eof = false;
        bool m_is_eof = false;

        bool operator==(const LineIt &other) const {
            // return true for EOF
            return m_is_eof == other.m_is_eof &&
                   (m_is_eof || m_file_line_it == other.m_file_line_it);
        }
        std::string_view operator*() const {
            return *m_file_line_it;
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
            // if at last line, we return sentinel
            auto next = m_file_line_it;
            if (m_is_eof) {
                assert(m_file_line_it->length() == 0);
                return *this;
            }

            assert(m_file_line_it->length() != 0);

            // if we are at the last line
            if ((++next)->length() == 0 || m_going_into_eof) {
                m_is_eof = true;
                ++m_file_line_it;
                assert(m_file_line_it->length() == 0);
                return *this;
            }

            // if we are at the second last line
            if ((++next)->length() == 0) {
                // read more to see how much of the last line we can fill.
                // go into eof if and only if we did not find a new line
                m_going_into_eof = !m_pipe_handle->read_to_newl_into_temp();
            }

            ++m_file_line_it;
            return *this;
        }
        LineIt operator++(int) {
            PipeHandle::LineIt to_return = *this;
            ++(*this);
            return to_return;
        }
        LineIt &operator--() {
            --m_file_line_it;
            if (m_is_eof) {
                m_is_eof = false;
                m_going_into_eof = true;
            } else {
                m_going_into_eof = false;
            }
            return *this;
        }
        LineIt operator--(int) {
            auto to_return = *this;
            --(*this);
            return to_return;
        }
        size_t line_begin_offset() const {
            return m_file_line_it.line_begin_offset();
        }
        size_t line_end_offset() const {
            return m_file_line_it.line_end_offset();
        }
        FileHandle const *get_model() const {
            return m_file_handle;
        }
    };

    PipeHandle(PipeHandle const &other) = delete;
    PipeHandle &operator=(PipeHandle const &other) = delete;

    PipeHandle(PipeHandle &&other) = delete;
    PipeHandle &operator=(PipeHandle &&other) = delete;
    ~PipeHandle() {
        close(m_fd);
    }

    /* friend void swap(PipeHandle &lhs, PipeHandle &rhs) { */
    /*     using std::swap; */
    /*     swap(lhs.m_file_handle, rhs.m_file_handle); */
    /*     swap(lhs.m_fd, rhs.m_fd); */
    /* } */

    static PipeHandle initialize(std::string path, int fd) {
        // create a temp file
        char temp_filename[7] = "XXXXXX";
        int temp_fd = mkstemp(temp_filename);
        if (temp_fd == -1) {
            fprintf(stderr, "error making temp file. %s\n", strerror(errno));
            exit(1);
        }
        unlink(temp_filename);

        return PipeHandle{std::move(path), fd, temp_fd};
    }

  public:
    ssize_t read_into_temp(size_t num_to_read = 4096) {
        struct stat statbuf;
        fstat(m_file_handle.get_fd(), &statbuf);
        off_t offset = (off_t)statbuf.st_size;
        ssize_t ret_val = splice(m_fd, NULL, m_file_handle.get_fd(), &offset,
                                 num_to_read, SPLICE_F_NONBLOCK);
        if (ret_val == 0) {
            return 0;
        }
        if (ret_val == -1) {
            fprintf(stderr, "PipeHandle error splicing. %s\n", strerror(errno));
            exit(1);
        }
        m_file_handle.read_to_eof();
        return ret_val;
    }

    void read_to_eof_into_temp() {
        ssize_t num_read = 0;
        do {
            num_read = read_into_temp();
        } while (num_read != 0);
        m_file_handle.read_to_eof();
    }

    // returns true if found a newline
    // returns false if read to EOF without a newline
    bool read_to_newl_into_temp() {
        ssize_t num_read = 0;
        size_t found_pos = std::string::npos;
        do {
            num_read = read_into_temp();
            FileHandle::LineIt last_line = --m_file_handle.end();
            found_pos = last_line->find_first_of("\n");
        } while (num_read != 0 && found_pos == std::string::npos);

        return found_pos != std::string::npos;
    }

    PipeHandle::LineIt begin() {
        auto fh_line_it = m_file_handle.begin();
        return {this, &m_file_handle, fh_line_it,
                PipeHandle::LineIt::is_going_to_eof(fh_line_it), false};
    }

    PipeHandle::LineIt end() {
        return {this, &m_file_handle, m_file_handle.end(), false, true};
    }

    PipeHandle::LineIt get_nth_line(size_t line_idx) {

        auto file_handle_line_it = m_file_handle.begin();

        if (file_handle_line_it->length() == 0) {
            return {this, &m_file_handle, m_file_handle.end(), false, true};
        }

        // if we are on the last line, and line_idx != 0;
        // we also return the sentinel EOF
        if (file_handle_line_it->back() != '\n') {
            if (line_idx == 0) {
                return {this, &m_file_handle, m_file_handle.begin(), true,
                        false};
            } else {
                return {this, &m_file_handle, m_file_handle.end(), false, true};
            }
        }

        // else we are guaranteed that there is at least one line ahead of us
        while (line_idx-- > 0) {
            // every iteration we need to try to advance the file_handle_line_it
            auto next = file_handle_line_it;
            ++next;
            if ((++next)->length() == 0) {
                // if the next line is the last line (not EOF)
                bool has_newl = read_to_newl_into_temp();
                if (!has_newl) {
                    // if there is no more newline in our new read
                    if (line_idx == 0) {
                        // but this is the line that we want
                        // so just advance the handle and return it
                        return {this, &m_file_handle, ++file_handle_line_it,
                                true, false};
                    } else {
                        // else we have no hope of returning it
                        return {this, &m_file_handle, m_file_handle.end(),
                                false, true};
                    }
                }
            } else {
                // if not, just advance the current line
                ++file_handle_line_it;
            }
        }

        return {this, &m_file_handle, file_handle_line_it,
                PipeHandle::LineIt::is_going_to_eof(file_handle_line_it)};
    }

    PipeHandle::LineIt get_last_line() {
        // get the left and right bounds
        read_to_eof();
        auto file_handle_line_it = --m_file_handle.end();
        return {this, &m_file_handle, file_handle_line_it,
                PipeHandle::LineIt::is_going_to_eof(file_handle_line_it)};
    }

    PipeHandle::LineIt get_line_at_byte_offset(size_t byte_offset) {
        size_t curr_length = m_file_handle.length();
        ssize_t byte_diff = (ssize_t)(byte_offset - curr_length);
        if (byte_diff >= 0) {
            read_into_temp((size_t)byte_diff + 1);
        }

        FileHandle::LineIt fh_line_it =
            m_file_handle.get_line_at_byte_offset(byte_offset);
        if (fh_line_it.line_end_offset() <= byte_offset) {
            return {this, &m_file_handle, m_file_handle.end(), false, true};
        }
        return {this, &m_file_handle, fh_line_it,
                PipeHandle::LineIt::is_going_to_eof(fh_line_it)};
    }

    std::string_view get_contents() const {
        return m_file_handle.get_contents();
    }

    ssize_t num_byte_diff() const {
        int pipe_size = 0;
        if (ioctl(m_file_handle.get_fd(), FIONREAD, &pipe_size) == -1) {
            fprintf(stderr, "Error calling ioctl on pipe. %s\n",
                    strerror(errno));
            exit(1);
        }

        return (ssize_t)pipe_size;
    }

    bool has_changed() const {
        return num_byte_diff() != 0;
    }

    size_t get_num_processed_bytes() const {
        return m_file_handle.get_num_processed_bytes();
    }

    ssize_t read_n_more_bytes(size_t num_read) {
        ssize_t size_diff = num_byte_diff();
        if (size_diff <= 0) {
            return size_diff;
        }
        read_into_temp(num_read);
        return size_diff;
    }

    ssize_t read_to_eof() {
        ssize_t size_diff = num_byte_diff();
        if (size_diff <= 0) {
            return size_diff;
        }
        read_to_eof_into_temp();
        return size_diff;
    }

    size_t length() const {
        return m_file_handle.length();
    };

    std::string_view relative_path() const {
        return m_path;
    }

    void update_line_idxs(const std::vector<size_t> &offsets) {
        assert(offsets.size() >= 1);
        m_file_handle.update_line_idxs(offsets);
    }
};
