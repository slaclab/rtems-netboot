/* $Id$ */

#include <assert.h>

#include <rtems.h>
#include <termios.h>
#include <rtems/termiostypes.h>
#include <bsp.h>

#include <ctrlx.h>

#define  CTRL_X 030
/* our dummy line discipline */
#define  HACKDISC 6 /* 'loadable' according to termios.c */


static int rebootChar = CTRL_X;

static int specialChars[10] = {0};
static int numSpecialChars  =  -1; /* can't add chars prior to installing hack */

static int lastSpecialChar = -1;

int
getConsoleSpecialChar(void)
{
int rval = lastSpecialChar;
	lastSpecialChar = -1;
	return rval;
}

/* ugly hack to get an rtems_termios_tty handle */
/* hack into termios - THIS ROUTINE RUNS IN INTERRUPT CONTEXT */
static
void incharIntercept(struct termios *t, void *arg)
{
/* Note that struct termios is not struct rtems_termios_tty */ 
struct rtems_termios_tty *tty = (struct rtems_termios_tty*)arg;
char                     ch   = tty->rawInBuf.theBuf[tty->rawInBuf.Tail];
int                      i;
	/* did they just press Ctrl-C? */
	if (rebootChar == tty->rawInBuf.theBuf[tty->rawInBuf.Tail]) {
			/* OK, we shouldn't call anything from IRQ context,
			 * but for reboot - who cares...
			 */
			rtemsReboot();
	}

	if (lastSpecialChar >= 0)
		return;

	for (i = 0; i<numSpecialChars; i++) {
		if (ch == specialChars[i]) {
			lastSpecialChar = ch;
			return;
		}
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

int
addConsoleSpecialChar(int ch)
{
	if (numSpecialChars >=0 &&
		numSpecialChars < sizeof(specialChars)/sizeof(specialChars[0])) {
		specialChars[numSpecialChars++] = ch;
		return 0;
	} 
	return -1;
}

int
installConsoleCtrlXHack(int magicChar)
{
int				d=HACKDISC,o;
struct linesw	dorig;

	if (magicChar)
		rebootChar=magicChar;

	/* now install our 'Ctrl-C' hack, so they can abort anytime while
	 * network lookup and/or loading is going on...
	 */

	/* just by installing the line discipline, the
	 * rtems_termios_tty pointer gets 'magically' installed into the
	 * ttywakeup struct...
	 *
	 * Start with retrieving the original ldisc...
	 */
	assert(0==ioctl(0,TIOCGETD,&o));
	for ( d = HACKDISC; d == o && d < MAXLDISC; d++ )
			/* nothing else to do */;
	assert(d<MAXLDISC);

	/* switch line discs */
	dorig = linesw[d];
	linesw[d]=dummy_ldisc;
	assert(0==ioctl(0,TIOCSETD,&d));

	/* make sure we got a rtems_termios_tty pointer */
	assert(ctrlCIntercept.sw_arg);

	/* reinstall the original discipline */
	assert(0==ioctl(0,TIOCSETD,&o));
	linesw[d]=dorig;

	/* finally install our handler */
	assert(0==ioctl(0,RTEMS_IO_RCVWAKEUP,&ctrlCIntercept));
	numSpecialChars = 0;
	return 0;
}
