#pragma once

#include <algorithm>
#include <cassert>
#include <deque>
#include <ncurses.h>
#include <string>
#include <vector>

#include "Model.h"

class WindowBorder {
    int m_starting_col;
    int m_starting_row;
    int m_height;
    int m_width;

  public:
    WindowBorder(int height, int width)
        : m_starting_col(0), m_starting_row(0), m_height(height),
          m_width(width) {
    }

    int starting_row() const {
        return m_starting_row;
    }

    int ending_row() const {
        return m_starting_row + m_height;
    }

    int starting_col() const {
        return m_starting_col;
    }

    int ending_col() const {
        return m_starting_col + m_width;
    }

    int height() const {
        return m_height;
    }

    int width() const {
        return m_width;
    }

    void move_right(int delta) {
        m_starting_row += delta;
    }

    void move_left(int delta) {
        // should really throw an exception
        assert(m_starting_row >= delta);
        m_starting_row -= delta;
    }

    void move_up(int delta) {
        // should really throw an exception
        assert(m_starting_col >= delta);
        m_starting_col -= delta;
    }

    void move_down(int delta) {
        m_starting_col += delta;
    }

    void chase_point(int row, int col) {
        if (col >= m_starting_col + m_width) {
            m_starting_col = col + 1 - m_width;
        } else if (col < m_starting_col) {
            m_starting_col = col;
        }

        if (row >= m_starting_row + m_height) {
            m_starting_row = row + 1 - m_height;
        } else if (row < m_starting_row) {
            m_starting_row = row;
        }
    }
};

struct TextWindow {
    WINDOW *m_window_ptr;
    std::vector<std::string> m_lines;
    // left_boundary
    size_t m_left_boundary;
    // height of the screen
    size_t m_num_rows;
    // width of the screen
    size_t m_num_cols;

    // if the boundaries are not defined, we delegate its construction
    TextWindow(WINDOW *window_ptr, size_t num_rows, size_t num_cols)
        : TextWindow(window_ptr, num_rows, num_cols, 0) {
    }

    TextWindow(WINDOW *window_ptr, size_t num_rows, size_t num_cols,
               size_t left_boundary)
        : m_window_ptr(window_ptr), m_left_boundary(left_boundary),
          m_num_rows(num_rows), m_num_cols(num_cols) {
        for (size_t row = 0; row < m_num_rows; row++) {
            m_lines.push_back(std::string("~"));
        }
    }

    void update(std::vector<std::string> &&new_contents) {
        assert(new_contents.size() == m_num_rows);
        m_lines.clear();
        m_lines = std::move(new_contents);
    }

    void render() {
        // should this be shifted into update?
        // then render just calls the rendering stuff;
        assert(m_lines.size() == m_num_rows);
        // clear the current screen
        werase(m_window_ptr);

        // get a "string_view" for each row and place it onto the screen
        for (size_t row_idx = 0; row_idx < m_num_rows; row_idx++) {
            mvwaddstr(m_window_ptr, row_idx, 0, m_lines.at(row_idx).c_str());
        }

        // place the attributes on the screen
        wstandend(m_window_ptr);
        // for (size_t row_idx = 0; row_idx < m_num_rows; row_idx++) {
        //     for (std::String const &tag : m_lines.at(row_idx).get_tags()) {
        //         mvwchgat(m_window_ptr, row_idx, tag.m_start_pos,
        //         tag.length(),
        //                  (attr_t)tag.m_attribute, (short)tag.m_colour, NULL);
        //     }
        // }

        wrefresh(m_window_ptr);
    }

    size_t height() const {
        return m_num_rows;
    }

    size_t get_line_length_at(size_t index) const {
        return m_lines.at(index).size();
    }
};

// The driver class that drives the TextWindow struct
class TextWidget {
    Model const *m_model;
    TextWindow m_text_window;
    WindowBorder m_text_window_border;

  public:
    TextWidget(Model const *model, WINDOW *main_window_ptr, int height,
               int width)
        : m_model(model), m_text_window(main_window_ptr, height, width),
          m_text_window_border(height, width) {
    }

    ~TextWidget() {
    }
    TextWidget(TextWidget const &) = delete;
    TextWidget &operator=(TextWidget const &) = delete;
    TextWidget(TextWidget &&) = delete;
    TextWidget &operator=(TextWidget &&) = delete;

    void render() {
        m_text_window.render();
    }

    void update_state() {
        // get the relevant strings within the rows of the current border
        std::vector<std::string> lines_in_window;
        lines_in_window.reserve(m_text_window_border.height());
        for (size_t row_idx = m_text_window_border.starting_row();
             row_idx < (size_t)m_text_window_border.ending_row() &&
             row_idx < m_model->num_lines();
             ++row_idx) {
            lines_in_window.push_back(m_model->get_line_at(row_idx));
        }
        // pad it so that we have the correct amount
        while (lines_in_window.size() < m_text_window.height()) {
            lines_in_window.push_back("~");
        }

        // do some word wrapping here?
        // cut out the text based on the box
        auto cut_out_line = [](std::string &line_to_cut,
                               WindowBorder const &window_border) {
            line_to_cut = line_to_cut.substr(window_border.starting_col(),
                                             window_border.width());
        };

        // move the altered text into the text window
        m_text_window.update(std::move(lines_in_window));
    }
};