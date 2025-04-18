#ifndef BITS_H_
#define BITS_H_ 1

#include <stdint.h>

#define ROT(N) static inline uint ## N ## _t rotate_left_ ## N (uint ## N ## _t x, unsigned n) \
     { return (x << n) | (x >> (N - n)); }

ROT(8)
ROT(16)
ROT(32)
ROT(64)

#undef ROT

/*
  Clang 17 recognizes these as bit reversals.
  On ARM64, for example, the 32 and 64 bit versions compile to a single RBIT instruction.
  Thus, inline them.
*/

static inline uint8_t reverse_bits_8(uint8_t x)
{
     uint8_t t;
     t = x & 0x33; x = rotate_left_8(t, 4) | (t ^ x);
     t = x & 0x55; x = rotate_left_8(t, 2) | (t ^ x);
     return rotate_left_8(x, 1);
}

static inline uint16_t reverse_bits_16(uint16_t x)
{
     uint16_t t;
     t = x & 0x0F0F; x = rotate_left_16(t, 8) | (t ^ x);
     t = x & 0x3333; x = rotate_left_16(t, 4) | (t ^ x);
     t = x & 0x5555; x = rotate_left_16(t, 2) | (t ^ x);
     return rotate_left_16(x, 1);
}

static inline uint32_t reverse_bits_32(uint32_t x)
{
     uint32_t t;
     t = x & 0x00FF00FF; x = rotate_left_32(t, 16) | (t ^ x);
     t = x & 0x0F0F0F0F; x = rotate_left_32(t, 8) | (t ^ x);
     t = x & 0x33333333; x = rotate_left_32(t, 4) | (t ^ x);
     t = x & 0x55555555; x = rotate_left_32(t, 2) | (t ^ x);
     return rotate_left_32(x, 1);
}

static inline uint64_t reverse_bits_64(uint64_t x)
{
     uint64_t t;
     t = x & 0x0000FFFF0000FFFF; x = rotate_left_64(t, 32) | (t ^ x);
     t = x & 0x00FF00FF00FF00FF; x = rotate_left_64(t, 16) | (t ^ x);
     t = x & 0x0F0F0F0F0F0F0F0F; x = rotate_left_64(t, 8) | (t ^ x);
     t = x & 0x3333333333333333; x = rotate_left_64(t, 4) | (t ^ x);
     t = x & 0x5555555555555555; x = rotate_left_64(t, 2) | (t ^ x);
     return rotate_left_64(x, 1);
}

#endif /* BITS_H_ */
