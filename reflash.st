int  ho="134.79.34.70"; int i;
int ll="powerpc-rtems/svgm/img/netboot.flashimg.bin";
int lc="powerpc-rtems/svgm/img/coredump.flashimg.bin";
void lwr(int fn,int a){
printf("done\nLoading '%s' from '%s'\n",fn,ho);
load(fn,0x10000,ho); printf("Writing FLASH ...");
flashwrite(a,r5,r6-r5,2); }
printf("FLASHing with RTEMS loader\nERASING..."); flashwren(0);
for (i=4; i<0xc; i++) flasheraseblock(i,2); lwr(ll,0x80000);
lwr(lc,0x100000); printf("done\nLoading SMON scripts...");
loadEB("powerpc-rtems/svgm/img/smonscript.st","rtems");
loadEB("powerpc-rtems/svgm/img/coredump.st","coredump");
loadEB("powerpc-rtems/svgm/img/reflash.st","reflash");
printf("done\nYou should reboot now\n");
