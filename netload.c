/*  Init
 *
 *
 *  $Id$
 */

#define RSH_CMD			"cat "					/* Command for loading the image using RSH */
#define TFTP_PREPREFIX	"/TFTP/"
#define TFTP_PREFIX		"/TFTP/BOOTP_HOST/"		/* filename to prepend when accessing the image via TFTPfs (only if "/TFTP/" not already present) */
#define CMDPARM_PREFIX	"BOOTFILE="				/* if defined, 'BOOTFILE=<image filename>' will be added to the kernel commandline */

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>

#include <rtems.h>
#include <rtems/error.h>
#include <rtems/rtems_bsdnet.h>


#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <bsp.h>

/* define after including <bsp.h> */

#ifdef LIBBSP_POWERPC_SVGM_BSP_H
#define NVRAM_START		((unsigned char*)0xffe9f000)				/* use pSOS area */
#define NVRAM_END		((unsigned char*)0xffe9f4ff)				/* use pSOS area */
#define NVRAM_STR_START	(NVRAM_START + 2*sizeof(unsigned short))
#define NVRAM_SIGN		0xcafe										/* arbitrary signature */
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

#include <readline/readline.h>
#include <readline/history.h>

#include <termios.h>
#include <rtems/termiostypes.h>

#define HACKDISC 5	/* our dummy line discipline */

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
	"es0",
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

#define FLAG_MAND	1
#define FLAG_NOUSE	2	/* dont put into the commandline at all */
#define FLAG_CLRBP  4	/* field needs to be cleared for bootp  */

typedef int (*GetProc)(char *prompt, char **proposal, int mandatory);

typedef struct ParmRec_ {
	char	*name;
	char	**pval;
	char	*prompt;
	GetProc	getProc;
	int		flags;
} ParmRec, *Parm;

static unsigned short
appendNVRAM(unsigned char **pnvram, Parm parm);

static int
readNVRAM(Parm parmList);

static void
writeNVRAM(Parm parmList);

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

/* all kernel commandline parameters */
static char *cmdline=0;
/* editable part of commandline */
static char *bootparms=0;
/* server IP address */
static char *srvname=0;
/* image file name */
static char *filename=0;
static char *tftp_prefix=0;

/* flags need to strdup() these! */
static char *use_bootp="Y";
static char *auto_delay_secs=DELAY_DEF;

static int getString();
static int getCmdline();
static int getIpAddr();
static int getYesNo();
static int getNum();

#define FILENAME_IDX 0
#define SERVERIP_IDX 2
#define BOOTP_EN_IDX 15

/* The code assembling the kernel boot parameter line depends on the order
 * the parameters are listed
 */
static ParmRec parmList[]={
	{ "BP_FILE=",  &filename,
			"Boot file name (may be '~user/path' to specify rsh user):\n"
			" >",
			getString,		FLAG_MAND,
	},
	{ "BP_PARM=",  &bootparms,
			"Command line parameters:\n"
			" >",
			getCmdline,		FLAG_NOUSE,
	},
	{ "BP_SRVR=",  &srvname,
			"Server IP:    >",
			getIpAddr,		FLAG_MAND,
	},
	{ "BP_GTWY=",  &rtems_bsdnet_config.gateway,
			"Gateway IP:   >",
			getIpAddr,		FLAG_CLRBP, 
	},
	{ "BP_MYIP=",  &eth_ifcfg.ip_address,
			"My IP:        >",
			getIpAddr,		FLAG_MAND| FLAG_CLRBP,
	},
	{ "BP_MYMK=",  &eth_ifcfg.ip_netmask,
			"My netmask:   >",
			getIpAddr,		FLAG_MAND | FLAG_CLRBP,
	},
	{ "BP_MYNM=",  &rtems_bsdnet_config.hostname,
			"My name:      >",
			getString,		FLAG_CLRBP,
	},
	{ "BP_MYDN=",  &rtems_bsdnet_config.domainname,
			"My domain:    >",
			getString,		FLAG_CLRBP,
	},
	{ "BP_LOGH=",  &rtems_bsdnet_config.log_host,
			"Loghost IP:   >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_DNS1=",  &rtems_bsdnet_config.name_server[0],
			"DNS server 1: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_DNS2=",  &rtems_bsdnet_config.name_server[1],
			"DNS server 2: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_DNS3=",  &rtems_bsdnet_config.name_server[2],
			"DNS server 3: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_NTP1=",  &rtems_bsdnet_config.ntp_server[0],
			"NTP server 1: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_NTP2=",  &rtems_bsdnet_config.ntp_server[1],
			"NTP server 2: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_NTP3=",  &rtems_bsdnet_config.ntp_server[2],
			"NTP server 3: >",
			getIpAddr,		FLAG_CLRBP,
	},
	{ "BP_ENBL=",  &use_bootp,
			"Use DHCP:                           [Y/N] >",
			getYesNo,		0,
	},
	{ "BP_DELY=",  &auto_delay_secs,
			"Autoboot Delay: ["
					DELAY_MIN "..."
					DELAY_MAX      "secs] (0==forever) >",
			getNum,			FLAG_NOUSE,
	},
	{ 0, }
};

