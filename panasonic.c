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

struct param_t {
  uint32_t tag39[6];
  uint32_t tag3A[6];
  uint16_t maxval;
  uint16_t initial[4];
  // bits 24-31: tag41, 0-64
  // bits 16-23: tag40a, 0-17
  // bits 0-15: tag40b, 0-0x0fff
  uint32_t huff_coeff[17];
  uint64_t hufftable1[17];
  uint64_t hufftable2[17];
  bool use_gamma;
  uint16_t gamma_table[0x10000];
  bool use_extrahuff;
  uint8_t extrahuff[0x10000];
};

static uint16_t param_gammaCurve(const struct param_t *p, uint16_t value);
static void param_init(struct param_t *p,
		       const struct panasonic_raw_tags_t *meta);

static void param_show(const struct param_t *p, FILE *f)
{
  fprintf(f, "Panasonic Compression 8 parameters:\n");
  fprintf(f, "  Gamma: %s\n", p->use_gamma ? "yes" : "no");
  for (int i = 0; i < 17; i++) {
    fprintf(f, "  Huff %02d: %08x %016lx %016lx\n",
	    i, p->huff_coeff[i], p->hufftable1[i], p->hufftable2[i]);
  }
  fprintf(f, "  Extra Huffmann table: %s\n",
	  p->use_extrahuff ? "yes" : "no");
  if (p->use_extrahuff) {
    size_t start = 0;
    uint8_t val = p->extrahuff[0];
    for (size_t i = 1; i < 0x10000; i++) {
      if (p->extrahuff[i] != val) {
	fprintf(f, "      %5d - %5d: %2d\n", (int) start, (int) (i - 1),
		val);
	start = i;
	val = p->extrahuff[i];
      }
    }
    fprintf(f, "      %5d - %5d: %2d\n", (int) start, 0xffff, val);
  }
  fprintf(f, "  Initial: ");
  for (int i = 0; i < 4; i++)
    fprintf(f, " %6u", p->initial[i]);
  fprintf(f, "\n  Tag 0x39:");
  for (int i = 0; i < 6; i++)
    fprintf(f, " %6u", p->tag39[i]);
  fprintf(f, "\n  Tag 0x3A:");
  for (int i = 0; i < 6; i++)
    fprintf(f, " %6u", p->tag3A[i]);
  fprintf(f, "\n  Maxval (Tag 0x3B): %u\n", p->maxval);
}

static uint8_t param_GetDBit(const struct param_t *p, uint64_t bits)
{
  for (uint8_t i = 0; i < 17; i++) {
    if ((bits & p->hufftable2[i]) == p->hufftable1[i])
      return i;
  }
  return 17;
}

static uint16_t clamp_u16(uint16_t value, uint16_t max)
{
  return (value <= max) ? value : max;
}

static uint32_t limit_nat(int32_t value, uint32_t max)
{
  if (value < 0)
    return 0;
  if ((uint32_t) value > max)
    return max;
  return (uint32_t) value;
}

static void write_raw(const struct param_t *restrict param,
		      uint16_t *restrict raw_image, unsigned raw_width,
		      unsigned current_row,
		      const uint16_t *restrict srcrow,
		      unsigned int width, uint16_t left_margin)
{
  unsigned destrow = current_row * 2;
  uint16_t *destrow0 = raw_image + (destrow * raw_width) + left_margin;
  uint16_t *destrow1 = raw_image + (destrow + 1) * raw_width + left_margin;
  if (param->use_gamma) {
    for (unsigned col = 0; col < width - 1; col += 2) {
      const unsigned c6 = col * 4;
      destrow0[col] = param->gamma_table[srcrow[c6]];
      destrow0[col + 1] = param->gamma_table[srcrow[c6 + 2]];
      destrow1[col] = param->gamma_table[srcrow[c6 + 4]];
      destrow1[col + 1] = param->gamma_table[srcrow[c6 + 6]];
    }
  } else {
    for (unsigned col = 0; col < width - 1; col += 2) {
      const unsigned c6 = col * 4;
      destrow0[col] = srcrow[c6];
      destrow0[col + 1] = srcrow[c6 + 2];
      destrow1[col] = srcrow[c6 + 4];
      destrow1[col + 1] = srcrow[c6 + 6];
    }
  }
}

static void param_DecodeC8(const struct param_t *param,
			   struct bufio_t *bufio, unsigned int width,
			   unsigned int height, uint16_t left_margin,
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
    line_base[i] = param->initial[i];

  uint8_t outline[4 * doublewidth];

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
      uint8_t huff_index;
      // highest 16 bits of pixbits used to get huff_index
      if (param->use_extrahuff)
	huff_index = param->extrahuff[(pixbits >> 48) & 0xffffu];
      else {
	huff_index = param_GetDBit(param, pixbits);
	if (huff_index > 16)
	  fatal("internal error 3");
      }
      const uint32_t hc = param->huff_coeff[huff_index];
      const uint8_t hc2_5 = (hc >> 16) & 0x1F; // actual range 0-16 inclusive
      pixbits <<= hc2_5;
      const int32_t v39 = (huff_index == 0) ? 0 : (uint16_t) (pixbits >> (64 - huff_index));
      int32_t delta;
      if ((int64_t) pixbits < 0)
	delta = (uint16_t) v39;
      else if (huff_index != 0)
	delta = (uint16_t) v39 - (1 << huff_index) + 1;
      else
	delta = 0;

      uint32_t *destpixel = (uint32_t *) (outline + 16 * (col >> 2));

      int32_t val;
      switch (col & 3) {
      case 0:
	val = (int32_t) current_base[0] + delta;
	destpixel[0] = limit_nat(val, param->maxval);
	break;
      case 1:
	val = (int32_t) current_base[2] + delta;
	destpixel[2] = limit_nat(val, param->maxval);
	break;
      case 2:
	val = (int32_t) current_base[1] + delta;
	destpixel[1] = limit_nat(val, param->maxval);
	break;
      default:
	val = (int32_t) current_base[3] + delta;
	destpixel[3] = limit_nat(val, param->maxval);
	memcpy(current_base, destpixel, sizeof current_base);
	memcpy(line_base, outline, sizeof line_base);
	break;
      }
      bittail = pixbits << huff_index;
      bitportion -= hc2_5 + huff_index;
    }

    write_raw(param, raw_image, raw_width, current_row,
	      (const uint16_t *) outline, width, left_margin);
  }
}


