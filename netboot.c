/*  Init
 *
 *
 *  $Id$
 */

#define RSH_CMD			"cat "					/* Command for loading the image using RSH */
#define TFTP_PREPREFIX	"/TFTP/"
#define TFTP_PREFIX		"/TFTP/BOOTP_HOST/"		/* filename to prepend when accessing the image via TFTPfs (only if "/TFTP/" not already present) */
#define CMDPARM_PREFIX	"BOOTFILE="				/* if defined, 'BOOTFILE=<image filename>' will be added to the kernel commandline */

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


#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <bsp.h>

#include <ctrlx.h>
/* define after including <bsp.h> */

#ifdef LIBBSP_POWERPC_SVGM_BSP_H
#define NVRAM_START		((unsigned char*)0xffe9f000)				/* use pSOS area */
#define NVRAM_END		((unsigned char*)0xffe9f4ff)				/* use pSOS area */
#define NVRAM_STR_START	(NVRAM_START + 2*sizeof(unsigned short))
/* CHANGE THE SIGNATURE WHEN CHANGING THE NVRAM LAYOUT */
#define NVRAM_SIGN		0xcafe										/* signature/version */
#else
#error This application (NVRAM code sections) only works on Synergy VGM BSP
#endif

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

/* this is not declared anywhere */
int
select(int  n,  fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);


#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM

#define CONFIGURE_MAXIMUM_SEMAPHORES    4
#define CONFIGURE_MAXIMUM_TASKS         4
#define CONFIGURE_MAXIMUM_DEVICES       4
#define CONFIGURE_MAXIMUM_REGIONS       4
#define CONFIGURE_MAXIMUM_MESSAGE_QUEUES		2

#define CONFIGURE_LIBIO_MAXIMUM_FILE_DESCRIPTORS 20

#define CONFIGURE_MICROSECONDS_PER_TICK 10000

/* readline uses 'setjmp' which saves/restores floating point registers */
#define CONFIGURE_INIT_TASK_ATTRIBUTES RTEMS_FLOATING_POINT

#define CONFIGURE_INIT
rtems_task Init (rtems_task_argument argument);
#include <confdefs.h>

extern int rtems_bsdnet_loopattach();

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
	rtems_bsdnet_do_bootp,

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
	4096*100,
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


static char *tftp_prefix=0;

#define __INSIDE_NETBOOT__
#include "nvram.c"

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

extern int rcmd();

#define RSH_PORT	514
#define RSH_BUFSZ	25000000

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

static void
help(void)
{
	printf("\n");
	printf("Press 's' for showing the current NVRAM configuration\n");
	printf("Press 'c' for changing your NVRAM configuration\n");
	printf("Press 'b' for manually entering filename/cmdline parameters only\n");
	printf("Press '@' for continuing the netboot (BOOTP flag from NVRAM)\n");
	printf("Press 'd' for continuing the netboot; enforce using BOOTP\n");
	printf("Press 'p' for continuing the netboot; enforce using BOOTP\n"
           "          but use file and cmdline from NVRAM\n");
	printf("Press 'm' for continuing the netboot; enforce using NVRAM config\n");
#ifdef SPC_REBOOT
	printf("Press 'R' to reboot now (you can always hit <Ctrl>-%c to reboot)\n",SPC2CHR(SPC_REBOOT));
#endif
	printf("Press any other key for this message\n");
}

