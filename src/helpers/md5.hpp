#pragma once

#include <string>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <cstdint>

namespace rouen::helpers {

class MD5 {
public:
    MD5() { init(); }

    explicit MD5(const std::string& text) {
        init();
        update(reinterpret_cast<const uint8_t*>(text.c_str()), text.length());
        finalize();
    }

    void update(const uint8_t* input, size_t length) {
        uint32_t index = (count[0] / 8U) % 64U;
        count[0] += static_cast<uint32_t>(length << 3U);
        if (count[0] < (static_cast<uint32_t>(length << 3U))) {
            count[1]++;
        }
        count[1] += static_cast<uint32_t>(length >> 29U);

        size_t partLen = 64U - index;
        size_t i = 0;

        if (length >= partLen) {
            std::memcpy(&buffer[index], input, partLen);
            transform(buffer);
            for (i = partLen; i + 63U < length; i += 64U) {
                transform(&input[i]);
            }
            index = 0;
        } else {
            i = 0;
        }

        std::memcpy(&buffer[index], &input[i], length - i);
    }

    MD5& finalize() {
        static const uint8_t PADDING[64] = { 0x80 };
        if (!finalized) {
            uint8_t bits[8];
            encode(bits, count, 8);
            uint32_t index = (count[0] / 8U) % 64U;
            uint32_t padLen = (index < 56U) ? (56U - index) : (120U - index);
            update(PADDING, padLen);
            update(bits, 8);
            encode(digest, state, 16);
            finalized = true;
        }
        return *this;
    }

    std::string hexdigest() const {
        if (!finalized) return "";
        std::ostringstream ss;
        for (int i = 0; i < 16; ++i) {
            ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(digest[i]);
        }
        return ss.str();
    }

    static std::string hash(const std::string& input) {
        MD5 md5(input);
        return md5.hexdigest();
    }

private:
    void init() {
        finalized = false;
        count[0] = 0;
        count[1] = 0;
        state[0] = 0x67452301U;
        state[1] = 0xefcdab89U;
        state[2] = 0x98badcfeU;
        state[3] = 0x10325476U;
        std::memset(buffer, 0, 64);
        std::memset(digest, 0, 16);
    }

    static inline uint32_t rotate_left(uint32_t x, uint32_t n) {
        return (x << n) | (x >> (32U - n));
    }

    static inline uint32_t F(uint32_t x, uint32_t y, uint32_t z) { return (x & y) | (~x & z); }
    static inline uint32_t G(uint32_t x, uint32_t y, uint32_t z) { return (x & z) | (y & ~z); }
    static inline uint32_t H(uint32_t x, uint32_t y, uint32_t z) { return x ^ y ^ z; }
    static inline uint32_t I(uint32_t x, uint32_t y, uint32_t z) { return y ^ (x | ~z); }

    static inline void FF(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
        a = rotate_left(a + F(b, c, d) + x + ac, s) + b;
    }
    static inline void GG(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
        a = rotate_left(a + G(b, c, d) + x + ac, s) + b;
    }
    static inline void HH(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
        a = rotate_left(a + H(b, c, d) + x + ac, s) + b;
    }
    static inline void II(uint32_t& a, uint32_t b, uint32_t c, uint32_t d, uint32_t x, uint32_t s, uint32_t ac) {
        a = rotate_left(a + I(b, c, d) + x + ac, s) + b;
    }

    void decode(uint32_t output[], const uint8_t input[], size_t len) {
        for (size_t i = 0, j = 0; j < len; i++, j += 4) {
            output[i] = (static_cast<uint32_t>(input[j])) |
                        (static_cast<uint32_t>(input[j + 1]) << 8U) |
                        (static_cast<uint32_t>(input[j + 2]) << 16U) |
                        (static_cast<uint32_t>(input[j + 3]) << 24U);
        }
    }

    void encode(uint8_t output[], const uint32_t input[], size_t len) {
        for (size_t i = 0, j = 0; j < len; i++, j += 4) {
            output[j] = static_cast<uint8_t>(input[i] & 0xffU);
            output[j + 1] = static_cast<uint8_t>((input[i] >> 8U) & 0xffU);
            output[j + 2] = static_cast<uint8_t>((input[i] >> 16U) & 0xffU);
            output[j + 3] = static_cast<uint8_t>((input[i] >> 24U) & 0xffU);
        }
    }

