/*
 * 80960 Emulator
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <i960-emu.h>
#include <i960-emu-bits.h>
#include <i960-emu-branch.h>
#include <i960-emu-compare.h>
#include <i960-emu-faults.h>

static inline uint32_t i960_read_lock (struct i960 *o, uint32_t addr)
{
	i960_lock (o);
	return i960_read_w (o, addr);
}

static inline void i960_write_unlock (struct i960 *o, uint32_t addr, uint32_t x)
{
	i960_write_w (o, addr, x);
	i960_unlock (o);
}

//

static inline uint32_t u32_modify (uint32_t old, uint32_t new, uint32_t mask)
{
	return (old & ~mask) | (new & mask);
}

//

static inline int i960_div_check (struct i960 *o, uint32_t d)
{
	if (d == 0)
		i960_fault (o, 0x30002);	/* division by zero */

	return d != 0;
}

static inline int i960_check_em (struct i960 *o)
{
	const int em = u32_bit_select (o->pc, I960_EM_POS);

	if (em == 0)
		i960_fault (o, 0xa0001);	/* type mismatch */

	return em;
}

/*
 * MP Adder Helpers
 */
static inline char u32_add (uint32_t *r, uint32_t x, uint32_t y)
{
	*r = x + y;
	return *r < x;
}

static inline char u32_adc (uint32_t *r, uint32_t x, uint32_t y, char c)
{
	uint32_t a;

	c = u32_add (&a, y, c);
	return c + u32_add (r, x, a);
}

static inline char u32_sub (uint32_t *r, uint32_t x, uint32_t y)
{
	*r = x - y;
	return *r > x;
}

static inline char u32_sbb (uint32_t *r, uint32_t x, uint32_t y, char c)
{
	uint32_t a;

	c = u32_add (&a, y, c);
	return c + u32_sub (r, x, a);
}

/*
 * FPU Not Implemented
 */
static inline
void i960_fpu (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	i960_on_undef (o);
}

/*
 *
 */
#define I960_DEF_REG(name, expr)					\
static inline void							\
reg_##name (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c) \
{									\
	o->r[c] = expr;							\
}

static inline uint32_t u32_setbit (uint32_t x, uint32_t pos)
{
	return x | u32_bit_mask (pos);
}

static inline uint32_t u32_clrbit (uint32_t x, uint32_t pos)
{
	return x & ~u32_bit_mask (pos);
}

static inline uint32_t u32_notbit (uint32_t x, uint32_t pos)
{
	return x ^ u32_bit_mask (pos);
}

static inline int i32_check_overflow (int32_t a, int32_t b, int32_t r)
{
	return (~(a ^ b) & (b ^ r)) < 0;  /* (bs == as) && (bs != rs) */
}

/*
 * 80960 REG Format: Bit Maniputations Operations
 *
 * 580  notbit		- btc, μ
 * 583  setbit		- bts, μ
 * 58C  clrbit		- btr, μ
 * 58F  alterbit	- μ
 *
 * oe      = nor (F3 ^ F2, F1 ^ F0) -- if zero then do bitwise ops
 * use xor = nor (F2, F1)
 * use set = nand (F2, F1) ? F1 : ac[1]
 */
I960_DEF_REG (notbit,   u32_notbit (b, a))
I960_DEF_REG (setbit,   u32_setbit (b, a))
I960_DEF_REG (clrbit,   u32_clrbit (b, a))
I960_DEF_REG (alterbit, o->ac & 2 ? u32_setbit (b, a) : u32_clrbit (b, a))

