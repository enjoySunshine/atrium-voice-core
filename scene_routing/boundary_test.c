#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "fluidsynth.h"

#define SR 44100
#define N 44100
#define TOL 0.0001

static double maxdiff(float *a, float *b, int lo, int n){
    double m=0; for(int i=lo;i<n;i++){double v=fabs(a[i]-b[i]); if(v>m)m=v;} return m;
}
static double peak(float *a, int lo, int n){
    double m=0; for(int i=lo;i<n;i++){double v=fabs(a[i]); if(v>m)m=v;} return m;
}

static fluid_synth_t *mk(fluid_settings_t **out){
    fluid_settings_t *st=new_fluid_settings();
    *out=st;
    fluid_settings_setnum(st,"synth.sample-rate",SR);
    fluid_settings_setint(st,"synth.threadsafe-api",0);
    fluid_settings_setint(st,"synth.cpu-cores",1);
    fluid_settings_setint(st,"synth.audio-channels",4);
    fluid_settings_setint(st,"synth.audio-groups",4);
    fluid_settings_setint(st,"synth.effects-groups",4);
    fluid_synth_t *s=new_fluid_synth(st);
    fluid_synth_sfload(s,"/workspace/sf2/VintageDreamsWaves-v2.sf2",1);
    return s;
}

static void move_all_key60(fluid_synth_t *syn, int at_tick){
    fluid_voice_t *vv[64];
    fluid_synth_get_voicelist(syn,vv,64,-1);
    for(int k=0;vv[k];k++)
        if(fluid_voice_get_key(vv[k])==60){
            if(at_tick<0) fluid_synth_voice_set_space(syn,vv[k],1);
            else fluid_synth_voice_set_space_at(syn,vv[k],1,(unsigned)at_tick);
        }
}

/* one note, moved to space 1 at move_tick, rendered in given chunk size */
static void render(int move_tick, int chunk, float *woodL, float *woodR){
    fluid_settings_t *st; fluid_synth_t *syn=mk(&st);
    float *bo[8],*bf[16],*po[8],*pf[16];
    for(int i=0;i<8;i++){bo[i]=calloc(N,4);po[i]=bo[i];}
    for(int i=0;i<16;i++){bf[i]=calloc(N,4);pf[i]=bf[i];}

    fluid_synth_noteon(syn,0,60,100);
    move_all_key60(syn,move_tick);

    int pos=0;
    while(pos<N){
        int n=N-pos; if(n>chunk)n=chunk;
        fluid_synth_process(syn,n,16,pf,8,po);
        for(int i=0;i<8;i++) po[i]+=n;
        for(int i=0;i<16;i++) pf[i]+=n;
        pos+=n;
    }
    for(int i=0;i<N;i++){woodL[i]=bo[2][i]; woodR[i]=bo[3][i];}
    delete_fluid_synth(syn); delete_fluid_settings(st);
    for(int i=0;i<8;i++) free(bo[i]);
    for(int i=0;i<16;i++) free(bf[i]);
}

int main(void)
{
    /* reference: note in wood (space 1) from sample 0 */
    fluid_settings_t *st; fluid_synth_t *syn=mk(&st);
    float *bo[8],*bf[16],*po[8],*pf[16];
    for(int i=0;i<8;i++){bo[i]=calloc(N,4);po[i]=bo[i];}
    for(int i=0;i<16;i++){bf[i]=calloc(N,4);pf[i]=bf[i];}
    fluid_synth_noteon(syn,0,60,100);
    move_all_key60(syn,-1);
    fluid_synth_process(syn,N,16,pf,8,po);
    float *refL=bo[2], *refR=bo[3];

    int ticks[3]={64,128,1000}; /* on-block-edge and mid-block cuts */
    int chunks[2]={64,512};
    int fail=0;
    for(int t=0;t<3;t++){
        for(int c=0;c<2;c++){
            float *wL=calloc(N,4),*wR=calloc(N,4);
            render(ticks[t],chunks[c],wL,wR);
            double silentBefore = peak(wL,0,ticks[t])+peak(wR,0,ticks[t]);
            double dL=maxdiff(wL,refL,ticks[t],N);
            double dR=maxdiff(wR,refR,ticks[t],N);
            int ok = silentBefore<TOL && dL<=TOL && dR<=TOL;
            if(!ok)fail++;
            printf("move@%5d chunk=%3d: silent-before=%.3g diffL=%.3g diffR=%.3g %s\n",
                ticks[t],chunks[c],silentBefore,dL,dR,ok?"PASS":"FAIL");
            free(wL);free(wR);
        }
    }
    printf(fail?"BOUNDARY FAIL\n":"BOUNDARY ALL PASS\n");
    return fail;
}
