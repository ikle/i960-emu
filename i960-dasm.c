/*
 * 80960 Disassembler
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

struct tabent {
	char name[12];
	int args;
};

static void imm (FILE *to, const char *prefix, uint32_t x)
{
	const char *fmt = x < 10 ? "%s%d" : "%s0x%x";

	fprintf (to, fmt, prefix, x);
}

static void label (FILE *to, const char *prefix, uint32_t efa)
{
	imm (to, prefix, efa);
}

static const char *get_arg (int M, int S, int i, int fp)
{
	static const char *const regs[32] = {
		"pfp",  "sp",   "rip",  "r3",   "r4",   "r5",   "r6",   "r7",
		"r8",   "r9",   "r10",  "r11",  "r12",  "r13",  "r14",  "r15",
		"g0",   "g1",   "g2",   "g3",   "g4",   "g5",   "g6",   "g7",
		"g8",   "g9",   "g10",  "g11",  "g12",  "g13",  "g14",  "fp",
	};
	static const char *const lits[32] = {
		"0",    "1",    "2",    "3",    "4",    "5",    "6",    "7",
		"8",    "9",    "10",   "11",   "12",   "13",   "14",   "15",
		"16",   "17",   "18",   "19",   "20",   "21",   "22",   "23",
		"24",   "25",   "26",   "27",   "28",   "29",   "30",   "31",
	};
	static const char *const sregs[32] = {
		"sf0",  "sf1",  "sf2",  "sf3",  "sf4",  "sf5",  "sf6",  "sf7",
		"sf8",  "sf9",  "sf10", "sf11", "sf12", "sf13", "sf14", "sf15",
		"sf16", "sf17", "sf18", "sf19", "sf20", "sf21", "sf22", "sf23",
		"sf24", "sf25", "sf26", "sf27", "sf28", "sf29", "sf30", "sf31",
	};
	static const char *const fregs[32] = {
		"fp0",  "fp1",  "fp2",  "fp3",  "fp4",  "fp5",  "fp6",  "fp7",
		"fp8",  "fp9",  "fp10", "fp11", "fp12", "fp13", "fp14", "fp15",
		"0.0",  "fp17", "fp18", "fp19", "fp20", "fp21", "1.0",  "fp23",
		"fp24", "fp25", "fp26", "fp27", "fp28", "fp29", "fp30", "fp31",
	};

	return S ? fp ? fregs[i] : sregs[i] : M ? lits[i] : regs[i];
}

static void reg_op (FILE *to, const char *prefix, int M, int S, int reg, int fp)
{
	fprintf (to, "%s%s", prefix, get_arg (M, S, reg, fp));
}

static uint32_t i960_inval (FILE *to, uint32_t len, uint32_t op, uint32_t disp)
{
	const char *fmt = len == 8 ? ".word\t0x%08x, 0x%08x" : "word\t0x%08x";

	fprintf (to, fmt, op, disp);
	return len;
}

static uint32_t i960_ctrl (FILE *to, uint32_t ip, uint32_t op, uint32_t disp)
{
	static const struct tabent map[32] = {
		[0x08] = { "b",		1, },	/* 08 */
		[0x09] = { "call",	1, },	/* 09 */
		[0x0a] = { "ret",	0, },	/* 0a */
		[0x0b] = { "bal",	1, },	/* 0b */
		[0x10] = { "bno",	1, },	/* 10 */
		[0x11] = { "bg",	1, },	/* 11 */
		[0x12] = { "be",	1, },	/* 12 */
		[0x13] = { "bge",	1, },	/* 13 */
		[0x14] = { "bl",	1, },	/* 14 */
		[0x15] = { "bne",	1, },	/* 15 */
		[0x16] = { "ble",	1, },	/* 16 */
		[0x17] = { "bo",	1, },	/* 17 */
		[0x18] = { "faultno",	0, },	/* 18 */
		[0x19] = { "faultg",	0, },	/* 19 */
		[0x1a] = { "faulte",	0, },	/* 1a */
		[0x1b] = { "faultge",	0, },	/* 1b */
		[0x1c] = { "faultl",	0, },	/* 1c */
		[0x1d] = { "faultne",	0, },	/* 1d */
		[0x1e] = { "faultle",	0, },	/* 1e */
		[0x1f] = { "faulto",	0, },	/* 1f */
	};

	const int i = (op >> 24) & 31;
	const int T = (op >>  1) & 1;
	const int R = (op >>  0) & 1;

	if (map[i].name[0] == 0 || R)
		return i960_inval (to, 4, op, disp);

	fprintf (to, "%s%s", map[i].name, T ? ".f" : "");

	if (map[i].args)
		label (to, "\t", ip + disp);

	return 4;
}

