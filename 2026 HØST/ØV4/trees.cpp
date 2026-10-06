//   Author: Elias Alexander Wiklund Ottersbo
// Standard: C++23
//    Build: UCRT GCC (Windows) / GCC (Linux), GCC 13 or newer
%:if defined(_WIN32)
%:define WIN32_LEAN_AND_MEAN
%:define NOMINMAX
%:include <windows.h>
%:include <shellapi.h>
%:endif

%:include <string_view>
%:include <type_traits>
%:include <algorithm>
%:include <concepts>
%:include <expected>
%:include <iostream>
%:include <iterator>
%:include <cstddef>
%:include <cstdint>
%:include <cstdlib>
%:include <utility>
%:include <variant>
%:include <memory>
%:include <ranges>
%:include <vector>
%:include <string>
%:include <array>
%:include <span>
%:include <bit>

namespace {

    // ================================================================
    // 1. The two number machines (tip1 + tip2), now load-bearing.
    //    maxDepth and rootBudget are never written as literals again.
    // ================================================================

    // tip2: a monadic-bind pipeline whose error type collapses into a
    // one-element index sequence, folded into the number 1.
    struct Vacant {};
    struct Ruin final {
        consteval Ruin(auto...) {}
        friend bool operator==(Ruin, Ruin) = delete;
    };

    template <typename T>
    struct MonadBind {
        static consteval auto compute() {
            return std::expected<Vacant, Ruin>(std::unexpect, Ruin{})
                .and_then([](Vacant) { return std::expected<int, Ruin>(42); })
                .transform_error([](Ruin) { return Vacant{}; });
        }
    };

    template <typename M>
    consteval auto absoluteZeroOrOne() {
        return []<typename T>() {
            if constexpr (std::same_as<typename decltype(M::compute())::error_type, Vacant>) {
                return []<auto... Is>(std::index_sequence<Is...>) {
                    return (static_cast<int>(!!sizeof((Is + 1)["x"])) + ... + 0);
                }(std::make_index_sequence<true>{});
            } else {
                struct [[no_unique_address]] Dummy {};
                return static_cast<int>(sizeof(Dummy));
            }
        }.template operator()<void>();
    }

    //a Peano four smuggled through std::expected and a fold over
    // sizeof. (The emoji literal is not a valid XID identifier, so the
    // suffix is Norwegian "å" instead.)
    consteval auto operator""_å(unsigned long long n) noexcept { return static_cast<std::byte>(n); }

    template <typename T>
    concept TrueFour = requires {
        []<std::size_t... Is>(std::index_sequence<Is...>) {
            return std::expected<int, std::byte>(std::unexpect, 1_å);
        }(std::make_index_sequence<sizeof(int)>{});
    };

    consteval std::uint8_t generateInt() {
        return []() consteval {
            if constexpr (TrueFour<void>) {
                return []<auto... N>(decltype(N)... x) {
                    return static_cast<std::uint8_t>((... + (sizeof(x) / sizeof(int))));
                }.template operator()<0, 0, 0, 0>(0, 0, 0, 0);
            } else {
                return [] { return [] { return [] { return 1 + 1 + 1 + 1; }(); }(); }();
            }
        }();
    }

    static_assert(absoluteZeroOrOne<MonadBind<void>>() == 1 and generateInt() == 4);

    inline constexpr std::uint8_t maxDepth = generateInt();
    inline constexpr std::uint8_t rootBudget = static_cast<std::uint8_t>(generateInt() << generateInt());

    static_assert(maxDepth == 4 and rootBudget == 64);

    // ================================================================
    // 2. The slot table, folded out of an index sequence (from trees2).
    // ================================================================

    inline constexpr auto slotTable = []<std::size_t... Level>(std::index_sequence<Level...>) static consteval {
        return std::array<std::uint8_t, sizeof...(Level)>{ static_cast<std::uint8_t>(rootBudget >> Level)... };
    }(std::make_index_sequence<maxDepth + 1uz>{}); // 64, 32, 16, 8, 4

    [[nodiscard]] constexpr std::size_t slotWidth(std::size_t depth) noexcept {
        if consteval {
            return rootBudget >> depth;
        } else {
            return depth[slotTable.data()];
        }
    }

    static_assert(slotWidth(0) == 64 and slotWidth(3) == 8 and 4[slotTable.data()] == 4);

    // ================================================================
    // 3. public_cast: the user's private-member leak, made load-bearing.
    //    The tree's root pointer is private; the renderer reads it through
    //    a pointer-to-member smuggled out via a hidden friend + ADL.
    // ================================================================

