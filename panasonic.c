/* -*- mode: C; c-basic-offset: 2; -*- */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include "panasonic.h"
#include "bits.h"

extern void fatal(const char *) __attribute__((noreturn));

void fatal(const char *err)
{
  fprintf(stderr, "Error: %s\n", err);
  exit(1);
}




static void reverse_bits(uint64_t *buf, size_t size)
{
  for (size_t i = 0; i < size; i++) {
    buf[i] = reverse_bits_64(buf[i]);
  }
}

static size_t bytes_to_qwords(size_t num_bytes)
{
  return (num_bytes + 7) >> 3;
}



#define BUFSIZE_QWORDS 8192

struct bufio_t {
  size_t index; // index into data
  size_t bytes_left;
  FILE *input;
  uint64_t data[BUFSIZE_QWORDS];
};

static void bufio_fill(struct bufio_t *p)
{
  size_t bytes = BUFSIZE_QWORDS * 8;
  if (bytes > p->bytes_left)
    bytes = p->bytes_left;
  size_t read = fread(p->data, 1, bytes, p->input);
  if (read != bytes || bytes == 0)
    fatal("EOF in decoding raw file");
  reverse_bits(p->data, bytes_to_qwords(read));
  p->bytes_left -= read;
}

static void bufio_init(struct bufio_t *p, FILE *stream, uint32_t start,
		       uint32_t total_bits)
{
  p->input = stream;
  p->bytes_left = (total_bits + 7) / 8;
  if (fseek(p->input, start, SEEK_SET) != 0)
    fatal("IO error in bufio_init");
  bufio_fill(p);
  p->index = 0;
}

static uint64_t bufio_next_qword(struct bufio_t *p)
{
  if (p->index == BUFSIZE_QWORDS) {
    bufio_fill(p);
    p->index = 0;
  }
  return p->data[p->index++];
}

static void init_index_table(uint8_t index[restrict 0x10000], const struct panasonic_raw_params_t *restrict p);

static void param_show(const struct panasonic_raw_params_t *p, FILE *f)
{
  fprintf(f, "Panasonic Compression 8 parameters:\n  Bits:");
  for (int i = 0; i < 17; i++) {
    fprintf(f, " %u", p->compression_bits[i]);
  }
  fprintf(f, "\n  Initial raw values:");
  for (int i = 0; i < 4; i++)
    fprintf(f, " %u", p->compression_initvalue[i]);
  fprintf(f, "\n  Maxval (Tag 0x3B): %u\n", p->maxval);
}

static uint32_t limit_nat(int32_t value, uint32_t max)
{
  if (value < 0)
    return 0;
  if ((uint32_t) value > max)
    return max;
  return (uint32_t) value;
}

static void write_raw(uint16_t *restrict raw_image, unsigned raw_width,
		      unsigned current_row,
		      const uint16_t *restrict srcrow,
		      unsigned int width, uint32_t left_margin)
{
  unsigned destrow = current_row * 2;
  uint16_t *destrow0 = raw_image + (destrow * raw_width) + left_margin;
  uint16_t *destrow1 = raw_image + (destrow + 1) * raw_width + left_margin;
  for (unsigned col = 0; col < width - 1; col += 2) {
    const unsigned c6 = col * 4;
    destrow0[col] = srcrow[c6];
    destrow0[col + 1] = srcrow[c6 + 2];
    destrow1[col] = srcrow[c6 + 4];
    destrow1[col + 1] = srcrow[c6 + 6];
  }
}

