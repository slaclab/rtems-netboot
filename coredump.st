ci; /* sideeffect: initialize MPIC and do PCI config */
fmcb ffeffe50 1 82 ; /* switch flash bank */
/* set ethernet chip's PCI IRQ LINE info */
wcr(0,0xc,1,0xf,rcr(0,0xc,1,0xf)|0xa);
r1 = 78; /* initial stack */
/*msr = 40;*/ /* mmu off */
r3=0; r4=0; r5=0; r6=0; r7=0; /* initial parameters */
g fff80000; /* go */
