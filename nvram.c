/*  nvram
 *
 *
 *  $Id$
 */


#include <rtems.h>

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>

#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>

#include <unistd.h>

#ifdef GET_RAW_INPUT
#include <termios.h>
#endif

#ifdef USE_READLINE
#include <readline/readline.h>
#include <readline/history.h>
#else
#include <libtecla.h>
#endif

#ifdef LIBTECLA_ACCEPT_NONPRINTING_LINE_END
#define getConsoleSpecialChar() do {} while (0)
#define addConsoleSpecialChar(arg) do {} while (0)
#elif !defined(USE_READLINE) && !defined(__INSIDE_NETBOOT__)
#define DISABLE_HOTKEYS
#endif

#include <bsp.h>

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
#ifdef __INSIDE_NETBOOT__
#define SPC_REBOOT		CTRL_X
#endif
#define SPC_ESC			CTRL_C
#define SPC_CLEAR_UNDO	CTRL_O

#define SPC2CHR(spc) ((spc)+'a'-1) 

#define FLAG_MAND		(1<<0)
#define FLAG_NOUSE		(1<<1)	/* dont put into the commandline at all */
#define FLAG_CLRBP  	(1<<2)	/* field needs to be cleared for bootp  */
#define FLAG_DUP		(1<<3)	/* field needs strdup at init time      */
#define FLAG_BOOTP		(1<<4)  /* dont put into commandline when BOOTP active */
#define FLAG_BOOTP_MAN	(1<<5)	/* DO put into commandline even when BOOTP active but manual override is effective */


typedef struct NetConfigCtxtRec_	*NetConfigCtxt;
typedef struct ParmRec_				*Parm;

typedef struct NetConfigCtxtRec_ {
	FILE	*out;
	FILE	*err;
	Parm	parmList;		/* descriptor list for parameters */
#ifndef USE_READLINE
	int		useHotkeys;
	GetLine *gl;
#endif
} NetConfigCtxtRec;

#define GET_PROC_ARG_PROTO NetConfigCtxt c, char *prompt, char **proposal, int mandatory
typedef int (*GetProc)(GET_PROC_ARG_PROTO);

typedef struct ParmRec_ {
	char	*name;
	char	**pval;
	char	*prompt;
	GetProc	getProc;
	int		flags;
} ParmRec;

static unsigned short
appendNVRAM(NetConfigCtxt c, unsigned char **pnvram, int i_parm);

static int
readNVRAM(NetConfigCtxt c);

static void
writeNVRAM(NetConfigCtxt c);

#define NUM_PARMS 18

#ifdef __INSIDE_NETBOOT__
/* all kernel commandline parameters */
static char *cmdline=0;
/* editable part of commandline */
static char *bootparms=0;
/* server IP address */
static char *srvname=0;
/* image file name */
static char *filename=0;

/* flags need to strdup() these! */
static char *use_bootp="Y";
static char *auto_delay_secs=DELAY_DEF;
static char *CPU_TAU_offset = 0;
#else
static char *strbuf[NUM_PARMS] = {0};
#endif

static int getString(GET_PROC_ARG_PROTO);
static int getCmdline(GET_PROC_ARG_PROTO);
static int getIpAddr(GET_PROC_ARG_PROTO);
static int getUseBootp(GET_PROC_ARG_PROTO);
static int getNum(GET_PROC_ARG_PROTO);

#define FILENAME_IDX 0
#define CMD_LINE_IDX 1
#define SERVERIP_IDX 2
#define BOOTP_EN_IDX 15
#define DELYSECS_IDX 16
#define CPU_TAU_IDX  17

/* The code assembling the kernel boot parameter line depends on the order
 * the parameters are listed.
 * The prompts should be chosen in a way so the first ~14 chars make sense...
 */
