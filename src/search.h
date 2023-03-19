#pragma once

#include <assert.h>
#include <stddef.h>

#define PCRE2_CODE_UNIT_WIDTH 8
#include <pcre2.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
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
// Base class for long running search tasks that need to be scheduled
// Do not use this if you're only trying to search within a page itself

struct SearchResult {
    enum class Type {
        Regular,
        Regex,
    };
    std::string search_pattern;
    size_t found_offset;
    size_t found_length;
    Type search_type;

    std::string pattern() const {
        return search_pattern;
    }

    size_t offset() const {
        return found_offset;
    }

    size_t length() const {
        return found_length;
    }

    Type type() const {
        return search_type;
    }

    bool has_position() const {
        return found_offset != std::string::npos;
    }
};

struct ResultsList {
    std::vector<SearchResult> results;
    size_t idx;

    ResultsList() {
        idx = 0;
    }

    void clear() {
        results.clear();
        idx = 0;
    }

    void push_back(SearchResult res) {
        results.push_back(std::move(res));
    }

    void start_at_front() {
        if (results.empty()) {
            throw std::runtime_error("can't start on empty results list");
        }
        idx = 0;
    }

    void start_at_back() {
        if (results.empty()) {
            throw std::runtime_error("can't start on empty results list");
        }
        idx = results.size() - 1;
    }

    SearchResult get_curr_result() {
        assert(idx < results.size());
        return results[idx];
    }

    void move_to_back() {
        if (results.empty() || idx == results.size() - 1) {
            throw std::runtime_error("can't move further back");
        }
        ++idx;
    }

    void move_to_front() {
        if (results.empty() || idx == 0) {
            throw std::runtime_error("can't move further front");
        }
        --idx;
    }
};

class SearchState {
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

  protected:
    std::string m_pattern;
    size_t m_starting_offset;
    size_t m_ending_offset;
    size_t m_num_iter;
    std::optional<SearchResult> m_found_position;

    Mode m_mode;
    Case m_case;

    bool m_is_running;
    bool m_need_more;

    SearchState(std::string pattern, size_t offset, Mode mode, Case search_case,
                size_t num_iterations)
        : m_pattern(std::move(pattern)),
          m_starting_offset(mode_to_start(mode, offset)),
          m_ending_offset(mode_to_end(mode, offset)),
          m_num_iter(num_iterations), m_mode(mode), m_case(search_case),
          m_is_running(true), m_need_more(false),
          m_found_position(std::nullopt) {
    }

    virtual ~SearchState(){};

  public:
    size_t num_iter() const {
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
    virtual void run(std::string_view contents) = 0;
    virtual bool can_search_more() const = 0;
    virtual bool is_done() const = 0;
    virtual bool has_result() const = 0;
    virtual SearchResult get_result() const = 0;
    virtual std::string pattern() const = 0;
};

class RegularSearch : public SearchState {
    static const size_t SEARCH_BLOCK_SIZE = 4096;
    ResultsList m_results_list;

