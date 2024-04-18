/*
 * 80960 Emulator CTRL Format Operations
 *
 * Copyright (c) 2024 Alexei A. Smekalkine <ikle@ikle.ru>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * 00  -	08  b		10  bno		18  faultno
 * 01  -	09  call	11  bg		19  faultg
 * 02  -	0A  ret		12  be		1A  faulte
 * 03  -	0B  bal		13  bge		1B  faultge
 * 04  -	0C  -		14  bl		1C  faultl
 * 05  -	0D  -		15  bne		1D  faultne
 * 06  -	0E  -		16  ble		1E  faultle
 * 07  -	0F  -		17  bo		1F  faulto
 */

#include <i960-emu.h>
#include <i960-emu-bits.h>
#include <i960-emu-branch.h>

/*
 * Operation Entry Point
 *
 * decoder height = 3
 */
static void ctrl_op (struct i960 *o, uint32_t op, uint32_t efa)
{
	const int C4 = u32_bit_select  (op, 24 + 4);	/* ---x ---- */
	const int C3 = u32_bit_select  (op, 24 + 3);	/* ---- x--- */
	const uint32_t i = u32_extract (op, 24 + 0, 2);	/* ---0 --xx */

	if (!C4)
		switch (i) {
		case 0:  i960_b    (o, efa);           break;  /* ---0 --00 */
		case 1:  i960_call (o, efa);           break;  /* ---0 --01 */
		case 2:  i960_ret  (o);                break;  /* ---0 --10 */
		case 3:  i960_bal  (o, efa, I960_LP);  break;  /* ---0 --11 */
		}
	else
	if (!C3)  i960_bcc     (o, op, efa);	/* ---1 0--- */
	else      i960_faultcc (o, op, efa);	/* ---1 1--- */
}

void i960_ctrl (struct i960 *o, uint32_t op, uint32_t ip)
{
	const int32_t disp = (((int32_t) op << 8) >> 8) & ~3;

	ctrl_op (o, op, ip + disp);
}
