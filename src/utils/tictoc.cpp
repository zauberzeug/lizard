#include "tictoc.h"

#include <stdio.h>

std::chrono::_V2::system_clock::time_point t;

void tic()
{
    t = std::chrono::high_resolution_clock::now();
}

void toc(const char *msg)
{
    auto dt = std::chrono::high_resolution_clock::now() - t;
    auto us = std::chrono::duration_cast<std::chrono::microseconds>(dt);
    printf("%s took %.3f ms\n", msg, 0.001 * us.count());
}
