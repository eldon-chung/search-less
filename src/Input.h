#pragma once

#include <assert.h>
#include <cctype>
#include <charconv>
#include <curses.h>
#include <fcntl.h>
#include <mutex>
#include <ncurses.h>
#include <poll.h>
#include <semaphore>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <string>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <thread>

#include "Channel.h"
#include "Command.h"

struct InputThread {
    std::mutex *nc_mutex;
    Channel<Command> *chan;
    std::thread t;
    int devttyfd;
    struct pollfd pollfds[1];

    std::string pattern_buf = "";
    uint16_t cursor_pos = 0;
    std::string history_filename;
    int history_maxsize;

    std::binary_semaphore fd_ready;

    InputThread(std::mutex *nc_mutex, Channel<Command> *chan, FILE *tty,
                std::string history_filename, int history_maxsize);

    InputThread(const InputThread &) = delete;
    InputThread &operator=(const InputThread &) = delete;
    InputThread(InputThread &&) = delete;
    InputThread &operator=(InputThread &&) = delete;
    ~InputThread() {
        t.join();
    }

    void poll() {
        ::poll(pollfds, 1, -1);
    }

    int poll_and_getch() {
        {
            // read from the getch buffer first instead of polling
            std::scoped_lock lock(*nc_mutex);
            int read_val = getch();
            if (read_val != ERR) {
                return read_val;
            }
        }

        // if we hit this point the nc buffer was empty
        poll();
        std::scoped_lock lock(*nc_mutex);
        return getch();
    }

    void multi_char_search(size_t num_payload);

    void start() {
        std::string num_payload_buf;

        while (true) {
            int ch = poll_and_getch();

            size_t num_payload = 0;
            // if any error happens during conversion, num_payload is unmodified
            std::from_chars(num_payload_buf.data(),
                            num_payload_buf.data() + num_payload_buf.length(),
                            num_payload);

            if ('0' <= ch && ch <= '9') {
                using namespace std::string_literals;
                num_payload_buf.push_back((char)ch);
                chan->push({Command::DISPLAY_COMMAND,
                            ":"s + num_payload_buf,
                            {},
                            num_payload_buf.size() + 1});
                continue;
            } else if (ch == KEY_BACKSPACE) {
                if (num_payload_buf.empty()) {
                    chan->push({Command::DISPLAY_COMMAND, "", {}, 0});
                    continue;
                } else {
                    using namespace std::string_literals;
                    num_payload_buf.pop_back();
                    chan->push({Command::DISPLAY_COMMAND,
                                ":"s + num_payload_buf,
                                {},
                                num_payload_buf.size() + 1});
                    continue;
                }
            } else {
                num_payload_buf = "";
            }

            switch (ch) {
            case KEY_RESIZE:
                chan->push(Command{Command::RESIZE});
                break;
            case 'q':
                chan->push({Command::QUIT});
                // return; // Kill input thread
                return;
            case 'j':
            case KEY_DOWN:
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_DOWN, "", {}, num_payload});
                break;
            case 'k':
            case KEY_UP:
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_UP, "", {}, num_payload});
                break;
            case 'F':
                chan->push({Command::FOLLOW_EOF, "", {}, 1});
                // wait for main to break us out
                while (poll_and_getch() != 69420) {
                    ; // only main can break us out
                }
                break;
            case 'f':
            case CTRL('f'):
            case CTRL('v'):
            case ' ':
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_DOWN_PAGE, "", {}, num_payload});
                break;
            case 'b':
            case CTRL('b'):
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_UP_PAGE, "", {}, num_payload});
                break;
            case 'z':
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                if (num_payload != 0) {
                    chan->push({Command::SET_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_DOWN_PAGE});
                break;
            case 'w':
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                if (num_payload != 0) {
                    chan->push({Command::SET_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_UP_PAGE});
                break;
            case 'd':
            case CTRL('d'):
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                if (num_payload != 0) {
                    chan->push(
                        {Command::SET_HALF_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_DOWN_HALF_PAGE});
                break;
            case 'u':
            case CTRL('u'):
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                if (num_payload != 0) {
                    chan->push(
                        {Command::SET_HALF_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_UP_HALF_PAGE});
                break;
            case 'g':
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_BOF});
                break;
            case 'G':
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push({Command::VIEW_EOF});
                break;
            case '/': {
                pattern_buf = "/";
                cursor_pos = 1;

                // start search mode in main
                chan->push({Command::SEARCH_START, pattern_buf, {}, 0});
                // now we start processing input in multi char search mode
                multi_char_search(num_payload);
                break;
            }
            case 'n': // this needs to work with search history eventually;
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push(
                    {Command::SEARCH_NEXT, pattern_buf, {}, num_payload});
                break;
            case 'N': // this needs to work with search history eventually;
                chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                chan->push(
                    {Command::SEARCH_PREV, pattern_buf, {}, num_payload});
                break;
            case 27: {
                int opt = poll_and_getch();
                // Set ESC option
                switch (opt) {
                case 'U':
                    chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                    chan->push({Command::SEARCH_CLEAR, "ESC-U"});
                    break;
                case 'u':
                    chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                    chan->push({Command::TOGGLE_HIGHLIGHTING, "ESC-u"});
                    break;
                default:
                    using namespace std::string_literals;
                    chan->push({Command::DISPLAY_STATUS,
                                "Unknown option: ESC-"s + keyname(opt)});
                    break;
                }
                break;
            }
            case '-': {
                chan->push({Command::DISPLAY_COMMAND, "Set option: -", {}, 13});
                // Set option
                int opt = poll_and_getch();
                switch (opt) {
                case 'I':
                    chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                    chan->push({Command::TOGGLE_CASELESS, "-I"});
                    break;
                case 'i':
                    chan->push({Command::DISPLAY_COMMAND, ":", {}, 1});
                    chan->push({Command::TOGGLE_CONDITIONALLY_CASELESS, "-i"});
                    break;
                default:
                    using namespace std::string_literals;
                    chan->push({Command::DISPLAY_STATUS,
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

void register_signal_handlers(Channel<Command> *to_register);
