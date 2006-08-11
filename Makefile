# REAL VALUES (for use in flash)
# SVGM:
FLASHSTART=0xfff80000
DEST=0x100

# mvme5500: Not needed; we burn the uncompressed binary
#FLASHSTART=0xf2000000
#DEST=0x100

# DEBUGGING VALUES (make clean; make when changing these)
# SVGM:
#FLASHSTART=0x200000
#DEST=0x10000

# MVME5500: (claim memory with malloc from MotLoad)
#FLASHSTART=0x4400000
#DEST=0x4000000

# Use a destination address > 0, e.g. 0x10000
# for debugging. In this case, the image will
# merely be uncompressed but _not_ started.
# also, there are some messages printed using
# SMON.
#
# NOTE:
#  debug mode makes a couple of assumptions:
#  A) compressed image is loaded at link address
#     (==FLASHADDR). With SMON this can be achieved
#     doing 'load "<file>" <FLASHADDR>'
#  B) downloaded image does not overlap uncompressed
#     image
#  C) uncompressed destination (DEST) is out of
#     the way of SMON.
#
#  since the uncompressed image can relocate itself,
#  it is still possible to start it in debug mode
#  (see below)
#
# Good values for debugging are:
# FLASHADDR=0x400000
# DEST     =0x010000
#
# Smon0>  load "netboot.gzimage" 0x400000
# Received 248900 bytes in 0.7 seconds.
# loaded netboot.gzimage at 400000
# Smon0>  g r5
# data copied
# bss cleared
# zs set
# inflateInit done
# inflate done
# done
# Smon0>  g r5
# -----------------------------------------
# Welcome to RTEMS RELEASE rtems-ss-20020301/svgm on VGM5/PowerPC/MPC7400
# SSRL Release $Name$/$Date$
# Build Date: Tue May 7 17:08:02 PDT 2002
# -----------------------------------------
# 

APPS=netboot
## T.S, 2006/08/11 Memory footprint on RTEMS-4.7 too big :-(
## disable 'coredump' for now
##ifeq ($(RTEMS_BSP),svgm)
###No point in making coredump for MotLoad BSPs; MotLoad clears all memory when booting
##APPS+=coredump
##endif
APPS+=libnetboot

mkelf=$(1:%=o-optimize/%$(EXTENS))

PROGELF=$(foreach i,$(filter-out lib%,$(APPS)),$(call mkelf,$i))
LIBRS  =$(patsubst lib%,$(ARCH)/lib%.a,$(filter lib%,$(APPS)))
# must still terminate in ".bin"
EXTENS=.nxe
IMGEXT=.flashimg.bin
TMPNAM=tmp
MAKEFILE=Makefile

HDRS = libnetboot.h

SCRIPTS=smonscript.st reflash.st
## coredump.st

include $(RTEMS_MAKEFILE_PATH)/Makefile.inc
include $(RTEMS_CUSTOM)
include $(CONFIG.CC)

#CC=$(CROSS_COMPILE)gcc
#LD=$(CROSS_COMPILE)ld
#OBJCOPY=$(CROSS_COMPILE)objcopy

CFLAGS=-O -I.

TMPIMG=$(TMPNAM).img
LINKBINS= $(TMPIMG).gz
LINKOBJS=gunzip.o
LINKSCRIPT=gunzip.lds

FINALTGT = $(PROGELF:%$(EXTENS)=%$(IMGEXT))

#
# "--just-symbols" files must be loaded _before_ the binary images
LINKARGS=--defsym DEST=$(DEST) --defsym FLASHSTART=$(FLASHSTART) -T$(LINKSCRIPT) $(LINKOBJS) --just-symbols=$(call mkelf,netboot) -bbinary $(LINKBINS) 

all:	$(FINALTGT)

$(TMPIMG):	$(call mkelf,netboot)
	$(OBJCOPY) -Obinary $^ $@

$(PROGELF)::
	$(MAKE) -f Makefile.rtems

$(LIBRS)::
	$(MAKE) -f Makefile.rtems

$(TMPIMG).gz: $(TMPIMG)
	$(RM) $@
	gzip -c9 $^ > $@

#self-relocation is still not fully implemented
#RELOCOPTS=-mrelocatable -G1000
gunzip.o: gunzip.c $(MAKEFILE)
	$(CC) -c $(CFLAGS) $(RELOCOPTS) -DDEST=$(DEST) -o $@ $<

ifeq ($(RTEMS_BSP),svgm)
$(filter %netboot$(IMGEXT),$(FINALTGT)): $(LINKOBJS) $(LINKBINS) $(LINKSCRIPT) $(MAKEFILE)
	$(RM) $@
	$(LD) -o $@ $(LINKARGS) -Map map --oformat=binary
#	$(LD) -o $(@:%.bin=%$(EXTENS)) $(LINKARGS)
	$(RM) $(LINKBINS)
else
$(filter %netboot$(IMGEXT),$(FINALTGT)):%$(IMGEXT):%$(EXTENS)
	$(RM) $@
	$(OBJCOPY) -Obinary $^ $@
endif


$(filter %coredump$(IMGEXT),$(FINALTGT)):%$(IMGEXT):%$(EXTENS)
	$(RM) $@
	$(OBJCOPY) -Obinary $^ $@

ifndef RTEMS_SITE_INSTALLDIR
RTEMS_SITE_INSTALLDIR = $(PROJECT_RELEASE)
endif

$(RTEMS_SITE_INSTALLDIR)/img:
	test -d $@ || mkdir -p $@

install-imgs: $(PROGELF) $(FINALTGT) $(SCRIPTS) $(RTEMS_SITE_INSTALLDIR)/img/
	$(INSTALL_CHANGE) $^

install-libs: $(LIBRS) $(RTEMS_SITE_INSTALLDIR)/lib/
	$(INSTALL_CHANGE) $^

install-hdrs: $(HDRS) $(RTEMS_SITE_INSTALLDIR)/include
	$(INSTALL_CHANGE) $^

install: install-imgs install-libs install-hdrs

clean:
	$(RM) $(TMPIMG) $(LINKBINS) $(LINKOBJS) map
	$(MAKE) -f Makefile.rtems clean

distclean: clean

balla:
	echo $(FINALTGT)
