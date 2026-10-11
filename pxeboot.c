//-----------------------------------------------------------------------------------------
// Copyright (C) 2026, SLAC National Accelerator Laboratory
//-----------------------------------------------------------------------------------------
// Purpose: Provides limited support for PXE boot configurations.
//-----------------------------------------------------------------------------------------
// Author: J. Lorelli
// Date: Oct 10, 2026
//-----------------------------------------------------------------------------------------
// This file is part of 'netboot'. It is subject to the license terms in the
// LICENSE file found in the top-level directory of this distribution.
// No part of 'netboot', including this file, may be copied, modified, propagated,
// or otherwise distributed except according to the terms contained in the LICENSE file.
//
// SPDX-License-Identifier: BSD-3-Clause
//-----------------------------------------------------------------------------------------
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>
#include <termios.h>

#include "tftpc/ttftp.h"

#ifdef __rtems__
#include <rtems/rtems_bsdnet.h>
#include <bsp.h>
#endif

extern char BSP_enetAddr0[7];
extern char BSP_enetAddr1[7];

#ifndef __rtems__
struct in_addr rtems_bsdnet_bootp_server_address; /* testing */
#else
extern struct in_addr rtems_bsdnet_bootp_server_address;
#endif

typedef struct pxeconfig {
    int prompt;
    int timeout;
    char* ontimeout;

    struct pxeimage* images;
} pxeconfig_t;

/** 
 * Defines an entry in the PXE file
 */
typedef struct pxeimage {
    char* label;
    char* kernel;
    char* append;
    char* menu_label;

    struct pxeimage* next;
} pxeimage_t;

static pxeconfig_t* parse_pxeboot(const void* data, size_t size);
static void pxeboot_free(pxeconfig_t* config);

/* Looks for the `LABEL rtems` entry in the file. */
#define IMAGE_LABEL "rtems"

static pxeimage_t*
pxeboot__get_default_boot(pxeconfig_t* config)
{
    /* Search for the 'ontimeout' image if provided. Otherwise default to the
     * generic 'rtems' label */
    const char* toSearch = config->ontimeout ? config->ontimeout : IMAGE_LABEL;

    for (pxeimage_t* img = config->images; img; img = img->next) {
        if (!strcmp(img->label, toSearch))
            return img;
    }
    return NULL;
}

/**
 * @brief Shows a boot menu and returns the selected image.
 * If the boot menu is not enabled, this will just return the default image.
 * @returns Image or NULL if there are no images.
 */
static pxeimage_t*
pxeboot_show_boot_menu(pxeconfig_t* config)
{
    if (!config->timeout) {
        return pxeboot__get_default_boot(config);
    }

    /* Config tweaks so our fgets behaves well */
    struct termios saved, newios;
    tcgetattr(STDIN_FILENO, &saved);
    newios = saved;
    newios.c_lflag |= ECHO;
    newios.c_lflag &= ~ICANON;
    newios.c_iflag |= IXOFF | IGNCR;
    newios.c_iflag &= ~(IXANY|IXON);
    newios.c_cc[VTIME] = config->timeout * 10;
    newios.c_cc[VMIN] = 0;
    tcsetattr(STDIN_FILENO, TCSANOW, &newios);

    fprintf(stderr, "--------------------------------\n");
    fprintf(stderr, "-         PXE Boot Menu        -\n");
    fprintf(stderr, "--------------------------------\n");
    fprintf(stderr, "Available Boot Options:\n");

    int n = 0;
    for (pxeimage_t* img = config->images; img; img = img->next) {
        fprintf(stderr, "  %d: '%s'\n", n++, img->menu_label ? img->menu_label : img->label);
    }

enter_boot_opt:
    fprintf(stderr, "Enter boot option number (%d seconds): ", config->timeout);

    char opt[128] = {0};
    fgets(opt, sizeof(opt), stdin);

    /* Entry timed out, boot the defautl */
    if (!*opt) {
        fprintf(stderr, "\nBooting default...\n");
        tcsetattr(STDIN_FILENO, TCSANOW, &saved);
        return pxeboot__get_default_boot(config);
    }

    int toBoot = atoi(opt);
    if (toBoot >= n) {
        fprintf(stderr, "\nNo such option '%d'\n", toBoot);
        goto enter_boot_opt;
    }

    /* Restore old termios config */
    tcsetattr(STDIN_FILENO, TCSANOW, &saved);

    n = 0;
    for (pxeimage_t* img = config->images; img; img = img->next, n++) {
        if (n == toBoot) {
            fprintf(stderr, "\nBooting option %d: '%s'\n", toBoot, img->label);
            return img;
        }
    }
    return NULL;
}

