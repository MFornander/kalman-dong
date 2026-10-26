#include "../doctest_main.h"

#include <string>

#include "kd/filter_bank.h"

using namespace kd;

TEST_CASE("Mahony is the default filter") {
    FilterBank bank;
    CHECK(std::string(bank.defaultFilter().name()) == "Mahony");
}

TEST_CASE("filters are found by name, falling back to the default") {
    FilterBank bank;
    CHECK(std::string(bank.byName("Kalman").name()) == "Kalman");
    CHECK(std::string(bank.byName("Mahony").name()) == "Mahony");
    CHECK(&bank.byName("NoSuchFilter") == &bank.defaultFilter());
    CHECK(&bank.byName(nullptr) == &bank.defaultFilter());
}

TEST_CASE("next cycles through every filter and wraps around") {
    FilterBank bank;
    OrientationFilter* f = &bank.defaultFilter();
    for (size_t i = 0; i < bank.count(); ++i) f = &bank.next(*f);
    CHECK(f == &bank.defaultFilter());
    CHECK(&bank.next(bank.defaultFilter()) != &bank.defaultFilter());
}

TEST_CASE("every filter in the bank has a distinct name") {
    FilterBank bank;
    for (size_t i = 0; i < bank.count(); ++i)
        for (size_t j = i + 1; j < bank.count(); ++j)
            CHECK(std::string(bank.at(i).name()) != bank.at(j).name());
}
