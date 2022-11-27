#pragma once

#include <assert.h>
#include <cctype>
#include <charconv>
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
        {
            // read from the getch buffer first instead of polling
            std::scoped_lock lock(*nc_mutex);
            int read_val = getch();
            if (read_val != ERR) {
                return read_val;
            }
        }

        // if we hit this point the nc buffer was empty
        poll(pollfds, 1, -1);
        std::scoped_lock lock(*nc_mutex);
        return getch();
    }

    void multi_char_search(size_t num_payload) {
        while (true) {
            int key = poll_and_getch();
            switch (key) {
            case KEY_LEFT:
                cursor_pos -= (cursor_pos > 1) ? 1 : 0;
                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {}, cursor_pos});
                break;
            case KEY_RIGHT:
                cursor_pos += (cursor_pos < pattern_buf.size()) ? 1 : 0;
                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {}, cursor_pos});
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
                chan->push({Command::BUFFER_CURS_POS, "", {}, cursor_pos});

                break;
            case KEY_ENTER:
            case '\n':
                break;
            default:
                // everything else goes into the buffer
                pattern_buf.insert(cursor_pos++, 1, (char)key);

                chan->push({Command::SEARCH_START, pattern_buf});
                chan->push({Command::BUFFER_CURS_POS, "", {}, cursor_pos});

                break;
            }

            if (cursor_pos == 0) {
                chan->push({Command::SEARCH_QUIT, ""});
                break;
            }

            if (key == KEY_ENTER || key == '\n') {
                chan->push(
                    {Command::SEARCH_EXEC, pattern_buf, {}, num_payload});
                break;
            }
        }

        return;
    }

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
                chan->push({Command::DISPLAY_COMMAND, ":"s + num_payload_buf});
                continue;
            } else {
                num_payload_buf = "";
            }

            switch (ch) {
            case 'q':
                chan->push({Command::QUIT});
                return; // Kill input thread
            case 'j':
            case KEY_DOWN:
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_DOWN, "", {}, num_payload});
                break;
            case 'k':
            case KEY_UP:
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_UP, "", {}, num_payload});
                break;
            case 'f':
            case CTRL('f'):
            case CTRL('v'):
            case ' ':
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_DOWN_PAGE, "", {}, num_payload});
                break;
            case 'b':
            case CTRL('b'):
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_UP_PAGE, "", {}, num_payload});
                break;
            case 'z':
                chan->push({Command::DISPLAY_COMMAND, ":"});
                if (num_payload != 0) {
                    chan->push({Command::SET_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_DOWN_PAGE});
                break;
            case 'w':
                chan->push({Command::DISPLAY_COMMAND, ":"});
                if (num_payload != 0) {
                    chan->push({Command::SET_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_UP_PAGE});
                break;
            case 'd':
            case CTRL('d'):
                chan->push({Command::DISPLAY_COMMAND, ":"});
                if (num_payload != 0) {
                    chan->push(
                        {Command::SET_HALF_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_DOWN_HALF_PAGE});
                break;
            case 'u':
            case CTRL('u'):
                chan->push({Command::DISPLAY_COMMAND, ":"});
                if (num_payload != 0) {
                    chan->push(
                        {Command::SET_HALF_PAGE_SIZE, "", {}, num_payload});
                }
                chan->push({Command::VIEW_UP_HALF_PAGE});
                break;
            case 'g':
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_BOF});
                break;
            case 'G':
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push({Command::VIEW_EOF});
                break;
            case '/': {
                pattern_buf = "/";
                cursor_pos = 1;

                // start search mode in main
                chan->push({Command::SEARCH_START, pattern_buf});

                // send it cursor position 0
                chan->push({Command::BUFFER_CURS_POS, "", {}, cursor_pos});
                // now we start processing input in multi char search mode
                multi_char_search(num_payload);
                break;
            }
            case 'n': // this needs to work with search history eventually;
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push(
                    {Command::SEARCH_NEXT, pattern_buf, {}, num_payload});
                break;
            case 'N': // this needs to work with search history eventually;
                chan->push({Command::DISPLAY_COMMAND, ":"});
                chan->push(
                    {Command::SEARCH_PREV, pattern_buf, {}, num_payload});
                break;
            case 27: {
                int opt = poll_and_getch();
                // Set ESC option
                switch (opt) {
                case 'U':
                    chan->push({Command::DISPLAY_COMMAND, ":"});
                    chan->push({Command::SEARCH_CLEAR, "ESC-U"});
                    break;
                case 'u':
                    chan->push({Command::DISPLAY_COMMAND, ":"});
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
                    chan->push({Command::DISPLAY_COMMAND, ":"});
                    chan->push({Command::TOGGLE_CASELESS, "-I"});
                    break;
                case 'i':
                    chan->push({Command::DISPLAY_COMMAND, ":"});
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