static void param_DecodeC8(const struct panasonic_raw_params_t *param,
			   struct bufio_t *bufio, unsigned int width,
			   unsigned int height, uint32_t left_margin,
			   uint16_t *raw_image, unsigned raw_width)
{
  const unsigned halfwidth = width / 2;
  const unsigned halfheight = height / 2;
  if (halfwidth == 0 || halfheight == 0)
    fatal("invalid input to DecodeC8");

  const unsigned doublewidth = 4 * halfwidth;
  int bitportion = 0; // actual range: -32 (or -31?) - 64
  uint64_t bittail = 0;
  uint32_t line_base[4];
  uint32_t current_base[4];
  for (int i = 0; i < 4; i++)
    line_base[i] = param->compression_initvalue[i];

  uint32_t outline[doublewidth];
  uint8_t extrahuff[0x10000];
  init_index_table(extrahuff, param);
  
  uint64_t inputqword = bufio_next_qword(bufio);
  
  for (unsigned current_row = 0; current_row < halfheight; current_row++) {
    for (int i = 0; i < 4; i++)
      current_base[i] = line_base[i];

    for (unsigned col = 0; col < doublewidth; col++) {
      uint64_t pixbits;
      if (bitportion < 0) {
	uint64_t inputqword_next = bufio_next_qword(bufio);
	bitportion += 64;
	pixbits = (inputqword_next >> bitportion) | (inputqword << (64 - bitportion));
	inputqword = inputqword_next;
      } else { // bitportion >= 0
	pixbits = (inputqword >> bitportion) | bittail;
	if (bitportion == 0) {
	  bitportion = 64;
	  inputqword = bufio_next_qword(bufio);
	}
      }
      // highest 16 bits of pixbits used to get huff_index
      const uint8_t huff_index = extrahuff[pixbits >> 48];
      const uint8_t nbits = param->compression_bits[huff_index];
      pixbits <<= nbits;
      int32_t delta;
      if (huff_index == 0) {
	delta = 0;
      } else {
	delta = (uint16_t) (pixbits >> (64 - huff_index));
	if ((int64_t) pixbits >= 0)
	  delta += 1 - (1 << huff_index);
      }
      int32_t val;
      switch (col & 3) {
      case 0:
	val = (int32_t) current_base[0] + delta;
	outline[col] = limit_nat(val, param->maxval);
	break;
      case 1:
	val = (int32_t) current_base[2] + delta;
	outline[col+1] = limit_nat(val, param->maxval);
	break;
      case 2:
	val = (int32_t) current_base[1] + delta;
	outline[col-1] = limit_nat(val, param->maxval);
	break;
      default:
	val = (int32_t) current_base[3] + delta;
	outline[col] = limit_nat(val, param->maxval);
	memcpy(current_base, &outline[col-3], sizeof current_base);
	memcpy(line_base, outline, sizeof line_base);
	break;
      }
      bittail = pixbits << huff_index;
      bitportion -= nbits + huff_index;
    }

    write_raw(raw_image, raw_width, current_row,
	      (const uint16_t *) outline, width, left_margin);
  }
}


void panasonic_new_load_raw(FILE *restrict input,
			    uint16_t *restrict raw_image,
			    unsigned int raw_width, unsigned int raw_height,
			    const struct panasonic_raw_params_t *restrict tags,
			    int verbose)
{
  unsigned totalw = 0;
  if (tags->stripe_count <= 0 || tags->stripe_count > 5)
    fatal("invalid stripe count");
  for (int i = 0; i < tags->stripe_count; i++) {
    if (tags->stripe_height[i] != raw_height)
      fatal("invalid stripe height");
    totalw += tags->stripe_width[i];
  }
  if (totalw != raw_width)
    fatal("invalid total stripe width");

  if (verbose)
    param_show(tags, stderr);
  struct bufio_t bufio;
  for (int stream = 0; stream < tags->stripe_count; stream++) {
    if (verbose)
      fprintf(stderr,
	      "Loading stripe %d: offset 0x%08x, compressed size: %u bits, width %d, height %u, left %u\n",
	      stream, tags->stripe_offsets[stream],
	      tags->stripe_compressed_size[stream],
	      tags->stripe_width[stream], tags->stripe_height[stream],
	      tags->stripe_left[stream]);
    bufio_init(&bufio, input, tags->stripe_offsets[stream],
	       tags->stripe_compressed_size[stream]);
    param_DecodeC8(tags, &bufio, tags->stripe_width[stream],
		   tags->stripe_height[stream], tags->stripe_left[stream],
		   raw_image, raw_width);
  }
}


static void init_index_table(uint8_t index[restrict 0x10000], const struct panasonic_raw_params_t *restrict p)
{
  uint16_t table1[17];
  uint16_t table2[17];
  
  for (int i = 0; i < 17; i++) {
    if (p->compression_param3[i] != 0)
      fatal("unsupported panasonic raw file: unexpected value in tag 0x0041");
  }

  for (unsigned hindex = 0; hindex < 17; hindex++) {
    uint8_t nbits = p->compression_bits[hindex];
    uint16_t v8 = 0;
    if (nbits != 0) {
      uint8_t h7 = nbits & 7;
      if (nbits >= 8) {
	uint32_t hdiff = h7 - nbits;
	v8 = 0;
	do {
	  v8 = (v8 << 8) | 0xFFu;
	  hdiff += 8;
	} while (hdiff);
      } else {
	v8 = 0;
      }
      v8 = (v8 << h7) | ((1 << h7) - 1);
    }
    uint16_t v9 = p->compression_param2[hindex] & v8;
    table1[hindex] = v9 << (16 - nbits);
    table2[hindex] = 0xFFFF << (16 - nbits);
  }

  for (size_t bits = 0; bits < 0x10000; bits++) {
    uint8_t idx;
    for (idx = 0; idx < 17; idx++) {
      if ((table2[idx] & bits) == table1[idx])
	break;
    }
    if (idx > 16)
      fatal("invalid coefficient index in table init");
    index[bits] = idx;
  }
}