/* ugly hack to get an rtems_termios_tty handle */
/* hack into termios - THIS ROUTINE RUNS IN INTERRUPT CONTEXT */
static
void incharIntercept(struct termios *t, void *arg)
{
/* Note that struct termios is not struct rtems_termios_tty */ 
struct rtems_termios_tty *tty = (struct rtems_termios_tty*)arg;
	/* did they just press Ctrl-C? */
	if (CTRL_X == tty->rawInBuf.theBuf[tty->rawInBuf.Tail]) {
			/* OK, we shouldn't call anything from IRQ context,
			 * but for reboot - who cares...
			 */
			rtemsReboot();
	}
}

static struct ttywakeup ctrlCIntercept = {
		incharIntercept,
		0
};

static int
openToGetHandle(struct rtems_termios_tty *tp)
{
		ctrlCIntercept.sw_arg = (void*)tp;
		return 0;
}

/* we need a dummy line discipline for retrieving an rtems_termios_tty handle :-( */
static struct linesw dummy_ldisc = { openToGetHandle,0,0,0,0,0,0,0 };


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

/* handle special characters; i.e. insert
 * them at the beginning of the current line
 * and accept the line.
 * Our 
 */
static int
handle_spc(int count, int k)
{
char t[2];
	t[0]=k; t[1]=0;
	if (SPC_REBOOT == k)
		rtemsReboot();
	rl_beg_of_line(0,k);
	rl_insert_text(t);
	return rl_newline(1,k);
}

static int
hack_undo(int count, int k)
{
  rl_free_undo_list();
  return 0;
}

static void
installHotkeys(void)
{
  rl_bind_key(SPC_UP,handle_spc);
  rl_bind_key(SPC_STOP,handle_spc);
  rl_bind_key(SPC_ESC,handle_spc);
}

static void
uninstallHotkeys(void)
{
  rl_unbind_key(SPC_UP);
  rl_unbind_key(SPC_STOP);
  rl_unbind_key(SPC_ESC);
}


/* The callers of this routine rely on not
 * getting an empty string.
 * Hence 'prompt()' must free() an empty
 * string and pass up a NULL pointer...
 */
static int
prompt(char *pr, char *proposal, char **answer)
{
char *nval;
int rval=0;
	if (proposal) {
		/* readline doesn't allow us to give a 'start' value
		 * therefore, we apply this ugly hack:
		 * we stuff the characters back into the input queue
		 */
		while (*proposal)
			rl_stuff_char(*proposal++);
		/* a special hack which will reset the undo list */
		rl_stuff_char(SPC_CLEAR_UNDO);
	}
	nval=readline(pr);
	if (!*nval) {
		free(nval); nval=0; /* discard empty answer */
	}
	if (nval) {
		register char *src, *dst;
		/* strip leading whitespace */
		for (src=nval; ' '==*src || '\t'==*src; src++);
		if (src>nval) {
			dst=nval;
			while ((*dst++=*src++));
		}
		switch (*nval) {
			case SPC_STOP:
			case SPC_UP:
			case SPC_ESC:
			/* SPC_REBOOT is processed by the lowlevel handler */
				rval = *nval;

				/* rearrange string to skip the special character */
				
				if (SPC_ESC != *(src=dst=nval) && *++src) {
					while ((*dst++=*src++)) /* do nothing else */;
				} else {
					free(nval);
					nval = 0;
				}
			break;

			default:
				add_history(nval);
			case 0:
				break;

		}
	}
	*answer=nval;
	return rval;
}

