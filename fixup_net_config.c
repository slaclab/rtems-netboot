/* provide access to the command line parameters */
extern char *BSP_commandline_string;

#include <rtems.h>
#include <rtems/rtems_bsdnet.h>
#include <rtems/rtems_mii_ioctl.h>

#include <assert.h>
#include <sys/socket.h>
#include <sys/sockio.h>
#include <netinet/in.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>

/* there is no public Workspace_Free() variant :-( */
#include <rtems/score/wkspace.h>

/* We'll store here what the application put into its
 * network configuration table.
 */
static void (*the_apps_bootp)(void)=0;
static void my_bootp_intercept(void);
static void fillin_srvrandfile(void);

#define bootp_file					rtems_bsdnet_bootp_boot_file_name
#define bootp_cmdline				rtems_bsdnet_bootp_cmdline
#define bootp_srvr					rtems_bsdnet_bootp_server_address
#define net_config					rtems_bsdnet_config
#define do_bootp					rtems_bsdnet_do_bootp
extern int rtems_bsdnet_loopattach(struct rtems_bsdnet_ifconfig*, int);
#define loopattach					rtems_bsdnet_loopattach

/* parameter table for network setup - separate file because
 * copied from the bootloader
 */
#include "nvram.c"

static char *boot_my_media = 0;

/* UGLY: this routine set BSP_commandline_string as a side effect
 *       of processing the parameter table!
 */
static void extract_boot_parms(char **pbuf)
{
Parm	p;
char	*beg,*end,*buf;

	buf = *pbuf;
	*pbuf = 0;

	parmList[CMD_LINE_IDX].pval = pbuf;

	for (beg=buf; beg; beg=end) {
		/* skip whitespace */
		while (' '==*beg) {
			if (!*++beg) {
				/* end of string reached; bail out */
				return;
			}
		}
		/* simple algorithm to find the end of quoted 'name=quoted'
		 * substrings. As a side effect, quotes are removed from
		 * the value.
		 */
		if ( (end = strchr(beg,'=')) ) {
			if ('\'' == *++end) {
				/* end points to the 1st char after '=' which is a '\'' */

				char *dst = end++;

				/* end points to 1st char after '\'' */

				while ('\'' != *end || '\'' == *++end) {
					if ( 0 == (*dst++=*end++) ) {
						/* NO TERMINATING QUOTE FOUND
						 * (for a properly quoted string we
						 * should never get here)
						 */
						end = 0;
						dst--;
						break;
					}
				}
				*dst = 0;
			} else {
				/* first space terminates non-quoted strings */
				if ( (end = strchr(end,' ')) )
					*(end++)=0;
			}
		}
		/* save special bootloader strings to our private environment
		 * and pass on the others
		 */
		for (p=parmList; p->name; p++) {
			if (!p->pval) continue;
			if (0 == strncmp(beg,p->name,strlen(p->name))) {
				/* found this one; since 'name' contains a '=' strchr will succeed */
				char *s=strchr(beg,'=')+1;

				/* p->pval might point into the
				 * network configuration which is invalid
				 * if we have no networking
				 */
				if (&net_config) {
					*p->pval=malloc(strlen(s)+1);
					strcpy(*p->pval,s);
				}
				break;
			}
		}
		/* unfound name=value pairs are dropped to the floor */
	}
}

static void
fillin_srvrandfile(void)
{
	/* OK - now let's see what we have */
	if (boot_srvname) {
		/* Seems we have a different file server */
		if (inet_pton(AF_INET,
					boot_srvname,
					&bootp_srvr)) {
		}
	}
	if (boot_filename) {
		/* Ha - they manually changed the file name and the parameters */
		bootp_file    = boot_filename;
		/* (dont bother freeing the old one - we don't really know if its malloced */
		boot_filename = 0;
	}
	if (boot_parms) {
		/* comments for boot_filename apply here as well */
		bootp_cmdline = boot_parms;
	} else {
		boot_parms  = bootp_cmdline;
	}
}

/* Scan the list of interfaces for the first non-loopback
 * one. (Same algorithm 'bootp' uses -- except we can't check
 * the flags because a) interfaces are not up yet (when called
 * from the 'fixup' routine or b) we don't have access to the
 * 'kernel' internals without even worse hacking...)
 */