static int
pxeboot_for_mac(const uint8_t* mac, char** kernel, char** append)
{
    char pxefile[256] = {0};
    snprintf(pxefile, sizeof(pxefile), "pxelinux.cfg/01-%02x-%02x-%02x-%02x-%02x-%02x",
        mac[0] & 0xFF, mac[1] & 0xFF, mac[2] & 0xFF,
        mac[3] & 0xFF, mac[4] & 0xFF, mac[5] & 0xFF);

    char server[128];
    strcpy(server, inet_ntoa(rtems_bsdnet_bootp_server_address));

    struct tftpc* c = tftpc_open(server);
    if (!c) {
        printf("do_pxeboot: tftpGet failed on '%s:%s'\n",
            server, pxefile);
        return -1;
    }

    printf("do_pxeboot: Looking up '%s' file '%s'\n", server, pxefile);

    void* data = NULL;
    size_t size = 0;
    int r = 0;
    if ((r = tftpc_get(c, pxefile, &data, &size)) < 0) {
        printf("do_pxeboot: tftpGet failed on '%s:%s': %s (%d)\n",
            server, pxefile, strerror(-r), r);
        tftpc_close(c);
        return -1;
    }

    tftpc_close(c);

    pxeconfig_t* config = parse_pxeboot(data, size);
    free(data);

    if (!config) {
        printf("do_pxeboot: No PXE entries provided by server '%s'\n", server);
        return -1;
    }

    /* Grab the desired image */
    pxeimage_t *rtemsImage = pxeboot_show_boot_menu(config);

    if (!rtemsImage) {
        printf("do_pxeboot: Could not find image '%s' in the PXE config.\n",
                IMAGE_LABEL);
        return -1;
    }

    *kernel = rtemsImage->kernel;
    *append = rtemsImage->append;
    rtemsImage->kernel = rtemsImage->append = NULL;

    pxeboot_free(config);

    return 0;
}

/**
 * Downloads and parses a PXE config for the specified mac address.
 * @param nextserver 'next-server' provided by DHCP (or some other server that provides TFTP for PXE)
 * @param kernel Kernel parameter from the config. Also known as 'filename'. free() after use.
 * @param append Append parameter from the config. Also known as 'cmdline' or 'uarg'. free() after use.
 * @returns 0 on success
 */
int
do_pxeboot(const char* nextserver, char** kernel, char** append)
{
    const uint8_t* hwaddr = rtems_bsdnet_config.ifconfig->hardware_address;

    printf(
        "do_pxeboot: Fetching for mac %02x:%02x:%02x:%02x:%02x:%02x\n",
        hwaddr[0] & 0xFF, hwaddr[1] & 0xFF, hwaddr[2] & 0xFF,
        hwaddr[3] & 0xFF, hwaddr[4] & 0xFF, hwaddr[5] & 0xFF
    );

    if (pxeboot_for_mac(hwaddr, kernel, append) == 0)
        return 0;

    return -1;
}

/**
 * Check for option and skip to the option value, if it passes.
 */
static int
pxeboot__check_and_skip_opt(const char* opt, const char** p)
{
    const size_t sl = strlen(opt);
    if (strncmp(opt, *p, sl) || !isspace((*p)[sl]))
        return 0;

    /* skip until the option value now */
    (*p) += sl;
    while (isspace(**p) && **p)
        (*p)++;
    return 1;
}