static ParmRec parmList[NUM_PARMS+1]={
	{ "BP_FILE=",
#ifdef __INSIDE_NETBOOT__
		   	&filename,
#else
			strbuf + 0,
#endif
			"Boot file name (may be '~user/path' to specify rsh user):\n"
			" >",
			getString,		FLAG_MAND | FLAG_BOOTP | FLAG_BOOTP_MAN,
	},
	{ "BP_PARM=",
#ifdef __INSIDE_NETBOOT__
		   	&bootparms,
#else
			strbuf + 1,
#endif
			"Command line parameters:\n"
			" >",
			getCmdline,		0 | FLAG_BOOTP | FLAG_BOOTP_MAN,
	},
	{ "BP_SRVR=",
#ifdef __INSIDE_NETBOOT__
			&srvname,
#else
			strbuf + 2,
#endif
			"Server IP:    >",
			getIpAddr,		FLAG_MAND | FLAG_BOOTP | FLAG_BOOTP_MAN,
	},
	{ "BP_GTWY=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.gateway,
#else
			strbuf + 3,
#endif
			"Gateway IP:   >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP, 
	},
	{ "BP_MYIP=",
#ifdef __INSIDE_NETBOOT__
			&eth_ifcfg.ip_address,
#else
			strbuf + 4,
#endif
			"My IP:        >",
			getIpAddr,		FLAG_MAND| FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_MYMK=",
#ifdef __INSIDE_NETBOOT__
			&eth_ifcfg.ip_netmask,
#else
			strbuf + 5,
#endif
			"My netmask:   >",
			getIpAddr,		FLAG_MAND | FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_MYNM=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.hostname,
#else
			strbuf + 6,
#endif
			"My name:      >",
			getString,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_MYDN=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.domainname,
#else
			strbuf + 7,
#endif
			"My domain:    >",
			getString,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_LOGH=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.log_host,
#else
			strbuf + 8,
#endif
			"Loghost IP:   >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_DNS1=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.name_server[0],
#else
			strbuf + 9,
#endif
			"DNS server 1: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_DNS2=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.name_server[1],
#else
			strbuf + 10,
#endif
			"DNS server 2: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_DNS3=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.name_server[2],
#else
			strbuf + 11,
#endif
			"DNS server 3: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_NTP1=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.ntp_server[0],
#else
			strbuf + 12,
#endif
			"NTP server 1: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_NTP2=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.ntp_server[1],
#else
			strbuf + 13,
#endif
			"NTP server 2: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_NTP3=",
#ifdef __INSIDE_NETBOOT__
			&rtems_bsdnet_config.ntp_server[2],
#else
			strbuf + 14,
#endif
			"NTP server 3: >",
			getIpAddr,		FLAG_CLRBP | FLAG_BOOTP,
	},
	{ "BP_ENBL=",
#ifdef __INSIDE_NETBOOT__
			&use_bootp,
#else
			strbuf + 15,
#endif
			"Use BOOTP: Yes, No or Partial (-> file and\n"
            "          command line from NVRAM) [Y/N/P]>",
			getUseBootp,	FLAG_DUP | FLAG_BOOTP,
	},
	{ "BP_DELY=",
#ifdef __INSIDE_NETBOOT__
			&auto_delay_secs,
#else
			strbuf + 16,
#endif
			"Autoboot Delay: ["
					DELAY_MIN "..."
					DELAY_MAX      "secs] (0==forever) >",
			getNum,
			FLAG_NOUSE | FLAG_DUP,
	},
	{ "BP_TAUO=",
#ifdef __INSIDE_NETBOOT__
			&CPU_TAU_offset,
#else
			strbuf + 17,
#endif
			"CPU Temp. Calibration - (LEAVE IF UNSURE) >",
			getNum,
			0,
	},
	{ 0, }
};

static void
tauOffsetHelp()
{
	printf("Your CPU Temperature calibration changed or was not initialized...\n");
	printf("To calibrate the CPU TAU (thermal assist unit), you must observer the following steps:\n");
	printf("  1. Let your board stabilize to ambient temperature (POWERED OFF)\n");
	printf("  2. Measure the ambient temperature Tamb (deg. C)\n");
	printf("  3. Power-up your board and read the Temperature printed by SMON/FDIAG (Tsmon)\n");
	printf("     NOTE: use ONLY the info printed IMMEDIATELY after powerup\n");
	printf("  4. Set the calibration offset to Tamb - Tsmon\n");
}


#ifdef USE_READLINE

#ifndef DISABLE_HOTKEYS
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
#ifdef SPC_REBOOT
  if (SPC_REBOOT == k)
      rtemsReboot();
#endif
  rl_beg_of_line(0,k);
  rl_insert_text(t);
  return rl_newline(1,k);
}
#endif /* DISABLE_HOTKEYS */

