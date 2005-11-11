/*  Init
 *
 *
 *  $Id$
 */

#define TFTP_PREPREFIX	"/TFTP/"
#define TFTP_PREFIX		"/TFTP/BOOTP_HOST/"		/* filename to prepend when accessing the image via TFTPfs (only if "/TFTP/" not already present) */
#define CMDPARM_PREFIX	"BOOTFILE="				/* if defined, 'BOOTFILE=<image filename>' will be added to the kernel commandline */

#ifndef COREDUMP_APP
#define APPNAME "netboot"
#define TFTP_OPEN_FLAGS (O_RDONLY)
#else
#define APPNAME "coredump"
#define TFTP_OPEN_FLAGS (O_WRONLY)
#endif


#define DEBUG

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>

#include <rtems.h>
#include <rtems/error.h>
#include <rtems/rtems_bsdnet.h>
#include <rtems/tftp.h>
#include <librtemsNfs.h>


#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <bsp.h>

#ifndef COREDUMP_APP
static int nfsInited     = 0;
#endif

static int tftpInited    = 0;

#ifndef COREDUMP_APP
#define RSH_SUPPORT
#define NFS_SUPPORT
#endif
#define TFTP_SUPPORT

#include "pathcheck.c"

#ifndef BARE_BOOTP_LOADER

#include <ctrlx.h>
/* define after including <bsp.h> */

#define DELAY_MIN "0"	/* 0 means forever */
#define DELAY_MAX "30"
#define DELAY_DEF "2"

#define CTRL_C		003	/* ASCII ETX */
#define CTRL_D		004 /* ASCII EOT */
#define CTRL_G		007 /* ASCII EOT */
#define CTRL_K		013 /* ASCII VT  */
#define CTRL_O		017 /* ASCII SI  */
#define CTRL_R		022 /* ASCII DC2 */
#define CTRL_X		030 /* ASCII CAN */

/* special answers */
#define SPC_STOP		CTRL_G
#define SPC_RESTORE		CTRL_R
#define SPC_UP			CTRL_K
#define SPC_REBOOT		CTRL_X
#define SPC_ESC			CTRL_C
#define SPC_CLEAR_UNDO	CTRL_O

#define SPC2CHR(spc) ((spc)+'a'-1) 

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <libtecla.h>
#ifdef LIBTECLA_ACCEPT_NONPRINTING_LINE_END
#define getConsoleSpecialChar() do {} while (0)
#define addConsoleSpecialChar(arg) do {} while (0)
#endif
int ansiTiocGwinszInstall(int line);
#endif

#include <termios.h>

#else
static char *cmdline="";

#endif

/* this is not declared anywhere */
int
select(int  n,  fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);


#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM

#ifndef COREDUMP_APP

/* NETBOOT configuration */
#define CONFIGURE_MAXIMUM_SEMAPHORES   	20 
#define CONFIGURE_MAXIMUM_TASKS         6
#define CONFIGURE_MAXIMUM_DEVICES       4
#define CONFIGURE_MAXIMUM_REGIONS       4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES	4
#define CONFIGURE_MAXIMUM_DRIVERS		4
#define CONFIGURE_LIBIO_MAXIMUM_FILE_DESCRIPTORS 20

#else

/* COREDUMP configuration */
#define CONFIGURE_MAXIMUM_SEMAPHORES    6
#define CONFIGURE_MAXIMUM_TASKS         6
#define CONFIGURE_MAXIMUM_DEVICES       4
#define CONFIGURE_MAXIMUM_REGIONS       4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES	0
#define CONFIGURE_MAXIMUM_DRIVERS		4
#define CONFIGURE_LIBIO_MAXIMUM_FILE_DESCRIPTORS 10

#endif



#define CONFIGURE_MICROSECONDS_PER_TICK 10000

/* readline uses 'setjmp' which saves/restores floating point registers */
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT

#define CONFIGURE_INIT
rtems_task Init (rtems_task_argument argument);
#include <confdefs.h>

/* just in case */
extern int
RTEMS_BSP_NETWORK_DRIVER_ATTACH(/*struct rtems_bsdnet_ifconfig* */);

extern int
rtems_bsdnet_loopattach();

#ifndef BARE_BOOTP_LOADER
static void (*do_bootp)(void) = rtems_bsdnet_do_bootp;

