#include <iostream>

int RunConfigTests();

// Simple test runner mirroring cameraunlock-core/cpp/tests/test_main.cpp.
// Camera math is covered by cameraunlock-core's own test suite.
int main() {
    std::cout << "RE2 Head Tracking Tests\n";
    std::cout << "=======================\n";

    int failures = 0;
    failures += RunConfigTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
