/* $Id$ */
#ifndef CTRLX_TERMIOS_HACK
#define CTRLX_TERMIOS_HACK

/* an ugly hack to intercept CTRL-X on the console for rebooting... */

/* magicChar causes a reboot; RETURNS always 0 */
int
installConsoleCtrlXHack(int magicChar);

#endif
