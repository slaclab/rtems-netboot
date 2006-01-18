#ifndef LIB_NETBOOT_PUB_H
#define LIB_NETBOOT_PUB_H

#include <stdio.h>

/* public interface to netboot library */

#ifdef __cplusplus
extern "C" {
#endif

/* Fixup the rtems_bsdnet configuration based on parameters
 * read from NVRAM (if 'readNvram' !=0) and the commandline.
 * which is parsed for special 'BP_xxxx=<value>' pairs to
 * be used to configure the networking environment.
 * The commandline is usually passed by the 'netboot' application.
 *
 * If the 'readNvram' argument is zero then only the commandline
 * parameters are used.
 *
 * The commandline string is modified as a side-effect of this
 * routine (all recognized name/value pairs are removed).
 *
 * NOTE: this routine must be called prior to network initialization.
 */
void 
nvramFixupBsdnetConfig(int readNvram, char *cmdline);

/* Change the NVRAM boot parameters interactively.
 *
 * RETURNS: 0 on success, -1 if the NVRAM is currently locked
 *          by another user/thread.
 */
int
nvramConfig();

/* Dump the configuration used for the last boot (may differ from
 * NVRAM config; some parameters might have been obtained through
 * BOOTP...
 * The FILE argument may be NULL; stdout is used in this case.
 */
void
bootConfigShow(FILE *f);

/* Dump the current NVRAM configuration to a file (stdout if NULL)
 *
 * RETURNS: 0 on success, -1 if the NVRAM is currently locked
 *          by another user/thread.
 */
int
nvramConfigShow(FILE *f);

/*
 * Scan 'buf' for name=value pairs.
 *
 * pair = name { ' ' } '=' value
 *
 * name:  string not containing '\'', ' ' or '='
 *
 * value: quoted_value | simple_value
 *
 * simple_value: string terminated by the first ' '
 *
 * quoted_value: '\'' { non_quote_char | '\'''\'' } '\''
 *
 * On each 'name=' tag found the callback is invoked with the
 * 'str' parameter pointing to the first character of the name.
 * The value is NULL terminated.
 *
 * If the callback returns 0 and the 'removeFound' argument is nonzero
 * then the 'name=value' pair is removed from 'buf'.
 *
 * SIDE EFFECTS:
 *    - space between 'name' and '=' is removed
 *    - values are unquoted and NULL terminated (undone after callback returns)
 *    - name=value pairs are removed from 'buf' if callback returns 0 and removeFound
 *      is nonzero.
 * If side-effects cannot be tolerated then the routine should be executed on a
 * 'strdup()'ed copy...
 */

void
cmdlinePairExtract(char *buf, int (*putpair)(char *str), int removeFound);

#ifdef __cplusplus
}
#endif

#endif
