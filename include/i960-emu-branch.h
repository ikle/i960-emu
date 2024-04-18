/*
 * 80960 Emulator Branch Logic
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef I960_EMU_BRANCH_H
#define I960_EMU_BRANCH_H  1

#include <i960-emu.h>

static inline void i960_ldx (struct i960 *o, uint32_t efa, size_t c)
{
	size_t i;

	for (i = 0; i < 16; ++i)
		o->r[c + i] = i960_read_w (o, efa + i);
}

static inline void i960_stx (struct i960 *o, uint32_t efa, size_t c)
{
	size_t i;

	for (i = 0; i < 16; ++i)
		i960_write_w (o, efa + i, o->r[c + i]);
}

static inline void i960_b (struct i960 *o, uint32_t efa)
{
	o->ip = efa;
}

static inline void i960_bal (struct i960 *o, uint32_t efa, size_t link)
{
	o->r[link] = o->ip;		/* save next instruction address */

	i960_b (o, efa);
}

static inline void i960_call (struct i960 *o, uint32_t efa)
{
	const uint32_t fp = (o->r[I960_SP] + 63) & ~63;

	o->r[I960_RIP] = o->ip;		/* save next instruction address */

	i960_stx (o, o->r[I960_FP], 16);

	o->r[I960_PFP] = o->r[I960_FP];
	o->r[I960_FP]  = fp;
	o->r[I960_SP]  = fp + 64;

	i960_b (o, efa);
}

#define I960_CALL_LOCAL		0
#define I960_CALL_FAULT		1
#define I960_CALL_SYSTEM	2
#define I960_CALL_SYSTEM_T	3
#define I960_CALL_INTR_S	6
#define I960_CALL_INTR		7

// TBD: system, fault and interrupt call return
static inline void i960_ret (struct i960 *o)
{
	o->r[I960_FP] = o->r[I960_PFP] & ~63;

	i960_ldx (o, o->r[I960_FP], 16);

	i960_b (o, o->r[I960_RIP]);
}

/*
 * 80960 Conditional Branch Logic
 */
static inline int i960_check_cond (struct i960 *o, uint32_t op)
{
	const uint32_t cc = u32_extract (op, 24, 3);

	return (o->ac && cc) != 0 || (o->ac & I960_CC_MASK) == cc;
}

static inline void i960_bcc (struct i960 *o, uint32_t op, uint32_t efa)
{
	if (i960_check_cond (o, op))
		i960_b (o, efa);
}

static inline void i960_faultcc (struct i960 *o, uint32_t op, uint32_t efa)
{
	if (i960_check_cond (o, op))
		i960_fault (o, 0x50001);  /* constraint range */
}

#endif  /* I960_EMU_BRANCH_H */
