/* "optional steps" */
/********************/
       /* clear hostbridge errors */
chberr;
       /* make sure pending errors are acknowledged
        * by reading off machine check exception vector
        */
d 0x200;
        /* enable MCP interrupt pin */
scpmc 1;
/*  "RTEMS loader"  */
/********************/
        /* message */
printf("Executing the RTEMS loader from FLASH at 0x80000\n");
		/* make sure network DMA is stopped */
netStop;
		/* map 2nd 'bank' starting at 0x80000 to 512k flash
         * window in the hostbridge/CPU memory map.
         * Note: 2 means 'userFlash', not related to 'bank'
		 */
flashAdr(0x80000,2);
		/* tell the loader to autofigure where it's running */
r5=0; r6=0;
		/* no command line (r3: start, r4: end+1) */
r3=0; r4=0;
		/* let it go */
g 0xfff80000;