static int
getIpAddr(char *what, char **pval, int mandatory)
{
struct	in_addr inDummy;
char	*p, *nval=0;
int		result=0;

#ifdef USE_TEMPLATE
	static const char *fmt="Enter %s(dot.dot):";
	int l,pad=0;

	l=strlen(fmt)+strlen(what);
	if (l<pad) {
		pad=35-l;
		l=35;
	}
	p=malloc(l+1);
	sprintf(p,fmt,what);
	while (pad>0) {
		p[35-pad]=' ';
		pad--;
	}
	p[l]=0;
#else
	p=what;
#endif

	do {
		if (mandatory<0) mandatory=0; /* retry flag */
		/* Do they want something special ? */
		result=prompt(p,*pval,&nval);
		if (nval) {
			if (!inet_aton(nval,&inDummy)) {
				fprintf(stderr,"Invalid address, try again\n");
				free(nval); nval=0;
				result = 0;
				if (!mandatory)
					mandatory=-1;
			}
		}
	} while ( !result && !nval && mandatory);

#ifdef USE_TEMPLATE
	free(p);
#endif

	/* prompt() sets nval to NULL on special answers */
	if (nval) {
		if (!mandatory && 0==strcmp(nval,"0.0.0.0")) {
			free(nval); nval=0;
		} else {
			free(*pval);
			*pval=nval;
		}
	}

	return result;
}

static int
getYesNo(char *what, char **pval, int mandatory)
{
char *nval=0,*chpt;
int  result=0;

	do {
		if (mandatory<0) mandatory=0; /* retry flag */
		/* Do they want something special ? */
		result=prompt(what,*pval,&nval);
		if (nval) {
			switch (*nval) {
				case 'Y':
				case 'y':
				case 'N':
				case 'n':
						for (chpt=nval; *chpt; chpt++)
							*chpt=toupper(*chpt);
						if (!*(nval+1))
							break; /* acceptable */

						if (!strcmp(nval,"YES") || !strcmp(nval,"NO")) {
							nval[1]=0; /* Y/N */
							break;
						}
						/* unacceptable, fall thru */

				default:fprintf(stderr,"What ??\n");
						if (!mandatory) mandatory=-1;
						/* fall thru */
				case 0:	free(nval); nval=0;
						result = 0;
				break;
			}
		}
	} while (!result && !nval && mandatory);
	if (nval) {
		free(*pval); *pval=nval;
	}
	return result;
}

static int
getNum(char *what, char **pval, int mandatory)
{
char *nval=0;
int  result=0;

	do {
		if (mandatory<0) mandatory=0; /* retry flag */
		/* Do they want something special ? */
		result=prompt(what,*pval,&nval);
		if (nval) {
			unsigned long	tst;
			char			*endp;
			tst=strtoul(nval,&endp,0);
			if (*endp) {
				fprintf(stderr,"Not a valid number - try again\n");
				free(nval); nval=0;
				if (!mandatory) mandatory=-1;
				result = 0;
			}
		}
	} while (!result && !nval && mandatory);
	if (nval || !result) {
		/* may also be a legal empty string */
		free(*pval); *pval=nval;
	}
	return result;
}

static int
getString(char *what, char **pval, int mandatory)
{
char *nval=0;
int  result=0;

	do {
		/* Do they want something special ? */
		result=prompt(what,*pval,&nval);
	} while (!result && !nval && mandatory);
	if (nval || !result) {
		/* may also be a legal empty string */
		free(*pval); *pval=nval;
	}
	return result;
}

static int
getCmdline(char *what, char **pval, int mandatory)
{
char *old=0;
int  retry = 1, result=0;

	while (retry-- && !result) {
		old = strdup(*pval);
		/* old value is released by getString */
		result=getString(what, pval, 0);
		if (*pval) {
			/* if they gave a special answer, the old value is restored */
			int i;
			if (0==strlen(*pval)) {
				free(*pval);
				*pval=0;
			} else {
			for (i=0; parmList[i].name; i++) {
				if (parmList[i].flags & FLAG_NOUSE)
					continue; /* this name is not used */
				if (strstr(*pval,parmList[i].name)) {
					fprintf(stderr,"must not contain '%s' - this name is private for the bootloader\n",parmList[i].name);
					retry=1;
					/* restore old value */
					free(*pval); *pval=old;
					old=0;
					break;
				}
			}
			}
		}
	}
	if (old) free(old);
	return result;
}

/* clear the history and call a get proc */
static int
callGet(GetProc p, char *prompt, char **ppval, int mandatory, int repeat)
{
int rval;
	clear_history();
	do {
		if (*ppval && **ppval) add_history(*ppval);
	} while ((rval=p(prompt,ppval,mandatory)) && repeat);
	return rval;
}