/*
 * 80960 REG Format: Bitwise Operations
 *
 * 580  notbit	-  xor ( m,  b)	-  and ( a,  b)	- 0000 μ
 * 581  and	-		-  and ( a,  b)	- 0001
 * 582  andnot	-		-  and (~a,  b)	- 0010
 * 583  setbit	- nand (~m, ~b)	- nand (~a,  b)	- 0011 μ
 * 584  notand	-		-  and ( a, ~b)	- 0100
 * 585  -	-		-  and ( a, ~b)	- 0101
 * 586  xor	-  xor (~a, ~b)	-  and (~a, ~b)	- 0110 x
 * 587  or	-		- nand (~a, ~b)	- 0111
 * 588  nor	-		-  and (~a, ~b)	- 1000
 * 589  xnor	- xnor (~a, ~b)	- nand (~a, ~b)	- 1001 x
 * 58A  not	- ornot (a, 0)	- nand ( a, ~b) - 1010
 * 58B  ornot	-		- nand ( a, ~b)	- 1011
 * 58C  clrbit	-  and (~m,  b)	-  and (~a,  b)	- 1100 μ
 * 58D  notor	-		- nand (~a,  b)	- 1101
 * 58E  nand	-		- nand ( a,  b)	- 1110
 * 58F  alterbit-      ( m,  b)	- nand ( a,  b)	- 1111 μ
 *
 * invert a = (F3 ^ F1)
 * invert b = (F3 ^ F2)
 * invert q = nand (F3 ^ F2, F3 ^ F1)
 * use xor  = nor (nand (F3 ^ F2, F3 ^ F1), F3 ^ F0)
 */
static inline
void reg_log_core (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F1 = u32_bit_select (op, 7 + 1);
	const int F2 = u32_bit_select (op, 7 + 2);
	const int F3 = u32_bit_select (op, 7 + 3);
	const int IA = F3 != F1;
	const int IB = F3 != F2;
	const int IQ = !(IA && IB);
	const int SX = !(IQ | (F3 != F0));

	const uint32_t A = IA ? ~a : a;
	const uint32_t B = IB ? ~b : b;
	const uint32_t q = SX ? ~(a ^ b) : ~(A & B);

	o->r[c] = IQ ? ~q : q;
}

I960_DEF_REG (and,     ( a &  b))
I960_DEF_REG (andnot,  (~a &  b))
I960_DEF_REG (notand,  ( a & ~b))
I960_DEF_REG (xor,     ( a ^  b))
I960_DEF_REG (or,      ( a |  b))
I960_DEF_REG (nor,    ~( a |  b))
I960_DEF_REG (xnor,   ~( a ^  b))
I960_DEF_REG (not,     (~a     ))
I960_DEF_REG (ornot,   (~a |  b))
I960_DEF_REG (notor,   ( a | ~b))
I960_DEF_REG (nand,   ~( a &  b))

void reg_log (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F = u32_extract (op, 7, 4);

	switch (F) {
	case 0x0:  reg_notbit   (o, op, a, b, c);  break;
	case 0x1:  reg_and      (o, op, a, b, c);  break;
	case 0x2:  reg_andnot   (o, op, a, b, c);  break;
	case 0x3:  reg_setbit   (o, op, a, b, c);  break;
	case 0x4:  reg_notand   (o, op, a, b, c);  break;
	case 0x5:  reg_notand   (o, op, a, b, c);  break;  /* filler */
	case 0x6:  reg_xor      (o, op, a, b, c);  break;
	case 0x7:  reg_or       (o, op, a, b, c);  break;
	case 0x8:  reg_nor      (o, op, a, b, c);  break;
	case 0x9:  reg_xnor     (o, op, a, b, c);  break;
	case 0xA:  reg_not      (o, op, a, b, c);  break;
	case 0xB:  reg_ornot    (o, op, a, b, c);  break;
	case 0xC:  reg_clrbit   (o, op, a, b, c);  break;
	case 0xD:  reg_notor    (o, op, a, b, c);  break;
	case 0xE:  reg_nand     (o, op, a, b, c);  break;
	case 0xF:  reg_alterbit (o, op, a, b, c);  break;
	}
}

/*
 * 80960 REG Format: Adder Operations
 *
 * 590  addo	591  addi	592  subo	593  subi
 * 5B0  addc	5B1  -		5B2  subc	5B3  -
 *
 * F0  -- integer vs ordinal
 * F1  -- sub vs add: invert a and carry-in
 * C1  -- with carry
 */
