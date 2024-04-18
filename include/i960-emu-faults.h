/*
 * 80960 Emulator Fault Helpers
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef I960_EMU_FAULTS_H
#define I960_EMU_FAULTS_H  1

#include <i960-emu.h>
#include <i960-emu-bits.h>

static inline void i960_on_undef (struct i960 *o)
{
	i960_fault (o, 0x20001);	/* invalid opcode */
}

static inline void i960_on_overflow (struct i960 *o)
{
	if (u32_bit_select (o->ac, I960_OM_POS))	/* if masked	*/
		o->ac |= u32_bit_mask (I960_OF_POS);	/* set flag	*/
	else
		i960_fault (o, 0x30001);	/* integer overflow	*/
}

#endif  /* I960_EMU_FAULTS_H */
