#include "Main.h"

int main(int argc, char **argv) {
    // Channel<Command> chan;
    // Channel<std::function<void(void)>> task_chan;

    // std::stop_source file_task_stop_source;
    // std::promise<void> file_task_promise;

    // register_for_sigwinch_channel(&chan);

    if (argc < 2) {
        fprintf(stderr, "missing filename.\n");
        exit(1);
    }

    // try to open the file
    std::filesystem::directory_entry read_file(argv[1]);

    if (read_file.is_directory()) {
        fprintf(stderr, "%s is a directory.\n", argv[1]);
        exit(1);
    } else if (!read_file.is_regular_file()) {
        fprintf(stderr,
                "%s is not a regular file. We don't support opening "
                "non-regular files.\n",
                argv[1]);
        exit(1);
    }
    Main main{read_file};
    main.run();
    return 0;
}