static void
help(void)
{
	printf("\n");
	printf("Press 's' for showing the current NVRAM configuration\n");
	printf("Press 'c' for changing your NVRAM configuration\n");
	printf("Press 'b' for manually entering filename/cmdline parameters only\n");
	printf("Press '@' for continuing the netboot (DHCP flag from NVRAM)\n");
	printf("Press 'd' for continuing the netboot; enforce using DHCP\n");
	printf("Press 'm' for continuing the netboot; enforce using NVRAM config\n");
	printf("Press 'R' to reboot now (you can always hit <Ctrl>-%c to reboot)\n",SPC2CHR(SPC_REBOOT));
	printf("Press any other key for this message\n");
}

static int
showConfig(int doReadNvram)
{
Parm p;
    if (doReadNvram && !readNVRAM(parmList)) {
		fprintf(stderr,"\nWARNING: no valid NVRAM configuration found\n");
	} else {
		fprintf(stderr,"\n%s configuration:\n\n",
				doReadNvram ? "NVRAM" : "Actual");
		for (p=parmList; p->name; p++) {
			char *chpt;
			fputs("  ",stderr);
			for (chpt=p->prompt; *chpt; chpt++) {
				fputc(*chpt,stderr);
				if ('\n'==*chpt)
					fputs("  ",stderr); /* indent */
			}
			if (*p->pval)
				fputs(*p->pval,stderr);
			fputc('\n',stderr);
		}
		fputc('\n',stderr);
	}
	return -1; /* continue looping */
}



static int
config(int howmany)
{
int  i=0;
Parm p;

	fprintf(stderr,"Changing NVRAM configuration\n");
	fprintf(stderr,"Use '<Ctrl>-%c' to go up to previous field\n",
				SPC2CHR(SPC_UP));
	fprintf(stderr,"Use '<Ctrl>-%c' to restore this field\n",
				SPC2CHR(SPC_RESTORE));
	fprintf(stderr,"Use '<Ctrl>-%c' to quit+write NVRAM\n",
				SPC2CHR(SPC_STOP));
	fprintf(stderr,"Use '<Ctrl>-%c' to quit+cancel (all values are restored)\n",
				SPC2CHR(SPC_ESC));
	fprintf(stderr,"Use '<Ctrl>-%c' to reboot\n",
				SPC2CHR(SPC_REBOOT));

if	(howmany<1)
	howmany=1;
else if	(howmany > sizeof(parmList)/sizeof(parmList[0]) - 1)
	howmany = sizeof(parmList)/sizeof(parmList[0]) - 1;

installHotkeys();

while ( i>=0 && i<howmany ) {
	switch (callGet(parmList[i].getProc,
					parmList[i].prompt,
					parmList[i].pval,
					parmList[i].flags&FLAG_MAND,
					0 /* dont repeat */)) {

		case SPC_ESC:
			fprintf(stderr,"Restoring previous configuration\n");
			if (readNVRAM(parmList))
				return -1;
			else {
				fprintf(stderr,"Unable to restore configuration, please start over\n");
			}
			i=0;
		break;

		case SPC_STOP:  i=-1;
		break;

		case SPC_UP:	if (0==i)
							i=howmany-1;
						else
							i--;
		break;

		/* SPC_REBOOT is processed directly by the handler  */

		default:		i++;
		break;
	}
}

uninstallHotkeys();

/* make sure we have all mandatory parameters */
for (p=parmList; p->name; p++) {
	if ( (p->flags&FLAG_MAND) ) {
		while ( !*p->pval) {
			fprintf(stderr,"Need parameter...\n");
			callGet(p->getProc,
					p->prompt,
					p->pval,
					FLAG_MAND,
					0);
		}
	}
}

/* make sure we have a reasonable auto_delay */
{
char *endp,*override=0;
unsigned long d,min,max;

	min=strtoul(DELAY_MIN,0,0);
	max=strtoul(DELAY_MAX,0,0);

	if (auto_delay_secs) {
			d=strtoul(auto_delay_secs, &endp,0);
			if (*auto_delay_secs && !*endp) {
				/* valid */
				if (d<min) {
					fprintf(stderr,"Delay too short - using %ss\n",DELAY_MIN);
					override=DELAY_MIN;
				} else if (d>max) {
					fprintf(stderr,"Delay too long - using %ss\n",DELAY_MAX);
					override=DELAY_MAX;
				}
			} else {
				fprintf(stderr,"Invalid delay - using default: %ss\n",DELAY_DEF);
				override=DELAY_DEF;
			}
	} else {
		override=DELAY_DEF;
	}
	if (override) {
		free(auto_delay_secs);
		auto_delay_secs=strdup(override);
	}
}

/* write to NVRAM */
writeNVRAM(parmList);

return -1;	/* continue looping */
}

