//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 13 or newer
//    Usage:  soeketre hode bein hals arm tann hånd tå
//            soeketre                (type words, finish with an empty line / EOF)

%:if defined(_WIN32)
%:define WIN32_LEAN_AND_MEAN
%:define NOMINMAX
%:include <windows.h>
%:include <shellapi.h>
%:endif

%:include <algorithm>
%:include <array>
%:include <bit>
%:include <concepts>
%:include <cstddef>
%:include <cstdint>
%:include <cstdlib>
%:include <iostream>
%:include <iterator>
%:include <memory>
%:include <ranges>
%:include <span>
%:include <string>
%:include <string_view>
%:include <type_traits>
%:include <utility>
%:include <variant>
%:include <vector>

namespace {

    inline constexpr std::uint8_t maxDepth = 4;
    inline constexpr std::uint8_t rootBudget = 64;

    // 64, 32, 16, 8, 4: one entry per level, folded out of an index sequence.
    inline constexpr auto slotTable = []<std::size_t... Level>(std::index_sequence<Level...>) static consteval {
        return std::array<std::uint8_t, sizeof...(Level)>{ static_cast<std::uint8_t>(rootBudget >> Level)... };
    }(std::make_index_sequence<maxDepth + 1uz>{});

    [[nodiscard]] constexpr std::size_t slotWidth(std::size_t depth) noexcept {
        if consteval {
            return rootBudget >> depth;
        } else {
            return depth[slotTable.data()];
        }
    }

    static_assert(slotWidth(0) == 64 and slotWidth(3) == 8 and 4[slotTable.data()] == 4);

    template <typename Key>
    concept nodePayload = std::movable<Key> and std::totally_ordered<Key>;

    template <typename Key>
    concept textual = nodePayload<Key> and std::constructible_from<std::string_view, const Key&>;

    template <nodePayload Key>
    struct TreeNode final {
        using Self = TreeNode;
        using Link = std::unique_ptr<Self>;

        [[no_unique_address]] Key key;
        Link kids<:2:>;

        template <typename Arg>
            requires std::constructible_from<Key, Arg&&>
        constexpr explicit TreeNode(Arg&& arg) noexcept(std::is_nothrow_constructible_v<Key, Arg&&>)
            : key(std::forward<Arg>(arg)) {}

        [[nodiscard]] constexpr Self* subtree(bool right) noexcept { return right[kids].get(); }
        [[nodiscard]] constexpr const Self* subtree(bool right) const noexcept { return right[kids].get(); }
    };

    template <nodePayload Key>
    struct RawNode final {
        Key key;
        RawNode* kids<:2:>;
    };

    template <typename N>
    concept rawNodeLike = requires(N* n) {
        { n->kids<:0:> };
        { n->key };
    };

    template <typename N>
    concept hasSubtree = requires(N* n) {
        { n->subtree(std::declval<bool>()) };
    };

    template <typename N>
    concept kidAccessible = rawNodeLike<N> or hasSubtree<N>;

    template <kidAccessible N>
    [[nodiscard]] constexpr auto* descend(N* node, bool right) noexcept {
        if constexpr (hasSubtree<N>) {
            return node->subtree(right);
        } else {
            return right[node->kids];
        }
    }

    inline constexpr RawNode<int> rawLeaf{ 7, { nullptr, nullptr } };
    static_assert(descend(&rawLeaf, true) == nullptr and descend(&rawLeaf, false) == nullptr);

    template <nodePayload Key>
    class BinarySearchTree final {
    public:
        using Node = TreeNode<Key>;
        using Link = typename Node::Link;

        constexpr BinarySearchTree() noexcept = default;
        BinarySearchTree(const BinarySearchTree&) = delete;
        BinarySearchTree& operator=(const BinarySearchTree&) = delete;

