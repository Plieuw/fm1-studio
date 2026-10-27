#define main hostsim_main
#include "hostsim.c"
#undef main
#include <time.h>
static double now(void){struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9;}
static double bench(uint32_t e, uint32_t pi, uint32_t notes){
  int32_t o[2*CTL]; uint32_t b,k; double t0,t1;
  host_tracks_init(); host_preset(&trk[0], e, pi); trk[0].p[P_VOICE]=V_POLY; trk[0].p[P_SUS]=127;
  for(k=0;k<notes;k++) trk_note_on(&trk[0], e==ENGI_TR909 ? 36+k*2 : 48+k*3, 100);
  for(b=0;b<50;b++) mix_block(o,CTL);
  t0=now(); for(b=0;b<4000;b++){ if(e==ENGI_TR909 && b%40==0) for(k=0;k<notes;k++) trk_note_on(&trk[0], 36+k*2, 100); mix_block(o,CTL);} t1=now();
  return (t1-t0)*1e9/(4000.0*CTL);
}
int main_all(void){
  double idle; int32_t o[2*CTL]; host_tracks_init(); for(int i=0;i<50;i++) mix_block(o,CTL);
  { double t0=now(); for(int b=0;b<4000;b++) mix_block(o,CTL); idle=(now()-t0)*1e9/(4000.0*CTL);}
  printf("idle mix %.1f ns/sample\n", idle);
  printf("JUNO  6 notes %.1f\n", bench(ENGI_JUNO,7,6));
  printf("JUNO  1 note  %.1f\n", bench(ENGI_JUNO,7,1));
  printf("JUNO pad 6    %.1f\n", bench(ENGI_JUNO,2,6));
  printf("ACID  1 note  %.1f\n", bench(ENGI_ACID,0,1));
  printf("TR909 4 hits  %.1f\n", bench(ENGI_TR909,0,4));
  printf("ANALOG 8 (ref) %.1f\n", bench(5,0,8));
  printf("VOICE 4 (ref) %.1f\n", bench(6,0,4));
  return 0;}
int main(int argc,char**argv){ uint32_t e=atoi(argv[1]),p=atoi(argv[2]),n=atoi(argv[3]); for(int r=0;r<5;r++) bench(e,p,n); printf("%.1f\n",bench(e,p,n)); return 0;}
