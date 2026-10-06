```cpp
template <class M, class Secret>
struct public_cast {
    static inline M m{};
};

template <class Secret, auto M>
struct Access {
    static const inline auto m = public_cast<decltype(M), Secret>::m = M;
};

template struct access<class CxSecret, &C::x>;
int x = c.*public_cast<int C::*, CxSecret>::m;
```