  public:
    RegularSearch(std::string pattern, size_t offset, Mode mode,
                  Case sensitivity = Case::INSENSITIVE, size_t num_iter = 1)
        : SearchState(std::move(pattern), offset, mode, sensitivity, num_iter) {
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

// wraps the PCRE stuff
class RegexSearch : public SearchState {
    // unlike regular search we'll run on entire chunk sizes
    // and cache results because it's probably
    // cheaper that way (for backwards searching at least)

    struct LineResults {
        struct Result {
            size_t start;
            size_t length;
            bool is_partial;
            bool is_complete;
        };
        std::vector<Result> results;
        std::optional<std::vector<Result>::iterator> curr_result;
        // add api here to increment result_idx

        bool has_it() const {
            return curr_result.has_value();
        }

        size_t size() const {
            return results.size();
        }

        bool empty() const {
            return results.empty();
        }

        void clear() {
            results.clear();
            curr_result = std::nullopt;
        }

        void set_at_begin() {
            if (results.empty()) {
                throw std::runtime_error("can't set at begin on empty list");
            }
            curr_result = results.begin();
        }

        void set_at_end() {
            if (results.empty()) {
                throw std::runtime_error("can't set at end on empty list");
            }
            curr_result = results.end();
            --(*curr_result);
        }

        // might have an issue if it's only a strictly partial match
        Result get_curr_result() const {
            return **curr_result;
        }

        void move_back() {
            if (!has_it()) {
                throw std::runtime_error("can't set move back on an without "
                                         "first setting an iterator");
            }

            --(*curr_result);
        }

        void move_next() {
            if (!has_it()) {
                throw std::runtime_error("can't set move back on an without "
                                         "first setting an iterator");
            }

            ++(*curr_result);
        }

        bool at_first_result() const {
            return curr_result && *curr_result == results.begin();
        }

        bool at_last_result() const {
            return curr_result && *curr_result == (--results.end());
        }

        void push_back_result(size_t offset, size_t length,
                              bool is_partial = false,
                              bool is_complete = false) {
            if (has_it()) {
                throw std::runtime_error(
                    "you should only be pushing back after the current "
                    "iterator has cleared");
            }

            // if we have a match starting at the same offset, we extend it
            if (!results.empty() && results.back().start == offset) {
                results.back().length = length;
                results.back().is_partial = is_partial;
                results.back().is_complete |=
                    is_complete; // we don't want to overwrite a previous match
            } else {
                // else we push a new thing in
                results.push_back({offset, length, is_partial, is_complete});
            }
        }

      private:
        void push_back(size_t offset, size_t length, bool is_partial,
                       bool is_complete) {
            if (has_it()) {
                throw std::runtime_error(
                    "you should only be pushing back after the current "
                    "iterator has cleared");
            }
            results.push_back({offset, length, is_partial, is_complete});
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
    std::optional<LineResults::Result> m_last_best_result;

  public:
    RegexSearch(std::string pattern, Mode mode, size_t offset,
                Case sensitivity = Case::INSENSITIVE, size_t num_iter = 1)
        : SearchState(mode_to_start(mode, offset), mode_to_end(mode, offset),
                      num_iter, true, false),
          m_pattern(std::move(pattern)), m_mode(mode),
          m_last_best_result(std::nullopt) {

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

    // todo: should you be updating result?
    bool needs_more(size_t __attribute__((unused)) content_length) const {
        return (!m_last_best_result || !m_last_best_result->is_complete);
    }

    void yield() {
        m_is_running = false;
    }

    void schedule() {
        m_is_running = true;
    }

    bool can_search_more() const {
        return m_ending_offset > m_starting_offset;
    }

    bool is_done() const {
        return !m_is_running;
    }

    // already has an intermediate result
    bool has_result() const {
        if (m_last_best_result) {
            return true;
        }

        if (!m_line_results.empty()) {
            // if last best result was not set
            // it must be because m_line_results has only one partial match
            assert(m_line_results.empty() == 1);

            LineResults::Result res = m_line_results.get_curr_result();
            assert(!res.is_complete && res.is_partial);

            auto maybe_partial_result = test_partial_result(res);
            return maybe_partial_result.has_value();
        }

        return false;
    }

    // final position here
    bool has_position() const {
        return m_last_best_result.has_value();
    }

    size_t found_position() const {
        if (!m_last_best_result) {
            throw std::runtime_error(
                "found_position: no valid position found.");
        }
        return m_last_best_result->start;
    }

    // pattern
    std::string pattern() const {
        return m_pattern;
    }

    void run(std::string_view contents) {
        switch (m_mode) {
        case Mode::NEXT: {
            size_t truncation_offset =
                std::min(m_starting_offset + 4096, m_ending_offset);
            contents = contents.substr(0, truncation_offset);
            run_next(contents);
            break;
        }
        case Mode::PREV: {
            // decide to do these inside, not
            // need to find the first newline before the chunk
            contents = contents.substr(0, m_ending_offset);
            run_prev(contents);
            break;
        }
        default:
            break;
        }
        yield();
    }

  private:
    std::optional<std::pair<size_t, size_t>>
    test_partial_result(LineResults::Result result) const {
        assert(result.is_partial);

        auto copied_code = pcre2_code_copy(m_compiled_pcre_code);
        auto temp_data =
            pcre2_match_data_create_from_pattern(copied_code, nullptr);

        int match_result =
            pcre2_match(copied_code, (PCRE2_SPTR8) "\0", 1, result.start,
                        PCRE2_NOTBOL | PCRE2_PARTIAL_HARD, temp_data, nullptr);

        if (match_result == PCRE2_ERROR_NOMATCH ||
            match_result == PCRE2_ERROR_PARTIAL) {
            return std::nullopt;
        }
        auto ovector = pcre2_get_ovector_pointer(m_match_data);
        std::pair<size_t, size_t> to_return = {ovector[0], ovector[1]};

        pcre2_code_free(copied_code);
        pcre2_match_data_free(m_match_data);

        return to_return;
    }

    bool update_if_complete() {
        auto curr_res = m_line_results.get_curr_result();
        assert(curr_res.is_complete || curr_res.is_partial);
        if (curr_res.is_complete) {
            m_last_best_result = curr_res;
            return true;
        }
        return false;
    }

    void run_next(std::string_view contents) {

        // there is another result after us
        if (!m_line_results.empty() && !m_line_results.at_last_result()) {
            m_line_results.move_next();
            // test and update the result. return if we got something good
            if (update_if_complete()) {
                return;
            }
        }

        // should only enter this case either:
        // when we are out of results or the last result
        // is not complete.
        // either way, we should only do this on the last result
        assert(m_line_results.at_last_result());

        // we know that we need to get more results
        // make sure we're cleared
        m_line_results.clear();

        // start caching all the results we can find in the current contents
        size_t curr_starting_offset = m_starting_offset;
        fill_results_line(contents, curr_starting_offset, contents.size());

        // update to our new starting offset
        m_starting_offset = contents.size();

        // if we have results, we start a new iterator
        // and get the current result
        if (!m_line_results.empty()) {
            m_line_results.set_at_begin();
            update_if_complete();
        }
    }

    void run_prev(std::string_view contents) {

        // there is another result after us
        if (!m_line_results.empty() && !m_line_results.at_first_result()) {
            m_line_results.move_back();
            // test and update the result. return if we got something good
            if (update_if_complete()) {
                return;
            }
        }

        assert(m_line_results.at_first_result());

        // should be the case that we're right in front of some
        // newl right now or 0
        size_t curr_starting_offset;
        if (m_ending_offset < 4096) {
            curr_starting_offset = m_starting_offset;
        } else {
            curr_starting_offset =
                std::max(m_ending_offset - 4096, m_starting_offset);
        }
        assert(curr_starting_offset == 0 ||
               contents.data()[curr_starting_offset - 1] == '\n');

        if (size_t prev_newl = contents.rfind('\n', curr_starting_offset);
            prev_newl != std::string::npos) {
            curr_starting_offset = prev_newl;
        } else {
            // i dont like doing this
            // but optimise for this case later.
            curr_starting_offset = 0;
        }

        m_line_results.clear();

        // start caching all the results we can find in the current contents
        fill_results_line(contents, curr_starting_offset, contents.size());

        // update m_ending_offset
        // we want to include the newline later on to
        // force only full matches, hence the +1
        m_ending_offset =
            (curr_starting_offset == 0) ? 0 : curr_starting_offset + 1;

        // if we have results, we start a new iterator
        // and get the current result
        if (!m_line_results.empty()) {
            m_line_results.set_at_end();
            update_if_complete();
        }
    }

    void fill_results_line(std::string_view contents, size_t left_bound,
                           size_t right_bound) {

        while (left_bound < right_bound) {
            size_t match_length = right_bound - left_bound;
            // can we just always turn NOTBOL on?
            // need to not match empty string i think
            uint32_t match_options = PCRE2_PARTIAL_HARD | PCRE2_NOTBOL;

            int match_result = pcre2_match(
                m_compiled_pcre_code, (PCRE2_SPTR8)contents.data(),
                match_length, left_bound, match_options, m_match_data, nullptr);

            if (match_result == PCRE2_ERROR_NOMATCH) {
                left_bound = right_bound;
            }

            auto ovector = pcre2_get_ovector_pointer(m_match_data);
            if (match_result == PCRE2_ERROR_PARTIAL) {
                assert(ovector[1] == right_bound);
                m_line_results.push_back_result(
                    ovector[0], ovector[1] - ovector[0], true, false);
            } else {
                m_line_results.push_back_result(
                    ovector[0], ovector[1] - ovector[0], false, true);
            }
            left_bound = ovector[1];
        }
    }
};
