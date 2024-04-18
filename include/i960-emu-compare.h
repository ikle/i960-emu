/*
 * 80960 Emulator Comparator
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef I960_EMU_COMPARE_H
#define I960_EMU_COMPARE_H  1

#include <i960-emu.h>

static inline void i960_set_cond (struct i960 *o, uint32_t cc)
{
	o->ac = (o->ac & ~I960_CC_MASK) | cc;
}

static inline void i960_cmp (struct i960 *o, uint32_t a, uint32_t b, int I)
{
	const int lt = I ? (int32_t) a < (int32_t) b : a < b;

	i960_set_cond (o, lt ? 4 : a == b ? 2 : 1);
}

static inline void i960_concmp (struct i960 *o, uint32_t a, uint32_t b, int I)
{
	const int le = I ? (int32_t) a <= (int32_t) b : a <= b;

	if ((o->ac & 4) == 0)
		i960_set_cond (o, le ? 2 : 1);
}

#endif  /* I960_EMU_COMPARE_H */
