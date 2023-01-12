#pragma once

#include <assert.h>
#include <stddef.h>

#include <algorithm>
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

  public:
    enum class Case {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };
    enum class Mode {
        NEXT,
        PREV,
    };

  private:
    size_t m_found_position;
    // can we merge these two?
    std::string m_pattern;
    size_t m_starting_offset;
    size_t m_ending_offset;
    Mode m_mode;
    Case m_case;
    size_t m_num_iter;
    bool m_is_running;
    bool m_need_more;

  public:
    Search(std::string pattern, size_t starting_offset, size_t ending_offset,
           Mode mode = Mode::NEXT, Case sensitivity = Case::INSENSITIVE,
           size_t num_iter = 1)
        : m_found_position(std::string::npos), m_pattern(std::move(pattern)),
          m_starting_offset(starting_offset), m_ending_offset(ending_offset),
          m_mode(mode), m_case(sensitivity), m_num_iter(num_iter),
          m_is_running(true), m_need_more(false) {
        assert(!m_pattern.empty());
        // assert(m_starting_offset < m_ending_offset);
    }

    size_t &num_iter() {
        return m_num_iter;
    }

    bool need_more() const {
        return m_need_more;
    }

    void ask_for_more() {
        m_need_more = true;
    }

    void give_more() {
        m_need_more = false;
    }

    void terminate() {
        m_is_running = false;
    }

    void schedule() {
        m_is_running = true;
    }

    // entry point that dispatches the corresponding algorithms
    void run(std::string_view contents) {
        std::vector<size_t> results;
        switch (m_mode) {
        case Mode::NEXT:
            m_found_position = find_next(contents);
            break;
        case Mode::PREV:
            m_found_position = find_prev(contents);
            break;
        default:
            break;
        }
    }

    bool is_done() const {
        return !m_is_running;
    }

    bool has_result() const {
        return m_found_position != std::string::npos;
    }

    size_t result() const {
        return m_found_position;
    }

    std::string pattern() const {
        return m_pattern;
    }

  private:
    // invoke all the algos here

    size_t find_next(std::string_view contents) {
        assert(!contents.empty());
        assert(m_starting_offset < contents.size());
        // assert(m_starting_offset < m_ending_offset);

        size_t result = std::string::npos;

        if (m_starting_offset >= m_ending_offset) {
            terminate();
            return result;
        }

        // if the contents can't even fit the pattern
        if (contents.size() < m_pattern.size()) {
            ask_for_more();
            terminate();
            return result;
        }

        // if our substring into the contents can't even fit the pattern
        if (m_ending_offset - m_starting_offset < m_pattern.size()) {
            ask_for_more();
            terminate();
            return result;
        }

        // create an ending index that we need to stop our search
        size_t chunk_end = std::min(m_ending_offset, contents.size());

        if (m_starting_offset < SIZE_MAX - Search::SEARCH_BLOCK_SIZE) {
            chunk_end = std::min(chunk_end,
                                 m_starting_offset + Search::SEARCH_BLOCK_SIZE);
        }

        result = basic_search_first(contents, m_pattern, m_starting_offset,
                                    chunk_end, m_case == Case::INSENSITIVE);
        // update the starting offset
        if (result != std::string::npos) {
            // skip by length of pattern
            terminate();
        } else {
            m_starting_offset = chunk_end - m_pattern.length() + 1;
        }

        // if we've hit the end we can just terminate
        if (chunk_end == contents.size()) {
            ask_for_more();
            terminate();
        }

        return result;
    }

    size_t find_prev(std::string_view contents) {
        assert(!contents.empty());
        assert(m_starting_offset < contents.size());
        // assert(m_starting_offset < m_ending_offset);

        size_t result = std::string::npos;

        if (m_starting_offset >= m_ending_offset) {
            terminate();
            return result;
        }

        // if the contents can't even fit the pattern
        if (contents.size() < m_pattern.size()) {
            terminate();
            return result;
        }

        // if our substring into the contents can't even fit the pattern
        if (m_ending_offset - m_starting_offset < m_pattern.size()) {
            terminate();
            return result;
        }

        // create an starting index that we need to stop our search
        size_t chunk_start = m_starting_offset;
        if (m_ending_offset >= m_starting_offset + Search::SEARCH_BLOCK_SIZE) {
            chunk_start = m_ending_offset - Search::SEARCH_BLOCK_SIZE;
        }

        result =
            basic_search_last(contents, m_pattern, chunk_start, m_ending_offset,
                              m_case == Case::INSENSITIVE);

        if (result != std::string::npos) {
            m_ending_offset = result;
            terminate();
        } else {
            m_ending_offset = m_starting_offset + m_pattern.length() - 1;
        }

        if (chunk_start == m_starting_offset) {
            terminate();
        }

        // otherwise we still have iterations but we have ended from the chunk
        // assert(chunk_end - m_starting_offset >= m_pattern.size() - 1);
        // set this as the next position to start from next round
        m_ending_offset = chunk_start + m_pattern.size() - 1;

        // we have hit BOF
        if (chunk_start == 0) {
            terminate();
        }

        return result;
    }
};

struct SearchResult {
    std::string m_search_pattern;
    // a global offset for the current main that we have
    size_t m_result_offset;

    std::string_view pattern() const {
        return m_search_pattern;
    }

    bool has_pattern() const {
        return !m_search_pattern.empty();
    }

    size_t offset() const {
        return m_result_offset;
    }

    bool has_result() const {
        return m_result_offset != std::string::npos;
    }

    void clear() {
        m_search_pattern.clear();
        m_result_offset = std::string::npos;
    }
};
