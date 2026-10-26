// Include once per test program, instead of DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN:
// PlatformIO's doctest runner counts test cases from doctest's --success
// output, so it has to be switched on.
#pragma once

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest.h>

int main(int argc, char** argv) {
    doctest::Context context;
    context.setOption("success", true);
    context.applyCommandLine(argc, argv);
    return context.run();
}
