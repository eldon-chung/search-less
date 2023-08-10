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

        // start with stuff to read if possible
        // TODO: add some form of timeout perhaps
        while (true) {
            if (read_to_eof()) {
                break;
            }
        }
    }

  private:
    ssize_t read_into_temp(size_t num_to_read = 1 * 1024 * 1024 * 1024) {

        // get the temp file size
        // so we know where to start writing to the file
        struct stat statbuf;
        fstat(m_temp_fd, &statbuf);
        off_t offset = (off_t)statbuf.st_size;

        // splice from m_pipe_fd into temp file
        ssize_t ret_val = splice(m_pipe_fd, NULL, m_temp_fd, &offset,
                                 num_to_read, SPLICE_F_NONBLOCK);

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

        // get the temp file size again so we can remap
        // into m_contents
        fstat(m_temp_fd, &statbuf);
        size_t curr_file_size = (size_t)statbuf.st_size;

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

    void read_to_eof_into_temp() {
        ssize_t num_read = 0;
        do {
            num_read = read_into_temp();
        } while (num_read != 0);
    }

  public:
    bool read_more() final {
        ssize_t total_read = 0;
        ssize_t num_read = 0;
        do {
            num_read = read_into_temp();
            total_read += num_read;
        } while (num_read != 0);
        return total_read != 0;
    }

    bool read_to_eof() final {
        bool has_more = false;
        while (read_more()) {
            has_more = true;
        }
        return has_more;
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
