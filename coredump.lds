OUTPUT_FORMAT("elf32-powerpc", "elf32-powerpc",
              "elf32-powerpc")
OUTPUT_ARCH(powerpc)
ENTRY(_start)
/* Do we need any of these for elf?
   __DYNAMIC = 0;    */
PROVIDE (__stack = 0);

SECTIONS
{
  /* discard the 'shared/vector.S' entry point section */
  /DISCARD/ :
  {
 	*(.entry_point_section)
  }


  /DISCARD/ /*.vectors 0x100*/ :
  {
	/* should be the first thing... */
	*(.svgm_preloader_section)

   /*
    * This section is used only if NO_DYNAMIC_EXCEPTION_VECTOR_INSTALL
    * is defined in vectors/vectors.S
    *  *(.vectors)
    * We actually RELY on dynamic vector installation since we need
    * this space for the preloader...
    */
  } 

  /* START OF THE LOADED IMAGE (parts moved by the preloader) */
  .text 0xfff80000 :
  {
	*(.bootstrap);
	__rtems_start = .;
	*(.init);
    *(.text)	
    /*
     * Special FreeBSD sysctl sections.
     */
    . = ALIGN (16);
    __start_set_sysctl_set = .;
    *(set_sysctl_*);
    __stop_set_sysctl_set = ABSOLUTE(.);
    *(set_domain_*);
    *(set_pseudo_*);
 
   /* .gnu.warning sections are handled specially by elf32.em.  */
    *(.gnu.linkonce.t*)
    _fini = .; *(.fini)
    *(.rodata*) *(.gnu.linkonce.r*)
    *(.rodata1)
_SDA2_BASE_ = __SDATA2_START__ + 0x8000;
    *(.sdata2) *(.gnu.linkonce.s2.*);
PROVIDE (__sbss2_start = .);
    *(.sbss2); *(.gnu.linkonce.sb2.*);
    /* avoid empty sdata2/sbss2 area because __eabi wouldn't set up r2
     * (IMPORTANT if run-time loading is involved)
     */
    . += 1 ;
PROVIDE (__sbss2_end = .);
    *(.eh_frame);
	. = ALIGN(4);
  _etext = .;
PROVIDE (etext = .);
    }
  /* Adjust the address for the data segment.  Align to a page boundary */
	. = ALIGN(0x1000);
	.data 0x6000 : AT(LOADADDR(.text)+SIZEOF(.text))
	{
PROVIDE(__DATA_START__ = ABSOLUTE(.) );
    *(.data)
    *(.gnu.linkonce.d*)	
    CONSTRUCTORS
PROVIDE (__EXCEPT_START__ = .);
    *(.gcc_except_table)
PROVIDE (__EXCEPT_END__ = .);
    *(.got1)
    *(.dynamic)
  /* Put .ctors and .dtors next to the .got2 section, so that the pointers
     get relocated with -mrelocatable. Also put in the .fixup pointers.
     The current compiler no longer needs this, but keep it around for 2.7.2  */
PROVIDE (_GOT2_START_ = .);
    *(.got2)
    KEEP(*crtbegin.o(.ctors))
    KEEP(*(EXCLUDE_FILE(*crtend.o) .ctors))
    KEEP(*(SORT(.ctors.*)))
    KEEP(*(.ctors))
    KEEP(*crtbegin.o(.dtors))
    KEEP(*(EXCLUDE_FILE(*crtend.o) .dtors))
    KEEP(*(SORT(.dtors.*)))
    KEEP(*(.dtors))
PROVIDE (_FIXUP_START_ = .);
    *(.fixup)
PROVIDE (_FIXUP_END_ = .);
PROVIDE (_GOT2_END_ = .);
PROVIDE (_GOT_START_ = .);
    *(.got)
    *(.got.plt)
PROVIDE (_GOT_END_ = .);
	KEEP (*(.jcr))

  /* We want the small data sections together, so single-instruction offsets
     can access them all, and initialized data all before uninitialized, so
     we can shorten the on-disk segment size.  */
_SDA_BASE_ = __SDATA_START__ + 0x8000;
  *(.sdata) *(.gnu.linkonce.s.*)
	. = ALIGN(4);
  _edata  =  .;
PROVIDE (edata = .);
  }
/* END OF THE LOADED IMAGE (parts moved by the preloader) */
/* BELOW THIS POINT, NO LOADABLE ITEMS MUST APPEAR */
  .bss       :
  {
   PROVIDE (__sbss_start = ABSOLUTE(.));
   *(.sbss)
   *(.scommon)
   *(.dynsbss)
  /* avoid empty sdata/sbss area because __eabi wouldn't set up r13
   * (IMPORTANT if run-time loading is involved)
   */
   . += 1 ;
   PROVIDE (__sbss_end = ABSOLUTE(.));
   PROVIDE (__bss_start = ABSOLUTE(.));
   *(.dynbss)
   *(.bss)
   *(COMMON)
  . = ALIGN(16);
  }  
  /* proper alignment for SYSV stack
   * (init stack is allocated just after __rtems_end
   */
  . = ALIGN(16);
  _end = . ;
  __rtems_end = . ;
  PROVIDE (end = .);
  /DISCARD/ : 
  {
    *(.comment)
  }


  /* Stabs debugging sections.  */
  .stab 0 : { *(.stab) }
  .stabstr 0 : { *(.stabstr) }
  .stab.excl 0 : { *(.stab.excl) }
  .stab.exclstr 0 : { *(.stab.exclstr) }
  .stab.index 0 : { *(.stab.index) }
  .stab.indexstr 0 : { *(.stab.indexstr) }
  .comment 0 : { *(.comment) }

  /* DWARF debug sections.
     Symbols in the DWARF debugging sections are relative to the beginning
     of the section so we begin them at 0.  */
  /* DWARF 1 */
  .debug          0 : { *(.debug) }
  .line           0 : { *(.line) }
  
  /* GNU DWARF 1 extensions */
  .debug_srcinfo  0 : { *(.debug_srcinfo) }
  .debug_sfnames  0 : { *(.debug_sfnames) }
  
  /* DWARF 1.1 and DWARF 2 */
  .debug_aranges  0 : { *(.debug_aranges) }
  .debug_pubnames 0 : { *(.debug_pubnames) }
  
  /* DWARF 2 */
  .debug_info     0 : { *(.debug_info) }
  .debug_abbrev   0 : { *(.debug_abbrev) }
  .debug_line     0 : { *(.debug_line) }
  .debug_frame    0 : { *(.debug_frame) }
  .debug_str      0 : { *(.debug_str) }
  .debug_loc      0 : { *(.debug_loc) }
  .debug_macinfo  0 : { *(.debug_macinfo) }
  
  /* SGI/MIPS DWARF 2 extensions */
  .debug_weaknames 0 : { *(.debug_weaknames) }
  .debug_funcnames 0 : { *(.debug_funcnames) }
  .debug_typenames 0 : { *(.debug_typenames) }
  .debug_varnames  0 : { *(.debug_varnames) }
  /* These must appear regardless of  .  */
}

