/*
 * $Id$
 *
 *  Copyright (C) 1999 Eric Valette. valette@crf.canon.fr
 *
 *  Derived from Eric's version powerpc/shared/start/start.S version
 *  by Till Straumann, 2004 <strauman@slac.stanford.edu>
 *  
 *  The license and distribution terms for this file may be
 *  found in found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 */

#include <asm.h>
#include <rtems/score/cpu.h>
#include <libcpu/io.h>

	.text
	.globl	__boot_from_fdiag
	.type	__boot_from_fdiag,@function
__boot_from_fdiag:
/* 
 * PREP
 * This is jumped to on prep systems right after the kernel is relocated
 * to its proper place in memory by the boot loader.  The expected layout
 * of the regs is:	
 * T.S: NOTE r3..r5 are not used on SVGM.
 *   r3: ptr to residual data
 *   r4: initrd_start or if no initrd then 0
 *   r5: initrd_end - unused if r4 is 0
 *   r6: Start of command line string (or 0)
 *   r7: End of command line string (or 0)
 *
 *   The Prep boot loader insure that the MMU is currently off...
 *   In our case (SVGM - T.S), we use an SMON/FDIAG script to 
 *   set up an adequate environment...
 *
 */
	mr	r31,r3			/* save parameters */
	mr	r30,r4
	mr	r29,r5
	mr	r28,r6
	mr	r27,r7
	/*
	 * Make sure we have nothing in TLB
	 * but keep the BAT mappings SMON/Fdiag set up
	 * they map RAM, ROM and PCI devices which is good.
	 * We don't want to mess with this...
	 */
	bl	flush_tlbs

enter_C_code:
	bl	MMUon
	bl  __eabi	/* setup EABI and SYSV environment */
	bl	zero_bss
	/*
	 * restore prep boot params
	 */
	mr	r3,r31
	mr	r4,r30
	mr	r5,r29
	mr	r6,r28
	mr	r7,r27 
	bl	save_boot_params
	/*
	 * stack = &__rtems_end + 4096
	 */
	addis	r9,r0, __rtems_end+(4096-CPU_MINIMUM_STACK_FRAME_SIZE)@ha
    addi	r9,r9, __rtems_end+(4096-CPU_MINIMUM_STACK_FRAME_SIZE)@l
	mr	r1, r9
	/*
	 * We are know in a environment that is totally independent from bootloader setup.
	 */
    lis	r5,environ@ha
    la	r5,environ@l(r5)	/* environp */
	li	r4, 0			/* argv */
	li	r3, 0			/* argc */
	bl	boot_card
	bl	_return_to_ppcbug
	
flush_tlbs:
	lis	r20, 0x1000
1:	addic.	r20, r20, -0x1000
	tlbie	r20
	bgt	1b
	sync
	blr
