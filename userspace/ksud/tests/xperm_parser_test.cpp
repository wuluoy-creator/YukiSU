#include "../src/sepolicy/xperm_parser.hpp"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace {

void expect(std::string input, std::vector<std::string> expected) {
    const auto parsed = ksud::parse_xperm_set(input);
    assert(parsed.has_value());
    assert(*parsed == expected);
}

void reject(std::string input) {
    assert(!ksud::parse_xperm_set(input).has_value());
}

}  // namespace

int main() {
    expect("0x1234", {"0x1234"});
    expect("0x1200-0x12ff", {"0x1200-0x12ff"});
    expect("{ 0x1234 0x5600-0x56ff }", {"0x1234", "0x5600-0x56ff"});

    reject("");
    reject("{}");
    reject("{   }");
    reject("*");
    reject("{ 0x1234");
    reject("0x1234 trailing");
    reject("{ 0x1234, 0x5678 }");

    std::cout << "xperm_parser_test: all checks passed\n";
    return 0;
}
