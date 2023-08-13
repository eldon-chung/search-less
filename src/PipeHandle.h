#pragma once

#include <fcntl.h>
#include <filesystem>
#include <stddef.h>
#include <string.h>
#include <string_view>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "ContentHandle.h"

class PipeHandle final : public ContentHandle {
    int m_pipe_fd; // the pipe file des
    int m_temp_fd; // the temp file des

  public:
    PipeHandle(PipeHandle const &other) = delete;
    PipeHandle &operator=(PipeHandle const &other) = delete;

    PipeHandle(PipeHandle &&other) = delete;
    PipeHandle &operator=(PipeHandle &&other) = delete;
    ~PipeHandle() {
        close(m_pipe_fd);
        close(m_temp_fd);
    }

    PipeHandle(int fd) : m_pipe_fd(fd) {
        // create a temp file
        char temp_filename[7] = "XXXXXX";
        int temp_fd = mkstemp(temp_filename);
        if (temp_fd == -1) {
            fprintf(stderr, "error making temp file. %s\n", strerror(errno));
            exit(1);
        }
        unlink(temp_filename);

        m_temp_fd = temp_fd;

        // read some stuff if possible, just give up if not possible
        read_to_eof();
    }

  private:
    ssize_t read_into_temp(std::shared_lock<std::shared_mutex> &lock,
                           size_t num_to_read = 1 * 1024 * 1024 * 1024) {
        // splice from m_pipe_fd into temp file
        ssize_t ret_val = splice(m_pipe_fd, NULL, m_temp_fd, NULL, num_to_read,
                                 SPLICE_F_NONBLOCK);

        // if nothing was read, we just return
        if (ret_val == 0) {
            return 0;
        }
        if (ret_val == -1) {
            if (errno == EAGAIN) {
                // this is actually fine, we'll try again
                return 0;
            }
            fprintf(stderr, "PipeHandle error splicing. %s\n", strerror(errno));
            exit(1);
        }

        if (!lock.owns_lock()) {
            lock.lock();
        }
        size_t curr_file_size = m_contents.size() + (size_t)ret_val;
        char *new_contents_ptr =
            m_contents.data()
                ? (char *)mremap((void *)m_contents.data(), m_contents.size(),
                                 curr_file_size, MREMAP_MAYMOVE)
                : (char *)mmap(NULL, curr_file_size, PROT_READ, MAP_PRIVATE,
                               m_temp_fd, 0);
        if ((void *)new_contents_ptr == MAP_FAILED) {
            // Map failed for some reason
            fprintf(stderr, "mremap or mmap error. %s\n", strerror(errno));
            exit(1);
        }
        m_contents = {new_contents_ptr, new_contents_ptr + curr_file_size};

        return ret_val;
    }

  public:
    bool read_more() final {
        std::shared_lock<std::shared_mutex> lock(m_mutex, std::defer_lock);
        ssize_t total_read = 0;
        ssize_t num_read = 0;
        do {
            num_read = read_into_temp(lock);
            total_read += num_read;
        } while (num_read != 0);
        return total_read != 0;
    }

    bool read_to_eof() final {
        return read_more();
    }

    std::string_view get_path() const final {
        return "";
    }

    bool has_changed() const final {
        int result = 0;
        if (ioctl(m_pipe_fd, FIONREAD, &result) == -1) {
            fprintf(stderr, "PipeHandle: ioctl error %s\n", strerror(errno));
            exit(1);
        }

        return result != 0;
    }
};
