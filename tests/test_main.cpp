#include <iostream>

int RunConfigTests();
int RunCameraMathTests();

// Simple test runner mirroring cameraunlock-core/cpp/tests/test_main.cpp.
int main() {
    std::cout << "RE2 Head Tracking Tests\n";
    std::cout << "=======================\n";

    int failures = 0;
    failures += RunConfigTests();
    failures += RunCameraMathTests();

    if (failures == 0) {
        std::cout << "All tests passed!\n";
        return 0;
    }
    std::cout << failures << " test(s) FAILED\n";
    return 1;
}