/* hook into rtems_bsdnet_initialize() after interfaces are attached */
static void my_bootp_proc();

static char *boot_if_media = 0;
#else
#define	my_bootp_proc rtems_bsdnet_do_bootp
#endif

static struct rtems_bsdnet_ifconfig lo_ifcfg = {
	"lo0",
	rtems_bsdnet_loopattach,
	NULL,			/*next if*/
	"127.0.0.1",
	"255.0.0.0",
};


static struct rtems_bsdnet_ifconfig eth_ifcfg =
{
        /*
         * These two entries must be supplied for each interface.
        char            *name;
        int             (*attach)(struct rtems_bsdnet_ifconfig *conf);
         */
	/* try use macros here */
	RTEMS_BSP_NETWORK_DRIVER_NAME,
	RTEMS_BSP_NETWORK_DRIVER_ATTACH,

        /*
         * Link to next interface
        struct rtems_bsdnet_ifconfig *next;
         */
	&lo_ifcfg,

        /*
         * The following entries may be obtained
         * from BOOTP or explicitily supplied.
        char            *ip_address;
        char            *ip_netmask;
        void            *hardware_address;
         */
	0,
	0,
	(void*) 0,

        /*
         * The driver assigns defaults values to the following
         * entries if they are not explicitly supplied.
        int             ignore_broadcast;
        int             mtu;
        int             rbuf_count;
         */
	0,0,0,0,

        /*
         * For external ethernet controller board the following
         * parameters are needed
        unsigned int    port;   * port of the board *
        unsigned int    irno;   * irq of the board *
        unsigned int    bpar;   * memory of the board *
         */
	0,0,0 /* seems to be unused by the scc/fec driver */

};


struct rtems_bsdnet_config rtems_bsdnet_config = {
      /*
       * This entry points to the head of the ifconfig chain.
      struct rtems_bsdnet_ifconfig *ifconfig;
       */
	&eth_ifcfg,

      /*
       * This entry should be rtems_bsdnet_do_bootp if BOOTP
       * is being used to configure the network, and NULL
       * if BOOTP is not being used.
      void                    (*bootp)(void);
       */
	my_bootp_proc,

      /*
       * The remaining items can be initialized to 0, in
       * which case the default value will be used.
      rtems_task_priority  network_task_priority;  * 100        *
       */
	0,
      /*
      unsigned long        mbuf_bytecount;         * 64 kbytes  *
       */
	0,
      /*
      unsigned long        mbuf_cluster_bytecount; * 128 kbytes *
       */
	0,
      /*
      char                *hostname;               * BOOTP      *
       */
	0,
      /*
      char                *domainname;             * BOOTP      *
       */
	0,
      /*
      char                *gateway;                * BOOTP      *
       */
	0,
      /*
      char                *log_host;               * BOOTP      *
       */
	0,
      /*
      char                *name_server[3];         * BOOTP      *
       */
	{0,0,0},
      /*
      char                *ntp_server[3];          * BOOTP      *
       */
	{0,0,0},
    };


#ifndef BARE_BOOTP_LOADER
#define __INSIDE_NETBOOT__
#include "nvram.c"
#endif

#ifndef COREDUMP_APP
/* NOTE: rtems_bsdnet_ifconfig(,SIOCSIFFLAGS,) does only set, but not
 *       clear bits in the flags !!
 */
static void
bringdown_netifs(struct rtems_bsdnet_ifconfig *ifs)
{
int				sd,err;
struct ifreq	ir;
char			*msg;

	sd=socket(AF_INET, SOCK_DGRAM, 0);

	if (sd<0) {
		perror("socket");
	} else {
		for (; ifs; ifs=ifs->next) {
			strncpy(ir.ifr_name, ifs->name, IFNAMSIZ);

			msg="SIOCGIFFLAGS";

			err = ioctl(sd, SIOCGIFFLAGS, &ir)<0;

			if (!err) {
				ir.ifr_flags &= ~IFF_UP;
				msg = "SIOCSIFFLAGS";
				err = ioctl(sd, SIOCSIFFLAGS, &ir) < 0;
			}

			if (err) {
				printf("WARNING: unable to bring down '%s' - may corrupt memory!!!\n",
					ifs->name);
				perror(msg);
			} else {
				printf("%s successfully shut down\n",ifs->name);
			}
		}
	}
	fflush(stdout);
	if (sd>=0) close(sd);
}

