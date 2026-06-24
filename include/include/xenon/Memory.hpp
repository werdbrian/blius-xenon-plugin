#pragma once

// Freestanding memory utilities for WASM plugins (no stdlib available)
// Standard C names at global scope — no conflicts since there's no libc

inline void* memcpy(void* dst, const void* src, size_t n)
{
    auto* d = static_cast<unsigned char*>(dst);
    const auto* s = static_cast<const unsigned char*>(src);
    for (size_t i = 0; i < n; ++i)
        d[i] = s[i];
    return dst;
}

inline void* memset(void* dst, int val, size_t n)
{
    auto* d = static_cast<unsigned char*>(dst);
    const auto v = static_cast<unsigned char>(val);
    for (size_t i = 0; i < n; ++i)
        d[i] = v;
    return dst;
}

inline int memcmp(const void* a, const void* b, size_t n)
{
    const auto* pa = static_cast<const unsigned char*>(a);
    const auto* pb = static_cast<const unsigned char*>(b);
    for (size_t i = 0; i < n; ++i)
    {
        if (pa[i] != pb[i])
            return pa[i] < pb[i] ? -1 : 1;
    }
    return 0;
}
