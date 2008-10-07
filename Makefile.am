#  $Id$
AUTOMAKE_OPTIONS=foreign

include $(top_srcdir)/rtems-pre.am

EXEEXT=$(APPEXEEXT)

libnetboot_a_SOURCES    = fixup_net_config.c pairxtract.c

include_HEADERS         = libnetboot.h

lib_LIBRARIES           = libnetboot.a

bin_PROGRAMS            = netboot
# must not use DATA because DATA files are pre-installed
# during the first phase of the build which must not
# pre-install anything that needs to be generated
execimg_SCRIPTS         = netboot.flashimg.bin

execimgdir              = $(exec_prefix)/img

netboot_SOURCES         = netboot.c ctrlx.c termsize/term.c ctrlx.h 
EXTRA_netboot_SOURCES   = uC5282_mover.c

netboot_LDADD           = $(LDADD_$(rtems_bsp))
netboot_DEPENDENCIES    = $(LDADD_$(rtems_bsp))

CLEANFILES              = $(execimg_SCRIPTS)

netboot_LDADD          += -ltecla_r $(NFSLIB)
#AM_LDFLAGS             += -L../cexp/libtecla -L../rtemsNfs/src -L../rtemsNfs/proto

AM_CPPFLAGS            += -DWINS_LINE_DISC 
#AM_CPPFLAGS            += -DUSE_SHELL
AM_CPPFLAGS            += $(AM_CPPFLAGS_$(rtems_bsp))

AM_CPPFLAGS_uC5282      = -DNVRAM_READONLY -DNVRAM_UCDIMM '-DBSP_mem_size=(1024*1024*8)'
LDADD_uC5282            = uC5282_mover.$(OBJEXT)

include $(top_srcdir)/rtems.am

fixup_net_config.o: nvram.c

netboot.o: nvram.c pathcheck.c

if NETBOOT_COMPRESSED
%.tmpbin: %$(EXEEXT)
	$(RM) $@
	$(OBJCOPY) -Obinary $< $@

%.gz:	%.tmpbin
	$(RM) $@
	gzip -c9 $<  > $@

LINKOBJS=gunzip.$(OBJEXT)
LINKSCRIPT=$(srcdir)/gunzip.lds

DEST=0x100
FLASHSTART_svgm=0xfff80000

# "--just-symbols" files must be loaded _before_ the binary images
LINKARGS=--defsym DEST=$(DEST) --defsym FLASHSTART=$(FLASHSTART_$(rtems_bsp)) -T$(LINKSCRIPT) $(LINKOBJS) --just-symbols=$(@:%.flashimg.bin=%$(EXEEXT))

	
%.flashimg.bin: $(LINKOBJS) %.gz $(LINKSCRIPT)
	$(RM)   $@
	$(LD) -o $@ $(LINKARGS) -Map linkmap --oformat=binary
else
%.flashimg.bin: %$(EXEEXT)
	$(OBJCOPY) -Obinary $< $@
endif
