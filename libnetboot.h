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
nvramFixupBsdnetConfig(int readNvram);

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

#ifdef __cplusplus
}
#endif

#endif
