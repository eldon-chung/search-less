#include "Input.h"

Channel<Command> *sigwinch_channel;

void handle_sigwinch(int) {
    sigwinch_channel->push_signal(Command{Command::RESIZE, "WINCH"});
}

void register_for_sigwinch_channel(Channel<Command> *to_register) {
    sigwinch_channel = to_register;
    signal(SIGWINCH, handle_sigwinch);
}
