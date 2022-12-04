#include <stdio.h>

#include <readline/history.h>
#include <readline/readline.h>

#include "Command.h"
#include "Input.h"

Channel<Command> *command_channel;

void handle_sigwinch(int) {
    rl_resize_terminal();
    command_channel->push_signal(Command{Command::RESIZE});
}

void register_for_sigwinch_channel(Channel<Command> *to_register) {
    command_channel = to_register;
    signal(SIGWINCH, handle_sigwinch);
}

std::optional<std::string> readline_result;

void InputThread::multi_char_search(size_t num_payload) {
    readline_result = std::nullopt;
    rl_tty_set_echoing(0);
    rl_redisplay_function = []() {
        command_channel->push(Command{Command::SEARCH_START,
                                      std::string("/") + rl_line_buffer,
                                      {},
                                      (size_t)(rl_point + 1)});
    };
    rl_persistent_signal_handlers = 1;
    rl_bind_key('\t', rl_insert);
    bool is_in_escape_mode = false;
    rl_callback_handler_install("/", [](char *input) {
        if (input) {
            readline_result = input;
        } else {
            readline_result = "";
        }
        rl_callback_handler_remove();
    });
    rl_cleanup_after_signal();
    while (!readline_result) {
        poll();
        char c;
        read(devttyfd, &c, 1);
        if (is_in_escape_mode) {
            is_in_escape_mode = false;
            rl_stuff_char(c);
            rl_callback_read_char();
            continue;
        }
        if (c == '\x16') {
            // This is ^V, someone wants to put a literal keystroke in
            is_in_escape_mode = true;
            rl_stuff_char(c);
            rl_callback_read_char();
            continue;
        }
        // not in escape mode

        if (c == '\x03') {
            // This is ^C, let's exit
            readline_result = "";
            rl_callback_handler_remove();
            continue;
        }

        // backspace on empty buffer
        if (c == '\x7f' && *rl_line_buffer == '\0') {
            readline_result = "";
            rl_callback_handler_remove();
            break;
        }

        rl_stuff_char(c);
        rl_callback_read_char();
    }

    if (readline_result->empty()) {
        command_channel->push(Command{Command::SEARCH_QUIT});
    } else {
        add_history(readline_result->c_str());
        command_channel->push(Command{Command::SEARCH_EXEC,
                                      std::move(*readline_result),
                                      {},
                                      num_payload});
    }
}
