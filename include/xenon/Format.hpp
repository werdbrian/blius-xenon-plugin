#pragma once

// Number formatting and TextBuilder for WASM plugins (no stdlib available)
// Requires Memory.hpp and String.hpp to be included first (SDK.hpp controls include order)

namespace xenon
{
namespace fmt
{

// Format an integer into buf. Returns number of chars written (excluding null terminator).
// Handles negative numbers. Returns 0 if buf_size < 2.
inline int int_to_str(int value, char* buf, int buf_size)
{
    if (buf_size < 2)
        return 0;

    if (value == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    bool negative = value < 0;
    // Work with positive value using unsigned to handle INT_MIN
    unsigned int uval = negative ? static_cast<unsigned int>(-(value + 1)) + 1u : static_cast<unsigned int>(value);

    // Write digits in reverse into a temp buffer
    char tmp[12];
    int len = 0;
    while (uval > 0)
    {
        tmp[len++] = '0' + static_cast<char>(uval % 10);
        uval /= 10;
    }
    if (negative)
        tmp[len++] = '-';

    // Check if it fits
    if (len >= buf_size)
    {
        buf[0] = '\0';
        return 0;
    }

    // Reverse into output
    for (int i = 0; i < len; ++i)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

// Format an unsigned integer into buf. Returns number of chars written.
inline int uint_to_str(unsigned int value, char* buf, int buf_size)
{
    if (buf_size < 2)
        return 0;

    if (value == 0)
    {
        buf[0] = '0';
        buf[1] = '\0';
        return 1;
    }

    char tmp[11];
    int len = 0;
    while (value > 0)
    {
        tmp[len++] = '0' + static_cast<char>(value % 10);
        value /= 10;
    }

    if (len >= buf_size)
    {
        buf[0] = '\0';
        return 0;
    }

    for (int i = 0; i < len; ++i)
        buf[i] = tmp[len - 1 - i];
    buf[len] = '\0';
    return len;
}

// Format a float into buf (e.g. "12.3", "-0.5"). Returns number of chars written.
// decimals controls digits after the decimal point (default 1).
inline int float_to_str(float value, char* buf, int buf_size, int decimals = 1)
{
    if (buf_size < 4)
        return 0;

    int pos = 0;

    if (value < 0.f)
    {
        buf[pos++] = '-';
        value = -value;
    }

    // Compute rounding factor
    float rounder = 0.5f;
    for (int d = 0; d < decimals; ++d)
        rounder *= 0.1f;
    value += rounder;

    auto intPart = static_cast<unsigned int>(value);
    float fracPart = value - static_cast<float>(intPart);

    // Format integer part
    int written = uint_to_str(intPart, buf + pos, buf_size - pos);
    if (written == 0 && intPart != 0)
        return 0;
    // uint_to_str returns 0 for value 0 only when buf_size < 2, but we already checked
    pos += written;

    if (decimals > 0 && pos + 1 + decimals < buf_size)
    {
        buf[pos++] = '.';
        for (int d = 0; d < decimals; ++d)
        {
            fracPart *= 10.f;
            int digit = static_cast<int>(fracPart);
            if (digit > 9) digit = 9;
            buf[pos++] = '0' + static_cast<char>(digit);
            fracPart -= static_cast<float>(digit);
        }
    }

    buf[pos] = '\0';
    return pos;
}

} // namespace fmt

// Stack-allocated string builder for easy text concatenation.
// Usage:
//   TextBuilder<64> buf;
//   buf.put("HP: ").putInt(hp).put("/").putInt(maxHp);
//   Draw::Text(x, y, Color::White(), buf.c_str());
template <int N>
class TextBuilder
{
    char m_buf[N];
    int m_len;

public:
    TextBuilder() : m_len(0)
    {
        m_buf[0] = '\0';
    }

    TextBuilder& put(const char* str)
    {
        while (*str && m_len < N - 1)
            m_buf[m_len++] = *str++;
        m_buf[m_len] = '\0';
        return *this;
    }

    TextBuilder& putChar(char c)
    {
        if (m_len < N - 1)
        {
            m_buf[m_len++] = c;
            m_buf[m_len] = '\0';
        }
        return *this;
    }

    TextBuilder& putInt(int value)
    {
        char tmp[12];
        fmt::int_to_str(value, tmp, sizeof(tmp));
        return put(tmp);
    }

    TextBuilder& putFloat(float value, int decimals = 1)
    {
        char tmp[24];
        fmt::float_to_str(value, tmp, sizeof(tmp), decimals);
        return put(tmp);
    }

    const char* c_str() const { return m_buf; }
    int length() const { return m_len; }

    void clear()
    {
        m_len = 0;
        m_buf[0] = '\0';
    }
};

} // namespace xenon
