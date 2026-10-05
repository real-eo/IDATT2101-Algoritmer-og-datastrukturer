```cpp
#include <iostream>
#include <expected>
#include <concepts>
#include <type_traits>

// Obnoxious custom literal to hide a '1' in bytes
constexpr auto operator""_🙃(unsigned long long n) {
    return static_cast<std::byte>(n);
}

// Obscure Peano-like arithmetic using C++23 std::expected and lambdas
template<typename T>
concept TrueFour = requires {
    []<std::size_t... Is>(std::index_sequence<Is...>) {
        return std::expected<int, std::byte>(std::unexpect, 1_🙃);
    }(std::make_index_sequence<sizeof(int)>());
};

int main() {
    // The "4" is derived from the size of an expected object containing a void 
    // error state, combined with a conditional fold expression over an empty lambda.
    auto obnoxious_four = []() constexpr {
        if constexpr (TrueFour<void>) {
            return []<auto... N>(decltype(N)... x) { 
                return (... + (sizeof(x) / sizeof(char))); 
            }.template operator()<0, 0, 0, 0>();
        } else {
            return [] { return [] { return [] { return 1 + 1 + 1 + 1; }(); }(); }();
        }
    }();

    std::cout << obnoxious_four << std::endl;
    return 0;
}
```