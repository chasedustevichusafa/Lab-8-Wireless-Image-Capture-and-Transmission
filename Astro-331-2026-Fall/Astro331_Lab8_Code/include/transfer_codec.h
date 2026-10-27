#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

namespace kfs {
// RFC 4648 Base64 keeps binary file data inside the existing CRC-checked ASCII
// records: three useful bytes use four characters instead of six hex digits.
// This is deliberately not a new binary UART parser. Newlines, pipes and NUL
// cannot occur in the encoded field, so normal resynchronization still works.
inline void base64Encode(const uint8_t* bytes, size_t n, char* output) {
    static const char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t out = 0;
    for (size_t i = 0; i < n; i += 3) {
        uint32_t value = (uint32_t)bytes[i] << 16;
        if (i+1 < n) value |= (uint32_t)bytes[i+1] << 8;
        if (i+2 < n) value |= bytes[i+2];
        output[out++] = alphabet[(value >> 18) & 63];
        output[out++] = alphabet[(value >> 12) & 63];
        output[out++] = i+1 < n ? alphabet[(value >> 6) & 63] : '=';
        output[out++] = i+2 < n ? alphabet[value & 63] : '=';
    }
    output[out] = 0;
}
inline int base64Digit(char c) {
    if (c >= 'A' && c <= 'Z') return c-'A';
    if (c >= 'a' && c <= 'z') return c-'a'+26;
    if (c >= '0' && c <= '9') return c-'0'+52;
    return c == '+' ? 62 : c == '/' ? 63 : -1;
}
// Reject noncanonical padding and unused nonzero tail bits, not just alphabet
// errors. Capacity is checked BEFORE writing a quartet's output. n is assigned
// only on complete success; malformed input never becomes a short valid WRITE.
inline bool base64Decode(const char* text, uint8_t* output, size_t capacity, size_t& n) {
    size_t length = strlen(text), used = 0;
    if (!length || length % 4) return false;
    for (size_t i = 0; i < length; i += 4) {
        int a = base64Digit(text[i]), b = base64Digit(text[i+1]);
        int c = text[i+2] == '=' ? 0 : base64Digit(text[i+2]);
        int d = text[i+3] == '=' ? 0 : base64Digit(text[i+3]);
        bool pad2 = text[i+2] == '=', pad1 = text[i+3] == '=';
        size_t count = pad2 ? 1 : pad1 ? 2 : 3;
        if (a < 0 || b < 0 || c < 0 || d < 0 ||
                (pad2 && (!pad1 || (b & 15))) || (pad1 && !pad2 && (c & 3)) ||
                ((pad1 || pad2) && i+4 != length) || used+count > capacity) return false;
        uint32_t value = ((uint32_t)a << 18) | ((uint32_t)b << 12) | ((uint32_t)c << 6) | (uint32_t)d;
        output[used++] = (uint8_t)(value >> 16);
        if (count > 1) output[used++] = (uint8_t)(value >> 8);
        if (count > 2) output[used++] = (uint8_t)value;
    }
    n = used;
    return true;
}
} // namespace kfs