/**
 * @brief Parse a PXE config into a linked list of boot options
 */
static pxeconfig_t*
parse_pxeboot(const void* data, size_t size)
{
    const char* const start = data;
    const char* s = data;
    const char* p = NULL;
    int n;

    const int maxLine = 512;
    char line[maxLine+1];

    pxeconfig_t* config = calloc(1, sizeof(pxeconfig_t));
    if (!config) {
        return NULL;
    }

    /* break into lines */
    for (n = 0; *s && s <= (start + size); s++) {
        if (*s == '\n' || s >= (start + size)) {
            line[n++] = 0;
            n = 0;
        } else {
            line[n++] = *s;
            continue; /* append char and continue */
        }

        p = line;

        /* random empty line */
        if (!*p)
            continue;

        /* new boot entry */
        if (!strncmp(p, "LABEL", 5)) {
            pxeimage_t* new = calloc(sizeof(pxeimage_t), 1);
            new->next = config->images;
            config->images = new;

            /* skip whitespace and strdup label */
            p += 5;
            while (isspace(*p) && *p) ++p;
            new->label = strdup(p);

            continue;
        } else if (!isspace(*p)) {
            /* Root level options */
            if (pxeboot__check_and_skip_opt("TIMEOUT", &p)) {
                config->timeout = atoi(p);
            } else if (pxeboot__check_and_skip_opt("PROMPT", &p)) {
                config->prompt = atoi(p);
            } else if (pxeboot__check_and_skip_opt("ONTIMEOUT", &p)) {
                config->ontimeout = strdup(p);
            } else {
                continue; /* we don't handle most options; just ignore the unknown ones */
            }
        } else {
            /* skip leading whitespace */
            while (isspace(*p) && *p) ++p;

            char** which = NULL;
            size_t toSkip = 0;
            if (!strncmp(p, "KERNEL", 6)) {
                which = config->images ? &config->images->kernel : NULL;
                toSkip = 6;
            } else if (!strncmp(p, "APPEND", 6)) {
                which = config->images ? &config->images->append : NULL;
                toSkip = 6;
            } else if (!strncmp(p, "MENU LABEL", 10)) {
                which = config->images ? &config->images->menu_label : NULL;
                toSkip = 10;
            }

            /* unknown option, floating option (w/o LABEL), or random indent */
            if (!which)
                continue;

            /* skip parameter name plus whitespace */
            p += toSkip;
            while (isspace(*p) && *p) ++p;

            /* avoid leaking memory if there's a duplicate line,
             * i.e. back-to-back APPEND or KERNEL opts */
            if (*which) free(*which);
            *which = strdup(p);
        }
    }
    return config;
}

/**
 * @brief Frees the pxeboot linked list
 */
static void
pxeboot_free(pxeconfig_t* config)
{
    /* Free global config options */
    free(config->ontimeout);

    /* Free all of the images */
    pxeimage_t* h;
    for (h = config->images; h;) {
        pxeimage_t* n = h->next;
        free(h->append);
        free(h->kernel);
        free(h->label);
        free(h);
        h = n;
    }
}

#ifdef __x86_64__
#define TEST
#endif

#ifdef TEST


char BSP_enetAddr0[7] = {0x01, 0x00, 0x01, 0xaf, 0x14, 0x63, 0xba};
char BSP_enetAddr1[7] = {0x01, 0x00, 0x01, 0xaf, 0x14, 0x63, 0xba};

int
main()
{
    rtems_bsdnet_bootp_server_address.s_addr = inet_addr("134.79.216.52");

    char *kernel, *append;
    if (pxeboot_for_mac(BSP_enetAddr0, &kernel, &append) != 0) {
        printf("Failed!\n");
        return -1;
    }

    printf("kernel='%s'\n", kernel);
    printf("append='%s'\n", append);
}

#endif