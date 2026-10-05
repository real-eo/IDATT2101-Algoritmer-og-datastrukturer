//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC
//
// Deloppgave 2 — binary search tree of words, printed level-by-level with
// fixed per-level character budgets (64/32/16/8), words centered in their slot.

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <memory>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <utility>
#include <vector>

namespace {

    inline constexpr std::uint8_t maxDepth = 4;
    inline constexpr std::uint8_t rootBudget = 64;

    template <class T>
    concept nodePayload = std::movable<T> && std::totally_ordered<T>;

    template <nodePayload K>
    struct treeNode final {
        using self = treeNode;
        using link = std::unique_ptr<self>;

        [[no_unique_address]] K key;
        link kids[2];

        constexpr explicit treeNode(K&& k) noexcept(std::is_nothrow_move_constructible_v<K>) : key(std::move(k)) {}
        constexpr explicit treeNode(const K& k) noexcept(std::is_nothrow_copy_constructible_v<K>) : key(k) {}

        [[nodiscard]] constexpr self* subt(bool right) noexcept { return kids[right]; }
        [[nodiscard]] constexpr const self* subt(bool right) const noexcept { return kids[right].get(); }
    };

    template <nodePayload K>
    struct rawNode final {
        K key;
        rawNode* kids[2];
    };

    template <class N>
    concept rawNodeLike = requires(N* n) {
        { n->kids[0] };
        { n->key };
    };

    template <class N>
    concept hasSubt = requires(N* n) {
        { n->subt(std::declval<bool>()) };
    };

    template <class N>
    concept kidAccessible = rawNodeLike<N> || hasSubt<N>;

    template <kidAccessible N>
    [[nodiscard]] constexpr auto* descend(N* n, bool right) noexcept {
        if constexpr (hasSubt<N>) {
            return n->subt(right);
        } else {
            return n->kids[right];
        }
    }

    template <kidAccessible N>
    [[nodiscard]] constexpr auto* descend(const N* n, bool right) noexcept {
        if constexpr (hasSubt<N>) {
            return n->subt(right);
        } else {
            return n->kids[right];
        }
    }

    template <nodePayload K>
    class binarySearchTree final {
    public:
        using node = treeNode<K>;
        using link = typename node::link;

        constexpr binarySearchTree() noexcept = default;

        [[nodiscard("insert result must be observed to keep ownership")]] bool insert(const K& word) {
            link* cursor = &root;
            while (auto* current = cursor->get()) {
                const bool goRight = current->key < word;
                if (!goRight && !(word < current->key)) {
                    return false;
                }
                cursor = &current->kids[goRight];
            }
            *cursor = std::make_unique<node>(word);
            ++cardinality;
            return true;
        }

        [[nodiscard]] constexpr std::size_t size() const noexcept { return cardinality; }
        [[nodiscard]] constexpr bool empty() const noexcept { return cardinality == 0; }

        [[nodiscard]] const node* rootNode() const noexcept { return root.get(); }

    private:
        link root{};
        std::size_t cardinality = 0;
    };

    template <class T, class... Args>
        requires (std::same_as<T, Args> && ...)
    [[nodiscard]] constexpr std::array<T, sizeof...(Args)> makeArray(Args&&... args) noexcept(
        (std::is_nothrow_constructible_v<T, Args&&> && ...)) {
        return std::array<T, sizeof...(Args)>{ T(std::forward<Args>(args))... };
    }

    [[nodiscard]] constexpr std::uint8_t slotWidth(std::uint8_t depth) noexcept {
        return static_cast<std::uint8_t>(rootBudget >> depth);
    }

    enum class slotState : std::uint8_t { vacant, occupied };

    struct levelCanvas final {
        std::string glyphs;
        std::vector<slotState> taken;

        explicit levelCanvas(std::size_t stride)
            : glyphs(stride, ' '), taken(stride, slotState::vacant), strideOfCanvas(stride) {}

        void stamp(std::size_t origin, std::string_view word) {
            const std::size_t width = strideOfCanvas;
            const std::size_t offset = (width - word.size()) / 2;
            std::ranges::copy(word, glyphs.begin() + static_cast<std::ptrdiff_t>(origin + offset));
            std::ranges::fill(taken | std::views::drop(origin) | std::views::take(word.size()), slotState::occupied);
        }

        std::size_t strideOfCanvas = 0;
    };

    template <nodePayload K>
    [[nodiscard]] std::vector<levelCanvas> paintLevels(const treeNode<K>* root) {
        std::vector<levelCanvas> canvases;
        canvases.reserve(maxDepth);

        struct frame {
            const treeNode<K>* node;
            std::size_t origin;
            std::uint8_t depth;
        };

        std::vector<frame> pending;
        if (root) {
            pending.push_back(frame{ root, std::size_t{0}, std::uint8_t{0} });
        }

        while (!pending.empty()) {
            frame f = pending.back();
            pending.pop_back();
            const treeNode<K>* current = f.node;
            const std::size_t origin = f.origin;
            const std::uint8_t depth = f.depth;

            if (depth >= maxDepth) {
                continue;
            }
            if (depth >= canvases.size()) {
                canvases.emplace_back(rootBudget);
            }

            const std::string_view word(current->key);
            canvases[depth].stamp(origin, word);

            const std::size_t half = slotWidth(depth) / 2;
            if (auto* l = descend(current, false)) {
                pending.push_back(frame{ l, origin, static_cast<std::uint8_t>(depth + 1) });
            }
            if (auto* r = descend(current, true)) {
                pending.push_back(frame{ r, origin + half, static_cast<std::uint8_t>(depth + 1) });
            }
        }

        return canvases;
    }

    void render(const std::vector<levelCanvas>& canvases) {
        for (const auto& canvas : canvases) {
            std::string line(canvas.glyphs);
            while (!line.empty() && line.back() == ' ') {
                line.pop_back();
            }
            std::cout << line << '\n';
        }
        std::cout << std::flush;
    }

    template <class... Ts>
    struct overloaded : Ts... {
        using Ts::operator()...;
    };
    template <class... Ts>
    overloaded(Ts...) -> overloaded<Ts...>;

    [[nodiscard]] bool readTokens(std::vector<std::string>& sink) {
        std::string token;
        while (std::cin >> token) {
            sink.push_back(std::move(token));
        }
        return !sink.empty();
    }

} // namespace

int main(int argc, char* argv[]) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    binarySearchTree<std::string> tree;

    const std::span<const char* const> args{ const_cast<const char* const*>(argv), static_cast<std::size_t>(argc) };
    for (std::string_view word : args | std::views::drop(1)) {
        (void)tree.insert(std::string(word));
    }

    if (tree.empty()) {
        std::vector<std::string> tokens;
        if (!readTokens(tokens)) {
            return EXIT_SUCCESS;
        }
        for (std::string& word : tokens) {
            (void)tree.insert(word);
        }
    }

    render(paintLevels(tree.rootNode()));
    return EXIT_SUCCESS;
}