/*
 *  This routine starts the application.  It includes application,
 *  board, and monitor specific initialization and configuration.
 *  The generic CPU dependent initialization has been performed
 *  before this routine is invoked.
 *
 *  COPYRIGHT (c) 1989-1998.
 *  On-Line Applications Research Corporation (OAR).
 *  Copyright assigned to U.S. Government, 1994.
 *
 *  The license and distribution terms for this file may be
 *  found in the file LICENSE in this distribution or at
 *  http://www.OARcorp.com/rtems/license.html.
 *
 *  Modified to support the MCP750.
 *  Modifications Copyright (C) 1999 Eric Valette. valette@crf.canon.fr
 *
 *  $Id$
 */

#include <string.h>

#include <bsp.h>
#include <rtems/libio.h>
#include <rtems/libcsupport.h>
#include <bsp/consoleIo.h>
#include <bsp/uart.h>
#include <libcpu/spr.h>
#include <bsp/pci.h>
#include <bsp/openpic.h>
#include <bsp/irq.h>
#include <libcpu/bat.h>
#include <bsp/vectors.h>
#include <libchip/vmeUniverse.h>

extern void _return_to_ppcbug();
extern unsigned long __rtems_end;
extern unsigned long _end;
extern void L1_caches_enables();
extern unsigned get_L2CR();
extern void set_L2CR(unsigned);
extern void bsp_cleanup(void);

#undef  SHOW_MORE_INIT_SETTINGS

/*
 * Vital Board data Start using DATA RESIDUAL
 */
/*
 * Total memory using RESIDUAL DATA
 */
unsigned int BSP_mem_size;
/*
 * PCI Bus Frequency
 */
unsigned int BSP_bus_frequency;
/*
 * processor clock frequency
 */
unsigned int BSP_processor_frequency;
/*
 * Time base divisior (how many tick for 1 second).
 */
unsigned int BSP_time_base_divisor;
/*
 * system init stack and soft ir stack size
 */
#define INIT_STACK_SIZE 0x1000
#define INTR_STACK_SIZE CONFIGURE_INTERRUPT_STACK_MEMORY

void BSP_panic(char *s)
{
  printk("%s PANIC %s\n",_RTEMS_version, s);
  __asm__ __volatile ("sc"); 
}

void _BSP_Fatal_error(unsigned int v)
{
  printk("%s PANIC ERROR %x\n",_RTEMS_version, v);
  __asm__ __volatile ("sc"); 
}

/* flags are: 4 BT (must be MSR[DR])
 *            2 break on store
 *            1 break on load
 */
void
BSPsetDABR(void *addr, int flags)
{
	printk("BSP: setting breakpoint at 0x%08x\n",addr);
	__asm__ __volatile__("sync; mtspr 1013, %0; isync"::"r"((((long)addr)&~7) | (flags & 7)));
}

/*
 *  The original table from the application and our copy of it with
 *  some changes.
 */

extern rtems_configuration_table Configuration;

rtems_configuration_table  BSP_Configuration;

rtems_cpu_table Cpu_table;

char *rtems_progname;

/*
 *  Use the shared implementations of the following routines
 */
 
void bsp_postdriver_hook(void);
void bsp_libc_init( void *, unsigned32, int );

/*
 *  Function:   bsp_pretasking_hook
 *  Created:    95/03/10
 *
 *  Description:
 *      BSP pretasking hook.  Called just before drivers are initialized.
 *      Used to setup libc and install any BSP extensions.
 *
 *  NOTES:
 *      Must not use libc (to do io) from here, since drivers are
 *      not yet initialized.
 *
 */
 
