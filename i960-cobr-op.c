/*
 * 80960 Emulator COBR Format Operations
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * 20  testno	28  -		30  bbc		38  cmpibno
 * 21  testg	29  -		31  cmpobg	39  cmpibg
 * 22  teste	2A  -		32  cmpobe	3A  cmpibe
 * 23  testge	2B  -		33  cmpobge	3B  cmpibge
 * 24  testl	2C  -		34  cmpobl	3C  cmpibl
 * 25  testne	2D  -		35  cmpobne	3D  cmpibne
 * 26  testle	2E  -		36  cmpoble	3E  cmpible
 * 27  testo	2F  -		37  bbs		3F  cmpibo
 */

#include <i960-emu.h>
#include <i960-emu-bits.h>
#include <i960-emu-branch.h>
#include <i960-emu-compare.h>

static inline
void cobr_testcc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, int32_t disp)
{
	const size_t c = u32_extract (op, 19, 5);

	o->r[c] = i960_check_cond (o, op);
}

static inline
void cobr_bb (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, int32_t disp)
{
	const int C0 = u32_bit_select (op, 24 + 0);	/* ---1 0--x */
	const int ok = !(u32_bit_select (b, a) ^ C0);

	i960_set_cond (o, ok ? 2 : 0);

	if (ok)
		i960_b (o, o->ip + disp);
}

static inline
void cobr_cmpbcc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, int32_t disp)
{
	const int C3 = u32_bit_select (op, 24 + 3);	/* ---1 x--- */

	i960_cmp (o, a, b, C3);
	i960_bcc (o, op, o->ip + disp);
}

/*
 * Operation Entry Point
 *
 * decoder height = mux + max (mux, 3 * nand/nor) <= 4
 */
static
void cobr_op (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, int32_t disp)
{
	const int C4 = u32_bit_select (op, 24 + 4);	/* ---x ---- */

	if (!C4)
		cobr_testcc (o, op, a, b, disp);	/* ---0 ---- */
	else
	if (op == 0x30 || op == 0x37)			/* ---1 -000 */
		cobr_bb     (o, op, a, b, disp);	/* ---1 -111 */
	else
		cobr_cmpbcc (o, op, a, b, disp);
}

void i960_cobr (struct i960 *o, uint32_t op, uint32_t ip)
{
	const uint32_t ai = u32_extract (op, 19, 5);
	const uint32_t bi = u32_extract (op, 14, 5);
	const uint32_t a  = u32_bit_select (op, 13) ? ai : o->r[ai];
	const uint32_t b  = o->r[bi];
	const int32_t disp = (((int32_t) op << 19) >> 19) & ~3;

	cobr_op (o, op, a, b, disp);
}