/* "Conditional linking" is not supported, unfortunately.
 * This BSP supports getting a network configuration
 * from NVRAM settings. Hence, the startup code must
 * access the 'rtems_bsdnet_config' structure.
 * However, that symbol (and a couple of other networking
 * related symbols) should only be referenced if the application
 * actually does networking - otherwise a lot of
 * code would pulled in from the libnetworking.a under
 * all circumstances.
 *
 * bspstart.c uses these alias names and determines
 * at runtime if they are void or valid (by comparing
 * against the NULL address).
 *
 * Unfortunately, it is not possible to conditionally
 * declare a symbol 'EXTERN' - hence we must rely on
 * other code (probably cross-references within
 * libnetworking itself) causing a reference to the
 * 'real' symbols...
 */

__BSP_wrap_rtems_bsdnet_bootp_boot_file_name =
	DEFINED(rtems_bsdnet_bootp_boot_file_name) ?
		rtems_bsdnet_bootp_boot_file_name
		: 0 ;
__BSP_wrap_rtems_bsdnet_bootp_cmdline =
	DEFINED(rtems_bsdnet_bootp_cmdline) ?
		rtems_bsdnet_bootp_cmdline
		: 0 ;
__BSP_wrap_rtems_bsdnet_bootp_server_address =
	DEFINED(rtems_bsdnet_bootp_server_address) ?
		rtems_bsdnet_bootp_server_address
		: 0 ;
__BSP_wrap_rtems_bsdnet_config =
	DEFINED(rtems_bsdnet_config) ?
		rtems_bsdnet_config
		: 0 ;
__BSP_wrap_rtems_bsdnet_do_bootp =
	DEFINED(rtems_bsdnet_do_bootp) ?
		rtems_bsdnet_do_bootp
		: 0 ;
__BSP_wrap_inet_pton =
	DEFINED(inet_pton) ?
		inet_pton
		: 0 ;
__BSP_wrap_rtems_bsdnet_loopattach =
	DEFINED(rtems_bsdnet_loopattach) ?
		rtems_bsdnet_loopattach
		: 0 ;