static int
hack_undo(int count, int k)
{
  rl_free_undo_list();
  return 0;
}

#endif /* USE_READLINE */

static void
installHotkeys(NetConfigCtxt c)
{
#ifndef DISABLE_HOTKEYS
#ifdef USE_READLINE
  rl_bind_key(SPC_UP,handle_spc);
  rl_bind_key(SPC_STOP,handle_spc);
  rl_bind_key(SPC_ESC,handle_spc);
#else
  c->useHotkeys = 1;
  /* clear buffer */
  getConsoleSpecialChar();
#endif
#endif
}

static void
uninstallHotkeys(NetConfigCtxt c)
{
#ifndef DISABLE_HOTKEYS
#ifdef USE_READLINE
  rl_unbind_key(SPC_UP);
  rl_unbind_key(SPC_STOP);
  rl_unbind_key(SPC_ESC);
#else
  c->useHotkeys = 0;
#endif
#endif
}

/* The callers of this routine rely on not
 * getting an empty string.
 * Hence 'prompt()' must free() an empty
 * string and pass up a NULL pointer...
 */
static int
prompt(NetConfigCtxt c, char *prmpt, char *proposal, char **answer)
{
char *nval = 0, *pr, *nl;
int rval,i;

	do {
	rval = 0;
#ifndef USE_READLINE
	free (nval);
	/* print head of multiline prompts (not handled by tecla) */
	for (pr = prmpt; pr && *pr && (nl=strchr(pr, '\n')); pr = nl+1) {
		while (pr<=nl)
			fputc(*pr++, stdout);
	}
	nval=gl_get_line(c->gl, pr, proposal, -1);
#else
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
	nval = readline(prompt);
#endif
	if (!*nval) {
#ifdef USE_READLINE
		free(nval);
#endif
		nval=0; /* discard empty answer */
	}
#ifndef USE_READLINE
	 else {
		nval = strdup(nval);
		/* strip trailing '\n' '\r' */
		if ( (i = strlen(nval)) > 0 ) {
			/* if TECLA passes along the special char, we have a much easier life
			 * supporting "hotkeys"
			 */
			nval[i]  = nval[i-1]; /* preserve special character */
			nval[i-1]='\0';
			if (c->useHotkeys) {
				switch (nval[i]) {
					case SPC_STOP:	
					case SPC_RESTORE:
					case SPC_UP:
#ifdef SPC_REBOOT
					case SPC_REBOOT:
#endif
					case SPC_ESC:
					case SPC_CLEAR_UNDO:
							rval = nval[i];
					default:
							break;
				}
			}
		}
		if (!*nval) {
			free(nval);
			nval = 0;
		}

	}
#endif
	if (nval) {
		register char *src, *dst;
		/* strip leading whitespace */
		for (src=nval; ' '==*src || '\t'==*src; src++);
		if (src>nval) {
			dst=nval;
			while ((*dst++=*src++));
		}
#ifdef USE_READLINE
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
#endif
	}
#ifndef DISABLE_HOTKEYS
#if !defined(USE_READLINE) && !defined(LIBTECLA_ACCEPT_NONPRINTING_LINE_END)
	if (c->useHotkeys) {
		rval = getConsoleSpecialChar();
		if (rval < 0)
			rval = 0;
	}
#endif
#endif
	} while (SPC_RESTORE == rval);
	*answer=nval;
	return rval;
}

