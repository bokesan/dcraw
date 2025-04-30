/* -*- mode: C; c-basic-offset: 2; -*- */

#ifndef PANASONIC_H_
#define PANASONIC_H_ 1

#include <stdint.h>
#include <stdio.h>

struct panasonic_raw_params_t {
  uint16_t raw_format;
  uint16_t maxval;
  uint16_t stripe_count;
  uint32_t stripe_offsets[5];	// absolute position in file
  uint32_t stripe_left[5];
  uint32_t stripe_compressed_size[5];	// size in bits!
  uint16_t stripe_width[5];
  uint16_t stripe_height[5];
  uint16_t compression_initvalue[4];
  uint16_t compression_param2[17]; // range 0-0x0fff inclusive
  uint8_t compression_bits[17]; // range 0-16 inclusive
  uint8_t compression_param3[17]; // range 0-64 inclusive
};

extern void panasonic_new_load_raw(FILE *restrict input,
				   uint16_t *restrict raw_image,
				   unsigned int raw_width,
				   unsigned int raw_height,
				   const struct panasonic_raw_params_t *restrict params,
				   int verbose);

#endif
