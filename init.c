/*  Init
 *
 *
 *  $Id$
 */

#undef USE_CEXP
#undef USE_SHELL

#include <bsp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <rtems/rtems_bsdnet.h>
#include <rtems/error.h>
#ifdef USE_SHELL
#include <rtems/shell.h>
#endif
#ifdef USE_CEXP
#include <cexp.h>
#endif

#include <sys/socket.h>
#include <arpa/inet.h>

/* this is not declared anywhere */
int
select(int  n,  fd_set  *readfds,  fd_set  *writefds, fd_set *exceptfds, struct timeval *timeout);


#define CONFIGURE_APPLICATION_NEEDS_CONSOLE_DRIVER
#define CONFIGURE_APPLICATION_NEEDS_CLOCK_DRIVER

#define CONFIGURE_RTEMS_INIT_TASKS_TABLE

#if defined(USE_SHELL) || defined(USE_CEXP)
#define CONFIGURE_USE_IMFS_AS_BASE_FILESYSTEM
#endif

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
#define RSH_BUFSZ	6000000

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

static char *
rshLoad(char *host, char *user, char *cmd)
{
rtems_interrupt_level l;
char *chpt=host,*buf,*mem=0;
long fd,errfd,ntot;
register unsigned long algn;

	fd=rcmd(&chpt,RSH_PORT,user,user,cmd,&errfd);
	if (fd<0) {
		fprintf(stderr,"rcmd: got no remote stdout descriptor\n");
		goto cleanup;
	}
	if (errfd<0) {
		fprintf(stderr,"rcmd: got no remote stderr descriptor\n");
		goto cleanup;
	}
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
	fprintf(stderr,"Starting @%p...\n",buf);
	fflush(stderr); fflush(stdout);
	sleep(1);

#if 0 /* testing */
	close(fd); close(errfd);
	return mem;
#else
	/* fire up a loaded image */
	rtems_interrupt_disable(l);
	__asm__ __volatile__("mr %%r5, %0; mr %%r6, %1; mr %%r7, %2;  mtlr %0; blr"
						::"r"(buf),"r"(buf+ntot),"r"(algn):"r5","r6","r7");
#endif

cleanup:
	free(mem);
	if (fd>=0)		close(fd);
	if (errfd>=0)	close(errfd);
	return 0;
}

rtems_task Init(
  rtems_task_argument ignored
)
{

  rtems_bsdnet_initialize_network(); 
#ifdef USE_CEXP
  if (rtems_bsdnet_initialize_tftp_filesystem())
	rtems_panic("TFTP FS initialization failed");
#endif
#if 0
  rtems_initialize_telnetd();
  {
  char ch;
  rtems_rdbg_initialize();
  printf("initialized RDBG, may I continue?"); fflush(stdout);
  read(0,&ch,1);
  printf("%c\n",ch);
  }

  putenv("TERMCAP=/TFTP/BOOTP_HOST/termcap");
  putenv("TERM=xterm"); /* xfree/xterm */
#endif
#ifndef USE_SHELL
#ifndef USE_CEXP
  {
#define FNSZ	500
	char srvname[50];
	char cmd[FNSZ+8],*fn;
	char ubuf[20];
	char *username;
	int  i,ch,again;
	extern struct in_addr rtems_bsdnet_bootp_server_address;
	extern char           *rtems_bsdnet_bootp_boot_file_name;
#define SADR rtems_bsdnet_bootp_server_address
#define BOFN rtems_bsdnet_bootp_boot_file_name
#define filename (cmd+4)

	for (again=0;1;again=1) {
		strcpy(cmd,"cat ");
		if (again || !inet_ntop(AF_INET,&SADR,srvname,sizeof(srvname))) {
			if (!again)
				fprintf(stderr,"Unable to convert server address to name\n");
		   	fprintf(stderr,"Enter server IP (dot.not):");
			for (i=0; i<sizeof(srvname)-1 && (ch=getchar())>0 &&
				('.'==ch || ('0'<=ch && '9'>=ch)); i++)
				srvname[i]=(char)ch;
			srvname[i]=0;
			fprintf(stderr,"\n");
		}

		if (again  || !(BOFN)) {
			if (!again)
				fprintf(stderr,"Didn't get a filename from DHCP server\n");
			fprintf(stderr,"Enter filename (maybe ~user to specify rsh user):");
			for (i=0; i<FNSZ && (ch=getchar())>0 && '\n'!=ch; i++) {
				filename[i]=(char)ch;
			}
			filename[i]=0;
		} else {
			strcpy(filename,BOFN);
		}
		fn=filename;
		if ('~'==*fn) {
			char *dst;
			/* they gave us user name */
			username=++fn;
			if ((fn=strchr(fn,'/')))
				*(fn++)=0;
			strncpy(ubuf,username,sizeof(ubuf));
			ubuf[sizeof(ubuf)-1]=0;
			username=ubuf;
			if (!fn || !*fn) {
				fprintf(stderr,"No file; trying 'rtems'\n");
				fn="rtems";
			}
			/* cat filename to command */
			for (dst=filename; (*dst++=*fn++););
		} else {
			fprintf(stderr,"No user; trying 'rtems'\n");
			username="rtems";
		}
#if 1
		fprintf(stderr,"Hello, this is the RTEMS remote loader; trying to load '%s'\n",
						filename);
		rshLoad(srvname,username,cmd);
		fprintf(stderr,"Unable to rsh '%s'\n",cmd);
#else
		fprintf(stderr,"Server: '%s'\n",srvname);
		fprintf(stderr,"user:   '%s'\n",username);
		fprintf(stderr,"cmd:    '%s'\n",cmd);
#endif
	}
  }
#else
  do {
	static int argc=2;
	static char *argv[]={"cexp","/TFTP/BOOTP_HOST/svimg.syms",0};
	char *buf=0;
	/*
	buf=rshLoad("134.79.33.86","vxtarget");
	fprintf("loaded a buffer at %p\n",buf);
	*/
	cexp_main(argc,argv);
	free(buf);
	argc=1; /* prevent from re-loading the symtab */
  } while (1);
#endif
#else
  shell_add_cmd("rsh",		"rsh",    "rsh <args>  # rsh test app",
		rsh_main);

  sleep(1);
  shell_init("shel",100000,10,"/dev/console",B9600|CS8,0);
#endif
  rtems_task_delete(RTEMS_SELF);

  exit( 0 );
}

