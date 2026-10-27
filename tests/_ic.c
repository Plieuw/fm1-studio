#define main hostsim_main
#include "hostsim.c"
#undef main
/* one engine on track 0, the others silent; N blocks; instruction counts come from callgrind */
int main(int argc,char**argv){ uint32_t e=atoi(argv[1]),pi=atoi(argv[2]),notes=atoi(argv[3]),b,k; int32_t o[2*CTL];
  host_tracks_init(); for(k=1;k<NTRK;k++){ trk[k].p[P_LEVEL]=0; }
  host_preset(&trk[0], e, pi); trk[0].p[P_VOICE]=V_POLY; trk[0].p[P_SUS]=127;
  if(argc>4) trk[0].p[P_MX_HIGH]=6, trk[0].p[P_MX_MID]=4, trk[0].p[P_MX_LOW]=3, trk[0].p[P_MX_GAIN]=3, trk[0].p[P_MX_LCUT]=1;
  for(k=0;k<notes;k++) trk_note_on(&trk[0], e==ENGI_TR909 ? 36+k*2 : 48+k*3, 100);
  for(b=0;b<1378;b++){ if(e==ENGI_TR909 && b%40==0) for(k=0;k<notes;k++) trk_note_on(&trk[0], 36+k*2, 100); mix_block(o,CTL);}  /* 1 s */
  return 0;}
