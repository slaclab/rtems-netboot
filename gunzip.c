/* $Id$ */

/* Support for compressed bootloader images */

/* Author: Till Straumann <strauman@slac.stanford.edu>, 5/2002 */

/* The VGM's flash window (i.e. the part ('bank') of the flash that
 * is visible from the CPU address space) is only 512k.
 *
 * This code supports compressed flash images and works as follows:
 *  0) text and read-only data are stored in flash.
 *  1) on boot, gunzip's _own_ data section is copied into RAM, reserving enough
 *     space for the uncompressed image (which supposedly starts at DEST)
 *     and the (gunzip) BSS is cleared.
 *
 *     ROM layout:
 *      <gunzip text> | < compressed netload text/data image > | <aligmnent> | <gunzip data> |
 * 
 *                    ^                                        ^             ^
 *                    |                                        |             |
 *    __zimage_start--|                     __zimage_end-------|  __zl_etext-|
 *
 *
 *     RAM layout:
 *     0 <needed by SMON>   | <reserved for uncompressed image> | <gunzip data section> | <stack>   | <heap>
 *
 *                          ^                                   ^         ^                ^           ^
 *                          |                __zl_data_start ---|         |                |           |
 *                DEST------|                           __zl_bss_start ---| __zl_bss_end---^  free_mem-|
 *
 * 2) The initial (gunzip) stack is setup, EARLY_STACK_SIZE bytes higher than the end
 *    of the data section.
 * 3) gunzip needs malloc(); hence a trivial heap is set up on top of the stack
 *    early_malloc() just allocates space off the heap; free does nothing.
 * 4) The netload image is gunzipped from flash into ram starting at DEST.
 * 5) Control is transferred to DEST; the uncompressed netload starts.
 *
 * NOTE: nonzero 'DEST' is supported for debugging only. Using 'DEST', critical
 *    SMON memory regions can be preserved and SMON can be used for IO.
 *    In 'debug' mode (DEST!=0), step 3 is omitted.
 */

/* variables defined by the linker script */
extern char __zimage_start;
extern char __zimage_end;

extern unsigned long __zl_bss_start;
extern unsigned long __zl_bss_end;

extern unsigned long __zl_etext;
extern unsigned long __zl_data_start;

#define IMAGE_LEN	((unsigned)&__zl_len)

#define EARLY_STACK_SIZE 10000

#define GZ_ALIGN(p) (((unsigned long)(p)+7)&~7)

#define CACHE_LINE_SIZE 32

#if DEST >= 0x3000
#define TEST_IN_RAM
#endif

#ifdef TEST_IN_RAM
static void zlprint(char *s);
static void zlstop(unsigned v1, unsigned v2, unsigned v3);
#else
#define zlprint(arg) do {} while(0)
#define zlstop(arg,...) do {} while(0)
#endif

/* memory beyond the stack is free */
static void *free_mem;
static void gunzip();

/* gcc -O4 doesn't preserve start() at the beginning */
__asm__ ("  .globl _start; _start: b start");

void
start(char *r3, char *r4, char *r5, char *r6)
{
register unsigned long *ps;	/* MUST NOT BE ON THE STACK (which might get destroyed) */
register unsigned long *pd;	/* MUST NOT BE ON THE STACK (which might get destroyed) */

	/* copy the data section into ram */
	for (ps=&__zl_etext, pd = &__zl_data_start; pd < &__zl_bss_start; )
		*pd++=*ps++;
	zlprint("data copied\n");

	/* clear out the BSS */
	for (pd = &__zl_bss_start; pd < &__zl_bss_end; )
		*pd++=0;
	zlprint("bss cleared\n");

	/* set free memory / heap start address */
	free_mem=(void*)GZ_ALIGN(((char*)&__zl_bss_end + EARLY_STACK_SIZE));
#ifndef TEST_IN_RAM
	/* load up initial stack */
	__asm__ __volatile__("mr %%r1, %0"::"r"(free_mem - 16));
#endif
	/* call a routine, so we can use the new stack */
	gunzip();
	/* point of no return */
	__asm__ __volatile__(
			"li %%r3,0\n"
			"mr %%r4, %%r3\n"
			"mr %%r5, %0\n"
			"mr %%r6, %1\n"
			"mr %%r7, %%r3\n"
#ifndef TEST_IN_RAM
			"mtlr	%0\n"
			"blr\n"
#else	
			/* return to SMON */
			"li	%%r10,0x63\n"
			"sc\n"
#endif
			::"r"(DEST),"r"(&__zl_data_start)
			: "r3","r4","r5","r6","r7","r10");
}

#include "zlib.c"

static void
*early_malloc(void *op, unsigned int items, unsigned int size)
{
char *rval=free_mem;

	free_mem=(void*)GZ_ALIGN(free_mem+items*size);
	return rval;
}

static void
early_free(void *opaque, void *address, unsigned int bytes)
{
/* don't bother */
}

#define HEAD_CRC    2
#define EXTRA_FIELD 4
#define ORIG_NAME   8
#define COMMENT     0x10
#define RESERVED    0xe0

#define DEFLATED    8

static void gunzip(void)
{
z_stream zs;
unsigned skip = 10, flags, rval;
char	 *src = &__zimage_start;

	flags = src[3];
	if (DEFLATED != src[2] || (flags & RESERVED)) {
		zlprint("not a gzipped image\n");
		return;
	}
	if ((flags & EXTRA_FIELD))
		skip = 12 + src[10] + (src[11]<<8);
	if ((flags & ORIG_NAME))
		while (src[skip++]) /* do nothing else */;
	if ((flags & COMMENT))
		while (src[skip++]) /* do nothing else */;
	if ((flags & HEAD_CRC))
		skip += 2;

	src += skip;

	zs.next_in   = src;
	zs.avail_in  = (&__zimage_end-src);
	zs.total_in  = 0;

	zs.next_out  = (void*)DEST;
	zs.avail_out = (unsigned)&__zl_data_start - DEST;
	zs.total_out = 0;

	zs.zalloc    = early_malloc;
	zs.zfree     = early_free;
	zs.opaque    = 0;

	zlprint("zs set\n");

	/* setup zstream */
	if (Z_OK!=inflateInit2(&zs, -MAX_WBITS )) {
		zlprint("inflateInit failed\n");
		return;
	}
	zlprint("inflateInit done\n");
	if (Z_STREAM_END != (rval=inflate(&zs, Z_FINISH))) {
		zlprint("inflate failed\n");
		return;
	}
	zlprint("inflate done\n");
	inflateEnd(&zs);
	/* flush the cache */
	for (src=(void*)DEST; src < (char*)&__zl_data_start; src+=CACHE_LINE_SIZE)
		__asm__ __volatile__("dcbst 0, %0; icbi 0, %0"::"r"(src));
	__asm__ __volatile__("sync; isync");
	zlprint("done\n");
}

static void *
memcpy(char *dest, char *src, int n)
{
	while (n--) {
		dest[n]=src[n];
	}
	return dest;
}

#ifdef TEST_IN_RAM
static void
zlputc(int ch)
{
__asm__ __volatile__("li %%r10, %0; sc"::"i"(0x20));
}

static void
zlprint(char *str)
{
__asm__ __volatile__("li %%r10, %0; sc"::"i"(0x21));
}

static void
zlstop(unsigned v1, unsigned v2, unsigned v3)
{
__asm__ __volatile__("li %%r10, %0; sc"::"i"(0x63));
}

#endif