static inline
void reg_add (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F1 = u32_bit_select (op, 7 + 1);

	o->r[c] = F1 ? b - a : b + a;

	if (F0 && i32_check_overflow (a, b, o->r[c]))
		i960_on_overflow (o);
}

void reg_addc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F1 = u32_bit_select (op, 7 + 1);

	uint32_t r;
	const char co = F1 ? u32_sbb (&r, a, b, (o->ac & 2) != 0) :
			     u32_adc (&r, a, b, (o->ac & 2) != 0);
	o->r[c] = r;
	i960_set_cond (o, (co << 1) | i32_check_overflow (a, b, r));
}

/*
 * 80960 REG Format: Extended Compare Operations (J)
 *
 * 594  cmpob	595  cmpib	596  cmpos	597  cmpis
 *
 * F0  -- integer vs ordinal
 * F1  -- short vs byte
 */
static inline
void reg_cmpe (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F1 = u32_bit_select (op, 7 + 1);

	a = F1 ? F0 ? (int16_t) a : (uint16_t) a :
		 F0 ? (int8_t)  a : (uint8_t)  a ;
	b = F1 ? F0 ? (int16_t) b : (uint16_t) b :
		 F0 ? (int8_t)  b : (uint8_t)  b ;

	i960_cmp (o, a, b, F0);
}

/*
 * op 590..597
 */
void reg_addx (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F2)	reg_cmpe (o, op, a, b, c);
	else	reg_add  (o, op, a, b, c);
}

/*
 * 80960 REG Format: Shift Operations
 *
 * 598  shro		59C  shlo	- (0, b) vs (b, 0)
 * 599  -		59D  rotate	- (-, -) vs (b, b)
 * 59A  shrdi		59E  shli	- (s, b) vs (b, 0)
 * 59B  shri		59F  -		- (s, b) vs (-, -)
 *
 * 5C8  -	5C9  -		5CA  -		5CB
 * 5D8  eshro	5D9  -		5DA  -		5DB
 * 5E8  -	5E9  -		5EA  -		5EB
 * 5F8  -	5F9  -		5FA  -		5FB
 *
 * F0  -- shri vs shrdi
 * F0  -- rotate vs shl
 * F1  -- integer vs ordinal
 * F2  -- left vs right
 *
 * C2  -- eshro vs shro	- C
 */
static inline
void reg_eshro (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t bh = o->r[u32_extract (op, 14, 5) | 1];
	const uint64_t bl = (uint64_t) bh << 32 | b;

	o->r[c] = (bl >> (a & 31));		/* (bh, bl) >> (n & 31) */
}

static inline
void reg_shro (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
//	const int C2 = u32_bit_select (op, 24 + 2);
//
//	if (C2)
//		reg_eshro (o, op, a, b, c);
//	else
		o->r[c] = a < 32 ? b >> a : 0;	/* (0, b) >> n */
}

static inline
void reg_shrdi (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t n = a < 32 ? a : 31;

	o->r[c] = (int32_t) b >> n;

//	if ((int32_t) b < 0 && b != (o->r[c] << n))  /* round to zero */
	if (b < (o->r[c] << n))  /* round to zero */
		o->r[c] += 1;
}

static inline
void reg_shri (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = (int32_t) b >> (a < 32 ? a : 31);  /* (s, b) >> n */
}

static inline
void reg_shlo (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = a < 32 ? b << a : 0;		/* (b, 0) >> (-n & 31) */
}

static inline
void reg_rotate (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = b << (a & 31) | b >> (-a & 31);  /* (b, b) >> (-n & 31) */
}

static inline
void reg_shli (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int64_t  x = (int32_t) b;
	const uint64_t r = x << (a < 32 ? a : 32);

	o->r[c] = r;

	if ((r ^ x) >> 31 != 0)
		i960_on_overflow (o);
}

/*
 * op 598..59F, decoder height = 3
 */
