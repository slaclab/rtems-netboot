/*  Init
 *
 *
 *  $Id$
 */

#define RSH_CMD			"cat "					/* Command for loading the image using RSH */
#define TFTP_PREFIX		"/TFTP/BOOTP_HOST/"		/* filename to prepend when accessing the image via TFTPfs (only if "/TFTP/" not already present) */
#define CMDPARM_PREFIX	"BOOTFILE="				/* if defined, 'BOOTFILE=<image filename>' will be added to the kernel commandline */
#define ABORT_WAIT_SECS	2						/* how many seconds to give the user for aborting a netboot */


#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>

#include <rtems.h>
#include <rtems/error.h>
#include <rtems/rtems_bsdnet.h>

#include <sys/socket.h>
#include <sys/sockio.h>

#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include <bsp.h>


#include <readline/readline.h>
#include <readline/history.h>

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

register long ntot=0,got;

	if (n<fd)		n=fd;
	if (n<errfd)	n=errfd;

	n++;
	while (fd>=0 || errfd>=0) {
		FD_ZERO(&r);
		FD_ZERO(&w);
		FD_ZERO(&e);

		timeout.tv_sec=5;
		timeout.tv_usec=0;
		if (fd>=0) 		FD_SET(fd,&r);
		if (errfd>=0)	FD_SET(errfd,&r);
		if ((got=select(n,&r,&w,&e,&timeout))<=0) {
				if (got) {
					fprintf(stderr,"rsh select() error: %s.\n",
							strerror(errno));
				} else {
					fprintf(stderr,"rsh timeout\n");
				}
				return 0;
		}
		if (errfd>=0 && FD_ISSET(errfd,&r)) {
				got=read(errfd,errbuf,sizeof(errbuf));
				if (got<0) {
					fprintf(stderr,"rsh error (reading stderr): %s.\n",
							strerror(errno));
					return 0;
				}
				if (got)
					write(2,errbuf,got);
				else {
					errfd=-1; 
				}
		}
		if (fd>=0 && FD_ISSET(fd,&r)) {
				got=read(fd,bufp,size);
				if (got<0) {
					fprintf(stderr,"rsh error (reading stdout): %s.\n",
							strerror(errno));
					return 0;
				}
				if (got) {
					bufp+=got;
					ntot+=got;
					if ((size-=got)<=0) {
							fprintf(stderr,"rsh buffer too small for image\n");
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

/* kernel commandline parameters */
static char *cmdline=0;
/* server IP address */
static char *srvname=0;
/* image file name */
static char *filename=0;
static char *tftp_prefix=0;


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

	fprintf(stderr,"Starting loaded image @%p NOW...\n",buf);

	/* make sure they see our messages */
	fflush(stderr); fflush(stdout);
	sleep(1);
	/* fire up a loaded image */
	rtems_interrupt_disable(l);
	{
	char *cmdline_end=cmdline;
	if (cmdline_end)
		cmdline_end+=strlen(cmdline);
	__asm__ __volatile__("mr %%r3, %0; mr %%r4, %1; mr %%r5, %2; mr %%r6, %3; mr %%r7, %4;  mtlr %2; blr"
						::"r"(cmdline), "r"(cmdline_end), "r"(buf),"r"(buf+ntot),"r"(algn)
						:"r3","r4","r5","r6","r7");
	}
#endif

cleanup:
	free(mem);
	if (fd>=0)		close(fd);
	if (errfd>=0)	close(errfd);
	return 0;
}


static char *
prompt(char *pr, char *proposal)
{
char *rval;
	if (proposal) {
		while (*proposal)
			rl_stuff_char(*proposal++);
	}
	rval=readline(pr);
	if (rval && *rval)
		add_history(rval);
	return rval;
}

static char *
getIpAddr(char *what, char *old, struct in_addr *ip)
{
	struct in_addr inDummy;
	static const char *fmt="Enter %s IP (dot.dot):";
	char *p,*rval=0;

	if (!ip) ip=&inDummy;
	clear_history();
	if (old && *old) add_history(old);

	p=malloc(strlen(fmt)+strlen(what)+1);
	sprintf(p,fmt,what);

	do {
		rval=prompt(p,old);
		if (rval && !inet_aton(rval,ip)) {
				fprintf(stderr,"Invalid address, try again\n");
				free(rval); rval=0;
		}
	} while (!rval);
	free(p);

	return rval;
}

static char *
getString(char *what, char *old)
{
	clear_history();
	if (old && *old) add_history(old);
	return prompt(what,old);
}

static char *
getCmdline(char *old)
{
char *rval;
	do {
		rval=getString("Command Line Parameters:",old);
		if (rval && strstr(rval,CMDPARM_PREFIX)) {
					fprintf(stderr,"must not contain '%s'\n",CMDPARM_PREFIX);
					free(old); old=rval;
					rval=0;
		}
	} while (!rval);
	free(old);
	return rval;
}

static void
help(void)
{
	printf("Press 'c' for manually entering your IP configuration\n");
	printf("Press 'b' for manually entering filename/cmdline parameters only\n");
	printf("Press 'a' for continuing the automatic netboot\n");
	printf("Press any other key for this message\n");
}

static int
config(void)
{
struct in_addr i;
char **p,*old;
p=&rtems_bsdnet_config.ifconfig->ip_address; *p=getIpAddr("my IP address",*p,&i);
p=&rtems_bsdnet_config.ifconfig->ip_netmask; *p=getIpAddr("my netmask",*p,&i);

p=&srvname;                                  *p=getIpAddr("server address",*p,&i);
free(tftp_prefix);
#define TFTP_PREPREFIX	"/TFTP/"
tftp_prefix=malloc(strlen(TFTP_PREPREFIX)+strlen(srvname)+2);
sprintf(tftp_prefix,"%s%s/",TFTP_PREPREFIX,srvname);

p=&rtems_bsdnet_config.gateway;              *p=getIpAddr("gateway address",*p,&i);
if (0==i.s_addr) {
	free(*p); *p=0; /* allow '0.0.0.0' gateway */
}
old = filename;
filename = getString("Enter filename (maybe ~user to specify rsh user):",old);
free(old);
cmdline = getCmdline(cmdline);
rtems_bsdnet_config.bootp=0;	/* disable bootp */
return -2;
}

rtems_task Init(
  rtems_task_argument ignored
)
{

  int tftpInited=0;
  int manual=0;

  char *cmd=0, *fn;
  char *username;
  int  useTftp,fd,errfd;
  extern struct in_addr rtems_bsdnet_bootp_server_address;
  extern char           *rtems_bsdnet_bootp_boot_file_name;

#define SADR rtems_bsdnet_bootp_server_address
#define BOFN rtems_bsdnet_bootp_boot_file_name

	tftp_prefix=strdup(TFTP_PREFIX);

	fprintf(stderr,"\n\nRTEMS bootloader by Till Straumann <strauman@slac.stanford.edu>\n");
	fprintf(stderr,"$Id$\n");

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
				fprintf(stderr,"\n\nType any character to abort netboot:xx");
				for (secs=ABORT_WAIT_SECS; secs; secs--) {
					fprintf(stderr,"\b\b%2i",secs);
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
								case 'c':	manual=config(); 	break;
								case 'b':	manual=1;			break;
								case 'a':	manual=0;			break;
								default:	help();
											manual=-1;
											break;
							}
						} while (-1==manual && 1==read(0,&ch,1));
					}
				}
				tcsetattr(0,TCSANOW,&ot);
			}
		}
	}

	{
		extern int yellowfin_debug;
		yellowfin_debug=1;
	}

  	rtems_bsdnet_initialize_network(); 

	if (manual>=0) {
		/* -2 means we have a manual IP configuration */
		if (BOFN)
			filename=strdup(BOFN);
		srvname = strdup("xxx.xxx.xxx.xxx.");
		if (!inet_ntop(AF_INET,&SADR,srvname,strlen(srvname))) {
			free(srvname);
			srvname=0;
		}
	}
	
	for (;1;manual=1) {
		if (manual>0  || !filename) {
			char *old=0;
			if (!manual)
				fprintf(stderr,"Didn't get a filename from DHCP server\n");
			old=filename;
			filename=getString("Enter filename (maybe ~user to specify rsh user):",old);
			free(old);
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
				char			*old=0;

				if (!srvname)
					fprintf(stderr,"Unable to convert server address to name\n");
				else {
					old=srvname;
				}

				srvname=getIpAddr("server address",old,0);
				free(old);
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
			fprintf(stderr,"No user; using TFTP for download\n");
			fn=filename;
			if (strncmp(filename,"/TFTP/",6)) {
				fn=cmd=realloc(cmd,strlen(tftp_prefix)+strlen(filename)+1);
				sprintf(cmd,"%s%s",tftp_prefix,filename);
			}
			if ((fd=open(fn,O_RDONLY,0))<0) {
					fprintf(stderr,"unable to open %s\n",fn);
					continue;
			}
			errfd=-1;
		}
		/* assemble command line */
		if (manual>0) {
			cmdline=getCmdline(cmdline);
		} /* else cmdline==0 [init] */
		{
		int len=cmdline ? strlen(cmdline) : 0;
		if (len && cmdline[len-1]!=' ') {
			/* add space for a separating ' ' */
			len++;
		}
		cmdline=realloc(cmdline, len + strlen(CMDPARM_PREFIX) + strlen(filename) + 1);
		cmdline[len]=0;
		if (len)
			cmdline[len-1]=' ';
		strcat(cmdline,CMDPARM_PREFIX);
		strcat(cmdline,fn);
		}
		fprintf(stderr,"Hello, this is the RTEMS remote loader; trying to load '%s'\n",
						filename);
		doLoad(fd,errfd);
	}
  rtems_task_delete(RTEMS_SELF);

  exit( 0 );
}