/* figure out the cache line size */
static unsigned long
probeCacheLineSize(unsigned long *workspace, int nels)
{
register unsigned long *u,*l;
	u=l=workspace+(nels>>1);
	while (nels--) workspace[nels]=(unsigned long)-1;
	__asm__ __volatile__("dcbz 0,%0"::"r"(u));
	while (!*u) u++;
	while (!*l) l--;
	return ((u-l)-1)*sizeof(*u);
}

static long
handleInput(int fd, int errfd, char *bufp, long size)
{
long	n=0;
fd_set	r,w,e;
char	errbuf[1000];
struct  timeval timeout;
int		doTftp = (-1==errfd);

register long ntot=0,got;

	if (n<fd)		n=fd;
	if (n<errfd)	n=errfd;

	n++;
	while (fd>=0 || errfd>=0) {
		if (!doTftp) {
		FD_ZERO(&r);
		FD_ZERO(&w);
		FD_ZERO(&e);

		timeout.tv_sec=5;
		timeout.tv_usec=0;
		if (fd>=0) 		FD_SET(fd,&r);
		if (errfd>=0)	FD_SET(errfd,&r);
		if ((got=select(n,&r,&w,&e,&timeout))<=0) {
				if (got) {
					fprintf(stderr,"network select() error: %s.\n",
							strerror(errno));
				} else {
					fprintf(stderr,"network read timeout\n");
				}
				return 0;
		}
		if (errfd>=0 && FD_ISSET(errfd,&r)) {
				got=read(errfd,errbuf,sizeof(errbuf));
				if (got<0) {
					fprintf(stderr,"network read error (reading stderr): %s.\n",
							strerror(errno));
					return 0;
				}
				if (got)
					write(2,errbuf,got);
				else {
					errfd=-1; 
				}
		}
		}
		if (fd>=0 && ((doTftp) || FD_ISSET(fd,&r))) {
				got=read(fd,bufp,size);
				if (got<0) {
					fprintf(stderr,"network error (reading stdout): %s.\n",
							strerror(errno));
					return 0;
				}
				if (got) {
					bufp+=got;
					ntot+=got;
					if ((size-=got)<=0) {
							fprintf(stderr,"download buffer too small for image\n");
							return 0;
					}
				} else {
					fd=-1;
				}
		}
	}
	return ntot;
}

static void
flushCaches(char *start, long size, long algn)
{
register char *end=start+size;
		if ((unsigned long)start & (algn-1))
				rtems_panic("flushCaches: misaligned buffer\n");
		while (start<end) {
				/* flush the icache also - theoretically, there could
				 * old stuff be lingering around there...
				 */
				__asm__ __volatile__("dcbst 0,%0; icbi 0,%0"::"r"(start));
				start+=algn;
		}
		__asm__ __volatile__("sync");
}