static int
getIpAddr(NetConfigCtxt c, char *what, char **pval, int mandatory)
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
		result=prompt(c,p,*pval,&nval);
		if (nval) {
			if (!inet_aton(nval,&inDummy)) {
				fprintf(c->err,"Invalid address, try again\n");
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
	if (nval || !result) {
		if (!mandatory && 0==strcmp(nval,"0.0.0.0")) {
			free(nval); nval=0;
		}
		free(*pval);
		*pval=nval;
	}

	return result;
}

static int
getUseBootp(NetConfigCtxt c, char *what, char **pval, int mandatory)
{
char *nval=0,*chpt;
int  result=0;

	do {
		if (mandatory<0) mandatory=0; /* retry flag */
		/* Do they want something special ? */
		result=prompt(c,what,*pval,&nval);
		if (nval) {
			switch (*nval) {
				case 'Y':
				case 'y':
				case 'N':
				case 'n':
				case 'p':
				case 'P':
						for (chpt=nval; *chpt; chpt++)
							*chpt=toupper(*chpt);
						if (!*(nval+1))
							break; /* acceptable */

						if (!strcmp(nval,"YES") || !strcmp(nval,"NO")) {
							nval[1]=0; /* Y/N */
							break;
						}
						/* unacceptable, fall thru */

				default:fprintf(c->err,"What ??\n");
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
getNum(NetConfigCtxt c, char *what, char **pval, int mandatory)
{
char *nval=0;
int  result=0;

	do {
		if (mandatory<0) mandatory=0; /* retry flag */
		/* Do they want something special ? */
		result=prompt(c,what,*pval,&nval);
		if (nval) {
			unsigned long	tst;
			char			*endp;
			tst=strtoul(nval,&endp,0);
			if (*endp) {
				fprintf(c->err,"Not a valid number - try again\n");
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
getString(NetConfigCtxt c, char *what, char **pval, int mandatory)
{
char *nval=0;
int  result=0;

	do {
		/* Do they want something special ? */
		result=prompt(c,what,*pval,&nval);
	} while (!result && !nval && mandatory);
	if (nval || !result) {
		/* may also be a legal empty string */
		free(*pval); *pval=nval;
	}
	return result;
}

static int
getCmdline(NetConfigCtxt c, char *what, char **pval, int mandatory)
{
char *old=0;
int  retry = 1, result=0;

	while (retry-- && !result) {
		old = *pval ? strdup(*pval) : 0;
		/* old value is released by getString */
		result=getString(c, what, pval, 0);
		if (*pval) {
			/* if they gave a special answer, the old value is restored */
			int i;
			if (0==strlen(*pval)) {
				free(*pval);
				*pval=0;
			} else {
			for (i=0; c->parmList[i].name; i++) {
				if (c->parmList[i].flags & FLAG_NOUSE)
					continue; /* this name is not used */
				if (strstr(*pval,c->parmList[i].name)) {
					fprintf(c->err,"must not contain '%s' - this name is private for the bootloader\n",c->parmList[i].name);
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
callGet(NetConfigCtxt c, int idx, int repeat)
{
int rval;
Parm p = &c->parmList[idx];
#ifdef USE_READLINE
	clear_history();
#endif
	do {
#ifdef USE_READLINE
		if (*ppval && **ppval) add_history(*ppval);
#endif
	} while ((rval=p->getProc(c,p->prompt,p->pval, repeat)) && repeat);
	return rval;
}


/* return -1 if we have all necessary parameters
 * so we could boot:
 *  - bootp flag on  -> some fields may be empty
 *  - bootp flag 'P' -> mandatory fields other than filename and server may be empty
 *  - bootp flag off -> need all mandatory fields
 * otherwise, the index of the first offending parameter is returned.
 */

static int
haveAllMandatory(NetConfigCtxt c, char override)
{
Parm	p;
int		i,min,max;
int		mode = 'Y';
int		rval = -1;

	min = max = 0;
	p = c->parmList + BOOTP_EN_IDX;
	if ( p->pval && *p->pval ) {
		switch ((mode = toupper( **p->pval ))) {
			case 'P':	max = SERVERIP_IDX + 1; break;
			case 'N':	max = NUM_PARMS    + 1; break;
			default: break; /* means 'Y' */
		}
	} else {
		/* no value means BOOTP is ON */
	}

	/* did they manually override anything ? */
	switch ( toupper(override) ) {
		case 'P':	max = SERVERIP_IDX + 1; break;
		case 'D':	max = 0;                break;
		case 'M':	max = NUM_PARMS + 1;    break;
		case 'B':	if ( 'N' == mode ) {
						min = SERVERIP_IDX + 1;
						max = NUM_PARMS + 1;
					}
					/* fall thru */
		default:	break;
		
	}

	if (override)
		mode = override;

	for (p=c->parmList+(i=min); i<max; p++,i++)
		if ( (p->flags&FLAG_MAND) && (!p->pval || ! *p->pval) ) {
			if (rval < 0)
				rval = p-c->parmList; /* record the first offending one */
			if (override) {
				fprintf(stderr,"Unable to override with the '%c' key:\n", override);
			}
			fprintf(stderr,"A mandatory NVRAM field is missing");
			if (override)
				fprintf(stderr,".\nChoose a different key or provide the following from NVRAM:\n");
			else {
				fprintf(stderr," for BOOTP mode '%c'.\n", mode);
				fprintf(stderr,"Choose a different mode or provide the following:\n\n");
			}
			do {
				if ( (p->flags&FLAG_MAND) && (!p->pval || ! *p->pval) ) {
					fprintf(stderr,"  ** '%.14s' **\n", p->prompt);
				}
				i++; p++;
			} while (i<max);
			return rval;
		}

	return -1;
}


static int
showConfig(NetConfigCtxt c, int doReadNvram)
{
Parm p;
int  i;
    if (doReadNvram && !readNVRAM(c)) {
		fprintf(c->err,"\nWARNING: no valid NVRAM configuration found\n");
	} else {
		fprintf(c->out,"\n%s configuration:\n\n",
				doReadNvram ? "NVRAM" : "Actual");
		for (p=c->parmList, i=0; p->name; p++,i++) {
			char *chpt;
			fputs("  ",c->out);
			for (chpt=p->prompt; *chpt; chpt++) {
				fputc(*chpt,c->out);
				if ('\n'==*chpt)
					fputs("  ",c->out); /* indent */
			}
			if (*p->pval)
				fputs(*p->pval,c->out);
			fputc('\n',c->out);
		}
		fputc('\n',c->out);
	}
	return -1; /* continue looping */
}



static int
config(NetConfigCtxt c)
{
int  i,howmany;
int  rval;
Parm p;

	fprintf(c->out,"Changing NVRAM configuration\n");
#ifndef DISABLE_HOTKEYS
	fprintf(c->out,"Use '<Ctrl>-%c' to go up to previous field\n",
				SPC2CHR(SPC_UP));
	fprintf(c->out,"Use '<Ctrl>-%c' to restore this field\n",
				SPC2CHR(SPC_RESTORE));
	fprintf(c->out,"Use '<Ctrl>-%c' to quit+write NVRAM\n",
				SPC2CHR(SPC_STOP));
	fprintf(c->out,"Use '<Ctrl>-%c' to quit+cancel (all values are restored)\n",
				SPC2CHR(SPC_ESC));
#ifdef SPC_REBOOT
	fprintf(c->out,"Use '<Ctrl>-%c' to reboot\n",
				SPC2CHR(SPC_REBOOT));
#endif
#endif

for (p=c->parmList,i=0; p->name; p++)
	/* nothing else to do */;
howmany = p-c->parmList;

installHotkeys(c);

i = 0;

do {

for ( rval=0; i>=0 && i<howmany; ) {
	switch ( callGet(c, i, 0 /* dont repeat */) ) {

		case SPC_ESC:
			fprintf(c->out,"Restoring previous configuration\n");
			if (readNVRAM(c))
				return -1;
			else {
				fprintf(c->out,"Unable to restore configuration, please start over\n");
			}
			i=0;
		break;

		case SPC_STOP:  i=-1; rval = 1;
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

/* make sure we have all mandatory parameters */
} while ( (i=haveAllMandatory(c, 0)) >= 0 );

uninstallHotkeys(c);

/* make sure we have a reasonable auto_delay */
{
char *endp,*override=0;
unsigned long d,min,max;
char *secs = *c->parmList[DELYSECS_IDX].pval;
	
	min=strtoul(DELAY_MIN,0,0);
	max=strtoul(DELAY_MAX,0,0);

	if (secs) {
			d=strtoul(secs, &endp,0);
			if (*secs && !*endp) {
				/* valid */
				if (d<min) {
					fprintf(c->out,"Delay too short - using %ss\n",DELAY_MIN);
					override=DELAY_MIN;
				} else if (d>max) {
					fprintf(c->out,"Delay too long - using %ss\n",DELAY_MAX);
					override=DELAY_MAX;
				}
			} else {
				fprintf(c->out,"Invalid delay - using default: %ss\n",DELAY_DEF);
				override=DELAY_DEF;
			}
	} else {
		override=DELAY_DEF;
	}
	if (override) {
		free(*c->parmList[DELYSECS_IDX].pval);
		*c->parmList[DELYSECS_IDX].pval = strdup(override);
		rval = 0;
	}
}

return rval; /* OK to write NVRAM */
}

static unsigned short
appendNVRAM(NetConfigCtxt c, unsigned char **pnvram, int i_parm)
{
unsigned char *src, *dst;
unsigned short sum;
unsigned char *jobs[3], **job;


	jobs[0]=c->parmList[i_parm].name;
	if ( ! (jobs[1]=*c->parmList[i_parm].pval) )
		return 0;
	jobs[2]=0;

	sum=0;
	dst=*pnvram;

	for (job=jobs; *job; job++) {
		for (src=*job; *src && dst<NVRAM_END-1; ) {
			sum += (*dst++=*src++);
		}

		if (*src) {
			fprintf(c->err,"WARNING: NVRAM overflow - not enough space\n");
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
writeNVRAM(NetConfigCtxt c)
{
unsigned char	*nvchpt=NVRAM_STR_START;
unsigned short	sum;
Parm			p;
int				i;

		sum = 0;
		for (p=c->parmList, i=0; p->name; p++,i++) {
			sum += appendNVRAM(c, &nvchpt, i);
			*nvchpt=0;
		}
		/* tag the end - there is space for the terminating '\0', it's safe */
		*nvchpt=0;

		nvchpt=NVRAM_STR_START;
		sum += (*--nvchpt=(NVRAM_SIGN & 0xff));
		sum += (*--nvchpt=((NVRAM_SIGN>>8) & 0xff));
		*--nvchpt=sum&0xff;
		*--nvchpt=((sum>>8)&0xff);
		fprintf(c->out,"\nNVRAM configuration updated\n");
}

static int
readNVRAM(NetConfigCtxt c)
{
unsigned short	sum,tag;
unsigned char	*nvchpt=NVRAM_START, *str, *pch, *end;
Parm			p;
int				i;

	sum=(*nvchpt++)<<8;
	sum+=(*nvchpt++);
	sum=-sum;
	sum+=(tag=*nvchpt++);
	tag= (tag<<8) | *nvchpt;
	sum+=*nvchpt++;
	if (tag != NVRAM_SIGN) {
			fprintf(c->err,"No NVRAM signature found; compiled for: 0x%04x; found: 0x%04x\n",
							NVRAM_SIGN,
							tag);
			return 0;
	}
	str=nvchpt;
	/* verify checksum */
	while (*nvchpt && nvchpt<NVRAM_END)
			sum+=*nvchpt++;

	if (*nvchpt) {
			fprintf(c->err,"No end of string found in NVRAM\n");
			return 0;
	}
	if (sum) {
			fprintf(c->err,"NVRAM checksum error\n");
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
			for (p=c->parmList,i=0; p->name; p++, i++) {
				if (strncmp(pch, p->name, val-pch))
				continue;
					/* found the parameter */
					free(*p->pval);
					*p->pval = strdup(val);
				break; /* for p=c->parmList */
			}
		}
	}

cleanup:
	free(str);
	return 1;
}


static void
netConfigCtxtFinalize(NetConfigCtxt c)
{
Parm p;

	for (p = c->parmList; p->name; p++) {
		free(*p->pval);
		*p->pval = 0;
	}
#ifndef USE_READLINE
	del_GetLine(c->gl);
#endif
}

static void
netConfigCtxtInitialize(NetConfigCtxt c, FILE *f)
{
char	*tmp, *fn;
Parm	p;

	memset(c, 0, sizeof(*c));

/* copy static pointers into local buffer pointer array
   * (pointers in the ParmRec struct initializers are easier to maintain
   * but we want the 'config/showConfig' routines to be re-entrant
   * so they can be used by a full-blown system outside of 'netboot')
   */

	c->err    = stderr;
	c->out    = f ? f : stdout;
	c->parmList = parmList;

	fn = tmp = malloc(500);
	*fn = 0;

#ifdef USE_READLINE
	rl_initialize();

#ifdef SPC_REBOOT
	rl_bind_key(SPC_REBOOT,handle_spc);
#endif
	rl_bind_key(SPC_CLEAR_UNDO, hack_undo);
	/* readline (temporarily) modifies the argument to rl_parse_and_bind();
	 * mustn't be static/ro text
	 */
	tmp=strdup("Control-r:revert-line");
	rl_parse_and_bind(tmp);
#else
	/* no history */
	c->gl = new_GetLine(500,0);

#ifndef DISABLE_HOTKEYS
	fn += sprintf(fn,"bind ^%c newline\n",SPC2CHR(SPC_STOP));
	addConsoleSpecialChar(SPC_STOP);
	fn += sprintf(fn,"bind ^%c newline\n",SPC2CHR(SPC_RESTORE));
	addConsoleSpecialChar(SPC_RESTORE);
	fn += sprintf(fn,"bind ^%c newline\n",SPC2CHR(SPC_UP));
	addConsoleSpecialChar(SPC_UP);
   	fn += sprintf(fn,"bind ^%c newline\n",SPC2CHR(SPC_ESC));
	addConsoleSpecialChar(SPC_ESC);
#endif

	gl_configure_getline(c->gl, tmp, 0, 0);
#endif

	free(tmp);

	/* initialize buffers; all configuration variables
	 * must be malloc()ed
	 */
	for (p = c->parmList; p->name; p++) {
		if ( (p->flags & FLAG_DUP) && *p->pval )
				*p->pval = strdup(*p->pval);
	}
}

#ifndef __INSIDE_NETBOOT__

volatile rtems_id nvramMutex = 0;

static void
lockInitOnce(volatile rtems_id *pl)
{
rtems_id		tmp;
unsigned long	flags;
	assert( RTEMS_SUCCESSFUL ==
			rtems_semaphore_create(
					rtems_build_name('N','V','R','S'),
					1,
					RTEMS_SIMPLE_BINARY_SEMAPHORE,
					0,
					&tmp) );
rtems_interrupt_disable(flags);
	if (!*pl) {
		*pl = tmp;
		tmp = 0;
	}
rtems_interrupt_enable(flags);
	if (tmp)
		rtems_semaphore_delete(tmp);
}

static int
lock()
{
	if (!nvramMutex)
		lockInitOnce(&nvramMutex);
	if (rtems_semaphore_obtain(nvramMutex, RTEMS_NO_WAIT, RTEMS_NO_TIMEOUT)) {
		fprintf(stderr,"NVRAM currently busy/accessed by other user; try later\n");
		return -1;
	}
	return 0;
}

#define unlock() rtems_semaphore_release(nvramMutex)

#define NONEQ(charp) ((charp) ? (charp) : "<NONE>")

static void
prip(FILE *f, char *header, struct in_addr *pip)
{
char buf[18];
	fprintf(f,"%-15s%-16s\n",header,inet_ntop(AF_INET,pip,buf,sizeof(buf)));
}

static void
note(FILE *f)
{
	fputc('\n',f);
	fprintf(f,"NOTE: Active and NVRAM boot configuration may differ\n");
	fprintf(f,"      due to user intervention (manual override of NVRAM)\n");
	fprintf(f,"      at boot time.\n");
}

void
bootConfigShow(FILE *f)
{
char buf[20];
int  i;
extern char *rtems_bsdnet_bootp_boot_file_name;
extern char *rtems_bsdnet_bootp_cmdline;
extern char *rtems_bsdnet_domain_name;
extern struct in_addr rtems_bsdnet_bootp_server_address;
extern struct in_addr rtems_bsdnet_log_host_address;
extern struct in_addr rtems_bsdnet_nameserver[];
extern struct in_addr rtems_bsdnet_ntpserver[];
extern int  rtems_bsdnet_nameserver_count; 
extern int  rtems_bsdnet_ntpserver_count; 
	if (!f) f = stdout;
	
	fprintf(f,"\nThis system was booted with the following configuration:\n\n");
	fprintf(f,"%-15s'%s'\n","Boot file:",NONEQ(rtems_bsdnet_bootp_boot_file_name));
	fprintf(f,"%-15s'%s'\n","Command line:",NONEQ(rtems_bsdnet_bootp_cmdline));
	prip(f,"Server IP:",&rtems_bsdnet_bootp_server_address);
	strcpy(buf,"<NONE>");
	gethostname(buf,sizeof(buf));
	buf[sizeof(buf)-1]=0;
	fprintf(f,"%-15s%s\n","My name:",buf);
	fprintf(f,"%-15s%s\n","My domain:",rtems_bsdnet_domain_name);
	prip(f,"Loghost IP:",&rtems_bsdnet_log_host_address);
	for (i = 0; i< rtems_bsdnet_nameserver_count; i++) {
		sprintf(buf,"DNS server %i:",i+1);
		prip(f,buf,&rtems_bsdnet_nameserver[i]);
	}
	for (i = 0; i< rtems_bsdnet_ntpserver_count; i++) {
		sprintf(buf,"NTP server %i:",i+1);
		prip(f,buf,&rtems_bsdnet_ntpserver[i]);
	}
	fprintf(f,"\nTo show interface and route configuration use:\n");
	fprintf(f,"  rtems_bsdnet_show_if_stats()\n");
	fprintf(f,"  rtems_bsdnet_show_inet_routes()\n");
	fprintf(f,"To show the NVRAM configuration use:\n");
	fprintf(f,"  nvramConfigShow()\n");
	note(f);
}



int
nvramConfigShow(FILE *f)
{
NetConfigCtxtRec ctx;
	if (lock())
		return -1;
	if (!f)
		f = stdout;
	netConfigCtxtInitialize(&ctx,f);
	showConfig(&ctx, 1);
	netConfigCtxtFinalize(&ctx);
	unlock();
	note(f);
	fprintf(f,"      To show the active configuration use: bootConfigShow()\n");
	return 0;
}

static int
confirmed(NetConfigCtxt c)
{
char ch;
#ifdef GET_RAW_INPUT
struct termios ot,nt;
	tcgetattr(fileno(stdin),&ot);
	nt             = ot;
	nt.c_lflag    &= ~ICANON;
	nt.c_cc[VMIN]  = 1;
	nt.c_cc[VTIME] = 0;
#else
	fputc('\n',stdout);
#endif
	do {
#ifdef GET_RAW_INPUT
		fprintf(stderr,"\nOK to write NVRAM? [y/n]");
		fflush(stderr);
		tcsetattr(fileno(stdin),TCSANOW,&nt);
		/* what to do with the stream?? Hack: assume the buffer is empty
		 * and do nothing
		 */
		read(fileno(stdin),&ch,1);
		tcsetattr(fileno(stdin),TCSANOW,&ot);
		ch = toupper(ch);
#else
		char *resp=gl_get_line(c->gl, "OK to write NVRAM? [y]/n:", 0, -1);
		ch = (resp && *resp) ? toupper(*resp) : 'Y';
		if ( '\n' == ch || '\r' == ch )
			ch = 'Y';
#endif
	} while ( 'Y' != ch  && 'N' != ch );
#ifdef GET_RAW_INPUT
	fputc('\n',stderr);
#endif
	return ('N'!=ch);
}

int
nvramConfig()
{
int				 got;
NetConfigCtxtRec ctx;
	
	if (lock())
		return -1;
	netConfigCtxtInitialize(&ctx,stdout);
	readNVRAM(&ctx);
	if ( !*ctx.parmList[CPU_TAU_IDX].pval )
		tauOffsetHelp();
	if ( (got=config(&ctx)) >= 0 ) {
		if (got > 0 || confirmed(&ctx) ) {
			writeNVRAM(&ctx);
		} else {
			fprintf(stderr,"Changes aborted...\n");
		}
	}
	netConfigCtxtFinalize(&ctx);
	unlock();
	return 0;
}

#include <cexpHelp.h>

CEXP_HELP_TAB_BEGIN
	HELP(
"Interactively change the NVRAM boot configuration",
		void, nvramConfig, (void)
		),
	HELP(
"Show the NVRAM boot configuration; note that the currently active\n"
"configuration may be different because the user may override the NVRAM\n"
"settings at the boot prompt. To see the active configuration, use\n"
"'bootConfigShow()'.\n",
		void, nvramConfigShow, (void)
		),
	HELP(
"Show the currently active boot configuration; note that the NVRAM\n"
"configuration may be different because the user may override the NVRAM\n"
"settings at the boot prompt. To see the NVRAM configuration, use\n"
"'nvramConfigShow()'.\n",
		void, bootConfigShow, (void)
		),
CEXP_HELP_TAB_END

#endif
