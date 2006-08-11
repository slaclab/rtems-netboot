int  ho=0 /* 0 = default or use "xxx.xxx.xxx.xxx" */; int i;
int ll="ssrlApps/powerpc-rtems/svgm/img/netboot.flashimg.bin";
int s1="ssrlApps/powerpc-rtems/svgm/img/smonscript.st";
int s3="ssrlApps/powerpc-rtems/svgm/img/reflash.st";
void lwr(int fn,int a){
printf("done\nLoading '%s' from '%s'\n",fn,ho);
load(fn,0x10000,ho); printf("Writing FLASH ...");
flashwrite(a,r5,r6-r5,2); }
printf("FLASHing with RTEMS loader\nERASING..."); flashwren(0);
for (i=4; i<0xc; i++) flasheraseblock(i,2); lwr(ll,0x80000);
lwr(lc,0x100000); printf("done\nLoading SMON scripts...");
loadEB(s1,"rtems");loadEB(s2,"coredump");loadEB(s3,"reflash");
printf("done\nYou should reboot now\n");