void panasonicC8_load_raw(FILE *input,
			  uint16_t *raw_image,
			  unsigned int raw_width, unsigned int raw_height,
			  const struct panasonic_raw_tags_t *tags,
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

  struct param_t param;
  param_init(&param, tags);
  if (verbose)
    param_show(&param, stderr);
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
    param_DecodeC8(&param, &bufio, tags->stripe_width[stream],
		   tags->stripe_height[stream], tags->stripe_left[stream],
		   raw_image, raw_width);
  }
}

static void param_init(struct param_t *p,
		       const struct panasonic_raw_tags_t *meta)
{
  for (int i = 0; i < 6; i++) {
    p->tag3A[i] = meta->tag3A[i];
    p->tag39[i] = meta->tag39[i];
  }
  p->maxval = meta->tag3B;
  for (int i = 0; i < 4; i++)
    p->initial[i] = meta->initial[i];

  for (int i = 0; i < 17; i++) {
    if (meta->tag41[i] != 0)
      fatal("unsupported raw file: unexpected value in tag 0x0041");
    p->huff_coeff[i] =
      ((uint32_t) (meta->tag41[i]) << 24) |
      ((uint32_t) (meta->tag40a[i]) << 16) |
      meta->tag40b[i];
  }

  p->use_gamma = false;
  for (unsigned i = 0; i < 0x10000; i++) {
    uint16_t val = param_gammaCurve(p, (uint16_t) i);
    p->gamma_table[i] = val;
    if (i != val)
      p->use_gamma = true;
  }

  uint32_t v7 = 0;
  for (unsigned hindex = 0; hindex < 17; hindex++) {
    uint32_t hc = p->huff_coeff[hindex];
    uint32_t hlow = (hc >> 16) & 0x1F;
    int16_t v8 = 0;
    if ((hc & 0x1F0000) != 0) {
      int h7 = hlow & 7;
      if (hlow - 1 >= 7) {
	uint32_t hdiff = h7 - hlow;
	v8 = 0;
	do {
	  v8 = (v8 << 8) | 0xFFu;
	  hdiff += 8;
	} while (hdiff);
      } else {
	v8 = 0;
      }
      for (; h7 != 0; h7--)
	v8 = 2 * v8 + 1;
    }

    uint16_t v9 = hc & v8;
    if (v7 < hlow)
      v7 = hlow;
    p->hufftable2[hindex] = 0xFFFFULL << (64 - hlow);
    p->hufftable1[hindex] = (uint64_t) v9 << (64 - hlow);
  }

  p->use_extrahuff = (v7 < 17);
  if (p->use_extrahuff) {
    uint64_t v17 = 0;
    for (int j = 0; j < 0x10000; j++) {
      p->extrahuff[j] = param_GetDBit(p, v17);
      if (p->extrahuff[j] > 16)
	fatal("invalid coefficient index in table init");
      v17 += 0x1000000000000ULL;
    }
  }
}

static uint16_t param_gammaCurve(const struct param_t *p, uint16_t value)
{
  unsigned int v2 = value | 0xFFFF0000;
  if ((value & 0x10000) == 0)
    v2 = value & 0x1FFFF;

  unsigned int v4 = (v2 < 0xFFFF) ? v2 : 0xFFFF;

  int v5 = 0;
  if ((v4 & 0x80000000) != 0)
    v4 = 0;

  if (v4 >= (0xFFFF & p->tag3A[1])) {
    v5 = 1;
    if (v4 >= (0xFFFF & p->tag3A[2])) {
      v5 = 2;
      if (v4 >= (0xFFFF & p->tag3A[3])) {
	v5 = 3;
	if (v4 >= (0xFFFF & p->tag3A[4]))
	  v5 = ((v4 | 0x500000000LL) -
		(uint64_t) (0xFFFF & p->tag3A[5])) >> 32;
      }
    }
  }
  unsigned int v6 = p->tag3A[v5];
  uint32_t v7 = p->tag39[v5];
  unsigned int v8 = v4 - (uint16_t) v6;
  char v9 = v7 & 0x1F;
  int64_t result = 0;

  if (v9 == 31) {
    result = (v5 == 5) ? 0xFFFFLL : ((p->tag3A[v5 + 1] >> 16) & 0xFFFF);
    return clamp_u16((uint16_t) result, p->maxval);
  }
  if ((v7 & 0x10) == 0) {
    if (v9 == 15) {
      result = ((v6 >> 16) & 0xFFFF);
      return clamp_u16((uint16_t) result, p->maxval);
    } else if (v9 != 0) {
      v8 = (v8 + (1 << (v9 - 1))) >> v9;
    }
  } else {
    v8 <<= v7 & 0xF;
  }
  result = v8 + ((v6 >> 16) & 0xFFFF);
  if (result < 0)
    fatal("negative result of gamma");
  return clamp_u16((uint16_t) result, p->maxval);
}
