#pragma once

#include <assert.h>
#include <stddef.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <functional>
#include <optional>
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

class SearchState {

  protected:
    size_t m_starting_offset;
    size_t m_ending_offset;
    size_t m_num_iter;

    bool m_is_running;
    bool m_need_more;

    SearchState(size_t starting_offset, size_t ending_offset,
                size_t num_iterations, bool is_running, bool need_more)
        : m_starting_offset(starting_offset), m_ending_offset(ending_offset),
          m_num_iter(num_iterations), m_is_running(is_running),
          m_need_more(need_more) {
    }

    virtual ~SearchState(){};

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

    size_t &num_iter() {
        return m_num_iter;
    }

    static size_t mode_to_start(Mode mode, size_t offset) {
        return (mode == Mode::NEXT) ? offset : 0;
    }

    static size_t mode_to_end(Mode mode, size_t offset) {
        return (mode == Mode::NEXT) ? std::string::npos : offset;
    }

    virtual bool needs_more(size_t content_length) const = 0;
    virtual void yield() = 0;
    virtual void schedule() = 0;
    virtual size_t starting() const = 0;
    virtual void run(std::string_view contents) = 0;
    virtual bool can_search_more() const = 0;
    virtual bool is_done() const = 0;
    virtual bool has_result() const = 0;
    virtual bool has_position() const = 0;
    virtual size_t found_position() const = 0;
    virtual std::string pattern() const = 0;
};

class RegularSearch : public SearchState {
    static const size_t SEARCH_BLOCK_SIZE = 4096;

  public:
  private:
    size_t m_found_position;
    size_t m_current_result;
    // can we merge these two?
    std::string m_pattern;
    Mode m_mode;
    Case m_case;

  public:
    RegularSearch(std::string pattern, Mode mode, size_t offset,
                  Case sensitivity = Case::INSENSITIVE, size_t num_iter = 1)
        : SearchState(mode_to_start(mode, offset), mode_to_end(mode, offset),
                      num_iter, true, false),
          m_found_position(std::string::npos), m_pattern(std::move(pattern)),
          m_mode(mode), m_case(sensitivity) {
        assert(!m_pattern.empty());
        // assert(m_starting_offset < m_ending_offset);
    }
    ~RegularSearch() {
    }

    bool needs_more(size_t content_length) const {
        return (m_mode == Mode::NEXT) &&
               (m_pattern.size() + m_starting_offset > content_length);
    }

    void yield() {
        m_is_running = false;
    }

    void schedule() {
        m_is_running = true;
    }

    size_t starting() const {
        return m_starting_offset;
    }

    // entry point that dispatches the corresponding algorithms
    void run(std::string_view contents) {
        switch (m_mode) {
        case Mode::NEXT:
            m_current_result = find_next(contents);
            if (m_current_result != std::string::npos) {
                m_found_position = m_current_result;
            }
            break;
        case Mode::PREV:
            m_current_result = find_prev(contents);
            if (m_current_result != std::string::npos) {
                m_found_position = m_current_result;
            }
            break;
        default:
            break;
        }
    }

    bool can_search_more() const {
        return m_ending_offset - m_starting_offset >= m_pattern.size();
    }

    bool is_done() const {
        return !m_is_running;
    }

    bool has_result() const {
        return m_current_result != std::string::npos;
    }

    bool has_position() const {
        return m_found_position != std::string::npos;
    }

    size_t found_position() const {
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

        // if the remaining contents can't fit the pattern
        if (m_starting_offset + m_pattern.size() > contents.size()) {
            yield();
            return result;
        }

        // create an ending index that we need to stop our search
        size_t chunk_end =
            std::min(m_starting_offset + RegularSearch::SEARCH_BLOCK_SIZE,
                     contents.size());

        result = basic_search_first(contents, m_pattern, m_starting_offset,
                                    chunk_end, m_case == Case::INSENSITIVE);

        // update the starting offset
        if (result != std::string::npos) {
            // skip by length of pattern
            m_starting_offset = result + m_pattern.length();
            yield();
        } else {
            m_starting_offset = chunk_end - m_pattern.length() + 1;
        }

        // if we've hit the end we can just yield
        if (chunk_end == contents.size()) {
            yield();
        }

        return result;
    }