void reg_shift (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F = u32_extract (op, 7, 3);

	switch (F) {
	case 0:  reg_shro   (o, op, a, b, c);	break;
	case 1:  reg_shro   (o, op, a, b, c);	break;  /* filler */
	case 2:  reg_shrdi  (o, op, a, b, c);	break;
	case 3:  reg_shri   (o, op, a, b, c);	break;
	case 4:  reg_shlo   (o, op, a, b, c);	break;
	case 5:  reg_rotate (o, op, a, b, c);	break;
	case 6:  reg_shli   (o, op, a, b, c);	break;
	case 7:  reg_rotate (o, op, a, b, c);	break;  /* filler */
	}
}

static inline
void reg_59 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F3 = u32_bit_select (op, 7 + 3);

	if (F3)	reg_shift (o, op, a, b, c);
	else	reg_addx  (o, op, a, b, c);
}

/*
 * 80960 REG Format: Compare Operations
 *
 * 5A0  cmpo		5A1  cmpi	5A2  concmpo	5A3  concmpi
 * 5A4  cmpinco		5A5  cmpinci	5A6  cmpdeco	5A7  cmpdeci
 *
 * F0		-- integer vs ordinal
 * F1		-- sub vs add (dec vs inc)
 * F1 & !F2	-- concmp vs cmp
 * F2		-- inc/dec after comparision
*/
void reg_cmp (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F1 = u32_bit_select (op, 7 + 1);
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F1 && !F2)
		i960_concmp (o, a, b, F0);
	else
		i960_cmp (o, a, b, F0);

	if (F2)
		reg_add (o, op & ~1, 1, b, c);
}

/*
 * 80960 REG Format: Misc Operations
 *
 * 5AC  scanbyte	5AD  bswap	5AE  chkbit	5AF  -
 *
 * bswap	- J
 * chkbit	- bt
 *
 * F0  -- bswap vs scanbyte
 * F1  -- chkbit vs bswap/scanbyte
 */
static inline
void reg_scanbyte (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
#if 1
	const uint32_t c0 = a ^ b;
	const uint32_t c1 = (c0 >> 16) & c0;
	const uint32_t c2 = (c1 >>  8) & c1;
	const int cond = (c2 & 0xff) == 0;
#else
	const int cond = (a & 0x000000ff) == (b & 0x000000ff) ||
			 (a & 0x0000ff00) == (b & 0x0000ff00) ||
			 (a & 0x00ff0000) == (b & 0x00ff0000) ||
			 (a & 0xff000000) == (b & 0xff000000);
#endif
	i960_set_cond (o, cond ? 2 : 0);
}

#include <byteswap.h>

static inline
void reg_bswap (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	/* (rol (a, 8) & 0x00ff00ff) | (rol (a, 24) & 0xff00ff00) */
	o->r[c] = bswap_32 (a);
}

static inline
void reg_chkbit (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	i960_set_cond (o, u32_bit_select (b, a) ? 2 : 0);
}

static inline
void reg_misc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F1 = u32_bit_select (op, 7 + 1);

	if (F1)		reg_chkbit   (o, op, a, b, c);
	else if (F0)	reg_bswap    (o, op, a, b, c);
	else		reg_scanbyte (o, op, a, b, c);
}

static inline
void reg_5A (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F3 = u32_bit_select (op, 7 + 3);

	if (F3)	reg_misc (o, op, a, b, c);
	else	reg_cmp  (o, op, a, b, c);
}

/*
 * 80960 REG Format: Interrupt Operations (J)
 *
 * 5B4  intdis		- cli
 * 5B5  inten		- sti
 *
 * F0  -- enable vs disable interrupts
 * F2  -- manage interrupts
 */
#define I960_ICON	0xff008510	/* interrupt control register	*/
#define I960_ICON_GIE	10		/* global interrupt enable	*/

static inline
void reg_intdis (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t icon = i960_read_w (o, I960_ICON);

	if (i960_check_em (o))
		i960_write_w (o, I960_ICON, u32_setbit (icon, I960_ICON_GIE));
}