    void transform(const uint8_t block[64]) {
        uint32_t a = state[0], b = state[1], c = state[2], d = state[3], x[16];
        decode(x, block, 64);

        FF(a, b, c, d, x[ 0], 7U, 0xd76aa478U);
        FF(d, a, b, c, x[ 1], 12U, 0xe8c7b756U);
        FF(c, d, a, b, x[ 2], 17U, 0x242070dbU);
        FF(b, c, d, a, x[ 3], 22U, 0xc1bdceeeU);
        FF(a, b, c, d, x[ 4], 7U, 0xf57c0fafU);
        FF(d, a, b, c, x[ 5], 12U, 0x4787c62aU);
        FF(c, d, a, b, x[ 6], 17U, 0xa8304613U);
        FF(b, c, d, a, x[ 7], 22U, 0xfd469501U);
        FF(a, b, c, d, x[ 8], 7U, 0x698098d8U);
        FF(d, a, b, c, x[ 9], 12U, 0x8b44f7afU);
        FF(c, d, a, b, x[10], 17U, 0xffff5bb1U);
        FF(b, c, d, a, x[11], 22U, 0x895cd7beU);
        FF(a, b, c, d, x[12], 7U, 0x6b901122U);
        FF(d, a, b, c, x[13], 12U, 0xfd987193U);
        FF(c, d, a, b, x[14], 17U, 0xa679438eU);
        FF(b, c, d, a, x[15], 22U, 0x49b40821U);

        GG(a, b, c, d, x[ 1], 5U, 0xf61e2562U);
        GG(d, a, b, c, x[ 6], 9U, 0xc040b340U);
        GG(c, d, a, b, x[11], 14U, 0x265e5a51U);
        GG(b, c, d, a, x[ 0], 20U, 0xe9b6c7aaU);
        GG(a, b, c, d, x[ 5], 5U, 0xd62f105dU);
        GG(d, a, b, c, x[10], 9U, 0x02441453U);
        GG(c, d, a, b, x[15], 14U, 0xd8a1e681U);
        GG(b, c, d, a, x[ 4], 20U, 0xe7d3fbc8U);
        GG(a, b, c, d, x[ 9], 5U, 0x21e1cde6U);
        GG(d, a, b, c, x[14], 9U, 0xc33707d6U);
        GG(c, d, a, b, x[ 3], 14U, 0xf4d50d87U);
        GG(b, c, d, a, x[ 8], 20U, 0x455a14edU);
        GG(a, b, c, d, x[13], 5U, 0xa9e3e905U);
        GG(d, a, b, c, x[ 2], 9U, 0xfcefa3f8U);
        GG(c, d, a, b, x[ 7], 14U, 0x676f02d9U);
        GG(b, c, d, a, x[12], 20U, 0x8d2a4c8aU);

        HH(a, b, c, d, x[ 5], 4U, 0xfffa3942U);
        HH(d, a, b, c, x[ 8], 11U, 0x8771f681U);
        HH(c, d, a, b, x[11], 16U, 0x6d9d6122U);
        HH(b, c, d, a, x[14], 23U, 0xfde5380cU);
        HH(a, b, c, d, x[ 1], 4U, 0xa4beea44U);
        HH(d, a, b, c, x[ 4], 11U, 0x4bdecfa9U);
        HH(c, d, a, b, x[ 7], 16U, 0xf6bb4b60U);
        HH(b, c, d, a, x[10], 23U, 0xbebfbc70U);
        HH(a, b, c, d, x[13], 4U, 0x289b7ec6U);
        HH(d, a, b, c, x[ 0], 11U, 0xeaa127faU);
        HH(c, d, a, b, x[ 3], 16U, 0xd4ef3085U);
        HH(b, c, d, a, x[ 6], 23U, 0x04881d05U);
        HH(a, b, c, d, x[ 9], 4U, 0xd9d4d039U);
        HH(d, a, b, c, x[12], 11U, 0xe6db99e5U);
        HH(c, d, a, b, x[15], 16U, 0x1fa27cf8U);
        HH(b, c, d, a, x[ 2], 23U, 0xc4ac5665U);

        II(a, b, c, d, x[ 0], 6U, 0xf4292244U);
        II(d, a, b, c, x[ 7], 10U, 0x432aff97U);
        II(c, d, a, b, x[14], 15U, 0xab9423a7U);
        II(b, c, d, a, x[ 5], 21U, 0xfc93a039U);
        II(a, b, c, d, x[12], 6U, 0x655b59c3U);
        II(d, a, b, c, x[ 3], 10U, 0x8f0ccc92U);
        II(c, d, a, b, x[10], 15U, 0xffeff47dU);
        II(b, c, d, a, x[ 1], 21U, 0x85845dd1U);
        II(a, b, c, d, x[ 8], 6U, 0x6fa87e4fU);
        II(d, a, b, c, x[15], 10U, 0xfe2ce6e0U);
        II(c, d, a, b, x[ 6], 15U, 0xa3014314U);
        II(b, c, d, a, x[13], 21U, 0x4e0811a1U);
        II(a, b, c, d, x[ 4], 6U, 0xf7537e82U);
        II(d, a, b, c, x[11], 10U, 0xbd3af235U);
        II(c, d, a, b, x[ 2], 15U, 0x2ad7d2bbU);
        II(b, c, d, a, x[ 9], 21U, 0xeb86d391U);

        state[0] += a;
        state[1] += b;
        state[2] += c;
        state[3] += d;
        std::memset(x, 0, sizeof(x));
    }

    bool finalized{false};
    uint8_t buffer[64]{0};
    uint32_t count[2]{0};
    uint32_t state[4]{0};
    uint8_t digest[16]{0};
};

} // namespace rouen::helpers
