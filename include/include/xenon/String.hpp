#pragma once

// Freestanding string utilities for WASM plugins (no stdlib available)
// Standard C names at global scope — no conflicts since there's no libc
// Requires Memory.hpp to be included first (SDK.hpp controls include order)

inline size_t strlen(const char* s)
{
    size_t len = 0;
    while (s[len] != '\0')
        ++len;
    return len;
}

inline int strcmp(const char* a, const char* b)
{
    while (*a && *a == *b)
    {
        ++a;
        ++b;
    }
    return static_cast<unsigned char>(*a) - static_cast<unsigned char>(*b);
}

inline char* strcpy(char* dst, const char* src)
{
    char* d = dst;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}

inline char* strncpy(char* dst, const char* src, size_t n)
{
    size_t i = 0;
    for (; i < n && src[i] != '\0'; ++i)
        dst[i] = src[i];
    for (; i < n; ++i)
        dst[i] = '\0';
    return dst;
}

inline char* strcat(char* dst, const char* src)
{
    char* d = dst;
    while (*d != '\0')
        ++d;
    while ((*d++ = *src++) != '\0')
        ;
    return dst;
}