static inline
void reg_inten (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t icon = i960_read_w (o, I960_ICON);

	if (i960_check_em (o))
		i960_write_w (o, I960_ICON, u32_clrbit (icon, I960_ICON_GIE));
}

static inline
void reg_5B (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F2 = u32_bit_select (op, 7 + 2);

	if (!F2)	reg_addc   (o, op, a, b, c);
	else if (F0)	reg_inten  (o, op, a, b, c);
	else		reg_intdis (o, op, a, b, c);
}

/*
 * 80960 REG Format: Move Operations
 *
 * 5C8  -	5C9  -		5CA  -		5CB
 * 5D8  eshro	5D9  -		5DA  -		5DB
 * 5E8  -	5E9  -		5EA  -		5EB
 * 5F8  -	5F9  -		5FA  -		5FB
 *
 * 5CC  mov	5CD  -		5CE  -		5CF  -
 * 5DC  movl	5DD  -		5DE  -		5DF  -
 * 5EC  movt	5ED  -		5EE  -		5EF  -
 * 5FC  movq	5FD  -		5FE  -		5FF  -
 *
 * eshro decoded in shifter?
 *
 * function decoder height = 2
 */
static inline
void reg_move (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 0, 2);  /* ---- -1xx */
	const size_t src = u32_extract (op, 0, 5);

	switch (i) {
	case 3:  o->r[c | 3] = o->r[src | 3];
	case 2:  o->r[c | 2] = o->r[src | 2];
	case 1:  o->r[c | 1] = o->r[src | 1];
	case 0:  o->r[c | 0] = a;
	}
}

static inline
void reg_5C (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F2)	reg_move  (o, op, a, b, c);
	else	reg_eshro (o, op, a, b, c);
}

/*
 * op 48..4F, 58..5F -- core ops block
 *
 * decoder height = 2 + 3, function decoded on fetch stage
 */
void reg_core (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 0, 3);  /* ---- -xxx */

	switch (i) {
	case 0:  reg_log (o, op, a, b, c);  break;  /* -000		*/
	case 1:  reg_59  (o, op, a, b, c);  break;  /* -001		*/
	case 2:  reg_5A  (o, op, a, b, c);  break;  /* -010		*/
	case 3:  reg_5B  (o, op, a, b, c);  break;  /* -011		*/
	case 4:  reg_5C  (o, op, a, b, c);  break;  /* -1--  filler	*/
	case 5:  reg_5C  (o, op, a, b, c);  break;  /* -1--  filler	*/
	case 6:  reg_5C  (o, op, a, b, c);  break;  /* -1--  filler	*/
	case 7:  reg_5C  (o, op, a, b, c);  break;  /* -1--		*/
	}
}

/*
 * 600  synmov	601  synmovl	602  synmovq	603  -
 * 604  -	605  -		606  -		607  -
 * 608  -	609  -		60A  -		60B  -
 * 60C  -	60D  -		60E  -		60F  -
 *
 * synmov	- K, S only
 */
static inline
void reg_synmov (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	i960_on_undef (o);
}

/*
 * 80960 REG Format: Atomic Operations
 *
 * 610  atmod	611  -		612  atadd	613  -
 * 614  -	615  -		616  -		617  -
 * 618  -	619  -		61A  -		61B  -
 * 61C  -	61D  -		61E  -		61F  -
 *
 * F1  -- add vs modify
 */
static inline
void reg_atomic (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F1 = u32_bit_select (op, 7 + 1);

	const uint32_t src = a & ~3;
	const uint32_t old = i960_read_lock (o, src);
	const uint32_t new = F1 ? old + b : u32_modify (old, o->r[c], b);

	i960_write_unlock (o, src, new);
	o->r[c] = old;
}

/*
 * 620  -	621  -		622  -		623  -
 * 624  -	625  -		626  -		627  -
 * 628  -	629  -		62A  -		62B  -
 * 62C  -	62D  -		62E  -		62F  -
 *
 * 630  sdma	631  udma	632  -		633  -
 * 634  -	635  -		636  -		637  -
 * 638  -	639  -		63A  -		63B  -
 * 63C  -	63D  -		63E  -		63F  -
 *
 * sdma/udma	- C
 */