rtems_task Init(
  rtems_task_argument ignored
)
{

  int tftpInited=0;
  int manual;
  int enforceBootp;

  char *cmd=0, *fn;
  char *username, *tmp=0;
  int  useTftp,fd,errfd;
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

 	installConsoleCtrlXHack(SPC_REBOOT);

#ifndef USE_READLINE
	ansiTiocGwinszInstall(7);
#endif

	netConfigCtxtInitialize(&ctx, stdout);

#define SADR rtems_bsdnet_bootp_server_address
#define BOFN rtems_bsdnet_bootp_boot_file_name
#define BCMD rtems_bsdnet_bootp_cmdline

	tftp_prefix=strdup(TFTP_PREFIX);

	fprintf(stderr,"\n\nRTEMS bootloader by Till Straumann <strauman@slac.stanford.edu>\n");
	fprintf(stderr,"$Id$\n");
	fprintf(stderr,"CVS tag $Name$\n");

	if (!readNVRAM(&ctx)) {
		fprintf(stderr,"No valid NVRAM settings found - initializing\n");
		writeNVRAM(&ctx);
	}

	if ( !CPU_TAU_offset )
		tauOffsetHelp();

	/* it was previously verified that auto_delay_secs contains
	 * a valid string...
	 */
	secs=strtoul(auto_delay_secs,0,0);

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
					fprintf(stderr,"\n\nType any character to abort netboot:xx");
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
								case 'c':	if (config(&ctx) >=0)
												writeNVRAM(&ctx);
											manual = -1;
									break;
								case 'b':	manual=1;					break;
								case '@':	manual=0;					break;
								case 'p':	manual=0; enforceBootp=2;	break;
								case 'd':	manual=0; enforceBootp=1;	break;
								case 'm':	manual=0; enforceBootp=-1;	break;

								case CTRL_X:
								case 'R':	rtemsReboot(); /* never get here */
										break;
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
	} while ( haveAllMandatory( &ctx, ch ) >=0 );

	{
		extern int yellowfin_debug;
		/* shut up the yellowfin */
		yellowfin_debug=0;
	}

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
				rtems_bsdnet_config.bootp = 0;
				if (!manual) manual = -2;

				/* rebuild tftp_prefix */
				free(tftp_prefix);
				tftp_prefix=malloc(strlen(TFTP_PREPREFIX)+strlen(srvname)+2);
				sprintf(tftp_prefix,"%s%s/",TFTP_PREPREFIX,srvname);
			} else {
				/* clear the 'bsdnet' fields - it seems that the bootp subsystem
				 * expects NULL pointers...
				 */
				for (p=ctx.parmList, i=0; p->name; p++, i++) {
					if ( !(p->flags & FLAG_CLRBP) )
						continue;
					free(*ctx.parmList[i].pval);
					*ctx.parmList[i].pval = 0;
				}

			}
	}

  	rtems_bsdnet_initialize_network(); 

	if (enforceBootp >= 0 && enforceBootp < 2) {
		/* use filename/server supplied by bootp */
		if (BOFN) {
			free(filename);
			filename=strdup(BOFN);
		}
		free(bootparms);
		bootparms = BCMD && *BCMD ? strdup(BCMD) : 0;

		srvname = strdup("xxx.xxx.xxx.xxx.");
		if (!inet_ntop(AF_INET,&SADR,srvname,strlen(srvname))) {
			free(srvname);
			srvname=0;
		}
	}
	
	for (;1;manual=1) {
		if (manual>0  || !filename) {
			if (!manual)
				fprintf(stderr,"Didn't get a filename from BOOTP server\n");
			callGet(&ctx, FILENAME_IDX, 1 /* loop until valid answer */);
		}
		fn=filename;

		useTftp = fn && '~'!=*fn;

		if (!useTftp) {

 			/* they gave us user name */
			username=++fn;
			if ((fn=strchr(fn,'/'))) {
				*(fn++)=0;
			}

			fprintf(stderr,"Loading as '%s' using RSH\n",username);

			if (!fn || !*fn) {
				fprintf(stderr,"No file; trying 'rtems'\n");
				fn="rtems";
			}

			if (manual>0 || !srvname) {

				if (!srvname)
					fprintf(stderr,"Unable to convert server address to name\n");

				callGet(&ctx, SERVERIP_IDX, 1/*loop until valid*/);
			}

			/* cat filename to command */
			cmd=realloc(cmd,strlen(RSH_CMD)+strlen(fn)+1);
			sprintf(cmd,"%s%s",RSH_CMD,fn);

			{ char *chpt=srvname;
			fd=rcmd(&chpt,RSH_PORT,username,username,cmd,&errfd);
			}
			free(cmd); cmd=0;
			if (fd<0) {
				fprintf(stderr,"rcmd (%s): got no remote stdout descriptor\n",
								strerror(errno));
				continue;
			}
			if (errfd<0) {
				fprintf(stderr,"rcmd (%s): got no remote stderr descriptor\n",
								strerror(errno));
				continue;
			}
			/* reassemble the filename */
			free(cmd);
			cmd=filename;
			filename=malloc(1+strlen(username)+1+strlen(fn)+1);
			sprintf(filename,"~%s/%s",username,fn);
			fn=filename;
		} else {
#ifndef USE_CEXP
  			if (!tftpInited && rtems_bsdnet_initialize_tftp_filesystem())
				BSP_panic("TFTP FS initialization failed");
			else
				tftpInited=1;
#endif
			fprintf(stderr,"No user specified; using TFTP for download\n");
			fn=filename;
			if (strncmp(filename,"/TFTP/",6)) {
				fn=cmd=realloc(cmd,strlen(tftp_prefix)+strlen(filename)+1);
				sprintf(cmd,"%s%s",tftp_prefix,filename);
			} else {
				/* may be necessary to rebuild the server name */
				if ((tmp=strchr(filename+6,'/'))) {
					*tmp=0;
					free(srvname);
					srvname=strdup(filename+6);
					*tmp='/';
				}
			}
			if ((fd=open(fn,O_RDONLY,0))<0) {
					fprintf(stderr,"unable to open %s\n",fn);
					continue;
			}
			errfd=-1;
		}
		if (manual>0) {
			callGet(&ctx, CMD_LINE_IDX, 0);
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
			} else if (manual>0) {
				/* they manually force 'no commandline' */
				bootparms = quoted = strdup("''");
			}


			/* then we append a bunch of environment variables */
			for (i=len=0, p=ctx.parmList; p->name; p++, i++) {
				char *v;
				int		incr;

				v = *ctx.parmList[i].pval;

				/* unused or empty parameter */
				if ( p->flags&FLAG_NOUSE			||
					 !v								||
					 ( rtems_bsdnet_config.bootp && 
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
		if (quoted) {
			bootparms = unquoted;
			free(quoted);
		}
		}
	}
  rtems_task_delete(RTEMS_SELF);

  exit( 0 );
}

void
BSP_vme_config(void)
{
}
