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

#include <rtems.h>
#include <rtems/shell.h>
#include <rtems/rtems_bsdnet.h>

static int netboot_sh_tftpget(int argc, char** argv);
static int netboot_sh_netstats(int argc, char** argv);
static int netboot_sh_ipstats(int argc, char** argv);
static int netboot_sh_routes(int argc, char** argv);
static int netboot_sh_init_network(int argc, char** argv);

static void
register_netboot_cmds()
{
    rtems_shell_add_cmd("tftpget", "net", "tftpget ADDR FILE", netboot_sh_tftpget);
    rtems_shell_add_cmd("ipstats", "net", "", netboot_sh_ipstats);
    rtems_shell_add_cmd("netstats", "net", "", netboot_sh_netstats);
    rtems_shell_add_cmd("routes", "net", "", netboot_sh_routes);
    rtems_shell_add_cmd("netinit", "net", "", netboot_sh_init_network);
}

static int
netboot_sh_tftpget(int argc, char** argv)
{
    size_t i;
    void* data = NULL;
    size_t sz = 0;
    int e = 0;

    if (argc < 3) {
        printf("USAGE: tftpget ADDR FILE\n");
        return -1;
    }

    struct tftpc* pc = tftpc_open(argv[1]);
    if (!pc) {
        printf("Failed to connected to %s\n", argv[1]);
        return -1;
    }

    if ((e = tftpc_get(pc, argv[2], &data, &sz)) != 0) {
        printf("tftpget failed: %s (%d)\n", strerror(-e), e);
    } else {
        for (i = 0; i < sz; ++i) {
            putc(((char*)data)[i], stdout);
        }
    }

    free(data);
    tftpc_close(pc);
    return e;
}

static int
netboot_sh_ipstats(int argc, char** argv)
{
    rtems_bsdnet_show_ip_stats();
    rtems_bsdnet_show_tcp_stats();
    rtems_bsdnet_show_udp_stats();
    return 0;
}

static int
netboot_sh_netstats(int argc, char** argv)
{
    rtems_bsdnet_show_if_stats();
    rtems_bsdnet_show_mbuf_stats();
    rtems_bsdnet_show_icmp_stats();
    return 0;
}

static int
netboot_sh_routes(int argc, char** argv)
{
    rtems_bsdnet_show_inet_routes();
    return 0;
}


static
int netboot_sh_init_network(int argc, char** argv)
{
    if (rtems_bsdnet_initialize_network() != 0) {
        printf("Error while initializing network.\n");
        return -1;
    }
    return 0;
}