/*
 * 80960 REG Format: Bit Field Operations
 *
 * 640  spanbit	641  scanbit	642  daddc	643  dsubc
 * 644  dmovt	645  modac	646  -		647  -
 * 648  -	649  -		64A  -		64B  -
 * 64C  -	64D  -		64E  -		64F  -
 *
 * 650  modify	651  extract	652  -		653  -
 * 654  modtc	655  modpc	656  -		657  -
 * 658  intctl	659  sysctl	65A  -		65B  icctl
 * 65C  dcctl	65D  halt	65E  -		65F  -
 *
 * daddc,dsubc	- K, S only
 * dmovt	- K, S only
 *
 * intctl	- J
 * sysctl	- C, J
 * icctl/dcctl	- J
 * halt		- J
 */
static inline
void reg_scanbit (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = a == 0 ? ~(uint32_t) 0 : 31 - __builtin_clz (a);
	i960_set_cond (o, a == 0 ? 0 : 2);
}

static inline
void reg_modac (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t ac = o->ac;

	o->ac   = u32_modify (ac, b, a);
	o->r[c] = ac;
}

static inline
void reg_64 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F2)
		reg_modac (o, op, a, b, c);
	else
		reg_scanbit (o, op, (F0 ? a : ~a), b, c);
}

static inline
void reg_modify (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = u32_modify (o->r[c], b, a);
}

static inline
void reg_extract (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = b > 31 ? o->r[c] : u32_extract (o->r[c], a, b);
}

static inline
void reg_modtc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t tc = o->tc;

	o->tc   = u32_modify (tc, b, a & 0x00ff00ff);
	o->r[c] = tc;
}

static inline
void reg_modpc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t pc = o->pc, m = b;  /* a should be equal to b */

	if (m != 0 && !i960_check_em (o))
		return;

	o->pc   = u32_modify (pc, o->r[c], m);
	o->r[c] = pc;

	/* check pending interrupts here */
}

static inline
void reg_65 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F2)
		if (F0) reg_modpc (o, op, a, b, c);
		else    reg_modtc (o, op, a, b, c);
	else
		if (F0) reg_extract (o, op, a, b, c);
		else    reg_modify  (o, op, a, b, c);
}

/*
 * 80960 REG Format: System Operations
 *
 * 660  calls	661  -		662  -		663  -
 * 664  -	665  -		666  -		667  -
 * 668  -	669  -		66A  -		66B  mark
 * 66C  fmark	66D  flushreg	66E  -		66F  syncf
 */
static inline
void reg_mark (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	/* trace support not implemented */
}

static inline
void reg_fmark (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	/* trace support not implemented */
}

static inline
void reg_flushreg (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	/* nothing to do */
}

static inline
void reg_syncf (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	/* nothing to do */
}

static inline
void reg_66 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F3 = u32_bit_select (op, 7 + 3);

	if (!F3)
		i960_calls (o, a);
}

/*
 * 80960 REG Format: Extended Multipy and Divide Operations (MP)
 *
 * 670  emul	671  ediv	672  -		673  -
 * 678  -	679  -		67A  -		67B  -
 *
 * F0  -- div vs mul
 * F2  -- FPU vs emul/ediv
 */
static inline
void reg_emul (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint64_t r = (uint64_t) a * b;

	o->r[c | 0] = r;
	o->r[c | 1] = r >> 32;
}

static inline
void reg_ediv (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t bh = o->r[u32_extract (op, 14, 5) | 1];
	const uint64_t bl = (uint64_t) bh << 32 | b;
	const int ok = i960_div_check (o, a);

	o->r[c | 0] = ok ? bl % a : b;
	o->r[c | 1] = ok ? bl / a : 0;
}

static inline
void reg_67 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);
	const int F2 = u32_bit_select (op, 7 + 2);

	if (F2) i960_fpu (o, op, a, b, c);
	else
	if (F0) reg_ediv (o, op, a, b, c);
	else    reg_emul (o, op, a, b, c);
}

