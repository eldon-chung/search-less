#pragma once

#include <algorithm>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/unistd.h>

class Model {
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
        stat(de.path().c_str(), &statbuf);

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
    }

    size_t num_lines() const {
        return m_line_offsets.size();
    }
    std::string get_line_at(size_t line_idx) const {
        // get the left and right bounds
        size_t left_bound = 0;
        size_t right_bound = 0;

        if (line_idx == 0) {
            right_bound = m_line_offsets.at(line_idx) - 1;
        } else {
            left_bound = m_line_offsets.at(line_idx - 1);
            right_bound = m_line_offsets.at(line_idx) - 1;
        }

        return std::string{m_contents.begin() + left_bound,
                           m_contents.begin() + right_bound};
    }
};