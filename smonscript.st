/* "optional steps" */
chberr;   /* clear hostbridge errors */
d 0x200;  /* make sure pending errors are acknowledged     */
          /* by reading off machine check exception vector */
scpmc 1;  /* enable MCP interrupt pin */
/*  "RTEMS loader"  */
printf("Executing the RTEMS loader from FLASH at 0x80000\n");
netStop;  /* make sure network DMA is stopped */
flashAdr(0x80000,2); /* map 2nd 'bank' starting at 0x80000  */
                     /* to 512k flash window.               */
          /* Note: 2 means 'usrFlash', not related to 'bank'*/
r5=0;r6=0;/* tell the loader to autodetect its load address */
r3=0;r4=0;/* no command line (r3: start, r4: end+1)         */
g 0xfff80000; /* let it go (0xfff80000 is the flash window  */
              /* address as seen from the CPU)              */
