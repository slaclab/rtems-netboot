OUTPUT_FORMAT("elf32-powerpc","elf32-powerpc","elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)

/* default values;
 * This is kind of a hack, but the linker doesn't
 * allow for defining these on the command line
 * (--defsym DEST=xxxx) which would be much cleaner.
 */
PROVIDE (DEST = 0);
PROVIDE (FLASHSTART = 0xfff80000);

FLASHSIZE = 0x80000 ;

SECTIONS {
	/* text section linked at FLASHSTART and loaded at FLASHSTART */
	.text FLASHSTART : {
		*(.text)
	} > CODE
	__zl_etext = ALIGN ( 4 );

	/* gunzip expects data in ram, static data
	 * is loaded into the flash however. It is copied
	 * to ram by init code
	 */
	.data DEST + __rtems_end - __rtems_start : AT( __zl_etext ) {
	__zl_data_start = . ;
		gunzip.o(.sdata)
		gunzip.o(.rodata)
		gunzip.o(.data)
		gunzip.o(.sdata2)
		. = ALIGN ( 4 );
	} > DATA
	__zl_bss_start = . ;
	.bss : {
		gunzip.o(.sbss)
		gunzip.o(.bss)
		. = ALIGN ( 4 );
	} > DATA
	__zl_bss_end = . ;

	/* compressed 'netload' image appended here */
	.zimage __zl_etext + __zl_bss_start - __zl_data_start : {
	__zimage_start = . ;
		*.img.gz(*)
	__zimage_end = . ;
	} > DATA

}
