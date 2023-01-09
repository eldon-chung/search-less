#pragma once

#include <functional>
#include <stddef.h>
#include <string>
#include <string_view>
#include <vector>

#include "Page.h"

class SearchState {
  public:
    enum class Case {
        SENSITIVE,
        CONDITIONALLY_SENSITIVE,
        INSENSITIVE,
    };

  private:
    Case m_case_mode = Case::SENSITIVE;
    // eventually this might be a regex thing
    std::string m_search_pattern = "";
    size_t m_offset = 0;
    bool m_is_active = false;
    size_t m_iter = 1;

    // some extra stuff that we need to make pages with
    // because search's behaviour actually depends on
    // the screen dimensions
    Page m_page;

  public:
    std::string_view search_pattern() const {
        return m_search_pattern;
    }

    Case mode() const {
        return m_case_mode;
    }

    size_t offset() const {
        return m_offset;
    }

    size_t iter() const {
        return m_iter;
    }

    Page page() const {
        return m_page;
    }

    Case &mode() {
        return m_case_mode;
    }

    bool is_active() const {
        return m_is_active;
    }

    void set_search_pattern(std::string search_pattern) {
        m_search_pattern = std::move(search_pattern);
    }

    void clear_search_pattern() {
        m_search_pattern.clear();
    }

    void set_offset(size_t offset) {
        m_offset = offset;
    }

    void set_active() {
        m_is_active = true;
    }

    void set_iter(size_t iter) {
        m_iter = iter;
    }

    void unset_active() {
        m_is_active = false;
    }
};

std::vector<size_t> basic_search_all(std::string_view file_contents,
                                     std::string_view pattern,
                                     size_t beginning_offset,
                                     size_t ending_offset,
                                     bool caseless /* = false */);

size_t basic_search_first(std::string_view file_contents,
                          std::string_view pattern, size_t beginning_offset,
                          size_t ending_offset, bool caseless /* = false */);

size_t basic_search_last(std::string_view file_contents,
                         std::string_view pattern, size_t beginning_offset,
                         size_t ending_offset, bool caseless /* = false */);

// void search_first_n(std::string_view contents, SearchState &search_state);