static char *
doLoad(long fd, long errfd)
{
rtems_interrupt_level l;
char *buf,*mem=0;
long ntot;
register unsigned long algn;

	if (!(mem=buf=malloc(RSH_BUFSZ))) {
		fprintf(stderr,"no memory\n");
		goto cleanup;
	}
	algn=probeCacheLineSize((void*)buf,128);
#ifdef DEBUG
	fprintf(stderr,"Cache line size seems to be %li bytes\n", algn);
#endif
	buf=(char*)((long)(buf+algn-1) & ~(algn-1));

	if (!(ntot=handleInput(fd,errfd,buf,RSH_BUFSZ-(buf-mem)))) {
		goto cleanup; /* error message has already been printed */
	}

	flushCaches(buf,ntot,algn);

	fprintf(stderr,"0x%lx (%li) bytes read\n",ntot,ntot);

#if 0 /* testing */
	close(fd); close(errfd);
	return mem;
#else

	/* VERY important: stop the network interface - otherwise,
	 * its DMA engine might continue writing memory, possibly
	 * corrupting the loaded system!
	 */
	bringdown_netifs(rtems_bsdnet_config.ifconfig);

	fprintf(stderr,"Starting loaded image @%p NOW...\n\n\n",buf);
#ifdef DEBUG
	fprintf(stderr,"Cmdline @%p: '%s'\n",cmdline,cmdline);
#endif

	/* make sure they see our messages */
	fflush(stderr); fflush(stdout);
	sleep(1);
	/* fire up a loaded image */
	rtems_interrupt_disable(l);
	{
	char *cmdline_end=cmdline;
	if (cmdline_end)
		cmdline_end+=strlen(cmdline);
	__asm__ __volatile__(
			/* setup commandline */
			"mr %%r3, %0; mr %%r4, %1; mr %%r5, %2; mr %%r6, %3; mr %%r7, %4\n"
			/* switch off MMU and interrupts (assume 1:1 virtual-physical mapping) */
			"mfmsr %0\n"
			"andc  %0, %0, %5\n"
			"mtspr %6, %0\n"
			"mtspr %7, %2\n"
			/* there we go... */
			"rfi\n"
			::"r"(cmdline), "r"(cmdline_end), "r"(buf),"r"(buf+ntot),"r"(algn),
			  "r"(MSR_EE | MSR_DR | MSR_IR), "i"(SRR1), "i"(SRR0)
			:"r3","r4","r5","r6","r7");
	}
#endif

cleanup:
	free(mem);
	if (fd>=0)		close(fd);
	if (errfd>=0)	close(errfd);
	return 0;
}

#else
void __attribute__((section(".bootstrap")))
bootstrap_everything()
{
extern unsigned _edata[];
extern unsigned _etext[];
extern unsigned __DATA_START__[];
extern void		__boot_from_fdiag();
register unsigned *from;
register unsigned *to;

	to = (unsigned*)0x3000 - 1; from = (unsigned *)0x0 - 1 ;
	do {
		*++to = *++from;
/*      asm volatile("dcbf 0,%0"::"r"(to)); */
	} while (to != (unsigned*)0x6000 - 1);

	/* save the exception handler area to 0x3000.0x6000 */
	to=__DATA_START__-1; from=_etext-1;
	do {
		*++to = *++from;
/*      asm volatile("dcbf 0,%0"::"r"(to)); */
	} while ( to != _edata - 1 );
	
	asm volatile(
		"mtlr %0		\n"
		" li %%r3, %1	\n"
		" mr %%r4, %%r3	\n"
		" mr %%r5, %%r3	\n"
		" mr %%r6, %%r3	\n"
		" mr %%r7, %%r3	\n"
		"sync           \n"
		" blr			\n"
		::"r"(__boot_from_fdiag),"i"(0):"r3","r4","r5","r6","r7");
}

#include <libcpu/pte121.h>

/* Provide pagetable setup routine. It serves two purposes:
 *
 *  1) Clamp memory used for the data area.
 *  2) Disable pagetables. The default mapping assumes
 *     data is above text which for this application doesn't
 *     apply!
 *     Don't bother using a different setup, just let the BSP
 *     fall back on the BAT setting...
 */
Triv121PgTbl
BSP_pgtbl_setup(unsigned int *pmemsize)
{
	*pmemsize = 0x80000;
	return 0;
}
#endif /* COREDUMP_APP */


#ifndef BARE_BOOTP_LOADER
static void
help(void)
{
	printf("\n");
	printf("Press 's' for showing the current NVRAM configuration\n");
#ifndef COREDUMP_APP
	printf("Press 'c' for changing your NVRAM configuration\n");
	printf("Press 'b' for manually entering filename/cmdline parameters only\n");
	printf("Press '@' for continuing the netboot (BOOTP flag from NVRAM)\n");
	printf("Press 'd' for continuing the netboot; enforce using BOOTP\n");
	printf("Press 'p' for continuing the netboot; enforce using BOOTP\n"
           "          but use file and cmdline from NVRAM\n");
	printf("Press 'm' for continuing the netboot; enforce using NVRAM config\n");
	printf("Press 'R' to reboot now "
#ifdef SPC_REBOOT
	       "(you can always hit <Ctrl>-%c to reboot)",SPC2CHR(SPC_REBOOT)
#endif
		  );
	fputc('\n',stdout);
#else
	printf("Press 'c' for changing your current configuration (NVRAM will not be changed)\n");
	printf("Press '@' for continuing to boot "APPNAME" (BOOTP flag from NVRAM)\n");
	printf("Press 'd' for continuing to boot "APPNAME"; enforce using BOOTP for network config\n");
	printf("Press 'm' for continuing the netboot; enforce using NVRAM config\n");
#endif
	printf("Press any other key for this message\n");
}