        // Tears the tree down on the heap instead of the call stack, so a
        // thousand alphabetically sorted words cannot overflow the stack.
        constexpr ~BinarySearchTree() {
            std::vector<Link> graveyard;
            graveyard.push_back(std::move(root));
            while (not graveyard.empty()) {
                Link doomed = std::move(graveyard.back());
                graveyard.pop_back();
                for (int side = 0; doomed and side < 2; ++side) {
                    graveyard.push_back(std::move(side<:doomed->kids:>));
                }
            }
        }

        [[nodiscard("insert result must be observed to keep ownership")]] constexpr bool insert(Key word) {
            Link* cursor = &root;
            while (auto* current = cursor->get()) {
                const bool goRight = current->key < word;
                if (not goRight and not (word < current->key)) {
                    return false;
                }
                cursor = &goRight<:current->kids:>;
            }
            *cursor = std::make_unique<Node>(std::move(word));
            return ++cardinality, true;
        }

        [[nodiscard]] constexpr std::size_t size() const noexcept { return cardinality; }
        [[nodiscard]] constexpr bool empty() const noexcept { return not cardinality; }
        [[nodiscard]] constexpr const Node* rootNode() const noexcept { return root.get(); }

    private:
        Link root{};
        std::size_t cardinality = 0;
    };

    // Compile-time proof that the tree itself behaves (heap allocation and all).
    [[nodiscard]] consteval bool treeSelfTest() {
        BinarySearchTree<std::string_view> tree;
        for (std::string_view word : { "hode", "bein", "hals", "arm", "tann", "hand", "tz", "hode" }) {
            (void)tree.insert(word);
        }
        const auto* root = tree.rootNode();
        return tree.size() == 7 and descend(root, true)->key == "tann"
            and descend(descend(root, false), true)->key == "hals"
            and descend(descend(root, true), true)->key == "tz"
            and descend(descend(root, true), false)->key == "hand";
    }

    static_assert(treeSelfTest());

    // One cell per output column: either vacant, or one UTF-8 code point
    // (a view straight into the word stored in the tree).
    class Canvas final {
    public:
        using Cell = std::variant<std::monostate, std::string_view>;

        [[nodiscard]] constexpr Cell& operator[](std::size_t level, std::size_t column) noexcept {
            return column[level[rows]];
        }

        [[nodiscard]] constexpr std::span<const Cell> row(std::size_t level) const noexcept {
            return std::span<const Cell>(level[rows]);
        }

        [[nodiscard]] constexpr std::size_t levels() const noexcept { return reached; }

        void stamp(std::size_t level, std::size_t origin, std::string_view word) {
            const std::size_t slot = slotWidth(level);
            // A code point is a lead byte followed by any number of 10xxxxxx bytes.
            auto pieces = word
                | std::views::chunk_by([](char, char next) static noexcept { return (next bitand 0xC0) == 0x80; })
                | std::views::take(slot);
            const std::size_t margin = (slot - static_cast<std::size_t>(std::ranges::distance(pieces))) / 2;
            for (auto&& [column, piece] : std::views::zip(std::views::iota(origin + margin), pieces)) {
                (*this)[level, column] = std::string_view(piece);
            }
            reached = std::max(reached, level + 1);
        }

    private:
        Cell rows<:maxDepth:><:rootBudget:>{};
        std::size_t reached = 0;
    };

    template <textual Key>
    [[nodiscard]] Canvas paintLevels(const TreeNode<Key>* root) {
        struct Frame {
            const TreeNode<Key>* node;
            std::size_t origin;
            std::size_t depth;
        };

        Canvas canvas;
        std::vector<Frame> pending;
        (void)(root and (pending.push_back(Frame{ root, 0, 0 }), true));

        while (not pending.empty()) {
            const Frame frame = pending.back();
            pending.pop_back();
            canvas.stamp(frame.depth, frame.origin, std::string_view(frame.node->key));

            // Both children at once: Side = 0 is the left slot, Side = 1 the right one.
            [&]<std::size_t... Side>(std::index_sequence<Side...>) {
                (void((frame.depth + 1 < maxDepth and descend(frame.node, static_cast<bool>(Side)))
                          ? (pending.push_back(Frame{ descend(frame.node, static_cast<bool>(Side)),
                                                      frame.origin + Side * slotWidth(frame.depth + 1),
                                                      frame.depth + 1 }),
                             0)
                          : 0),
                 ...);
            }(std::make_index_sequence<2>{});
        }

        return canvas;
    }

