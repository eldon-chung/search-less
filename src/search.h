#pragma once

#include <assert.h>
#include <stddef.h>

#include <functional>
#include <string>
#include <string_view>
#include <vector>

std::vector<size_t> basic_search_all(std::string_view model,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset,
                                     bool caseless = false);

size_t basic_search_first(std::string_view model, std::string_view pattern,
                          size_t beginning_offset, size_t ending_offset,
                          bool caseless = false);

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless = false);

class Search {
    static const size_t SEARCH_BLOCK_SIZE = 4096;

    enum class Case {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    enum class Mode {
        NEXT,
        PREV,
        ALL,
    };
    std::vector<size_t> m_found_positions;
    // can we merge these two?
    std::string m_pattern;
    size_t m_current_offset;
    size_t m_starting_offset;
    size_t m_ending_offset;
    size_t m_num_iter;
    Mode m_mode;
    Case m_case;
    bool m_is_running;

  public:
    Search() : m_is_running(false) {
    }
    void initialise(std::string pattern, size_t starting_offset,
                    size_t ending_offset, size_t num_iter = 1,
                    Mode mode = Mode::NEXT,
                    Case sensitivity = Case::SENSITIVE) {

        assert(!pattern.empty());
        assert(num_iter >= 1);
        assert(starting_offset < ending_offset);

        m_found_positions.clear();

        m_pattern = std::move(pattern);
        m_starting_offset = starting_offset;
        m_current_offset = starting_offset;
        m_ending_offset = ending_offset;
        m_num_iter = num_iter;
        m_mode = mode;
        m_case = sensitivity;

        m_is_running = true;
    }

    void terminate() {
        m_is_running = false;
    }

    // entry point that dispatches the corresponding algorithms
    void run(std::string_view contents) {
        if (is_done()) {
            return;
        }

        switch (m_mode) {
        case Mode::NEXT:
            find_next(contents);
            break;
        case Mode::PREV:
            find_prev(contents);
            break;
        case Mode::ALL:
            find_all(contents);
            break;
        default:
            break;
        }
    }

    bool is_done() const {
        return !m_is_running;
    }

    size_t current_offset() const {
        return m_current_offset;
    }

    bool has_results() const {
        return !m_found_positions.empty();
    }

    std::vector<size_t> get_positions() const {
        return m_found_positions;
    }

  private:
    // invoke all the algos here

    void find_next(std::string_view contents) {
        assert(!contents.empty());
        assert(m_num_iter >= 1);
        assert(m_starting_offset < contents.size());
        assert(m_starting_offset < m_ending_offset);

        // if the contents can't even fit the pattern
        if (contents.size() < m_pattern.size()) {
            terminate();
            return;
        }

        // if our substring into the contents can't even fit the pattern
        if (m_ending_offset - m_starting_offset < m_pattern.size()) {
            terminate();
            return;
        }

        // create an ending index that we need to stop our search
        size_t chunk_end = std::min(
            m_ending_offset, m_starting_offset + Search::SEARCH_BLOCK_SIZE);
        chunk_end = std::min(m_ending_offset, contents.size() - 1);

        size_t result = std::string::npos;
        while (m_num_iter-- > 0) {
            result = basic_search_first(contents, m_pattern, m_starting_offset,
                                        m_ending_offset,
                                        m_case == Case::INSENSITIVE);
            if (result == std::string::npos) {
                // add back one iteration because this current one failed
                ++m_num_iter;
                break;
            }

            m_starting_offset = result + 1;
            assert(m_starting_offset < m_ending_offset);
        }

        // we did our iterations and we found the result
        if (m_num_iter == 0) {
            // this only happens when result is found
            assert(result != std::string::npos);
            // add the result into the list of found positions
            m_found_positions.push_back(result);
            terminate();
            return;
        }

        // at this point we might have found results but we didnt finish
        // the number of iterations we needed

        // we have handled the case that the pattern can't fit
        assert(m_ending_offset - m_starting_offset >= m_pattern.size() - 1);
        m_starting_offset = m_ending_offset - m_pattern.size() + 1;

        // if we're at the end of the whole contents right now
        // no more running needed
        if (chunk_end == contents.size() - 1) {
            terminate();
        }
    }

    void find_prev(std::string_view contents) {
    }

    void find_all(std::string_view contents) {
    }
};