static uint32_t i960_cobr (FILE *to, uint32_t ip, uint32_t op, uint32_t disp)
{
	static const struct tabent map[32] = {
		[0x00] = { "testno",	1, },	/* 20 */
		[0x01] = { "testg",	1, },	/* 21 */
		[0x02] = { "teste",	1, },	/* 22 */
		[0x03] = { "testge",	1, },	/* 23 */
		[0x04] = { "testl",	1, },	/* 24 */
		[0x05] = { "testne",	1, },	/* 25 */
		[0x06] = { "testle",	1, },	/* 26 */
		[0x07] = { "testo",	1, },	/* 27 */
		[0x10] = { "bbc",	3, },	/* 30 */
		[0x11] = { "cmpobg",	3, },	/* 31 */
		[0x12] = { "cmpobe",	3, },	/* 32 */
		[0x13] = { "cmpobge",	3, },	/* 33 */
		[0x14] = { "cmpobl",	3, },	/* 34 */
		[0x15] = { "cmpobne",	3, },	/* 35 */
		[0x16] = { "cmpoble",	3, },	/* 36 */
		[0x17] = { "bbs",	3, },	/* 37 */
		[0x18] = { "cmpibno",	3, },	/* 38 */
		[0x19] = { "cmpibg",	3, },	/* 39 */
		[0x1a] = { "cmpibe",	3, },	/* 3a */
		[0x1b] = { "cmpibge",	3, },	/* 3b */
		[0x1c] = { "cmpibl",	3, },	/* 3c */
		[0x1d] = { "cmpibne",	3, },	/* 3d */
		[0x1e] = { "cmpible",	3, },	/* 3e */
		[0x1f] = { "cmpibo",	3, },	/* 3f */
	};

	const int i  = (op >> 24) & 31;
	const int c  = (op >> 19) & 31;
	const int b  = (op >> 14) & 31;
	const int a  = c;
	const int S3 = (op >> 13) & 1;
	const int M1 = S3;
	const int T  = (op >>  1) & 1;
	const int S2 = (op >>  0) & 1;

	if (map[i].name[0] == 0)
		return i960_inval (to, 4, op, disp);

	fprintf (to, "%s%s", map[i].name, T ? ".f" : "");

	if (i & 0x10) {
		reg_op (to, "\t", M1, 0, a, 0);
		reg_op (to, ", ", 0, S2, b, 0);
		label  (to, ", ", ip + disp);
	}
	else
		reg_op (to, "\t", 0, S3, c, 0);

	return 4;
}