    template <class M, class Secret>
    struct public_cast {
        static inline M m{};
    };

    template <class Secret, auto Member>
    struct Access {
        static const inline auto m = (public_cast<decltype(Member), Secret>::m = Member, Member);
    };

    // ================================================================
    // 4. The tree. Rotation-based teardown (no recursion, no vector in
    //    the destructor), consteval-friendly, private root reachable only
    //    through the smuggled member pointer.
    // ================================================================

    template <typename Key>
    concept nodePayload = std::movable<Key> and std::totally_ordered<Key>;

    template <typename Key>
    concept textual = nodePayload<Key> and std::constructible_from<std::string_view, const Key&>;

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

    template <kidAccessible N>
    [[nodiscard]] constexpr auto* descend(const N* node, bool right) noexcept {
        if constexpr (hasSubtree<N>) {
            return node->subtree(right);
        } else {
            return right[node->kids];
        }
    }

    inline constexpr RawNode<int> rawLeaf{ 7, { nullptr, nullptr } };
    static_assert(descend(&rawLeaf, true) == nullptr and descend(&rawLeaf, false) == nullptr);

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
    class BinarySearchTree final {
    public:
        using Node = TreeNode<Key>;
        using Link = typename Node::Link;

        constexpr BinarySearchTree() noexcept = default;
        BinarySearchTree(const BinarySearchTree&) = delete;
        BinarySearchTree& operator=(const BinarySearchTree&) = delete;

        // Day's rotation teardown: walk down, rotate every left child up,
        // and let each unique_ptr destroy exactly one node. No recursion,
        // no vector, no stack overflow on a degenerate tree.
        constexpr ~BinarySearchTree() {
            Link vine = std::move(root);
            while (vine) {
                if (0[vine->kids]) {
                    Link heir = std::move(0[vine->kids]);
                    0[vine->kids] = std::move(1[heir->kids]);
                    1[heir->kids] = std::move(vine);
                    vine = std::move(heir);
                } else {
                    vine = std::move(1[vine->kids]);
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

    private:
        // The renderer needs the root, but the root stays private: it is
        // handed over through a consteval pointer-to-member that only
        // friends can mint. (The Access/public_cast machinery above.)
        template <class Secret, auto Member>
        friend struct Access;

        // Hidden friend: only found via ADL, only callable by friends.
        template <class Secret>
        friend consteval Link BinarySearchTree::* smuggle(Secret, const BinarySearchTree*) {
            return &BinarySearchTree::root;
        }

        Link root{};
        std::size_t cardinality = 0;
    };

    struct RootSecret final {};

    template struct Access<RootSecret, &BinarySearchTree<std::string>::root>;

    // Compile-time proof of the whole pipeline: insert, shape, teardown.
    [[nodiscard]] consteval bool treeSelfTest() {
        BinarySearchTree<std::string_view> tree;
        for (std::string_view word : { "hode", "bein", "hals", "arm", "tann", "hand", "tz", "hode" }) {
            (void)tree.insert(word);
        }
        const auto* root = (tree.*smuggle(RootSecret{}, &tree)).get();
        return tree.size() == 7
            and root->key == "hode"
            and descend(root, false)->key == "bein"
            and descend(root, true)->key == "tann"
            and descend(descend(root, false), false)->key == "arm"
            and descend(descend(root, false), true)->key == "hals"
            and descend(descend(descend(root, false), true), true)->key == "hand"
            and descend(descend(root, true), true)->key == "tz";
    }

    static_assert(treeSelfTest());

    // ================================================================
    // 5. The canvas: one cell per column, holding either nothing or one
    //    UTF-8 code point sliced straight out of the tree's storage.
    // ================================================================

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
        Cell rows<:maxDepth:><:rootBudget * 2:>{}; // 4 x 128 cells
        std::size_t reached = 0;
    };

    static_assert(Canvas{}.row(0).size() == rootBudget * 2);

    // ================================================================
    // 6. Layout: iterative DFS with a fold-expression-as-if over both
    //    children at once (from trees2).
    // ================================================================

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

    // ================================================================
    // 7. Input: UTF-8 aware tokenizer (from trees2), wide-argv rescue on
    //    Windows, comma-operator control flow.
    // ================================================================

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

    // The root is private. No matter: main performs the public_cast —
    // reading the member pointer that the Access heist dropped into the
    // public_cast static at static-init time.
    const auto* root = (tree.*public_cast<BinarySearchTree<std::string>::Link BinarySearchTree<std::string>::*, RootSecret>::m).get();

    return tree.empty() ? EXIT_SUCCESS : (render(paintLevels(root)), EXIT_SUCCESS);
}