static void
my_bootp_proc()
{
int  media;
	/* configure network media before doing bootp but after
	 * the interface is attached.
	 */
	if ( boot_if_media && *boot_if_media ) {
		/* valid media has been asserted */
		media = rtems_str2ifmedia(boot_if_media, 0 /* 1st phy */);

		if ( rtems_bsdnet_ifconfig(eth_ifcfg.name, SIOCSIFMEDIA, &media) ) {
			perror("Setting interface media failed");
		}
	}
	if ( do_bootp )
		do_bootp();
}


#ifdef COREDUMP_APP

static int
doWrite(int fd, unsigned start, unsigned end)
{
int written;
	while ( start < end ) {
		written = write(fd, (void*)start, end-start);
		if ( written < 0 ) {
			perror("Unable to write; try a different server/file");
			break;
		}
		start += written;
	}
	return end-start;
}

static void
doIt(int manual, int enforceBootp, NetConfigCtxt ctx)
{
int fd = -1, dummy;
char *sstr = strdup("0x0");
char *estr = strdup("0x0");
unsigned long start,end;
char buf[4];

	while (1) {
		free(filename);
		filename = 0;

		if (fd > -1) {
			close(fd);
			fd = -1;
		}

		do {
			callGet(ctx, FILENAME_IDX, 1 /* loop until valid answer */);
		} while ( (fd=isTftpPath(&srvname, filename, &dummy, 0)) < 0 );
		do {
			while ( getNum(ctx, "Enter Start Address> ", &sstr, 1) )
				;
			/* error checking has already been done */
			start = strtoul(sstr,0,0);
			while ( getNum(ctx, "Enter End Address (== Start dumps entire memory)> ", &estr, 1) )
				;
			end = strtoul(estr,0,0);
		} while ( end < start
			     && fprintf(stderr,"\nEnding address must not be less than start\n") );

		if ( end == start ) {
#if 0
			unsigned char reg = *SYN_VGM_REG_INFO_MEMORY;
			start = 0;
			end   = SYN_VGM_REG_INFO_MEMORY_BANKS(reg)        /* number of banks */
					* SYN_VGM_REG_INFO_MEMORY_BANK_SIZE(reg); /* bank size */
#else
			/* we're not using page tables -- hence BSP_memSize is accurate */
			start = 0;
			end   = BSP_mem_size;
#endif
		}

		/* handle special case; write doesn't like a NULL buffer address */
		if ( 0 == start ) {
			unsigned tmpe = end > 4 ? 4 : end;
			for ( ; start<tmpe; start++ )
				buf[start] = *(char*)start;
			if (doWrite(fd,(unsigned)buf,(unsigned)(buf+tmpe)))
				continue;
			start += tmpe;
		}

		if (0==doWrite(fd, start, end))
			printf("Memory dump successfully written\n");
	}
}

#else