static uint32_t i960_mem (FILE *to, uint32_t ip, uint32_t op, uint32_t disp)
{
	static const struct tabent map[128] = {
		[0x00] = { "ldob",	2 },	/* 80 */
		[0x02] = { "stob",	1 },	/* 82 */
		[0x04] = { "bx",	0 },	/* 84 */
		[0x05] = { "balx",	2 },	/* 85 */
		[0x06] = { "callx",	0 },	/* 86 */
		[0x08] = { "ldos",	2 },	/* 88 */
		[0x0a] = { "stos",	1 },	/* 8a */
		[0x0c] = { "lda",	2 },	/* 8c */
		[0x10] = { "ld",	2 },	/* 90 */
		[0x12] = { "st",	1 },	/* 92 */
		[0x18] = { "ldl",	2 },	/* 98 */
		[0x1a] = { "stl",	1 },	/* 9a */
		[0x20] = { "ldt",	2 },	/* a0 */
		[0x22] = { "stt",	1 },	/* a2 */
		[0x2c] = { "dcinva",	0 },	/* ac */
		[0x30] = { "ldq",	2 },	/* b0 */
		[0x32] = { "stq",	1 },	/* b2 */
		[0x40] = { "ldib",	2 },	/* c0 */
		[0x42] = { "stib",	1 },	/* c2 */
		[0x48] = { "ldis",	2 },	/* c8 */
		[0x4a] = { "stis",	1 },	/* ca */
	};

	static const char F[16] = {
		0x4, 0x4, 0x4, 0x4,  0x2, 0x8, 0x0, 0x3,  /* 00-- 01xx- */
		0x6, 0x6, 0x6, 0x6,  0xC, 0xE, 0xD, 0xF,  /* 10-- 11xx */
	};

	const int i    = (op >> 24) & 127;
	const int c    = (op >> 19) & 31;
	const int b    = (op >> 14) & 31;
	const int mode = (op >> 10) & 15;
	const int a    = (op >>  0) & 31;
	const int S2   = (op >>  6) & 1;
	const int S1   = (op >>  5) & 1;

	const int scale = 1 << ((op >> 7) & 7);

	const char *base  = get_arg (0, S2, b, 0);
	const char *index = get_arg (0, S1, a, 0);
	const char *ifmt  = scale == 1 ? "[%s]" : "[%s*%d]";

	uint32_t len = F[mode] & 8 ? 8 : 4;

	if (map[i].name[0] == 0 || mode == 6)
		return i960_inval (to, len, op, disp);

	fprintf (to, "%s\t", map[i].name);

	if (map[i].args & 1)  fprintf (to, "%s, ", get_arg (0, 0, c, 0));

	if (mode == 5)    label (to, "", ip + 8 + disp);
	if (F[mode] & 4)  imm (to, "", disp);
	if (F[mode] & 2)  fprintf (to, "(%s)", base);
	if (F[mode] & 1)  fprintf (to, ifmt, index, scale);

	if (map[i].args & 2)  fprintf (to, ", %s", get_arg (0, 0, c, 0));

	return len;
}

