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

#include "ContentHandle.h"

class FileHandle final : public ContentHandle {
    int m_fd;
    std::string m_path;

  public:
    FileHandle(std::string path)
        : m_fd(open(path.c_str(), O_RDONLY)), m_path(std::move(path)) {
        if (m_fd == -1) {
            fprintf(stderr, "FileHandle: Error opening %s. %s\n",
                    m_path.c_str(), strerror(errno));
        }
        // read contents now
        read_more();
    }

    FileHandle(FileHandle const &) = delete;
    FileHandle &operator=(FileHandle const &) = delete;
    FileHandle(FileHandle &&other) = delete;
    FileHandle &operator=(FileHandle &&other) = delete;

    ~FileHandle() final {
        close(m_fd);
    }

    bool read_more() final {
        // if there is more to the file, we should read into m_contents
        size_t curr_file_size = current_file_size();
        if (curr_file_size == m_contents.size()) {
            return false;
        }

        // potentially needs an unmap.
        if (m_contents.data()) {
            munmap((void *)m_contents.data(), m_contents.size());
        }

        // remap now
        char *new_contents_ptr =
            (char *)mmap(NULL, curr_file_size, PROT_READ, MAP_PRIVATE, m_fd, 0);

        m_contents = std::string_view{new_contents_ptr, curr_file_size};
        return true;
    }

    std::string_view get_path() const final {
        return m_path;
    }

    bool read_to_eof() final {
        return read_more();
    }

    bool has_changed() const final {
        return m_contents.size() != current_file_size();
    }

  private:
    size_t current_file_size() const {
        struct stat statbuf;
        if (fstat(m_fd, &statbuf) == -1) {
            fprintf(stderr, "FileHandle: Could not stat file. %s\n",
                    strerror(errno));
            exit(1);
        }

        return (size_t)statbuf.st_size;
    }
};