static void
doIt(int manual, int enforceBootp, NetConfigCtxt ctx)
{
int  fd,errfd;
Parm p;
int  i;
	for (;1;manual=1) {
		if (manual>0  || !filename) {
			if (!manual)
				fprintf(stderr,"Didn't get a filename from BOOTP server\n");
			callGet(ctx, FILENAME_IDX, 1 /* loop until valid answer */);
		}

		if ( strchr(filename,'~') ) {
			do {
				if ( manual>0 || ( !srvname && '~'==*filename ) ) {
					if (!srvname)
						fprintf(stderr,"Unable to convert server address to name\n");
					callGet(ctx, SERVERIP_IDX, 1/*loop until valid*/);
				}
			} while ( -10 == (fd = isRshPath(&srvname, filename, &errfd, 0)) );
		} else {
			releaseMount( 0 );
		 	if ( (fd = isNfsPath(&srvname, filename, &errfd, 0, 0)) < -10 ) {
				if ( -2 == fd ) {
					/* not strictly necessary here - just to remind us that
					 * the file couldn't be opened but the mount was OK
					 */
					fprintf(stderr,"NFS mount OK but file couldn't be opened\n");
					releaseMount( 0 );
				}
				fd = isTftpPath(&srvname, filename, &errfd, 0);
			}
		}

		if ( fd < 0 )
			continue;

		if (manual>0) {
			callGet(ctx, CMD_LINE_IDX, 0);
		} /* else cmdline==0 [init] */

		/* assemble command line */
		{
			int 	len;
			char	*quoted=0;
			char	*unquoted=bootparms;
			char	*src,*dst;

			free(cmdline);
			cmdline=0;

			len = bootparms ? strlen(bootparms) : 0;

			/* we quote the apostrophs */

			if (bootparms) {
				/* count ' occurrence */
				for (src = bootparms + len - 1;
					 src >= bootparms;
					 src--) {
					if ('\'' == *src)
							len++;
				}

				quoted = malloc(len + 2 + 1); /* opening/ending quote + \0 */
				src = bootparms;
				dst=quoted;
				*dst++ = '\'';
				do {
					if ( '\'' == *src )
						*dst++ = *src;
				} while ( (*dst++ = *src++) );
				*dst-- = 0;
				*dst   = '\'';
				bootparms = quoted;
			} else if (manual>0 || 2==enforceBootp) {
				/* they manually force 'no commandline' */
				bootparms = quoted = strdup("''");
			}


			/* then we append a bunch of environment variables */
			for (i=len=0, p=ctx->parmList; p->name; p++, i++) {
				char *v;
				int		incr;

				if ( (p->flags & (FLAG_NOUSE | FLAG_UNSUPP)) )
					continue;

				v = *ctx->parmList[i].pval;

				/* unused or empty parameter */
				if ( !v											  ||
					 (  do_bootp                 && 
						(p->flags & FLAG_BOOTP)  &&						/* should obtain this by bootp               */
						! ((p->flags & FLAG_BOOTP_MAN) && (manual ||	/* AND it's not overridden manually          */
                                                      (2==enforceBootp) /*     nor by the enforceBootp value '2'     */
                                                          )             /*     which says we should use NVRAM values */
                          )
					 )
					)
					continue;

				if (len) {
					cmdline[len++]=' '; /* use space of the '\0' */
				}

				incr    = strlen(p->name) + strlen(v);
				cmdline = realloc(cmdline, len + incr + 1); /* terminating '\0' */
				sprintf(cmdline + len,"%s%s", p->name, v);
				len+=incr;
			}
		fprintf(stderr,"Hello, this is the RTEMS remote loader; trying to load '%s'\n",
						filename);

#if defined(DEBUG)
		fprintf(stderr,"Appending Commandline:\n");
		fprintf(stderr,"'%s'\n",cmdline ? cmdline : "<EMPTY>");
#endif
		doLoad(fd,errfd);
		/* if we ever return from a load attempt, we restore
		 * the unquoted parameter line
		 */
		close(fd);
		fd = -1;
		if (quoted) {
			bootparms = unquoted;
			free(quoted);
		}
		}
	}
}
#endif