static uint32_t i960_reg (FILE *to, uint32_t ip, uint32_t op, uint32_t disp)
{
	static const struct tabent map[1024] = {
		[0x180] = { "notbit",		7 },
		[0x181] = { "and",		7 },
		[0x182] = { "andnot",		7 },
		[0x183] = { "setbit",		7 },
		[0x184] = { "notand",		7 },
		[0x186] = { "xor",		7 },
		[0x187] = { "or",		7 },
		[0x188] = { "nor",		7 },
		[0x189] = { "xnor",		7 },
		[0x18a] = { "not",		5 },
		[0x18b] = { "ornot",		7 },
		[0x18c] = { "clrbit",		7 },
		[0x18d] = { "notor",		7 },
		[0x18e] = { "nand",		7 },
		[0x18f] = { "alterbit",		7 },
		[0x190] = { "addo",		7 },
		[0x191] = { "addi",		7 },
		[0x192] = { "subo",		7 },
		[0x193] = { "subi",		7 },
		[0x194] = { "cmpob",		3 },
		[0x195] = { "cmpib",		3 },
		[0x196] = { "cmpos",		3 },
		[0x197] = { "cmpis",		3 },
		[0x198] = { "shro",		7 },
		[0x19a] = { "shrdi",		7 },
		[0x19b] = { "shri",		7 },
		[0x19c] = { "shlo",		7 },
		[0x19d] = { "rotate",		7 },
		[0x19e] = { "shli",		7 },
		[0x1a0] = { "cmpo",		3 },
		[0x1a1] = { "cmpi",		3 },
		[0x1a2] = { "concmpo",		3 },
		[0x1a3] = { "concmpi",		3 },
		[0x1a4] = { "cmpinco",		7 },
		[0x1a5] = { "cmpinci",		7 },
		[0x1a6] = { "cmpdeco",		7 },
		[0x1a7] = { "cmpdeci",		7 },
		[0x1ac] = { "scanbyte",		3 },
		[0x1ad] = { "bswap",		5 },
		[0x1ae] = { "chkbit",		3 },
		[0x1b0] = { "addc",		7 },
		[0x1b2] = { "subc",		7 },
		[0x1b4] = { "intdis",		0 },
		[0x1b5] = { "inten",		0 },
		[0x1cc] = { "mov",		5 },
		[0x1d8] = { "eshro",		7 },
		[0x1dc] = { "movl",		5 },
		[0x1ec] = { "movt",		5 },
		[0x1fc] = { "movq",		5 },
		[0x200] = { "synmov",		3 },
		[0x201] = { "synmovl",		3 },
		[0x202] = { "synmovq",		3 },
		[0x203] = { "cmpstr",		7 },
		[0x204] = { "movqstr",		7 },
		[0x205] = { "movstr",		7 },
		[0x210] = { "atmod",		7 },
		[0x212] = { "atadd",		7 },
		[0x213] = { "inspacc",		5 },
		[0x214] = { "ldphy",		5 },
		[0x215] = { "synld",		5 },
		[0x217] = { "fill",		7 },
		[0x230] = { "sdma",		7 },
		[0x231] = { "udma",		0 },
		[0x240] = { "spanbit",		5 },
		[0x241] = { "scanbit",		5 },
		[0x242] = { "daddc",		7 },
		[0x243] = { "dsubc",		7 },
		[0x244] = { "dmovt",		5 },
		[0x245] = { "modac",		7 },
		[0x246] = { "condrec",		5 },
		[0x250] = { "modify",		7 },
		[0x251] = { "extract",		7 },
		[0x254] = { "modtc",		7 },
		[0x255] = { "modpc",		7 },
		[0x256] = { "receive",		5 },
		[0x258] = { "intctl",		5 },
		[0x259] = { "sysctl",		7 },
		[0x25b] = { "icctl",		7 },
		[0x25c] = { "dcctl",		7 },
		[0x25d] = { "halt",		0 },
		[0x260] = { "calls",		1 },
		[0x262] = { "send",		7 },
		[0x263] = { "sendserv",		1 },
		[0x264] = { "resumprcs",	1 },
		[0x265] = { "schedprcs",	1 },
		[0x266] = { "saveprcs",		0 },
		[0x268] = { "condwait",		1 },
		[0x269] = { "wait",		1 },
		[0x26a] = { "signal",		1 },
		[0x26b] = { "mark",		0 },
		[0x26c] = { "fmark",		0 },
		[0x26d] = { "flushreg",		0 },
		[0x26f] = { "syncf",		0 },
		[0x270] = { "emul",		7 },
		[0x271] = { "ediv",		7 },
		[0x273] = { "ldtime",		4 },
		[0x274] = { "cvtir",		13 },
		[0x275] = { "cvtilr",		13 },
		[0x276] = { "scalerl",		15 },
		[0x277] = { "scaler",		15 },
		[0x280] = { "atanr",		15 },
		[0x281] = { "logepr",		15 },
		[0x282] = { "logr",		15 },
		[0x283] = { "remr",		15 },
		[0x284] = { "cmpor",		11 },
		[0x285] = { "cmpr",		11 },
		[0x288] = { "sqrtr",		13 },
		[0x289] = { "expr",		13 },
		[0x28a] = { "logbnr",		13 },
		[0x28b] = { "roundr",		13 },
		[0x28c] = { "sinr",		13 },
		[0x28d] = { "cosr",		13 },
		[0x28e] = { "tanr",		13 },
		[0x28f] = { "classr",		9 },
		[0x290] = { "atanrl",		15 },
		[0x291] = { "logeprl",		15 },
		[0x292] = { "logrl",		15 },
		[0x293] = { "remrl",		15 },
		[0x294] = { "cmporl",		11 },
		[0x295] = { "cmprl",		11 },
		[0x298] = { "sqrtrl",		13 },
		[0x299] = { "exprl",		13 },
		[0x29a] = { "logbnrl",		13 },
		[0x29b] = { "roundrl",		13 },
		[0x29c] = { "sinrl",		13 },
		[0x29d] = { "cosrl",		13 },
		[0x29e] = { "tanrl",		13 },
		[0x29f] = { "classrl",		9 },
		[0x2c0] = { "cvtri",		13 },
		[0x2c1] = { "cvtril",		13 },
		[0x2c2] = { "cvtzri",		13 },
		[0x2c3] = { "cvtzril",		13 },
		[0x2c9] = { "movr",		13 },
		[0x2d9] = { "movrl",		13 },
		[0x2e1] = { "movre",		13 },
		[0x2e2] = { "cpysre",		15 },
		[0x2e3] = { "cpyrsre",		15 },
		[0x301] = { "mulo",		7 },
		[0x308] = { "remo",		7 },
		[0x30b] = { "divo",		7 },
		[0x341] = { "muli",		7 },
		[0x348] = { "remi",		7 },
		[0x349] = { "modi",		7 },
		[0x34b] = { "divi",		7 },
		[0x380] = { "addono",		7 },
		[0x381] = { "addino",		7 },
		[0x382] = { "subono",		7 },
		[0x383] = { "subino",		7 },
		[0x384] = { "selno",		7 },
		[0x38b] = { "divr",		15 },
		[0x38c] = { "mulr",		15 },
		[0x38d] = { "subr",		15 },
		[0x38f] = { "addr",		15 },
		[0x390] = { "addog",		7 },
		[0x391] = { "addig",		7 },
		[0x392] = { "subog",		7 },
		[0x393] = { "subig",		7 },
		[0x394] = { "selg",		7 },
		[0x39b] = { "divrl",		15 },
		[0x39c] = { "mulrl",		15 },
		[0x39d] = { "subrl",		15 },
		[0x39f] = { "addrl",		15 },
		[0x3a0] = { "addoe",		7 },
		[0x3a1] = { "addie",		7 },
		[0x3a2] = { "suboe",		7 },
		[0x3a3] = { "subie",		7 },
		[0x3a4] = { "sele",		7 },
		[0x3b0] = { "addoge",		7 },
		[0x3b1] = { "addige",		7 },
		[0x3b2] = { "suboge",		7 },
		[0x3b3] = { "subige",		7 },
		[0x3b4] = { "selge",		7 },
		[0x3c0] = { "addol",		7 },
		[0x3c1] = { "addil",		7 },
		[0x3c2] = { "subol",		7 },
		[0x3c3] = { "subil",		7 },
		[0x3c4] = { "sell",		7 },
		[0x3d0] = { "addone",		7 },
		[0x3d1] = { "addine",		7 },
		[0x3d2] = { "subone",		7 },
		[0x3d3] = { "subine",		7 },
		[0x3d4] = { "selne",		7 },
		[0x3e0] = { "addole",		7 },
		[0x3e1] = { "addile",		7 },
		[0x3e2] = { "subole",		7 },
		[0x3e3] = { "subile",		7 },
		[0x3e4] = { "selle",		7 },
		[0x3f0] = { "addoo",		7 },
		[0x3f1] = { "addio",		7 },
		[0x3f2] = { "suboo",		7 },
		[0x3f3] = { "subio",		7 },
		[0x3f4] = { "selo",		7 },
	};

	const int i = ((op >> 20) & 0x3f0) | ((op >> 7) & 0xf);

	if (map[i].name[0] == 0)
		return i960_inval (to, 4, op, disp);

	fprintf (to, map[i].name);

	const int c = (op >> 19) & 31;
	const int b = (op >> 14) & 31;
	const int a = (op >>  0) & 31;

	const int S3 = (op >> 13) & 1;
	const int M2 = (op >> 12) & 1;
	const int M1 = (op >> 11) & 1;
	const int S2 = (op >>  6) & 1;
	const int S1 = (op >>  5) & 1;

	const char *sep = "\t";
	const int fp = (map[i].args & 8) != 0;

	if (map[i].args & 1)  reg_op (to, sep, M1, S1, a, fp), sep = ", ";
	if (map[i].args & 2)  reg_op (to, sep, M2, S2, b, fp), sep = ", ";
	if (map[i].args & 4)  reg_op (to, sep,  0, S3, c, fp);

	return 4;
}

static int32_t i960_ctrl_disp (uint32_t op)
{
	const int32_t disp = op & 0xfffffc;

	return op & 0x800000 ? disp | ~0xffffff : disp;  /* sign-extend */
}

static int32_t i960_cobr_disp (uint32_t op)
{
	const int32_t disp = op & 0x1ffc;

	return op & 0x1000 ? disp | ~0x1fff : disp;  /* sign-extend */
}

static int32_t i960_mem_disp (uint32_t op, uint32_t disp)
{
	return op & 0x1000 ? disp : op & 0xfff;  /* MEMB vs MEMA */
}

uint32_t i960_dasm (FILE *to, uint32_t ip, uint32_t op, uint32_t disp)
{
	const uint32_t line = (op >> 28) & 15;

	if (line >= 8)  return i960_mem (to, ip, op, i960_mem_disp (op, disp));
	if (line >= 4)  return i960_reg (to, ip, op, disp);
	if (line >= 2)  return i960_cobr (to, ip, op, i960_cobr_disp (op));

	return i960_ctrl (to, ip, op, i960_ctrl_disp (op));
}
