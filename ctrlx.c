/* $Id$ */

#include <assert.h>

#include <rtems.h>
#include <termios.h>
#include <rtems/termiostypes.h>
#include <bsp.h>
#include <stdio.h>

#include <ctrlx.h>

#define  CTRL_X 030


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

/* hack into termios - THIS ROUTINE RUNS IN INTERRUPT CONTEXT */
static
int incharIntercept(int ch, struct rtems_termios_tty *tty)
{
/* Note that struct termios is not struct rtems_termios_tty */ 
int                      i;
char                     c;

	/* Do special processing only in canonical mode
	 * (e.g., do not do special processing during ansiQuery...
	 */
	if ( (tty->termios.c_lflag & ICANON) ) {
		printk(">%c\n",ch);

		/* did they just press Ctrl-X? */
		if (rebootChar == ch) {
			/* OK, we shouldn't call anything from IRQ context,
			 * but for reboot - who cares...
			 */
			rtemsReboot();
		}

		if ( lastSpecialChar < 0 ) {
			for (i = 0; i<numSpecialChars; i++) {
				if (ch == specialChars[i]) {
					lastSpecialChar = ch;
					break;
				}
			}
		}
	}

	/* Unfortunately, there is no way for a line discipline
	 * method (and we are in 'l_rint' at this point) to instruct
	 * termios to continue 'normal' processing.
	 * Hence we resort to an ugly hack:
	 * We temporarily set l_rint to NULL and call
	 * rtems_termios_enqueue_raw_characters() again:
	 */
	linesw[tty->t_line].l_rint = NULL;
	c = ch;
	rtems_termios_enqueue_raw_characters(tty, &c, 1);
	linesw[tty->t_line].l_rint = incharIntercept;

	return 0;
}

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
int				dsc;

	if (magicChar)
		rebootChar=magicChar;

	/* now install our 'Ctrl-X' hack, so they can abort anytime while
	 * network lookup and/or loading is going on...
	 */

	/* 
	 * Start with retrieving the original ldisc...
	 */
	assert(0==ioctl(0,TIOCGETD,&dsc));

	if ( linesw[dsc].l_rint ) {
		fprintf(stderr,"Current line discipline already has l_rint set -- unablt to install hack\n");
		return -1;
	}

	/* install hack */
	linesw[dsc].l_rint = incharIntercept;

	assert(0==ioctl(0,TIOCSETD,&dsc));

	numSpecialChars = 0;
	return 0;
}