static struct rtems_bsdnet_ifconfig *
find_first_real_if()
{
struct rtems_bsdnet_ifconfig *ifc;

	/* interfaces are not up yet -- we can't check their flags */
	for (ifc=net_config.ifconfig;
			ifc && loopattach==ifc->attach;
			ifc=ifc->next) {
		/* should probably make sure it's not a point-to-point
		 * IF either
		 */
	}
	return ifc;
}


/* if the bootloader loaded a different file
 * than what the BOOTP/DHCP server says we have
 * then we want to forge the respective system
 * variables.
 */
static void
my_bootp_intercept(void)
{
int   media;
	/* Interfaces are attached; now see if we should set the media
	 * Note that we treat the case of an empty media string and
	 * 'auto' differently. In the former case, we simply leave the
	 * IF alone, otherwise we enforce autoconfig.
	 */

	if ( boot_my_media && *boot_my_media ) {
		/* Only do something if the string is not empty */
		media = rtems_str2ifmedia(boot_my_media, 0/* only 1 phy supported here */);
		if ( !media ) {
			fprintf(stderr,"Unable to configure IF media - invalid parameter '%s'\n",boot_my_media);
		} else {
			/* network port has already been selected and the IF name fixed */
			struct rtems_bsdnet_ifconfig *ifc;

			ifc = find_first_real_if();

			if ( !ifc ) {
				fprintf(stderr,"Unable to set IF media - no interface found\n");
			} else {
				if ( rtems_bsdnet_ifconfig(ifc->name, SIOCSIFMEDIA, &media )) {
					fprintf(stderr,
							"Setting IF media on %s (SIOCSIFMEDIA) failed: %s\n",
							ifc->name, strerror(errno));
				}
			}
		}
	}

	/* now check if we should do real bootp */
	if ( !boot_use_bootp || 'N'!=toupper(*boot_use_bootp)) {
		/* Do bootp first */
		if (the_apps_bootp) {
			the_apps_bootp();
		} else {
			do_bootp();
		}
	}
	/* override the server/filename parameters */
	fillin_srvrandfile();
}


void 
nvramFixupBsdnetConfig(int readNvram)
{
Parm	                     p;
struct rtems_bsdnet_ifconfig *ifc;

	/* now hack into the network configuration... */

	/* extract_boot_params() modifies the commandline string (part of the fixup) */
	if ( readNvram ) {
		NetConfigCtxtRec ctx;
		
		lock();
		netConfigCtxtInitialize(&ctx,stdout,0);
		readNVRAM(&ctx);
		netConfigCtxtFinalize(&ctx);
		unlock();
	}

	extract_boot_parms(&BSP_commandline_string);

	if ( boot_my_if ) {
		if ( (boot_my_media = strchr(boot_my_if,':')) ) {
			*boot_my_media++ = 0;
			if ( 0 == *boot_my_if )
				boot_my_if = 0;
		} else {
			boot_my_media = boot_my_if;
			boot_my_if    = 0;
		}
#ifndef BSP_HAS_MULTIPLE_NETIFS
		/* just drop the interface name */
		boot_my_if = 0;
#endif
	}

	ifc = find_first_real_if();
	assert(ifc && "NO INTERFACE CONFIGURATION STRUCTURE FOUND");

	if ( boot_my_if )
		ifc->name = boot_my_if;

	if (boot_use_bootp && 'N'==toupper(*boot_use_bootp)) {
		/* no bootp */

		/* get pointers to the first interface's configuration */
		ifc->ip_address = boot_my_ip;
		boot_my_ip=0;
		ifc->ip_netmask = boot_my_netmask;
		boot_my_netmask = 0;

	} else {
		the_apps_bootp=net_config.bootp;
		/* release the strings that will be set up by
		 * bootp - bootpc relies on them being NULL
		 */
		for (p=parmList; p->name; p++) {
			if (!p->pval) continue;
			if (p->flags & FLAG_CLRBP) {
				free(*p->pval); *p->pval=0;
			}
		}
	}
	/* Always intercept; this gives us a chance do to things
	 * after the interfaces are attached
	 */
	net_config.bootp=my_bootp_intercept;
}
