#pragma once

#include <assert.h>
#include <cctype>
#include <fcntl.h>
#include <mutex>
#include <ncurses.h>
#include <poll.h>
#include <semaphore>
#include <signal.h>
#include <thread>

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>

#include "Channel.h"
#include "Command.h"

enum KeyType {
    ARROW,
    ALPHA,
    SPACE,
    NUMBER,
    OTHER,
};

struct InputThread {
    std::mutex *nc_mutex;
    Channel<Command> *chan;
    std::thread t;
    struct pollfd pollfds[1];

    std::string pattern_buf = "";
    uint16_t cursor_pos = 0;

    std::binary_semaphore fd_ready;

    InputThread(std::mutex *nc_mutex, Channel<Command> *chan)
        : nc_mutex(nc_mutex), chan(chan), fd_ready(0) {
        pollfds[0].fd = open("/dev/tty", O_RDONLY);
        pollfds[0].events = POLLIN;

        t = std::thread(&InputThread::start, this);
    }

    InputThread(const InputThread &) = delete;
    InputThread &operator=(const InputThread &) = delete;
    InputThread(InputThread &&) = delete;
    InputThread &operator=(InputThread &&) = delete;
    ~InputThread() {
        t.join();
    }

    int poll_and_getch() {
        poll(pollfds, 1, -1);
        std::scoped_lock lock(*nc_mutex);
        return getch();
    }

    // blocking getch. only do this if you expect the next input to come in
    // soon.
    int force_getch() {
        std::scoped_lock lock(*nc_mutex);
        return getch();
    }

    void multi_char_search() {
        while (true) {
            int key = poll_and_getch();
            switch (key) {
            case KEY_LEFT:
                cursor_pos -= (cursor_pos > 1) ? 1 : 0;
                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {cursor_pos}});
                break;
            case KEY_RIGHT:
                cursor_pos += (cursor_pos < pattern_buf.size()) ? 1 : 0;
                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {cursor_pos}});
                break;
            case KEY_UP:
            case KEY_DOWN:
                using namespace std::string_literals;
                chan->push({Command::DISPLAY_COMMAND,
                            "We aren't handling search history yet."s});
                break;
            case KEY_BACKSPACE:
                pattern_buf.erase(--cursor_pos, 1);
                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {cursor_pos}});

                break;
            case KEY_ENTER:
            case '\n':
                break;
            default:
                // everything else goes into the buffer
                pattern_buf.insert(cursor_pos++, 1, (char)key);

                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {cursor_pos}});

                break;
            }

            if (cursor_pos == 0) {
                chan->push({Command::SEARCH_QUIT, ""});
                break;
            }

            if (key == KEY_ENTER || key == '\n') {
                chan->push({Command::SEARCH_EXEC, pattern_buf});
                break;
            }
        }

        return;
    }

    void start() {
        while (true) {
            int ch = poll_and_getch();

            switch (ch) {
            case 'q':
                chan->push({Command::QUIT});
                return; // Kill input thread
            case 'j':
            case KEY_DOWN:
                chan->push({Command::VIEW_DOWN});
                break;
            case 'k':
            case KEY_UP:
                chan->push({Command::VIEW_UP});
                break;
            case 'g':
                chan->push({Command::VIEW_BOF});
                break;
            case 'G':
                chan->push({Command::VIEW_EOF});
                break;
            case '/': {
                pattern_buf = "/";
                cursor_pos = 1;

                // start search mode in main
                chan->push({Command::SEARCH_START, pattern_buf});

                // send it cursor position 0
                chan->push({Command::BUFFER_CURS_POS, "", {cursor_pos}});
                // now we start processing input in multi char search mode
                multi_char_search();
                break;
            }
            case 'n': // this needs to work with search history eventually;
                chan->push({Command::SEARCH_NEXT, pattern_buf});
                break;
            case 'N': // this needs to work with search history eventually;
                chan->push({Command::SEARCH_PREV, pattern_buf});
                break;
            case 27: {
                int opt = force_getch();
                // Set ESC option
                switch (opt) {
                case 'U':
                    chan->push({Command::SEARCH_CLEAR, "ESC-U"});
                    break;
                case 'u':
                    chan->push({Command::TOGGLE_HIGHLIGHTING, "ESC-u"});
                    break;
                default:
                    using namespace std::string_literals;
                    chan->push({Command::DISPLAY_COMMAND,
                                "Unknown option: ESC-"s + keyname(opt)});
                    break;
                }
                break;
            }
            case '-': {
                chan->push({Command::DISPLAY_COMMAND, "Set option: -"});
                // Set option
                int opt = poll_and_getch();
                switch (opt) {
                case 'I':
                    chan->push({Command::TOGGLE_CASELESS, "-I"});
                    break;
                case 'i':
                    chan->push({Command::TOGGLE_CONDITIONALLY_CASELESS, "-i"});
                    break;
                default:
                    using namespace std::string_literals;
                    chan->push({Command::DISPLAY_COMMAND,
                                "Unknown option: -"s + keyname(opt)});
                    break;
                }
                break;
            }
            default:
                chan->push({Command::INVALID, keyname(ch)});
                break;
            }
        }
    }
};

void register_for_sigwinch_channel(Channel<Command> *to_register);