static unsigned short
appendNVRAM(unsigned char **pnvram, Parm parm)
{
unsigned char *src, *dst;
unsigned short sum;
unsigned char *jobs[3], **job;

	if (!*parm->pval)
		return 0;

	jobs[0]=parm->name;
	jobs[1]=*parm->pval;
	jobs[2]=0;

	sum=0;
	dst=*pnvram;

	for (job=jobs; *job; job++) {
		for (src=*job; *src && dst<NVRAM_END-1; ) {
			sum += (*dst++=*src++);
		}

		if (*src) {
			fprintf(stderr,"WARNING: NVRAM overflow - not enough space\n");
			**pnvram=0;
			return 0;
		}
	}
	/* success, append separator */
	sum += (*dst++=' ');
	*pnvram = dst;
	return sum;
}

static void
writeNVRAM(Parm parmList)
{
unsigned char	*nvchpt=NVRAM_STR_START;
unsigned short	sum;
Parm			p;

		sum = 0;
		for (p=parmList; p->name; p++) {
			sum += appendNVRAM(&nvchpt, p);
			*nvchpt=0;
		}
		/* tag the end - there is space for the terminating '\0', it's safe */
		*nvchpt=0;

		nvchpt=NVRAM_STR_START;
		sum += (*--nvchpt=(NVRAM_SIGN & 0xff));
		sum += (*--nvchpt=((NVRAM_SIGN>>8) & 0xff));
		*--nvchpt=sum&0xff;
		*--nvchpt=((sum>>8)&0xff);
		fprintf(stderr,"\nNVRAM configuration updated\n");
}

static int
readNVRAM(Parm parmList)
{
unsigned short	sum,tag;
unsigned char	*nvchpt=NVRAM_START, *str, *pch, *end;
Parm			p;

	sum=(*nvchpt++)<<8;
	sum+=(*nvchpt++);
	sum=-sum;
	sum+=(tag=*nvchpt++);
	tag= (tag<<8) | *nvchpt;
	sum+=*nvchpt++;
	if (tag != NVRAM_SIGN) {
			fprintf(stderr,"No NVRAM signature found\n");
			return 0;
	}
	str=nvchpt;
	/* verify checksum */
	while (*nvchpt && nvchpt<NVRAM_END)
			sum+=*nvchpt++;

	if (*nvchpt) {
			fprintf(stderr,"No end of string found in NVRAM\n");
			return 0;
	}
	if (sum) {
			fprintf(stderr,"NVRAM checksum error\n");
			return 0;
	}
	/* OK, we found a valid string */
	str = strdup(str);

	for (pch=str; pch; pch=end) {
		/* skip whitespace */
		while (' '==*pch) {
			if (!*++pch)
				/* end of string reached; bail out */
				goto cleanup;
		}
		if ( (end=strchr(pch,'=')) ) {
			unsigned char *val=++end;

			/* look for the end of this parameter */
			if ((end = strchr(end, ' ')))
					*end++=0; /* tag */

			/* a valid parameter found */
			for (p=parmList; p->name; p++) {
				if (strncmp(pch, p->name, val-pch))
				continue;
					/* found the parameter */
					free(*p->pval);
					*p->pval=strdup(val);
				break; /* for p=parmList */
			}
		}
	}

cleanup:
	free(str);
	return 1;
}