    size_t find_prev(std::string_view contents) {
        assert(!contents.empty());
        assert(m_starting_offset < contents.size());
        // assert(m_starting_offset < m_ending_offset);

        size_t result = std::string::npos;

        // if the remaining contents can't even fit the pattern
        if (m_ending_offset < m_pattern.size()) {
            yield();
            return result;
        }

        // create an starting index that we need to stop our search
        size_t chunk_start = 0;
        if (m_ending_offset >
            m_starting_offset + RegularSearch::SEARCH_BLOCK_SIZE) {
            chunk_start = m_ending_offset - RegularSearch::SEARCH_BLOCK_SIZE;
        }

        result =
            basic_search_last(contents, m_pattern, chunk_start, m_ending_offset,
                              m_case == Case::INSENSITIVE);

        if (result != std::string::npos) {
            m_ending_offset = result;
            yield();
        } else {
            m_ending_offset = chunk_start + m_pattern.length() - 1;
        }

        // we have hit BOF
        if (chunk_start == 0) {
            yield();
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

    size_t length() const {
        return m_search_pattern.size();
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

// wraps the PCRE stuff
class RegexSearch : public SearchState {

    struct LineResults {
        struct Result {
            size_t start;
            size_t length;
            bool is_partial;
        };
        std::vector<Result> results;
        std::optional<size_t> result_idx;

        // add api here to increment result_idx

        bool at_last_result() const {
            return result_idx && result_idx == results.size() - 1;
        }

        bool has_result() const {
            assert((bool)result_idx == results.empty());
            return results.empty();
        }

        bool empty() const {
            assert((bool)result_idx == results.empty());
            return results.empty();
        }

        void clear() {
            results.clear();
            result_idx = std::nullopt;
        }

        void push_back(size_t start, size_t end, bool is_partial = false) {
            // if we're pushing must make sure the
            // most recent result we're pushing it is not a partial
            assert(!results.empty() || !results.back().is_partial);
            results.push_back({start, end, is_partial});
        }

        void pop_back() {
            results.pop_back();
        }

        void extend_last_result(size_t new_start, size_t new_end,
                                bool is_partial) {
            assert(!results.empty() && results.back().is_partial);
            assert(new_end >= new_start);
            results.back().length += (new_end - new_start);
            results.back().is_partial = is_partial;
        }

        bool last_result_is_partial() const {
            return !results.empty() && results.back().is_partial;
        }

        void push_result(size_t start, size_t end, bool is_partial) {
            if (last_result_is_partial()) {
                extend_last_result(start, end, is_partial);
            } else {
                push_back(start, end, is_partial);
            }
        }

        void inc_result_idx() {
            if (!result_idx) {
                result_idx = 0;
            } else {
                ++result_idx.value();
            }
        }
    };

    std::string m_pattern;
    uint32_t m_pcre_options;
    Mode m_mode;
    int m_pcre_error_code;
    PCRE2_SIZE m_pcre_erroroffset;
    pcre2_code *m_compiled_pcre_code;
    pcre2_match_data *m_match_data;
    LineResults m_line_results;
    size_t m_last_matched_position;
    size_t m_last_matched_length;

  public:
    RegexSearch(std::string pattern, Mode mode, size_t offset,
                Case sensitivity = Case::INSENSITIVE, size_t num_iter = 1)
        : SearchState(mode_to_start(mode, offset), mode_to_end(mode, offset),
                      num_iter, true, false),
          m_pattern(std::move(pattern)), m_mode(mode) {

        m_pcre_options = 0;

        if (sensitivity == Case::INSENSITIVE) {
            m_pcre_options |= PCRE2_CASELESS;
        }

        m_compiled_pcre_code = pcre2_compile(
            (PCRE2_SPTR)m_pattern.c_str(), m_pattern.length(), m_pcre_options,
            &m_pcre_error_code, &m_pcre_erroroffset, nullptr);

        if (m_compiled_pcre_code == nullptr) {
            PCRE2_UCHAR buffer[256];
            pcre2_get_error_message(m_pcre_error_code, buffer, sizeof(buffer));
            printf("PCRE2 compilation failed at offset %d: %s\n",
                   (int)m_pcre_erroroffset, buffer);
            exit(1);
        }

        m_match_data =
            pcre2_match_data_create_from_pattern(m_compiled_pcre_code, nullptr);
    }

    ~RegexSearch() {
        // have to decompile the pattern
        pcre2_code_free(m_compiled_pcre_code);
        pcre2_match_data_free(m_match_data);
    }

    bool needs_more(size_t content_length) const {
        return (m_mode == Mode::NEXT) &&
               m_line_results.last_result_is_partial();
    }

    void yield() {
        m_is_running = false;
    }

    void schedule() {
        m_is_running = true;
    }

    size_t starting() const {
        return m_starting_offset;
    }

    void run(std::string_view contents) {
        // if we can just refer to our precomputed
        // results, we do that, else we need to search

        if (!m_line_results.at_last_result()) {
            m_line_results.inc_result_idx();
            yield();
            return;
        }

        // if not we need to keep searching
        switch (m_mode) {
        case Mode::NEXT: {

            // limit our view to the first 4096 bytes after m_starting_offset
            std::string_view truncated_to_chunk_size =
                contents.substr(0, m_starting_offset + 4096);

            // if our last match has been partial
            if (m_line_results.last_result_is_partial()) {
                // try to extend the matcher
                try_extend_last_match(truncated_to_chunk_size);
            }
            // then we start working on the rest by filling results again
            fill_line_results_forwards(truncated_to_chunk_size);
            break;
        }
        case Mode::PREV: {
            std::string_view truncated_to_chunk_size =
                contents.substr(0, m_ending_offset);
            // because regex needs the entire line,
            // we need to chunk a little weirdly.
            fill_line_results_backwards(truncated_to_chunk_size);
            break;
        }
        default:
            break;
        }

        // then try again to see if we have new results
        if (!m_line_results.at_last_result()) {
            m_line_results.inc_result_idx();
            yield();
            return;
        }
    }

    bool can_search_more() const {
        return m_ending_offset > m_starting_offset;
    }

    bool is_done() const {
        return !m_is_running;
    }

    // already has an intermediate result
    bool has_result() const {
        return m_line_results.empty();
    }

    // final position here
    bool has_position() const {
        return m_line_results.empty();
    }

    size_t found_position() const {
        return m_line_results.results.back().start;
    }

    // pattern
    std::string pattern() const {
        return m_pattern;
    }

  private:
    void try_extend_last_match(std::string_view contents) {
        // only call this is the last result is a partial match
        assert(m_line_results.last_result_is_partial());
        // the partial match should be pointing at our current starting offset
        // for the new search
        assert(!m_line_results.empty() &&
               (m_line_results.results.back().start +
                    m_line_results.results.back().length ==
                m_starting_offset));

        size_t search_left_bound = m_starting_offset;
        size_t first_newl = contents.find('\n', search_left_bound);
        size_t search_length = (first_newl == std::string::npos)
                                   ? contents.size() - search_left_bound
                                   : first_newl - search_left_bound;
        size_t search_result =
            get_result_line(contents, search_left_bound, search_length,
                            PCRE2_PARTIAL_HARD | PCRE2_NOTBOL);

        m_starting_offset = (search_result == std::string::npos)
                                ? contents.size()
                                : search_result;
    }

    void fill_line_results_forwards(std::string_view contents) {
        if (m_starting_offset > 0 &&
            contents.data()[m_starting_offset - 1] == '\n' &&
            m_line_results.last_result_is_partial()) {
            // remove the last partial result if we're starting on a new line
            m_line_results.pop_back();
        }

        size_t search_left_bound = m_starting_offset;
        size_t search_result;
        while (search_left_bound < contents.size()) {
            size_t first_newl = contents.find('\n', search_left_bound);

            if (first_newl == std::string::npos) {
                search_result = get_result_line(
                    contents, search_left_bound,
                    contents.size() - search_left_bound, PCRE2_PARTIAL_HARD);
                break;
            } else {
                search_result = get_result_line(contents, search_left_bound,
                                                first_newl - search_left_bound,
                                                PCRE2_PARTIAL_HARD);
                search_left_bound = (search_result == std::string::npos)
                                        ? contents.size()
                                        : search_result;
            }
        }

        m_starting_offset = search_left_bound;
    }

    void fill_line_results_backwards(std::string_view contents) {
        // make sure it's been truncated properly
        assert(contents.size() == m_ending_offset);

        if (m_line_results.last_result_is_partial()) {
            // remove the last partial result since we're matching backwards
            m_line_results.pop_back();
        }

        if (m_ending_offset == 0) {
            return;
        }

        // prep the left boundary
        size_t chunk_left_idx =
            (m_ending_offset >= 4096) ? m_ending_offset - 4096 : 0;
        chunk_left_idx = std::max(chunk_left_idx, m_starting_offset);

        size_t search_right_bound = m_ending_offset;
        size_t search_result;
        while (search_right_bound > chunk_left_idx) {
            size_t first_newl = contents.rfind('\n', search_right_bound - 1);
            if (first_newl == std::string::npos) {
                search_result =
                    get_result_line(contents, chunk_left_idx,
                                    search_right_bound - chunk_left_idx);
                break;
            } else {
                search_result =
                    get_result_line(contents, first_newl + 1,
                                    search_right_bound - (first_newl + 1));
                search_right_bound = (search_result == std::string::npos)
                                         ? chunk_left_idx
                                         : first_newl;
            }
        }

        m_ending_offset = search_right_bound;
    }

    // returns the last index (exclusive) for which the matcher has either
    // matched something or consumed
    size_t get_result_line(std::string_view contents, size_t starting_offset,
                           size_t search_length, uint32_t match_options = 0) {
        int res = pcre2_match(
            m_compiled_pcre_code, (PCRE2_SPTR8)contents.data(), search_length,
            0, match_options | PCRE2_NOTEMPTY, m_match_data, nullptr);

        // in the case of no matches
        if (res == PCRE2_ERROR_NOMATCH || res == 1) {
            return std::string::npos;
        }

        auto ovector = pcre2_get_ovector_pointer(m_match_data);
        // partial match occured
        if (res == PCRE2_ERROR_PARTIAL) {
            m_line_results.push_result(ovector[0], ovector[1], true);
            return std::string::npos;
        } else {
            assert(res == 3);
            m_line_results.push_result(ovector[0], ovector[1], false);
            return ovector[1];
        }
    }
};