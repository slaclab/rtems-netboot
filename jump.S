#include <libcpu/cpu.h>
#include "asm.h"
	.globl flashaddr,_edata
	.section .exception_vectors,"rwx",@progbits
	/* load up r5,r6 with the flash
	 * image start-end address range
	 */
	lis r5,flashaddr@h
	ori r5,r5,flashaddr@l
	lis r6,_edata@h
	ori r6,r6,_edata@l
	add r6,r6,r5
	/* jump to the preloader */
	ba	flashaddr