rtems_task Init(
  rtems_task_argument ignored
)
{

  int tftpInited=0;
  int manual=0;
  int enforceBootp=0;

  char *cmd=0, *fn;
  char *username, *tmp=0;
  int  useTftp,fd,errfd;
  extern struct in_addr rtems_bsdnet_bootp_server_address;
  extern char           *rtems_bsdnet_bootp_boot_file_name;
  Parm	p;

  rl_initialize();

  rl_bind_key(SPC_REBOOT,handle_spc);
  rl_bind_key(SPC_CLEAR_UNDO, hack_undo);
  /* readline (temporarily) modifies the argument to rl_parse_and_bind();
   * mustn't be static/ro text
   */
  tmp=strdup("Control-r:revert-line");
  rl_parse_and_bind(tmp);
  free(tmp);

	/* initialize 'flags'; all configuration variables
	 * must be malloc()ed
	 */
	use_bootp=strdup(use_bootp);
	auto_delay_secs=strdup(auto_delay_secs);

#define SADR rtems_bsdnet_bootp_server_address
#define BOFN rtems_bsdnet_bootp_boot_file_name

	tftp_prefix=strdup(TFTP_PREFIX);

	fprintf(stderr,"\n\nRTEMS bootloader by Till Straumann <strauman@slac.stanford.edu>\n");
	fprintf(stderr,"$Id$\n");

	if (!readNVRAM(parmList)) {
		fprintf(stderr,"No valid NVRAM settings found - initializing\n");
		writeNVRAM(parmList);
	}


	/* give them a chance to abort the netboot */
	{
	struct termios ot,nt;
	char ch;
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
				int secs;
				/* it was previously verified that auto_delay_secs contains
				 * a valid string...
				 */
				secs=strtoul(auto_delay_secs,0,0);
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
								case 's':	manual=showConfig(1);		break;
								case 'c':	manual=config(1000);		break;
								case 'b':	manual=1;					break;
								case '@':	manual=0;					break;
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
	}

	{
		extern int yellowfin_debug;
		/* shut up the yellowfin */
		yellowfin_debug=0;
	}

	/* now install our 'Ctrl-C' hack, so they can abort anytime while
	 * network lookup and/or loading is going on...
	 */
	{
			int	d=HACKDISC,o;
			linesw[d]=dummy_ldisc;
			/* just by installing the line discipline, the
			 * rtems_termios_tty pointer gets 'magically' installed into the
			 * ttywakeup struct...
			 *
			 * Start with retrieving the original ldisc...
			 */
			assert(0==ioctl(0,TIOCGETD,&o));
			assert(0==ioctl(0,TIOCSETD,&d));
			/* make sure we got a rtems_termios_tty pointer */
			assert(ctrlCIntercept.sw_arg);
			/* for some reason, it seems that we must reinstall the original discipline
			 * otherwise, the system seems to freeze further down the line (during/after
			 * network init)
			 */
			assert(0==ioctl(0,TIOCSETD,&o));

			/* finally install our handler */
			assert(0==ioctl(0,RTEMS_IO_RCVWAKEUP,&ctrlCIntercept));
	}

	{
			/* check if they want us to use bootp or not */
			if (!enforceBootp)
				enforceBootp = ((*use_bootp && 'N' == toupper(*use_bootp)) ? -1 : 1); 
			else
				sprintf(use_bootp, enforceBootp>0 ? "Y" : "N");
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
				for (p=parmList; p->name; p++) {
					if ( !(p->flags & FLAG_CLRBP) )
						continue;
					free(*p->pval);
					*p->pval=0;
				}

			}
	}

  	rtems_bsdnet_initialize_network(); 

	if (enforceBootp >= 0) {
		/* use filename/server supplied by bootp */
		if (BOFN) {
			free(filename);
			filename=strdup(BOFN);
		}
		srvname = strdup("xxx.xxx.xxx.xxx.");
		if (!inet_ntop(AF_INET,&SADR,srvname,strlen(srvname))) {
			free(srvname);
			srvname=0;
		}
	}
	
	for (;1;manual=1) {
		if (manual>0  || !filename) {
			if (!manual)
				fprintf(stderr,"Didn't get a filename from DHCP server\n");
			callGet(getString,
					"Enter filename (maybe ~user to specify rsh user):",
					&filename,
					FLAG_MAND,
					1 /* loop until valid answer */);
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

				callGet(getIpAddr,"Server address: ",&srvname,FLAG_MAND,1/*loop until valid*/);
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
			callGet(getCmdline,"Command line parameters:",&bootparms,0,0);
		} /* else cmdline==0 [init] */

		/* assemble command line */
		{
			int 	len;
			Parm	end;

			/* The command line parameters come first */
			len = bootparms ? strlen(bootparms) : 0;

			cmdline = realloc(cmdline, len ? len+1 : 0); /* terminating ' \0' */
			if (len) {
				strcpy(cmdline, bootparms);
			}

			p=parmList;

			/* then we append a bunch of environment variables */
			if ( !rtems_bsdnet_config.bootp )
				end = p+1000; /* will encounter the end mark */
			else if (manual)
				end = p+SERVERIP_IDX+1;
			else
				end = p;

			for (; p<end && p->name; p++) {
				char *v;
				int		incr;

				v = *p->pval;	

				/* unused or empty parameter */
				if (p->flags&FLAG_NOUSE || !v) continue;

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
		}
	}
  rtems_task_delete(RTEMS_SELF);

  exit( 0 );
}

void
BSP_vme_config(void)
{
}
