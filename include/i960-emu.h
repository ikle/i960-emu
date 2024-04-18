/*
 * 80960 Emulator
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#ifndef I960_EMU_H
#define I960_EMU_H  1

#include <stddef.h>
#include <stdint.h>

#define I960_PFP		0	/* r0, previous frame pointer	*/
#define I960_SP			1	/* r1, stack pointer		*/
#define I960_RIP		2	/* return instruction pointer	*/
#define I960_LP			30	/* g14, link pointer		*/
#define I960_FP			31	/* g15, frame pointer		*/

#define I960_CC_MASK		0x7	/* AC, condition code mask	*/
#define I960_OF_POS		8	/* AC, integer overflow bit	*/
#define I960_OM_POS		12	/* AC, overflow mask bit	*/
#define I960_BIF_POS		15	/* AC, no-imprecise faults	*/

#define I960_TE_POS		0	/* PC, trace enable		*/
#define I960_EM_POS		1	/* PC, execution mode		*/
#define I960_TFP_POS		10	/* PC, trace fault pending	*/
#define I960_S_POS		13	/* PC, state			*/
#define I960_P_POS		16	/* PC, priority			*/
#define I960_P_MASK		0x1f

struct i960 {
	uint32_t r[32], ip, ac, pc, tc;
};

uint8_t  i960_read_b (struct i960 *o, uint32_t addr);
uint16_t i960_read_s (struct i960 *o, uint32_t addr);
uint32_t i960_read_w (struct i960 *o, uint32_t addr);

void i960_write_b (struct i960 *o, uint32_t addr, uint32_t x);
void i960_write_s (struct i960 *o, uint32_t addr, uint32_t x);
void i960_write_w (struct i960 *o, uint32_t addr, uint32_t x);

void i960_fault (struct i960 *o, int type);
void i960_calls (struct i960 *o, int type);

static inline void i960_lock   (struct i960 *o) {}
static inline void i960_unlock (struct i960 *o) {}

#endif  /* I960_EMU_H */
