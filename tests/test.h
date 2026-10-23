#pragma once
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>
struct TestCase {
    const char *name;
    void (*run)();
};
inline std::vector<TestCase> &all_tests() {
    static std::vector<TestCase> cases;
    return cases;
}
struct RegisterTest {
    RegisterTest(const char *name, void (*run)()) {
        all_tests().push_back({name, run});
    }
};
#define TEST(name)                                                                                           \
    void name();                                                                                             \
    static RegisterTest reg_##name(#name, name);                                                             \
    void name()
#define CHECK(...)                                                                                           \
    do {                                                                                                     \
        if (!(__VA_ARGS__))                                                                                  \
            throw std::runtime_error(std::string(__FILE__) + ":" + std::to_string(__LINE__) +                \
                                     ": check failed: " #__VA_ARGS__);                                       \
    } while (false)
#define THROWS(...)                                                                                          \
    do {                                                                                                     \
        bool caught = false;                                                                                 \
        try {                                                                                                \
            __VA_ARGS__;                                                                                     \
        } catch (const std::exception &) {                                                                   \
            caught = true;                                                                                   \
        }                                                                                                    \
        CHECK(caught);                                                                                       \
    } while (false)
inline int run_tests() {
    int failures = 0;
    for (const auto &test : all_tests())
        try {
            test.run();
            std::cout << "PASS " << test.name << '\n';
        } catch (const std::exception &error) {
            ++failures;
            std::cout << "FAIL " << test.name << ": " << error.what() << '\n';
        }
    std::cout << all_tests().size() << " tests, " << failures << " failures\n";
    return failures ? 1 : 0;
}
