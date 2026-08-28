// SPDX-License-Identifier: MIT
// Shadow SE - test suite entry point.
#include "test_framework.hpp"

int main() {
    std::printf("Shadow SE unit tests\n--------------------\n");
    return testfw::runAll();
}
