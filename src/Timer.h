#pragma once

#include <chrono>
#include <stdio.h>

struct Timer {
    std::chrono::high_resolution_clock::time_point start;
    Timer() : start(std::chrono::high_resolution_clock::now()) {
    }
    ~Timer() {
        fprintf(stderr, "%lf\n",
                (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::high_resolution_clock::now() - start)
                        .count() /
                    (double)1'000'000'000);
    }
};
