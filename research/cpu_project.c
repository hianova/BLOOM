// CPU 3D->2D cost: per-object perspective projection + frustum cull + depth-slice bucketing.
// SoA, single thread, then N threads. Objects = sphere bound (cx,cy,cz,r). Output: screen rect + slice id.
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <pthread.h>
#define NSLICE 64
typedef struct { float *x,*y,*z,*r; float *sx,*sy,*sr; uint8_t *slice,*vis; size_t n; float f,w,h,zn,zf; } W;
static double now(void){ struct timespec t; clock_gettime(CLOCK_MONOTONIC,&t); return t.tv_sec+t.tv_nsec*1e-9; }
static void run(W *w,size_t a,size_t b){
  float f=w->f, hw=w->w*0.5f, hh=w->h*0.5f, zn=w->zn, lz=logf(w->zf/zn);
  for(size_t i=a;i<b;i++){
    float z=w->z[i]; float inv=1.0f/z; float s=f*inv;
    float px=w->x[i]*s, py=w->y[i]*s, pr=w->r[i]*s;
    int v = (z+w->r[i]>zn) & (z-w->r[i]<w->zf) & (fabsf(px)-pr<hw) & (fabsf(py)-pr<hh) & (pr>0.25f);
    w->sx[i]=px+hw; w->sy[i]=py+hh; w->sr[i]=pr; w->vis[i]=(uint8_t)v;
    float t=logf(z>zn?z/zn:1.0f)/lz; int sl=(int)(t*NSLICE); w->slice[i]=(uint8_t)(sl<0?0:(sl>NSLICE-1?NSLICE-1:sl));
  } }
typedef struct { W*w; size_t a,b; } J; static void*th(void*p){ J*j=p; run(j->w,j->a,j->b); return 0; }
int main(void){
  size_t N=1u<<20; W w={0}; w.n=N; w.f=3326; w.w=3840; w.h=2160; w.zn=1; w.zf=1e6;
  float**a[]={&w.x,&w.y,&w.z,&w.r,&w.sx,&w.sy,&w.sr}; for(int i=0;i<7;i++) *a[i]=aligned_alloc(64,N*4);
  w.slice=malloc(N); w.vis=malloc(N); srand(1);
  for(size_t i=0;i<N;i++){ float z=powf(10,(rand()/(float)RAND_MAX)*6); w.z[i]=z; w.x[i]=((rand()/(float)RAND_MAX)-0.5f)*z*1.2f; w.y[i]=((rand()/(float)RAND_MAX)-0.5f)*z*0.7f; w.r[i]=z*0.002f+0.5f; }
  for(int nt=1;nt<=8;nt*=2){ double best=1e9; for(int rep=0;rep<20;rep++){ double t0=now(); pthread_t T[8]; J j[8];
      for(int k=0;k<nt;k++){ j[k]=(J){&w,N*k/nt,N*(k+1)/nt}; pthread_create(&T[k],0,th,&j[k]); } for(int k=0;k<nt;k++) pthread_join(T[k],0);
      double d=now()-t0; if(d<best)best=d; }
    size_t vis=0; for(size_t i=0;i<N;i++) vis+=w.vis[i];
    printf("threads=%d  N=%zu  best=%.3f ms  (%.1f ns/obj)  visible=%zu\n",nt,N,best*1e3,best*1e9/N,vis); }
}
