#include <stdint.h>
#include <rtems.h>
#include <stdio.h>
#include <stdlib.h>
#include <mcf5282/mcf5282.h>

typedef uint32_t xxx __attribute__((may_alias));

void _CPU_cache_flush_1_data_line(void*);
void _CPU_cache_invalidate_1_instruction_line(void*);


/* move code 'from' 'to' and flush instruction cache
 *
 * ASSUMPTIONS: 'to' area is either not overlapping 'from' area
 *              or it is at a lower address.
 *
 *              'to' is cache-line aligned.
 *
 *     'to': destination buffer address.
 *   'from': source buffer address.
 * 'nlines': number of cache lines to copy.
 *
 * NOTE: if 'to' equals 0x40000 (ucDimm ram image address)
 *       then this routine jumps to 0x40000 and
 *       never returns.
 */
void
movjmp(xxx *to, register xxx *from, register int nlines)
{
register xxx  *dst = to;
register xxx  *odst;
	while ( nlines-- > 0 ) {
		odst   = dst;
		*dst++ = *from++;
		*dst++ = *from++;
		*dst++ = *from++;
		*dst++ = *from++;
		/* Flush data line is noop on 5282 */
		/* Invalidate 1 instruction line   */
		asm volatile ("cpushl %%bc,(%0)" :: "a" (((uint32_t)odst)|0x400));
	}
	
	if ( (xxx*)0x40000 == to ) {
#if 1
		asm volatile ("jmp 0x40000");
#else
		/* go through a full reset */
	   __asm__ __volatile__ (
   		"move.l %0,%%d0\n\t"
		"move.l %1,%%d1\n\t"
        "trap #2\n\t"
		::"i"(0),"i"(0/*4 for exec_after */):"d0","d1");
#endif
		/* NEVER GET HERE */
	}
}