rtems_task Init(
  rtems_task_argument ignored
)
{

  int manual;
  int enforceBootp;

  extern struct in_addr rtems_bsdnet_bootp_server_address;
  extern char           *rtems_bsdnet_bootp_boot_file_name;
  Parm	p;
  int	i;
  NetConfigCtxtRec	ctx;
  char	ch;
  int	secs;

  /* copy static pointers into local buffer pointer array
   * (pointers in the ParmRec struct initializers are easier to maintain
   * but we want the 'config/showConfig' routines to be re-entrant
   * so they can be used by a full-blown system outside of 'netboot')
   */

#ifdef SPC_REBOOT
 	installConsoleCtrlXHack(SPC_REBOOT);
#endif

#ifndef USE_READLINE
	ansiTiocGwinszInstall(7);
#endif

	netConfigCtxtInitialize(&ctx, stdout);

#define SADR rtems_bsdnet_bootp_server_address
#define BOFN rtems_bsdnet_bootp_boot_file_name
#define BCMD rtems_bsdnet_bootp_cmdline

#ifndef COREDUMP_APP
	fprintf(stderr,"\n\nRTEMS bootloader by Till Straumann <strauman@slac.stanford.edu>\n");
#else
	fprintf(stderr,"\n\nRTEMS coredump helper by Till Straumann <strauman@slac.stanford.edu>\n");
#endif
	fprintf(stderr,"$Id$\n");
	fprintf(stderr,"CVS tag $Name$\n");

	if (!readNVRAM(&ctx)) {
		fprintf(stderr,"No valid NVRAM settings found - initializing\n");
		writeNVRAM(&ctx);
	}

#ifndef COREDUMP_APP
	if ( !(parmList[CPU_TAU_IDX].flags & FLAG_UNSUPP) && !CPU_TAU_offset )
		tauOffsetHelp();

	/* it was previously verified that auto_delay_secs contains
	 * a valid string...
	 */
	secs=strtoul(auto_delay_secs,0,0);
#else
	secs = 1;
#endif

	/* give them a chance to abort the netboot */
	do {
	struct termios ot,nt;

		manual = enforceBootp = 0;
		ch = 0;

		/* establish timeout using termios */
		if (tcgetattr(0,&ot)) {
			perror("TCGETATTR");
		} else {
			nt=ot;
			nt.c_lflag &= ~ICANON;
			nt.c_cc[VMIN]=0;
			/* 1s tics */
			nt.c_cc[VTIME]=10;
			if (tcsetattr(0,TCSANOW,&nt)) {
				perror("TCSETATTR");
			} else {
				if (secs<=0) {
					secs=-1;	/* forever */
					help();		/* display options */
				} else {
					fprintf(stderr,"\n\nType any character to abort "APPNAME":xx");
				}
				while (secs) {
					if (secs>0)
						fprintf(stderr,"\b\b%2i",secs--);
					if (read(0,&ch,1)) {
						/* got a character; abort */
						fputc('\n',stderr);
						manual=1;
						break;
					} else {
						ch = 0;
					}
				}
				fputc('\n',stderr);
				if (manual) {
					nt.c_cc[VMIN]=1;
					nt.c_cc[VTIME]=0;
					if (tcsetattr(0,TCSANOW,&nt)) {
						perror("TCSETATTR");
					} else {
						do {
							fputc('\n',stderr);
							switch (ch) {
								case 's':	manual=showConfig(&ctx, 1);
									break;
								case 'c':	if (config(&ctx) >=0) {
#ifndef COREDUMP_APP
												writeNVRAM(&ctx);
#else
												do {} while (0);
#endif
											}
											manual = -1;
									break;
#ifndef COREDUMP_APP
								case 'b':	manual=1;					break;
								case 'p':	manual=0; enforceBootp=2;	break;
								case 'R':
#endif
#ifdef SPC_REBOOT
								case CTRL_X:
#endif
#if defined(SPC_REBOOT) || !defined(COREDUMP_APP)
											rtemsReboot();
											/* never get here */
										break;
#endif

								case '@':	manual=0;					break;
								case 'd':	manual=0; enforceBootp=1;	break;
								case 'm':	manual=0; enforceBootp=-1;	break;

								default: 	manual=-1;
										break;
							}
							if (-1==manual)
								help();
						} while (-1==manual && 1==read(0,&ch,1));
					}
				}
				/* reset terminal attributes */
				tcsetattr(0,TCSANOW,&ot);
			}
		}
		secs = -1;
	} while ( haveAllMandatory( &ctx, ch ) >=
#ifndef COREDUMP_APP
			FILENAME_IDX
#else
			MYIPADDR_IDX
#endif
			);

#if 0
	{
		extern int yellowfin_debug;
		/* shut up the yellowfin */
		yellowfin_debug=0;
	}
#endif

	{
			/* check if they want us to use bootp or not */
			if (!enforceBootp) {
				switch ( toupper(*use_bootp) ) {
					default:  enforceBootp = 1;  break;
					case 'N': enforceBootp = -1; break;
					case 'P': enforceBootp = 2;  break;
				}
			} else {
				sprintf(use_bootp, enforceBootp>0 ? (enforceBootp>1 ? "P" : "Y") : "N");
			}
			if (enforceBootp<0) {
				do_bootp            = 0;
				if (!manual) manual = -2;
			} else {
				/* clear the 'bsdnet' fields - it seems that the bootp subsystem
				 * expects NULL pointers...
				 */
				for (p=ctx.parmList, i=0; p->name; p++, i++) {
					if ( !(p->flags & (FLAG_CLRBP | FLAG_UNSUPP)) )
						continue;
					free(*ctx.parmList[i].pval);
					*ctx.parmList[i].pval = 0;
				}

			}
	}

#ifdef BSP_HAS_MULTIPLE_NETIFS
	if ( bootif ) {
		/* validation of the interface name has already been performed */
		if ( (boot_if_media = strchr(bootif,':')) ) {
			boot_if_media++;
			eth_ifcfg.name = strdup(bootif);
			*strchr(eth_ifcfg.name,':') = 0;
		}
	}
#endif
	if ( !boot_if_media )
		boot_if_media = bootif;

	if ( BSP_mem_size > 32*1024*1024 ) {
		/* Some drivers are more hungry; increase mbuf space if
		 * we have plenty of memory...
		 */
		rtems_bsdnet_config.mbuf_cluster_bytecount = BSP_mem_size >> 7;
		rtems_bsdnet_config.mbuf_bytecount         = BSP_mem_size >> 8;
	}

  	rtems_bsdnet_initialize_network(); 

	if (enforceBootp >= 0) {
		/* use filename, script and server supplied by bootp */

		/* 'p' --> skip filename/bootparams */
		if ( 2 != enforceBootp ) {
			if (BOFN) {
				free(filename);
				filename=strdup(BOFN);
			}
			free(bootparms);
			bootparms = BCMD && *BCMD ? strdup(BCMD) : 0;
		}

		srvname = strdup("xxx.xxx.xxx.xxx.");
		if (!inet_ntop(AF_INET,&SADR,srvname,strlen(srvname))) {
			free(srvname);
			srvname=0;
		}
	}

	/* rebuild path_prefix */
	path_prefix=realloc(path_prefix, strlen(TFTP_PREPREFIX)+strlen(srvname)+2);
	sprintf(path_prefix,"%s%s/",TFTP_PREPREFIX,srvname);

#if 0 /* some parameters -- most notably the gateway are hard to retrieve :-(
       * for now, just switch to 'P' -- the most important part is the command line
       * anyways...
       */
	/* never use bootp on the bootee; we have all information */
	strcpy(use_bootp,"N"); 
	if ( do_bootp ) {
		char *tmp;
		do_bootp = 0;
		/* fill BOOTP-obtained params back */

		srvname = sip(&rtems_bsdnet_bootp_server_address);

		rtems_bsdnet_config.gateway = 0 /* TODO; really hard to read this (not publicly cached) */;
		/* cumbersome to do also: IP/Mask */

		tmp = malloc(40);
		if ( tmp && !gethostname(tmp,40) && *tmp)
			rtems_bsdnet_config.hostname = tmp;
		else
			free(tmp);

		if ( rtems_bsdnet_domain_name )
			rtems_bsdnet_config.domainname = strdup( rtems_bsdnet_domain_name );

		rtems_bsdnet_config.log_host = sip(&rtems_bsdnet_log_host_address);
			
		for ( i = 0; i < rtems_bsdnet_nameserver_count; i++ ) {
			rtems_bsdnet_config.name_server[i] = sip(&rtems_bsdnet_nameserver[i]);
		}

		for ( i = 0; i < rtems_bsdnet_ntpserver_count; i++ ) {
			rtems_bsdnet_config.ntp_server[i] = sip(&rtems_bsdnet_ntpserver[i]);
		}

	}
#else
	enforceBootp = 2;
	strcpy(use_bootp,"P");
#endif

	doIt(manual, enforceBootp, &ctx);

  rtems_task_delete(RTEMS_SELF);

  exit( 0 );
}

#else

rtems_task Init(
  rtems_task_argument ignored
)
{
int fd;
  	rtems_bsdnet_initialize_network(); 
 	if ( !rtems_bsdnet_initialize_tftp_filesystem() )
		BSP_panic("TFTP FS initialization failed\n");
	/* TODO: prepend 'TFTP prefix'; check for NFS */
	if ( (fd = open(rtems_bsdnet_bootp_boot_file_name,O_RDONLY)) < 0 )
		BSP_panic("Unable to open boot file\n");
	doLoad(fd,-1);
	BSP_panic("Loading failed\n");
}

#endif

void
BSP_vme_config(void)
{
}