/*
 * op 60..67 -- supplement ops block
 */
void reg_supp (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 0, 3);  /* ---- -xxx */

	switch (i) {
	case 0:  reg_synmov (o, op, a, b, c);  break;  /* -000		*/
	case 1:  reg_atomic (o, op, a, b, c);  break;  /* -001		*/
	case 2:  reg_synmov (o, op, a, b, c);  break;  /* -010	filler	*/
	case 3:  reg_atomic (o, op, a, b, c);  break;  /* -011	filler	*/
	case 4:  reg_64     (o, op, a, b, c);  break;  /* -100		*/
	case 5:  reg_65     (o, op, a, b, c);  break;  /* -101		*/
	case 6:  reg_66     (o, op, a, b, c);  break;  /* -110		*/
	case 7:  reg_67     (o, op, a, b, c);  break;  /* -111		*/
	}
}

/*
 * 80960 REG Format: FPU Function Operations
 *
 * 680  atanr	681  logepr	682  logr	683  remr
 * 684  cmpor	685  cmpr	686  -		687  -
 * 688  sqrtr	689  expr	68A  logbnr	68B  roundr
 * 68C  sinr	68D  cosr	68E  tanr	68F  classr
 *
 * 690  atanrl	691  logeprl	692  logrl	693  remrl
 * 694  cmporl	695  cmprl	696  -		697  -
 * 698  sqrtrl	699  exprl	69A  logbnrl	69B  roundrl
 * 69C  sinrl	69D  cosrl	69E  tanrl	69F  classrl
 *
 * C0  -- 64-bit vs 32-bit float
 *
 * 80960 REG Format: FPU Conversion Operations
 *
 * 674  cvtir	675  cvtilr	676  scalerl	677  scaler
 * 67C  -	67D  -		67E  -		67F  -
 *
 * 6C0  cvtri	6C1  cvtril	6C2  cvtzri	6C3  cvtzril
 * 6C4  -	6C5  -		6C6  -		6C7  -
 * 6C8  -	6C9  movr	6CA  -		6CB  -
 * 6CC  -	6CD  -		6CE  -		6CF  -
 *
 * 6D0  -	6D1  -		6D2  -		6D3  -
 * 6D4  -	6D5  -		6D6  -		6D7  -
 * 6D8  -	6D9  movrl	6DA  -		6DB  -
 * 6DC  -	6DD  -		6DE  -		6DF  -
 *
 * 6E0  -	6E1  -		6E2  cpysre	6E3  cpyrsre
 * 6E4  -	6E5  -		6E6  -		6E7  -
 * 6E8  -	6E9  movre	6EA  -		6EB  -
 * 6EC  -	6ED  -		6EE  -		6EF  -
 *
 * scale  -- ldexp(3)
 *
 * F0  -- long vs word (WTF with scale?)
 * F1  -- scale/cvtzri vs cvtir/cvtri
 * F2  -- cvtir vs cvtri
 *
 * 80960 REG Format: FPU ALU Operations
 *
 * 788  -	789  -		78A  -		78B  divr
 * 78C  mulr	78D  subr	78E  -		78F  addr
 *
 * 798  -	799  -		79A  -		79B  divrl
 * 79C  mulrl	79D  subrl	79E  -		79F  addrl
 *
 * F3  -- FPU ALU vs cond. ops
 * C0  -- 64-bit vs 32-bit float
 */

/*
 * op 68..6F
 */
static inline
void reg_fpu (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	i960_fpu (o, op, a, b, c);
}

/*
 * 80960 REG Format: Multipy and Divide Operations
 *
 * 701  mulo	708  remo	709  -		70B  divo
 * 741  muli	748  remi	749  modi	74B  divi
 *
 * F0  -- make compensation after rem (do mod)
 * F1  -- quotinent vs remainder
 * F3  -- div vs mul
 * C2  -- integer vs ordinal
 */
static inline
void reg_mulo (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = a * b;
}

