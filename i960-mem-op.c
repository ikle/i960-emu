/*
 * 80960 Emulator MEM Format Operations
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * 80  ldob	82  stob	C0  ldib	C2  stib
 * 88  ldos	8A  stos	C8  ldis	CA  stis
 * 90  ld	92  st
 * 98  ldl	9A  stl
 * A0  ldt	A2  stt
 * B0  ldq	B2  stq
 *
 * 84  bx	85  balx	86  callx	8C  lda
 *
 * C1    -- store vs load
 * C2    -- funcs vs transfer
 * C5:3  -- transfer type (size)
 * C6    -- integer vs ordinal
 */

#include <i960-emu.h>
#include <i960-emu-bits.h>
#include <i960-emu-branch.h>
#include <i960-emu-faults.h>

/*
 * Non-memory Access Functions
 *
 * decoder height = 3
 */
static void mem_funcs (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C3 = u32_bit_select (op, 24 + 3);      /* ---- x1-- */
	const uint32_t i = u32_extract (op, 24 + 0, 2);  /* ---- 01xx */

	if (C3)
		o->r[c] = efa;				/* 11--  lda	*/
	else
		switch (i) {
		case 0:  i960_b    (o, efa);     break;	/* 0100  bx	*/
		case 1:  i960_bal  (o, efa, c);  break;	/* 0101  balx	*/
		case 2:  i960_call (o, efa);     break;	/* 011-  callx	*/
		case 3:  i960_call (o, efa);     break;	/* 011-  filler	*/
		}
}

/*
 * Memory Loader Functions
 *
 * decoder height = 3, sign-extension delayed
 */
static inline void mem_ldb (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C6 = u32_bit_select (op, 24 + 6);  /* -x00 000- */
	const uint8_t x = i960_read_b (o, efa);

	o->r[c] = C6 ? (int8_t) x : x;  /* if integer then sign-extend */
}

static inline void mem_lds (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C6 = u32_bit_select (op, 24 + 6);  /* -x00 100- */
	const uint16_t x = i960_read_s (o, efa);

	o->r[c] = C6 ? (int16_t) x : x;  /* if integer then sign-extend */
}

static inline void mem_ld (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	o->r[c] = i960_read_w (o, efa);
}

static inline void mem_ldl (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	o->r[c | 0] = i960_read_w (o, efa + 0);
	o->r[c | 1] = i960_read_w (o, efa + 4);
}

static inline void mem_ldt (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	o->r[c | 0] = i960_read_w (o, efa + 0);
	o->r[c | 1] = i960_read_w (o, efa + 4);
	o->r[c | 2] = i960_read_w (o, efa + 8);
}

static inline void mem_ldq (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	o->r[c | 0] = i960_read_w (o, efa + 0);
	o->r[c | 1] = i960_read_w (o, efa + 4);
	o->r[c | 2] = i960_read_w (o, efa + 8);
	o->r[c | 3] = i960_read_w (o, efa + 12);
}

static void mem_load (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 3, 3);  /* --xx x00- */

	switch (i) {
	case 0:  mem_ldb (o, op, efa, c);  break;  /* --00 000-		*/
	case 1:  mem_lds (o, op, efa, c);  break;  /* --00 100-		*/
	case 2:  mem_ld  (o, op, efa, c);  break;  /* --01 000-		*/
	case 3:  mem_ldl (o, op, efa, c);  break;  /* --01 100-		*/
	case 4:  mem_ldt (o, op, efa, c);  break;  /* --10 -00-		*/
	case 5:  mem_ldt (o, op, efa, c);  break;  /* --10 -00-  filler	*/
	case 6:  mem_ldq (o, op, efa, c);  break;  /* --11 -00-		*/
	case 7:  mem_ldq (o, op, efa, c);  break;  /* --11 -00-  filler	*/
	}
}

/*
 * Memory Storer Functions
 *
 * decoder height = 3, overflow detection delayed
 */
static inline void mem_stb (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C6 = u32_bit_select (op, 24 + 6);  /* -x00 001- */
	const int32_t x = o->r[c];

	i960_write_b (o, efa, x);

	if (C6 && x != (int8_t) x)	/* if integer then check for overflow */
		i960_on_overflow (o);
}

static inline void mem_sts (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C6 = u32_bit_select (op, 24 + 6);  /* -x00 101- */
	const int32_t x = o->r[c];

	i960_write_s (o, efa, x);

	if (C6 && x != (int16_t) x)	/* if integer then check for overflow */
		i960_on_overflow (o);
}

static inline void mem_st (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	i960_write_w (o, efa, o->r[c]);
}

static inline void mem_stl (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	i960_write_w (o, efa +  0, o->r[c | 0]);
	i960_write_w (o, efa +  4, o->r[c | 1]);
}

static inline void mem_stt (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	i960_write_w (o, efa +  0, o->r[c | 0]);
	i960_write_w (o, efa +  4, o->r[c | 1]);
	i960_write_w (o, efa +  8, o->r[c | 2]);
}

static inline void mem_stq (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	i960_write_w (o, efa +  0, o->r[c | 0]);
	i960_write_w (o, efa +  4, o->r[c | 1]);
	i960_write_w (o, efa +  8, o->r[c | 2]);
	i960_write_w (o, efa + 12, o->r[c | 3]);
}

static void mem_store (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 3, 3);  /* --xx x01- */

	switch (i) {
	case 0:  mem_stb (o, op, efa, c);  break;  /* --00 001-		*/
	case 1:  mem_sts (o, op, efa, c);  break;  /* --00 101-		*/
	case 2:  mem_st  (o, op, efa, c);  break;  /* --01 001-		*/
	case 3:  mem_stl (o, op, efa, c);  break;  /* --01 101-		*/
	case 4:  mem_stt (o, op, efa, c);  break;  /* --10 -01-		*/
	case 5:  mem_stt (o, op, efa, c);  break;  /* --10 -01-  filler	*/
	case 6:  mem_stq (o, op, efa, c);  break;  /* --11 -01-		*/
	case 7:  mem_stq (o, op, efa, c);  break;  /* --11 -01-  filler	*/
	}
}

/*
 * Operation Entry Point
 *
 * decoder height = 2 + 3 = 5
 */
void mem_op (struct i960 *o, uint32_t op, uint32_t efa, size_t c)
{
	const int C1 = u32_bit_select (op, 24 + 1);  /* ---- --x- */
	const int C2 = u32_bit_select (op, 24 + 2);  /* ---- -x-- */

	if (C2)		mem_funcs (o, op, efa, c);  /* ---- -1-- */
	else if (C1)	mem_store (o, op, efa, c);  /* ---- -01- */
	else		mem_load  (o, op, efa, c);  /* ---- -00- */
}
