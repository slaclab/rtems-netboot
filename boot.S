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
#include <rtems/score/cpu.h>
#include <libcpu/io.h>

#ifndef CPU_MINIMUM_STACK_FRAME_SIZE
/* rtems 4.7 */
#include <rtems/powerpc/powerpc.h>
#define CPU_MINIMUM_STACK_FRAME_SIZE PPC_MINIMUM_STACK_FRAME_SIZE
#endif

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
 2   r3: ptr to residual data
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
	mr	31,3			/* save parameters */
	mr	30,4
	mr	29,5
	mr	28,6
	mr	27,7
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
	mr	3,31
	mr	4,30
	mr	5,29
	mr	6,28
	mr	7,27 
	bl	save_boot_params
	/*
	 * stack = &__rtems_end + 4096
	 */
	addis	9,0, __rtems_end+(4096-CPU_MINIMUM_STACK_FRAME_SIZE)@ha
    addi	9,9, __rtems_end+(4096-CPU_MINIMUM_STACK_FRAME_SIZE)@l
	mr	1,9
	/*
	 * We are know in a environment that is totally independent from bootloader setup.
	 */
    lis	5,environ@ha
    la	5,environ@l(5)	/* environp */
	li	4, 0			/* argv */
	li	3, 0			/* argc */
	bl	boot_card
	bl	_return_to_ppcbug
	
flush_tlbs:
	lis	20, 0x1000
1:	addic.	20, 20, -0x1000
	tlbie	20
	bgt	1b
	sync
	blr
