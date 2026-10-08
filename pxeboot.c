//-----------------------------------------------------------------------------------------
// Copyright (C) 2026, SLAC National Accelerator Laboratory
//-----------------------------------------------------------------------------------------
// Purpose: Defines routines used to download and parse PXE configuration files as an
//  alternative method of booting. Normally DHCP would be used to provide these parameters,
//  however not all tools support uarg (option 129) and easy overrides of 'filename'.
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

/** 
 * Defines an entry in the PXE file
 */
typedef struct pxeimage {
    char* label;
    char* kernel;
    char* append;

    struct pxeimage* next;
} pxeimage_t;

static pxeimage_t* parse_pxeboot(const void* data, size_t size);
static void pxeboot_free(pxeimage_t* head);

/* Looks for the `LABEL rtems` entry in the file. */
#define IMAGE_LABEL "rtems"

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

    pxeimage_t* imglist = parse_pxeboot(data, size);
    free(data);

    if (!imglist) {
        printf("do_pxeboot: No PXE entries provided by server '%s'\n", server);
        return -1;
    }

    /* Search for an entry in the PXE image list with the desired name */
    pxeimage_t *rtemsImage = NULL, *img = NULL;
    for (img = imglist; img; img = img->next) {
        if (!strcmp(img->label, IMAGE_LABEL)) {
            rtemsImage = img;
            break;
        }
    }

    if (!rtemsImage) {
        printf("do_pxeboot: Could not find image '%s' in the PXE config.\n",
                IMAGE_LABEL);
        return -1;
    }

    *kernel = rtemsImage->kernel;
    *append = rtemsImage->append;
    rtemsImage->kernel = rtemsImage->append = NULL;

    pxeboot_free(imglist);

    return 0;
}

/**
 * Downloads and parses a PXE config for the specified mac address.
 * @param kernel Kernel parameter from the config. Also known as 'filename'. free() after use.
 * @param append Append parameter from the config. Also known as 'cmdline' or 'uarg'. free() after use.
 * @returns 0 on success
 */
int
do_pxeboot(char** kernel, char** append)
{
    struct tftpc* pc = tftpc_open("134.79.217.70");
    if (!pc) {
        printf("Failed to lookup '134.79.217.70'\n");
        abort();
    }
    
    void* data = NULL;
    size_t size = 0;
    int r = tftpc_get(pc, "test.txt", &data, &size);
    if (r < 0) {
        printf("tftpc_get: %s (%d)\n", strerror(-r), r);
        abort();
    }
    
    printf("File contents:\n%*s\n", (int)size, (char*)data);
    
    tftpc_close(pc);
    
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
 * @brief Parse a PXE config into a linked list of boot options
 */
static pxeimage_t*
parse_pxeboot(const void* data, size_t size)
{
    const char* const start = data;
    const char* s = data;
    const char* p = NULL;
    int n;

    const int maxLine = 512;
    char line[maxLine+1];

    pxeimage_t* head = NULL;

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
            new->next = head;
            head = new;

            /* skip whitespace and strdup label */
            p += 5;
            while (isspace(*p) && *p) ++p;
            new->label = strdup(p);

            continue;
        } else if (!isspace(*p)) {
            continue; /* unknown root-level option... */
        } else {
            /* skip leading whitespace */
            while (isspace(*p) && *p) ++p;

            char** which = NULL;
            size_t toSkip = 0;
            if (!strncmp(p, "KERNEL", 6)) {
                which = head ? &head->kernel : NULL;
                toSkip = 6;
            } else if (!strncmp(p, "APPEND", 6)) {
                which = head ? &head->append : NULL;
                toSkip = 6;
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
    return head;
}

/**
 * @brief Frees the pxeboot linked list
 */
static void
pxeboot_free(pxeimage_t* head)
{
    pxeimage_t* h;
    for (h = head; h;) {
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