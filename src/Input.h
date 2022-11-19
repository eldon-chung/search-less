#pragma once

#include <fcntl.h>
#include <mutex>
#include <ncurses.h>
#include <poll.h>
#include <thread>

#include "Channel.h"
#include "Command.h"

struct InputThread {
    std::thread t;

    InputThread(std::mutex *nc_mutex, Channel<Command> *chan)
        : t(&InputThread::start, this, nc_mutex, chan) {
    }

    InputThread(const InputThread &) = delete;
    InputThread &operator=(const InputThread &) = delete;
    InputThread(InputThread &&) = delete;
    InputThread &operator=(InputThread &&) = delete;
    ~InputThread() {
        t.join();
    }

    void start(std::mutex *nc_mutex, Channel<Command> *chan) {
        // We'll use this fd to poll for input, but not actually read from it
        // ever.
        struct pollfd fds[1];
        fds[0].fd = open("/dev/tty", O_RDONLY);
        fds[0].events = POLLIN;

        while (true) {
            poll(fds, 1, -1);

            int ch;
            {
                std::scoped_lock lock(*nc_mutex);
                ch = getch();
            }

            Command command;
            switch (ch) {
            case 'q':
                command.type = Command::QUIT;
                break;
            case 'j':
            case KEY_DOWN:
                command.type = Command::VIEW_DOWN;
                break;
            case 'k':
            case KEY_UP:
                command.type = Command::VIEW_UP;
                break;
            case 'g':
                command.type = Command::VIEW_BOF;
                break;
            case 'G':
                command.type = Command::VIEW_EOF;
                break;
            case '/':
                command.type = Command::SEARCH;
                command.payload = "begin";
                break;
            }
            if (command.type == Command::INVALID) {
                command.payload = keyname(ch);
            }

            chan->push(std::move(command));
            if (command.type == Command::QUIT) {
                break;
            }
        }
    }
};
