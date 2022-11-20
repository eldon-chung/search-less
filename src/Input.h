#pragma once

#include <fcntl.h>
#include <mutex>
#include <ncurses.h>
#include <poll.h>
#include <signal.h>
#include <thread>

#include "Channel.h"
#include "Command.h"

struct InputThread {
    std::mutex *nc_mutex;
    Channel<Command> *chan;
    std::thread t;
    struct pollfd pollfds[1];

    InputThread(std::mutex *nc_mutex, Channel<Command> *chan)
        : nc_mutex(nc_mutex), chan(chan), t(&InputThread::start, this) {
        pollfds[0].fd = open("/dev/tty", O_RDONLY);
        pollfds[0].events = POLLIN;
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
            case '/':
                chan->push({Command::SEARCH, "gigachad"});
                break;
            case 'n':
                chan->push({Command::SEARCH_NEXT, "break"});
                break;
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