    template <class... Ts>
    struct Overloaded : Ts... {
        using Ts::operator()...;
    };
    template <class... Ts>
    Overloaded(Ts...) -> Overloaded<Ts...>;

    void render(const Canvas& canvas) {
        const Overloaded glyph{
            [](std::monostate) static noexcept { return std::string_view(&0[" "], 1); },
            [](std::string_view piece) static noexcept { return piece; }
        };

        for (std::size_t level = 0; level < canvas.levels(); ++level) {
            const std::span<const Canvas::Cell> cells = canvas.row(level);
            // Walk backwards to the last occupied column, then print up to it.
            const auto stop = std::ranges::find_if(cells | std::views::reverse,
                                                   [](const Canvas::Cell& cell) static noexcept { return cell.index() != 0uz; })
                                  .base();
            for (const Canvas::Cell& cell : std::ranges::subrange(cells.begin(), stop)) {
                std::cout << std::visit(glyph, cell);
            }
            std::cout << '\n';
        }
        std::cout << std::flush;
    }

    // Any byte above the space character belongs to a word (this includes every UTF-8 byte).
    inline constexpr auto isGlyph = [](char c) static noexcept { return std::bit_cast<std::uint8_t>(c) > 0x20; };

    template <textual Key>
        requires std::constructible_from<Key, std::string_view>
    std::size_t feedLine(BinarySearchTree<Key>& tree, std::string_view line) {
        std::size_t seen = 0;
        for (auto chunk : line | std::views::chunk_by([](char a, char b) static noexcept { return isGlyph(a) == isGlyph(b); })) {
            if (isGlyph(chunk.front())) {
                (void)tree.insert(Key(std::string_view(chunk))), ++seen;
            }
        }
        return seen;
    }

    template <textual Key>
        requires std::constructible_from<Key, std::string_view>
    void interactiveSession(BinarySearchTree<Key>& tree) {
        for (std::string line; (std::cout << "ord> " << std::flush) and std::getline(std::cin, line) and feedLine(tree, line);) {
        }
        std::cin.eof() and (std::cout << '\n');
    }

    [[nodiscard]] std::vector<std::string> commandLineWords([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
%:if defined(_WIN32)
        // The narrow argv is in the ANSI code page; ask Windows for the real UTF-16 line instead.
        std::vector<std::string> words;
        int count = 0;
        wchar_t** wide = CommandLineToArgvW(GetCommandLineW(), &count);
        for (int index = 1; wide and index < count; ++index) {
            const int size = WideCharToMultiByte(CP_UTF8, 0, index[wide], -1, nullptr, 0, nullptr, nullptr);
            std::string utf8(static_cast<std::size_t>(std::max(size, 1)), '\0');
            WideCharToMultiByte(CP_UTF8, 0, index[wide], -1, utf8.data(), size, nullptr, nullptr);
            utf8.pop_back();
            words.push_back(std::move(utf8));
        }
        LocalFree(wide);
        return words;
%:else
        const std::span<char*> raw{ argv, static_cast<std::size_t>(argc) };
        return std::vector<std::string>(raw.begin() + not raw.empty(), raw.end());
%:endif
    }

} // namespace

int main(int argc, char* argv[]) {
%:if defined(_WIN32)
    SetConsoleOutputCP(CP_UTF8), SetConsoleCP(CP_UTF8);
%:endif
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    BinarySearchTree<std::string> tree;

    for (std::string& word : commandLineWords(argc, argv)) {
        (void)(word.empty() or tree.insert(auto(std::move(word))));
    }

    tree.empty() and (interactiveSession(tree), true);

    return tree.empty() ? EXIT_SUCCESS : (render(paintLevels(tree.rootNode())), EXIT_SUCCESS);
}