void bsp_pretasking_hook(void)
{
    rtems_unsigned32        heap_start;    
    rtems_unsigned32        heap_size;

    heap_start = ((rtems_unsigned32) &__rtems_end) +INIT_STACK_SIZE + INTR_STACK_SIZE;
    if (heap_start & (CPU_ALIGNMENT-1))
        heap_start = (heap_start + CPU_ALIGNMENT) & ~(CPU_ALIGNMENT-1);

    heap_size = (BSP_mem_size - heap_start) - BSP_Configuration.work_space_size;

#ifdef SHOW_MORE_INIT_SETTINGS
    printk(" HEAP start %x  size %x\n", heap_start, heap_size);
#endif    
    bsp_libc_init((void *) heap_start, heap_size, 0);

#ifdef RTEMS_DEBUG
    rtems_debug_enable( RTEMS_DEBUG_ALL_MASK );
#endif
}

void zero_bss()
{
  extern unsigned long __bss_start, __sbss_start, __sbss_end;
  memset(&__sbss_start, 0, ((unsigned) (&__sbss_end)) - ((unsigned) &__sbss_start));
  memset(&__bss_start, 0, ((unsigned) (&__rtems_end)) - ((unsigned) &__bss_start));
}

void save_boot_params(void *r3, void *r4, void* r5, char *additional_boot_options)
{
  /* loader parameters not implemented by this BSP */ 
}

void
BSP_setLEDs(unsigned char val)
{
int i;
volatile unsigned char *ledreg=(volatile unsigned char*)0xffeffe80;
	for (i=0; i<8; i++,ledreg+=8,val>>=1)
		*ledreg = val&1;	
	__asm__ __volatile__("eieio");
}

/*
 *  bsp_start
 *
 *  This routine does the bulk of the system initialization.
 */

int _BSP_vme_bridge_irq=-1;