static inline
void reg_divo (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F1 = u32_bit_select (op, 7 + 1);

	if (i960_div_check (o, a))
		o->r[c] = F1 ? b / a : b % a;
}

static inline
void reg_70 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F3 = u32_bit_select (op, 7 + 3);

	if (F3)	reg_divo (o, op, a, b, c);
	else	reg_mulo (o, op, a, b, c);
}

static inline
void reg_muli (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int64_t r = (int64_t) a * (int64_t) b;

	o->r[c] = r;

	if (r < INT32_MIN || r > INT32_MAX)
		i960_on_overflow (o);
}

static inline
void reg_remi (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F0 = u32_bit_select (op, 7 + 0);

	if (!i960_div_check (o, a))
		return;

	o->r[c] = (int32_t) b % (int32_t) a;

	if (F0 && o->r[c] != 0 && (int32_t) (a ^ b) < 0)  /* do modi */
		o->r[c] += a;
}

static inline
void reg_divi (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	if (!i960_div_check (o, a))
		return;

	o->r[c] = (int32_t) b / (int32_t) a;

	if ((int32_t) (a ^ b ^ o->r[c]) < 0)  /* a == -1 && b == -2^31 */
		i960_on_overflow (o);
}

static inline
void reg_74 (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F1 = u32_bit_select (op, 7 + 1);
	const int F3 = u32_bit_select (op, 7 + 3);

	if (!F3)	reg_muli (o, op, a, b, c);
	else if (F1)	reg_divi (o, op, a, b, c);
	else		reg_remi (o, op, a, b, c);
}

/*
 * op 70..77
 */
void reg_muldiv (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int C2 = u32_bit_select (op, 24 + 2);

	if (C2)	reg_74 (o, op, a, b, c);
	else	reg_70 (o, op, a, b, c);
}

/*
 * 80960 REG Format: Conditional Operations (J)
 *
 * 780  addono	781  addino	782  subono	783  subino	784  selno
 * 790  addog	791  addig	792  subog	793  subig	794  selg
 * 7A0  addoe	7A1  addie	7A2  suboe	7A3  subie	7A4  sele
 * 7B0  addoge	7B1  addige	7B2  suboge	7B3  subige	7B4  selge
 * 7C0  addobl	7C1  addil	7C2  subol	7C3  subil	7C4  sell
 * 7D0  addone	7D1  addine	7D2  subone	7D3  subine	7D4  selne
 * 7E0  addole	7E1  addile	7E2  subole	7E3  subile	7E4  selle
 * 7F0  addoo	7F1  addio	7F2  suboo	7F3  subio	7F4  selo
 *
 * F0  -- integer vs ordinal
 * F1  -- sub vs add
 * F2  -- sel vs add/sub
 * F3  -- FPU ALU vs cond. ops
 */
static inline
void reg_addcc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	if (i960_check_cond (o, op))
		reg_add (o, op, a, b, c);
}

static inline
void reg_selcc (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	o->r[c] = i960_check_cond (o, op) ? b : a;
}

/*
 * op 78..7F
 */
void reg_cond (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const int F2 = u32_bit_select (op, 7 + 2);
	const int F3 = u32_bit_select (op, 7 + 3);

	if (F3) i960_fpu  (o, op, a, b, c);
	else
	if (F2)	reg_selcc (o, op, a, b, c);
	else	reg_addcc (o, op, a, b, c);
}

#if 0
void reg_op (struct i960 *o, uint32_t op, uint32_t a, uint32_t b, size_t c)
{
	const uint32_t i = u32_extract (op, 24 + 4, 2);  /* --xx ---- */

	switch (i) {
	case 0:  reg_core (o, op, a, b, c);  break;  /* --0-		*/
	case 1:  reg_core (o, op, a, b, c);  break;  /* --0-  filler	*/
//	case 2:  reg_supp (o, op, a, b, c);  break;  /* --10		*/
//	case 3:  reg_fpu  (o, op, a, b, c);  break;  /* --11		*/
	}
}
#endif
