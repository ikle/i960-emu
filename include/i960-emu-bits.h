/*
 * 80960 Emulator Bit Access Helpers
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef I960_EMU_BITS_H
#define I960_EMU_BITS_H  1

#include <stdint.h>

static inline uint32_t u32_bit_select (uint32_t x, uint32_t pos)
{
	return (x >> (pos & 31)) & 1;  /* u32_extract (x, pos, 1) */
}

static inline uint32_t u32_bit_mask (uint32_t pos)
{
	return (uint32_t) 1 << (pos & 31);
}

static inline uint32_t u32_extract (uint32_t x, uint32_t pos, uint32_t count)
{
	return (x >> (pos & 31)) & ~(~(uint32_t) 0 << count);
}

#endif  /* I960_EMU_BITS_H */