void bsp_start( void )
{
  unsigned char *stack;
  unsigned long *r1sp;
  unsigned l2cr;
  register unsigned char* intrStack;
  register unsigned int intrNestingLevel = 0;
  unsigned char *work_space_start;
  ppc_cpu_id_t myCpu;
  ppc_cpu_revision_t myCpuRevision;

  /*
   * Get CPU identification dynamically. Note that the get_ppc_cpu_type() function
   * store the result in global variables so that it can be used latter...
   */
  myCpu 	= get_ppc_cpu_type();
  myCpuRevision = get_ppc_cpu_revision();

  /*
   * Access to board registers and PCI devices.
   */
  setdbat(1, 0xf0000000, 0xf0000000, 0x10000000, IO_PAGE);


  /*
   * enables L1 Cache. Note that the L1_caches_enables() codes checks for
   * relevant CPU type so that the reason why there is no use of myCpu...
   */
  L1_caches_enables();
  /*
   * Enable L2 Cache. Note that the set_L2CR(L2CR) codes checks for
   * relevant CPU type (mpc750)...
   */
  l2cr = get_L2CR();
#ifdef SHOW_LCR2_REGISTER
  printk("Initial L2CR value = %x\n", l2cr);
#endif  
  if ( (! (l2cr & 0x80000000)) && ((int) l2cr == -1))
    set_L2CR(0xb9A14000);

  /*
   * the initial stack  has already been set to this value in start.S
   * so there is no need to set it in r1 again... It is just for info
   * so that It can be printed without accessing R1.
   */
  stack = ((unsigned char*) &__rtems_end) + INIT_STACK_SIZE - CPU_MINIMUM_STACK_FRAME_SIZE;

 /* tag the bottom (T. Straumann 6/36/2001 <strauman@slac.stanford.edu>) */
  *((unsigned32 *)stack) = 0;

  /* fill stack with pattern for debugging */
  __asm__ __volatile__("mr %0, %%r1":"=r"(r1sp));
  while (--r1sp >= (unsigned long*)&__rtems_end)
	  *r1sp=0xeeeeeeee;

  /*
   * Initialize the interrupt related settings
   * SPRG0 = interrupt nesting level count
   * SPRG1 = software managed IRQ stack
   *
   * This could be done latter (e.g in IRQ_INIT) but it helps to understand
   * some settings below...
   */
  intrStack = ((unsigned char*) &__rtems_end) + INIT_STACK_SIZE + INTR_STACK_SIZE - CPU_MINIMUM_STACK_FRAME_SIZE;

 /* tag the bottom (T. Straumann 6/36/2001 <strauman@slac.stanford.edu>) */
  *((unsigned32 *)intrStack) = 0;
  /* fill interrupt stack with pattern for debugging */
  r1sp=(unsigned long*)intrStack;
  while (--r1sp >= (unsigned long*)( intrStack - (INTR_STACK_SIZE - 8)))
	  *r1sp=0xeeeeeeee;

  asm volatile ("mtspr	273, %0" : "=r" (intrStack) : "0" (intrStack));
  asm volatile ("mtspr	272, %0" : "=r" (intrNestingLevel) : "0" (intrNestingLevel));
  /*
   * Initialize default raw exception hanlders. See vectors/vectors_init.c
   */
  initialize_exceptions();

  select_console(CONSOLE_SERIAL);

  printk("-----------------------------------------\n");
  printk("Welcome to %s **BOOTLOADER VERSION $Revision$ ** on %s\n", _RTEMS_version, "SVGM");
  printk("-----------------------------------------\n");
#ifdef SHOW_MORE_INIT_SETTINGS  
  printk("Initial system stack at %x\n",stack);
  __asm__ __volatile__ ("mr %0, %%r1":"=r"(stack));
  printk("(R1 stack pointer is 0x%08x)\n", stack);
  printk("Software IRQ stack at %x\n",intrStack);
  printk("-----------------------------------------\n");
#endif

#ifdef SHOW_MORE_INIT_SETTINGS
  printk("Going to start PCI buses scanning and initialization\n");
#endif  
  /* initialize pci driver. We just supply the SVGM's 
   * config_addr / config_data addresses here
   */
  InitializePCI();

  BSP_mem_size 				= 0x2000000;  /*TODO */
  BSP_bus_frequency			= 66000000; /* TODO */
  BSP_processor_frequency	= 366000000; /* TODO */
  BSP_time_base_divisor		= 4000; /* TODO */
  BSPBaseBaud				= 9600*156; /* TODO, found by experiment */
  /* TODO set openpic timer frequency */
  
  /*
   * Set up our hooks
   * Make sure libc_init is done before drivers initialized so that
   * they can use atexit()
   */

  Cpu_table.pretasking_hook 	 = bsp_pretasking_hook;    /* init libc, etc. */
  Cpu_table.postdriver_hook 	 = bsp_postdriver_hook;
  Cpu_table.do_zero_of_workspace = TRUE;
  Cpu_table.interrupt_stack_size = CONFIGURE_INTERRUPT_STACK_MEMORY;
  Cpu_table.clicks_per_usec 	 = BSP_processor_frequency/(BSP_time_base_divisor * 1000);
  Cpu_table.exceptions_in_RAM 	 = TRUE;

#ifdef SHOW_MORE_INIT_SETTINGS
/*  printk("BSP_Configuration.work_space_size = %x\n", BSP_Configuration.work_space_size); */
#endif  
  work_space_start = 
    (unsigned char *)BSP_mem_size - BSP_Configuration.work_space_size;

  if ( work_space_start <= ((unsigned char *)&__rtems_end) + INIT_STACK_SIZE + INTR_STACK_SIZE) {
    printk( "bspstart: Not enough RAM!!!\n" );
    bsp_cleanup();
  }

  BSP_Configuration.work_space_start = work_space_start;

  /*
   * Initalize RTEMS IRQ system
   */ 

  BSP_rtems_irq_mng_init(0);

#ifdef SHOW_MORE_INIT_SETTINGS
  /* printk("Exit from bspstart 0x%08x\n",*(unsigned long*)(0xffe4)); */
#endif  
}

void
rtemsReboot(void)
{
}
