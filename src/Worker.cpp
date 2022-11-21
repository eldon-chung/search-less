#include "Worker.h"

void compute_line_offsets(std::stop_token stop, Channel<Command> *chan,
                          std::promise<void> &promise,
                          std::string_view contents, size_t starting_offset) {
    std::vector<size_t> offset_list;

    const char *start_ptr = contents.data();
    const char *end_ptr = start_ptr + contents.length();
    while (start_ptr < end_ptr && !stop.stop_requested()) {
        const char *chunk_end = start_ptr;
        chunk_end += std::min(end_ptr - start_ptr, (ssize_t)1 << 20);

        while (start_ptr < end_ptr) {
            if (*start_ptr == '\n') {
                offset_list.push_back(starting_offset);
            }
            start_ptr++;
            starting_offset++;
        }
    }

    // lastly, append the number of processed bytes
    offset_list.push_back(starting_offset);

    chan->push(Command{.type = Command::UPDATE_LINE_IDXS,
                       .payload_str = "clo read byte(s):" +
                                      std::to_string(starting_offset),
                       .payload_nums = offset_list});
    promise.set_value();
}
