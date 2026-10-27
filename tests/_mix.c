#define main hostsim_main
#include "hostsim.c"
#undef main
/* a 4-part mix for 1 s: argv: e0 p0 n0  e1 p1 n1  e2 p2 n2  e3 p3 n3 (n = notes; a kit engine: hits every 1/16) */
int main(int argc,char**argv){ uint32_t k,b,i,e[4],p[4],n[4]; int32_t o[2*CTL];
  host_tracks_init();
  for(k=0;k<4;k++){ e[k]=atoi(argv[1+3*k]); p[k]=atoi(argv[2+3*k]); n[k]=atoi(argv[3+3*k]); host_preset(&trk[k],e[k],p[k]); trk[k].p[P_VOICE]=V_POLY; trk[k].p[P_SUS]=127; trk[k].p[P_AMODE]=0;
     if(!ENGINES[e[k]]->oneshot) for(i=0;i<n[k];i++) trk_note_on(&trk[k],48+i*3+k,100); }
  for(b=0;b<1378;b++){ for(k=0;k<4;k++) if(ENGINES[e[k]]->oneshot && b%12==0) for(i=0;i<n[k];i++) trk_note_on(&trk[k],36+i*2,100); mix_block(o,CTL);}
  return 0;}
