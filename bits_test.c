#include "bits.h"

#include <stdio.h>

static uint64_t rev(uint64_t x, unsigned w)
{
     uint64_t r = 0;
     for (unsigned pos = 0; pos < w; pos++) {
	  unsigned dest_pos = w - 1 - pos;
	  uint64_t bit = x & (1ULL << pos);
	  if (dest_pos >= pos) {
	       bit <<= dest_pos - pos;
	  } else {
	       bit >>= pos - dest_pos;
	  }
	  r |= bit;
     }
     return r;
}


int main(int argc, char **argv)
{
     printf("Testing uint8_t...\n");
     for (unsigned x = 0; x < 256; x++) {
	  uint8_t expected = rev(x, 8);
	  uint8_t r1 = reverse_bits_8(x);
	  if (r1 != expected) {
	       printf("ERROR for 0x%02x: expected 0x%02x, got 0x%02x\n", x, expected, r1);
	       return 1;
	  }
     }

     printf("Testing uint16_t...\n");
     for (unsigned x = 0; x < 65536; x++) {
	  uint16_t expected = rev(x, 16);
	  uint16_t r1 = reverse_bits_16(x);
	  if (r1 != expected) {
	       printf("ERROR for 0x%04x: expected 0x%04x, got 0x%04x\n", x, expected, r1);
	       return 1;
	  }
     }

     printf("Testing uint32_t...\n");
     for (uint64_t x = 0; x <= 0xffffffff; x += 13) {
	  uint32_t expected = rev(x, 32);
	  uint32_t r1 = reverse_bits_32(x);
	  if (r1 != expected) {
	       printf("ERROR for 0x%08llx: expected 0x%08x, got 0x%08x\n", x, expected, r1);
	       return 1;
	  }
     }

     printf("Success!\n");
     return 0;
}

