#define NVRAM_READONLY
#define NVRAM_GETVAR(n) bev_remap(n)
#include <stdlib.h>

#include "nvram.c"


int
main()
{
int ok;
NetConfigCtxtRec ctx;

	netConfigCtxtInitialize(&ctx, stdout, 0);
	if ( !(ok=readNVRAM(&ctx)) ) {
		fprintf(stderr,"ERROR: couldn't read NVRAM\n");
	}
	netConfigCtxtFinalize(&ctx);

	if ( !ok )
		exit(1);

#ifndef NVRAM_READONLY
	nvramConfig();
#endif

	nvramConfigShow(0);

	bootConfigShow(0);
}
