int srv="134.79.34.70";
int fle="powerpc-rtems/svgm/img/netboot.flashimg.bin";
printf("reprogramming FLASH with latest RTEMS loader\n");
flashwren(0); printf("ERASING...");
flasheraseblock(4,2); flasheraseblock(5,2);
flasheraseblock(6,2); flasheraseblock(7,2);
/* cannot define variables in scripts - use registers */
printf("done\nLoading (TFTP) '%s' from '%s'\n",fle,srv);
load(fle,0x10000,srv); /* 'load' has sideeffects on regs */
printf("Writing FLASH ...");
flashwrite(0x80000,r5,r6-r5,2);
printf("done\nLoading smon script to 'rtems' buffer...");
loadEB("powerpc-rtems/svgm/img/smonscript.st","rtems");
printf("done\nYou should reboot now\n");
