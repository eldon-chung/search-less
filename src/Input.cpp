#include <stdio.h>

#include <errno.h>
#include <fcntl.h>
#include <optional>
#include <readline/history.h>
#include <readline/readline.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

#include "Command.h"
#include "Input.h"

Channel<Command> *command_channel;

void handle_sigwinch(int) {
    command_channel->push_signal(Command{Command::RESIZE});
}

void handle_sigint(int) {
    command_channel->push_signal(Command{Command::INTERRUPT});
}

void register_signal_handlers(Channel<Command> *to_register) {
    command_channel = to_register;
    signal(SIGWINCH, handle_sigwinch);
    signal(SIGINT, handle_sigint);
}

InputThread::InputThread(std::mutex *nc_mutex, Channel<Command> *chan,
                         FILE *tty, std::string _history_filename,
                         int history_maxsize)
    : nc_mutex(nc_mutex), chan(chan),
      history_filename(std::move(_history_filename)),
      history_maxsize(history_maxsize), fd_ready(0) {
    if (read_history(history_filename.c_str())) {
        struct stat stats;
        int res = stat(history_filename.c_str(), &stats);
        if (res == -1 && errno == ENOENT) {
            // read_history because the history file doesn't exist, create it
            // and try again.
            // create file with mode 600 (octal)
            int fd = creat(history_filename.c_str(), 0600);
            if (fd != -1) {
                close(fd);
            }
            if (read_history(history_filename.c_str())) {
                // creating the file didn't help
                chan->push(
                    Command{Command::DISPLAY_STATUS,
                            "There was a problem reading the history file, "
                            "not using history this session."});
                history_filename.clear();
            }
        } else {
            // stat worked or errored for some OTHER reason.
            // this is unusual because the read_history call failed, so we're
            // just going to give up.
            chan->push(Command{Command::DISPLAY_STATUS,
                               "There was a problem reading the history file, "
                               "not using history this session."});
            history_filename.clear();
        }
    }
    devttyfd = fileno(tty);
    pollfds[0].fd = devttyfd;
    pollfds[0].events = POLLIN;

    t = std::thread(&InputThread::start, this);
}

std::optional<std::string> readline_result;

void InputThread::multi_char_search(size_t num_payload) {
    static int blockingpipefds[2];
    pipe(blockingpipefds);
    static FILE *blockingpipe = fdopen(blockingpipefds[0], "r");
    readline_result = std::nullopt;
    {
        std::scoped_lock lock{*nc_mutex};
        rl_change_environment = 0;
        rl_tty_set_echoing(0);
        rl_instream = blockingpipe;
        rl_redisplay_function = []() {
            command_channel->push(Command{Command::SEARCH_START,
                                          std::string("/") + rl_line_buffer,
                                          {},
                                          (size_t)(rl_point + 1)});
        };
        rl_persistent_signal_handlers = 1;
        rl_bind_key('\t', rl_insert);
        rl_callback_handler_install("/", [](char *input) {
            if (input) {
                readline_result = input;
            } else {
                readline_result = "";
            }
            rl_callback_handler_remove();
        });
        rl_cleanup_after_signal();
    }
    bool is_in_escape_mode = false;
    while (!readline_result) {
        char c;
        read(devttyfd, &c, 1);
        {
            std::scoped_lock lock{*nc_mutex};
            rl_resize_terminal();
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
    }

    if (readline_result->empty()) {
        command_channel->push(Command{Command::SEARCH_QUIT});
    } else {
        add_history(readline_result->c_str());
        append_history(1, history_filename.c_str());
        history_truncate_file(history_filename.c_str(), history_maxsize);
        command_channel->push(Command{Command::SEARCH_EXEC,
                                      std::move(*readline_result),
                                      {},
                                      num_payload});
    }
}
