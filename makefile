# REAL VALUES (for use in flash)
FLASHSTART=0xfff80000
DEST=0

# DEBUGGING VALUES (make clean; make when changing these)
#FLASHSTART=0x400000
#DEST=0x10000

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
# Smon0>  load "netload.gzimage" 0x400000
# Received 248900 bytes in 0.7 seconds.
# loaded netload.gzimage at 400000
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

PROGELF=o-optimize/netload
# must still terminate in ".bin"
IMGEXT=flashimg.bin
TMPNAM=tmp
MAKEFILE=makefile

CC=$(CROSS_COMPILE)gcc
LD=$(CROSS_COMPILE)ld
OBJCOPY=$(CROSS_COMPILE)objcopy

CFLAGS=-O

TMPIMG=$(TMPNAM).img
LINKBINS= $(TMPIMG).gz
LINKOBJS=gunzip.o
LINKSCRIPT=gunzip.lds

#
# "--just-symbols" files must be loaded _before_ the binary images
LINKARGS=$(LINKOBJS) --just-symbols=$(PROGELF) --defsym DEST=$(DEST) --defsym FLASHSTART=$(FLASHSTART) -b binary  $(LINKBINS) -T$(LINKSCRIPT)

all:	$(PROGELF).$(IMGEXT)

$(TMPIMG):	$(PROGELF)
	$(OBJCOPY) -Obinary $^ $@

$(PROGELF)::
	$(MAKE) -f Makefile.rtems 

$(TMPIMG).gz: $(TMPIMG)
	$(RM) $@
	gzip -c9 $^ > $@

gunzip.o: gunzip.c $(MAKEFILE)
	$(CC) -c $(CFLAGS) -DDEST=$(DEST) -o $@ $<

$(PROGELF).$(IMGEXT):	$(LINKOBJS) $(LINKBINS) $(LINKSCRIPT) $(MAKEFILE)
	$(RM) $@
	$(LD) -o $@ $(LINKARGS) -Map map --oformat=binary
#	$(LD) -o $(@:%.bin=%.elf) $(LINKARGS)
	$(RM) $(LINKBINS)

clean:
	$(RM) $(TMPIMG) $(LINKBINS) $(LINKOBJS)
	$(MAKE) -f Makefile.rtems clean
