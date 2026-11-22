/*
 * space_test.c — verification harness for per-voice output spaces ("rooms").
 *
 * Four spaces: 0=cave(洞穴) 1=wood hall(木厅) 2=courtyard(院子) 3=well(井口).
 * Each space has its own dry stereo pair, its own reverb unit and its own
 * chorus unit (synth.audio-groups = synth.effects-groups = 4).
 *
 * The drip (central C, key 60, vel 100) starts in the cave at sample 0 and is
 * moved to the wood hall at sample 22050 (0.5 s). The footstep (key 48, one
 * octave below, same channel 0) stays in the cave. Courtyard and well get no
 * note and must stay silent.
 *
 * The harness renders several scenarios with fluid_synth_process() (and
 * fluid_synth_write_float() for the classic single-stereo case), compares the
 * resulting buffers sample by sample, and writes a report_data.js consumed by
 * index.html (no server, no sound).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <fluidsynth.h>
#include "fluid_voice.h"   /* internal: exclusive-class inspection */
#include "fluid_gen.h"

#define SR       44100
#define N        66048      /* total frames: 512*129 = 64*1032 (~1.5 s) */
#define SWITCH   22050      /* drip moves cave -> hall at this sample (0.5 s) */
#define CC_TICK  33280      /* channel vol/pan change here (512*65 = 64*520) */
#define TOL      1e-4
#define SF2      "/workspace/sf2/VintageDreamsWaves-v2.sf2"

#define DRIP 60
#define FOOT 48
#define VEL  100
#define CHAN 0

#define CAVE 0
#define HALL 1
#define YARD 2
#define WELL 3

#define MAX_DRY 8           /* 4 stereo pairs */
#define MAX_FX  16          /* 4 units x (rev L, rev R, cho L, cho R) */

typedef struct
{
    int groups;
    float *dry[MAX_DRY];    /* dry[pair*2+lr] */
    float *fx[MAX_FX];      /* fx[unit*4 + fxchan*2 + lr] */
    float *interleaved;     /* only for write_float renders */
} bufs_t;

typedef struct
{
    int groups;             /* audio-groups == effects-groups */
    int chunk;              /* render quantum: 512, 64, ... */
    int notes;              /* bit0: drip, bit1: foot */
    int use_space;          /* 1: noteon_space, 0: plain noteon (default mapping) */
    int drip_space;
    int foot_space;
    int move_drip;          /* move drip to move_drip_space at SWITCH */
    int move_drip_space;
    int cc_vol_pan;         /* CC7->64 and CC10->100 at CC_TICK */
    int reverb_on;
    int chorus_on;
    int write_float;        /* render via fluid_synth_write_float (groups==1) */
} scen_t;

static void bufs_alloc(bufs_t *b, int groups)
{
    int i;
    b->groups = groups;
    for(i = 0; i < MAX_DRY; i++) { b->dry[i] = NULL; }
    for(i = 0; i < MAX_FX; i++)  { b->fx[i]  = NULL; }
    for(i = 0; i < groups * 2; i++) { b->dry[i] = calloc(N, sizeof(float)); }
    for(i = 0; i < groups * 4; i++) { b->fx[i]  = calloc(N, sizeof(float)); }
    b->interleaved = NULL;
}

static void bufs_free(bufs_t *b)
{
    int i;
    for(i = 0; i < MAX_DRY; i++) { free(b->dry[i]); }
    for(i = 0; i < MAX_FX; i++)  { free(b->fx[i]); }
    free(b->interleaved);
}

static fluid_synth_t *make_synth(fluid_settings_t **sout, const scen_t *sc)
{
    fluid_settings_t *settings = new_fluid_settings();
    fluid_settings_setint(settings, "synth.audio-groups", sc->groups);
    fluid_settings_setint(settings, "synth.audio-channels", sc->groups);
    fluid_settings_setint(settings, "synth.effects-groups", sc->groups);
    fluid_settings_setnum(settings, "synth.sample-rate", (double)SR);
    fluid_settings_setint(settings, "synth.reverb.active", sc->reverb_on);
    fluid_settings_setint(settings, "synth.chorus.active", sc->chorus_on);
    fluid_settings_setint(settings, "synth.threadsafe-api", 0);
    *sout = settings;
    return new_fluid_synth(settings);
}

/* Render a scenario into b. */
static void render(const scen_t *sc, bufs_t *b)
{
    fluid_settings_t *settings;
    fluid_synth_t *synth;
    unsigned int pos;

    bufs_alloc(b, sc->groups);
    synth = make_synth(&settings, sc);

    if(fluid_synth_sfload(synth, SF2, 1) == FLUID_FAILED)
    {
        fprintf(stderr, "sfload failed\n");
        exit(1);
    }

    /* notes at sample 0 */
    if(sc->notes & 1)
    {
        if(sc->use_space) { fluid_synth_noteon_space(synth, CHAN, DRIP, VEL, sc->drip_space); }
        else              { fluid_synth_noteon(synth, CHAN, DRIP, VEL); }
    }
    if(sc->notes & 2)
    {
        if(sc->use_space) { fluid_synth_noteon_space(synth, CHAN, FOOT, VEL, sc->foot_space); }
        else              { fluid_synth_noteon(synth, CHAN, FOOT, VEL); }
    }

    if(sc->write_float)
    {
        b->interleaved = calloc(2 * N, sizeof(float));
        fluid_synth_write_float(synth, N, b->interleaved, 0, 2, b->interleaved, 1, 2);
    }
    else
    {
        float *out[MAX_DRY];
        float *fx[MAX_FX];
        int nout = sc->groups * 2;
        int nfx  = sc->groups * 4;
        int i;

        pos = 0;
        while(pos < N)
        {
            unsigned int end = pos + sc->chunk;

            if(sc->move_drip && SWITCH >= pos && SWITCH < end)
            {
                fluid_synth_key_set_space(synth, CHAN, DRIP, sc->move_drip_space, SWITCH);
            }
            if(sc->cc_vol_pan && CC_TICK >= pos && CC_TICK < end)
            {
                fluid_synth_cc(synth, CHAN, 7, 64);    /* channel volume down */
                fluid_synth_cc(synth, CHAN, 10, 100);  /* pan right */
            }

            for(i = 0; i < nout; i++) { out[i] = b->dry[i] + pos; }
            for(i = 0; i < nfx;  i++) { fx[i]  = b->fx[i]  + pos; }

            fluid_synth_process(synth, sc->chunk, nfx, fx, nout, out);
            pos = end;
        }
    }

    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
}

/* ---- measurement helpers ---- */

static double peak_range(const float *x, int start, int len)
{
    double p = 0.0;
    int i;
    for(i = start; i < start + len; i++)
    {
        double a = fabs((double)x[i]);
        if(a > p) { p = a; }
    }
    return p;
}

static double maxdiff_range(const float *a, const float *b, int start, int len)
{
    double m = 0.0;
    int i;
    for(i = start; i < start + len; i++)
    {
        double d = fabs((double)a[i] - (double)b[i]);
        if(d > m) { m = d; }
    }
    return m;
}

/* last index where |a-b| > tol in [start,start+len), or -1 */
static int last_mismatch(const float *a, const float *b, int start, int len, double tol)
{
    int i, last = -1;
    for(i = start; i < start + len; i++)
    {
        if(fabs((double)a[i] - (double)b[i]) > tol) { last = i; }
    }
    return last;
}

/* ---- check bookkeeping ---- */

static int nfail = 0;
static int npass = 0;

static void check(int ok, const char *name, double value, double limit)
{
    if(ok) { npass++; printf("  PASS  %-58s (%g)\n", name, value); }
    else   { nfail++; printf("  FAIL  %-58s (%g, limit %g)\n", name, value, limit); }
}

/* stereo pair index helpers */
#define DRYL(b, pair)  ((b)->dry[(pair) * 2 + 0])
#define DRYR(b, pair)  ((b)->dry[(pair) * 2 + 1])
#define FX(b, unit, fxch, lr) ((b)->fx[(unit) * 4 + (fxch) * 2 + (lr)])
#define REVL(b, unit)  FX(b, unit, 0, 0)
#define REVR(b, unit)  FX(b, unit, 0, 1)
#define CHOL(b, unit)  FX(b, unit, 1, 0)
#define CHOR(b, unit)  FX(b, unit, 1, 1)

/* ---- report generation ---- */

#define BUCKET 64
#define NBUF   (N / BUCKET)

static void emit_minmax(FILE *f, const float *x)
{
    int b, i;
    fprintf(f, "{mn:[");
    for(b = 0; b < NBUF; b++)
    {
        float mn = x[b * BUCKET], mx = x[b * BUCKET];
        for(i = b * BUCKET; i < (b + 1) * BUCKET; i++)
        {
            if(x[i] < mn) { mn = x[i]; }
            if(x[i] > mx) { mx = x[i]; }
        }
        fprintf(f, "%s%.6g", b ? "," : "", (double)mn);
        /* stash max in a second pass below via a temp trick: print to mx later */
        (void)mx;
    }
    fprintf(f, "],mx:[");
    for(b = 0; b < NBUF; b++)
    {
        float mx = x[b * BUCKET];
        for(i = b * BUCKET; i < (b + 1) * BUCKET; i++)
        {
            if(x[i] > mx) { mx = x[i]; }
        }
        fprintf(f, "%s%.6g", b ? "," : "", (double)mx);
    }
    fprintf(f, "]}");
}

static void emit_channel(FILE *f, const char *name, const float *L, const float *R, int comma)
{
    fprintf(f, "%s:{L:", name);
    emit_minmax(f, L);
    fprintf(f, ",R:");
    emit_minmax(f, R);
    fprintf(f, "}%s\n", comma ? "," : "");
}

static void write_report(bufs_t *m, bufs_t *refHall, int align_hall)
{
    static const char *names[4] = { "cave", "hall", "yard", "well" };
    FILE *f = fopen("/workspace/work/report_data.js", "w");
    int s;

    if(!f) { fprintf(stderr, "cannot write report_data.js\n"); return; }

    fprintf(f, "// Generated by space_test.c — waveform peaks from the synth's buffers.\n");
    fprintf(f, "const SR=%d, N=%d, SWITCH=%d, ALIGN_HALL=%d, BUCKET=%d, NBUF=%d;\n",
            SR, N, SWITCH, align_hall, BUCKET, NBUF);
    fprintf(f, "const PEAK_HALL_BEFORE=%.6g;\n",
            fmax(peak_range(DRYL(m, HALL), 0, SWITCH), peak_range(DRYR(m, HALL), 0, SWITCH)));
    fprintf(f, "const W={\n");

    for(s = 0; s < 4; s++)
    {
        char nm[32];
        snprintf(nm, sizeof nm, "%s_dry", names[s]);
        emit_channel(f, nm, DRYL(m, s), DRYR(m, s), 1);
    }
    for(s = 0; s < 4; s++)
    {
        char nm[32];
        snprintf(nm, sizeof nm, "%s_rev", names[s]);
        emit_channel(f, nm, REVL(m, s), REVR(m, s), 1);
    }
    for(s = 0; s < 4; s++)
    {
        char nm[32];
        snprintf(nm, sizeof nm, "%s_cho", names[s]);
        emit_channel(f, nm, CHOL(m, s), CHOR(m, s), 1);
    }
    /* reference: drip alone in the hall from sample 0 */
    emit_channel(f, "ref_hall_dry", DRYL(refHall, HALL), DRYR(refHall, HALL), 0);

    fprintf(f, "};\n");
    fclose(f);
}

int main(void)
{
    bufs_t main512, main64, refCave, refFoot, refHall, dripMove;
    bufs_t def1wf, def1proc, def4proc;
    bufs_t voltest, volRefHall, volRefFoot, sendsOff;

    float *base_b1 = NULL, *base_b2 = NULL;
    FILE *f;
    int i;
    int align_hall;

    setbuf(stdout, NULL);

    /* ============ render all scenarios ============ */
    printf("rendering scenarios...\n");

    { scen_t sc = {4, 512, 3, 1, CAVE, CAVE, 1, HALL, 0, 1, 1, 0}; render(&sc, &main512); }
    { scen_t sc = {4,  64, 3, 1, CAVE, CAVE, 1, HALL, 0, 1, 1, 0}; render(&sc, &main64); }
    { scen_t sc = {4, 512, 3, 1, CAVE, CAVE, 0, HALL, 0, 1, 1, 0}; render(&sc, &refCave); }
    { scen_t sc = {4, 512, 2, 1, CAVE, CAVE, 0, HALL, 0, 1, 1, 0}; render(&sc, &refFoot); }
    { scen_t sc = {4, 512, 1, 1, HALL, CAVE, 0, HALL, 0, 1, 1, 0}; render(&sc, &refHall); }
    { scen_t sc = {4, 512, 1, 1, CAVE, CAVE, 1, HALL, 0, 1, 1, 0}; render(&sc, &dripMove); }
    { scen_t sc = {1,   N, 1, 0, CAVE, CAVE, 0, HALL, 0, 1, 1, 1}; render(&sc, &def1wf); }
    { scen_t sc = {1, 512, 1, 0, CAVE, CAVE, 0, HALL, 0, 1, 1, 0}; render(&sc, &def1proc); }
    { scen_t sc = {4, 512, 1, 0, CAVE, CAVE, 0, HALL, 0, 1, 1, 0}; render(&sc, &def4proc); }
    { scen_t sc = {4, 512, 3, 1, CAVE, CAVE, 1, HALL, 1, 1, 1, 0}; render(&sc, &voltest); }
    { scen_t sc = {4, 512, 1, 1, HALL, CAVE, 0, HALL, 1, 1, 1, 0}; render(&sc, &volRefHall); }
    { scen_t sc = {4, 512, 2, 1, CAVE, CAVE, 0, HALL, 1, 1, 1, 0}; render(&sc, &volRefFoot); }
    { scen_t sc = {4, 512, 3, 1, CAVE, CAVE, 1, HALL, 0, 0, 0, 0}; render(&sc, &sendsOff); }

    /* load baselines (rendered with the unmodified library) */
    base_b1 = malloc(2 * N * sizeof(float));
    base_b2 = malloc(6 * N * sizeof(float));
    f = fopen("/workspace/work/baseline_b1.bin", "rb");
    if(!f || fread(base_b1, sizeof(float), 2 * N, f) != (size_t)(2 * N)) { fprintf(stderr, "baseline_b1 missing\n"); return 1; }
    if(f) { fclose(f); }
    f = fopen("/workspace/work/baseline_b2.bin", "rb");
    if(!f || fread(base_b2, sizeof(float), 6 * N, f) != (size_t)(6 * N)) { fprintf(stderr, "baseline_b2 missing\n"); return 1; }
    if(f) { fclose(f); }

    printf("\n== A. drip moves cave->hall at %d, footstep stays in cave ==\n", SWITCH);

    /* A1: cave dry [0,SWITCH) == drip+foot-stay-in-cave render */
    check(maxdiff_range(DRYL(&main512, CAVE), DRYL(&refCave, CAVE), 0, SWITCH) <= TOL &&
          maxdiff_range(DRYR(&main512, CAVE), DRYR(&refCave, CAVE), 0, SWITCH) <= TOL,
          "cave dry [0,SWITCH) == drip+foot in cave",
          fmax(maxdiff_range(DRYL(&main512, CAVE), DRYL(&refCave, CAVE), 0, SWITCH),
               maxdiff_range(DRYR(&main512, CAVE), DRYR(&refCave, CAVE), 0, SWITCH)), TOL);

    /* A2: cave dry [SWITCH,N) == footstep-only render */
    check(maxdiff_range(DRYL(&main512, CAVE), DRYL(&refFoot, CAVE), SWITCH, N - SWITCH) <= TOL &&
          maxdiff_range(DRYR(&main512, CAVE), DRYR(&refFoot, CAVE), SWITCH, N - SWITCH) <= TOL,
          "cave dry [SWITCH,N) == footstep only",
          fmax(maxdiff_range(DRYL(&main512, CAVE), DRYL(&refFoot, CAVE), SWITCH, N - SWITCH),
               maxdiff_range(DRYR(&main512, CAVE), DRYR(&refFoot, CAVE), SWITCH, N - SWITCH)), TOL);

    /* A3: hall dry [SWITCH,N) == drip-alone-in-hall render (also proves no retrigger) */
    check(maxdiff_range(DRYL(&main512, HALL), DRYL(&refHall, HALL), SWITCH, N - SWITCH) <= TOL &&
          maxdiff_range(DRYR(&main512, HALL), DRYR(&refHall, HALL), SWITCH, N - SWITCH) <= TOL,
          "hall dry [SWITCH,N) == drip always in hall",
          fmax(maxdiff_range(DRYL(&main512, HALL), DRYL(&refHall, HALL), SWITCH, N - SWITCH),
               maxdiff_range(DRYR(&main512, HALL), DRYR(&refHall, HALL), SWITCH, N - SWITCH)), TOL);

    /* alignment sample for the report */
    {
        int mL = last_mismatch(DRYL(&main512, HALL), DRYL(&refHall, HALL), 0, N, TOL);
        int mR = last_mismatch(DRYR(&main512, HALL), DRYR(&refHall, HALL), 0, N, TOL);
        int m = mL > mR ? mL : mR;
        align_hall = m + 1;
        printf("  INFO  hall dry aligns with always-in-hall render from sample %d\n", align_hall);
        check(align_hall == SWITCH, "hall alignment sample == SWITCH", (double)align_hall, (double)SWITCH);
    }

    /* A4: hall dry silent before SWITCH */
    check(peak_range(DRYL(&main512, HALL), 0, SWITCH) < TOL &&
          peak_range(DRYR(&main512, HALL), 0, SWITCH) < TOL,
          "hall dry silent before SWITCH",
          fmax(peak_range(DRYL(&main512, HALL), 0, SWITCH),
               peak_range(DRYR(&main512, HALL), 0, SWITCH)), TOL);

    /* A5: courtyard + well dry silent (whole range) */
    check(peak_range(DRYL(&main512, YARD), 0, N) < TOL && peak_range(DRYR(&main512, YARD), 0, N) < TOL &&
          peak_range(DRYL(&main512, WELL), 0, N) < TOL && peak_range(DRYR(&main512, WELL), 0, N) < TOL,
          "courtyard+well dry silent",
          fmax(fmax(peak_range(DRYL(&main512, YARD), 0, N), peak_range(DRYR(&main512, YARD), 0, N)),
               fmax(peak_range(DRYL(&main512, WELL), 0, N), peak_range(DRYR(&main512, WELL), 0, N))), TOL);

    /* A6: courtyard + well reverb+chorus silent */
    {
        double p = 0;
        for(i = YARD; i <= WELL; i++)
        {
            p = fmax(p, peak_range(REVL(&main512, i), 0, N));
            p = fmax(p, peak_range(REVR(&main512, i), 0, N));
            p = fmax(p, peak_range(CHOL(&main512, i), 0, N));
            p = fmax(p, peak_range(CHOR(&main512, i), 0, N));
        }
        check(p < TOL, "courtyard+well reverb+chorus silent", p, TOL);
    }

    /* A7: hall reverb+chorus silent before SWITCH */
    check(peak_range(REVL(&main512, HALL), 0, SWITCH) < TOL &&
          peak_range(REVR(&main512, HALL), 0, SWITCH) < TOL &&
          peak_range(CHOL(&main512, HALL), 0, SWITCH) < TOL &&
          peak_range(CHOR(&main512, HALL), 0, SWITCH) < TOL,
          "hall reverb+chorus silent before SWITCH",
          fmax(fmax(peak_range(REVL(&main512, HALL), 0, SWITCH), peak_range(REVR(&main512, HALL), 0, SWITCH)),
               fmax(peak_range(CHOL(&main512, HALL), 0, SWITCH), peak_range(CHOR(&main512, HALL), 0, SWITCH))), TOL);

    /* A8: cave reverb tail still rings after the drip left (drip-only move) */
    check(peak_range(REVL(&dripMove, CAVE), SWITCH, 8000) > TOL ||
          peak_range(REVR(&dripMove, CAVE), SWITCH, 8000) > TOL,
          "cave reverb tail retained after drip left",
          fmax(peak_range(REVL(&dripMove, CAVE), SWITCH, 8000),
               peak_range(REVR(&dripMove, CAVE), SWITCH, 8000)), TOL);

    /* A9: the drip's reverb send arrived in the hall after the switch */
    check(peak_range(REVL(&main512, HALL), SWITCH, 8000) > TOL ||
          peak_range(REVR(&main512, HALL), SWITCH, 8000) > TOL,
          "hall reverb rings after drip arrived",
          fmax(peak_range(REVL(&main512, HALL), SWITCH, 8000),
               peak_range(REVR(&main512, HALL), SWITCH, 8000)), TOL);

    /* A10: the drip's chorus send arrived in the hall after the switch */
    check(peak_range(CHOL(&main512, HALL), SWITCH, 8000) > TOL ||
          peak_range(CHOR(&main512, HALL), SWITCH, 8000) > TOL,
          "hall chorus rings after drip arrived",
          fmax(peak_range(CHOL(&main512, HALL), SWITCH, 8000),
               peak_range(CHOR(&main512, HALL), SWITCH, 8000)), TOL);

    printf("\n== B. block-size independence: 512-chunk vs 8x64-chunk ==\n");
    {
        double m = 0;
        for(i = 0; i < 4 * 2; i++) { m = fmax(m, maxdiff_range(main512.dry[i], main64.dry[i], 0, N)); }
        for(i = 0; i < 4 * 4; i++) { m = fmax(m, maxdiff_range(main512.fx[i],  main64.fx[i],  0, N)); }
        check(m <= TOL, "512-chunk == 8x64-chunk (all dry+fx)", m, TOL);
    }

    printf("\n== C. default config identical to current rendering ==\n");
    /* C1: 1 group, write_float == baseline B1 */
    check(maxdiff_range(def1wf.interleaved, base_b1, 0, 2 * N) <= TOL,
          "1-group write_float == baseline",
          maxdiff_range(def1wf.interleaved, base_b1, 0, 2 * N), TOL);
    /* C2: 1 group, process == baseline B2 (dry + reverb + chorus) */
    {
        double m = 0;
        m = fmax(m, maxdiff_range(def1proc.dry[0], base_b2 + 0 * N, 0, N));
        m = fmax(m, maxdiff_range(def1proc.dry[1], base_b2 + 1 * N, 0, N));
        m = fmax(m, maxdiff_range(def1proc.fx[0],  base_b2 + 2 * N, 0, N));
        m = fmax(m, maxdiff_range(def1proc.fx[1],  base_b2 + 3 * N, 0, N));
        m = fmax(m, maxdiff_range(def1proc.fx[2],  base_b2 + 4 * N, 0, N));
        m = fmax(m, maxdiff_range(def1proc.fx[3],  base_b2 + 5 * N, 0, N));
        check(m <= TOL, "1-group process == baseline (dry+rev+cho)", m, TOL);
    }
    /* C3: 4 groups, note without space lands on channel pair, dry == baseline */
    check(maxdiff_range(def4proc.dry[0], base_b2 + 0 * N, 0, N) <= TOL &&
          maxdiff_range(def4proc.dry[1], base_b2 + 1 * N, 0, N) <= TOL,
          "4-group default note -> pair0 dry == baseline",
          fmax(maxdiff_range(def4proc.dry[0], base_b2 + 0 * N, 0, N),
               maxdiff_range(def4proc.dry[1], base_b2 + 1 * N, 0, N)), TOL);
    /* C3b: other pairs silent in that case */
    {
        double p = 0;
        for(i = 1; i < 4; i++) { p = fmax(p, peak_range(def4proc.dry[i * 2], 0, N)); p = fmax(p, peak_range(def4proc.dry[i * 2 + 1], 0, N)); }
        check(p < TOL, "4-group default note: pairs 1..3 silent", p, TOL);
    }

    printf("\n== D. channel volume/pan reach both spaces ==\n");
    /* D1: hall (drip) follows channel vol/pan == drip-alone-in-hall with same CC */
    check(maxdiff_range(DRYL(&voltest, HALL), DRYL(&volRefHall, HALL), CC_TICK, N - CC_TICK) <= TOL &&
          maxdiff_range(DRYR(&voltest, HALL), DRYR(&volRefHall, HALL), CC_TICK, N - CC_TICK) <= TOL,
          "hall drip follows channel vol/pan",
          fmax(maxdiff_range(DRYL(&voltest, HALL), DRYL(&volRefHall, HALL), CC_TICK, N - CC_TICK),
               maxdiff_range(DRYR(&voltest, HALL), DRYR(&volRefHall, HALL), CC_TICK, N - CC_TICK)), TOL);
    /* D2: cave (footstep) follows channel vol/pan == footstep-only with same CC */
    check(maxdiff_range(DRYL(&voltest, CAVE), DRYL(&volRefFoot, CAVE), CC_TICK, N - CC_TICK) <= TOL &&
          maxdiff_range(DRYR(&voltest, CAVE), DRYR(&volRefFoot, CAVE), CC_TICK, N - CC_TICK) <= TOL,
          "cave footstep follows channel vol/pan",
          fmax(maxdiff_range(DRYL(&voltest, CAVE), DRYL(&volRefFoot, CAVE), CC_TICK, N - CC_TICK),
               maxdiff_range(DRYR(&voltest, CAVE), DRYR(&volRefFoot, CAVE), CC_TICK, N - CC_TICK)), TOL);
    /* D3: sanity — the CC actually changed something (voltest differs from main512 after CC_TICK) */
    check(maxdiff_range(DRYL(&voltest, HALL), DRYL(&main512, HALL), CC_TICK, N - CC_TICK) > TOL,
          "channel vol/pan had audible effect",
          maxdiff_range(DRYL(&voltest, HALL), DRYL(&main512, HALL), CC_TICK, N - CC_TICK), TOL);

    printf("\n== E. sends off -> all fx silent ==\n");
    {
        double p = 0;
        for(i = 0; i < 4 * 4; i++) { p = fmax(p, peak_range(sendsOff.fx[i], 0, N)); }
        check(p < TOL, "all 4 reverb + 4 chorus silent when sends off", p, TOL);
        /* dry still audible */
        check(peak_range(DRYL(&sendsOff, CAVE), 0, SWITCH) > TOL,
              "dry still rendered when sends off",
              peak_range(DRYL(&sendsOff, CAVE), 0, SWITCH), TOL);
    }

    printf("\n== F. exclusive class still cuts across spaces ==\n");
    {
        fluid_settings_t *settings = new_fluid_settings();
        fluid_synth_t *synth;
        int sfont_id;
        fluid_voice_t *voices[16];
        fluid_voice_t *v1 = NULL;
        int killed_same_space, killed_cross_space, k;

        fluid_settings_setint(settings, "synth.audio-groups", 4);
        fluid_settings_setint(settings, "synth.audio-channels", 4);
        fluid_settings_setint(settings, "synth.effects-groups", 4);
        fluid_settings_setint(settings, "synth.threadsafe-api", 0);
        synth = new_fluid_synth(settings);
        sfont_id = fluid_synth_sfload(synth, SF2, 1);
        fluid_synth_program_select(synth, 9, sfont_id, 128, 4); /* exclusive class 1: keys 24,26 */

        /* control: both notes with default mapping (same space) */
        fluid_synth_noteon(synth, 9, 24, VEL);
        fluid_synth_get_voicelist(synth, voices, 15, -1);
        for(k = 0; voices[k]; k++) { if(fluid_voice_get_key(voices[k]) == 24) { v1 = voices[k]; } }
        fluid_synth_noteon(synth, 9, 26, VEL);
        killed_same_space = v1 && ((int)v1->gen[GEN_EXCLUSIVECLASS].val == 0);
        fluid_synth_all_notes_off(synth, 9);
        { float b0[256], b1[256]; float *o[2] = {b0, b1}; fluid_synth_process(synth, 256, 0, NULL, 2, o); }

        /* test: key24 in cave, key26 in hall — must still cut key24 */
        fluid_synth_noteon_space(synth, 9, 24, VEL, CAVE);
        fluid_synth_get_voicelist(synth, voices, 15, -1);
        v1 = NULL;
        for(k = 0; voices[k]; k++) { if(fluid_voice_get_key(voices[k]) == 24) { v1 = voices[k]; } }
        fluid_synth_noteon_space(synth, 9, 26, VEL, HALL);
        killed_cross_space = v1 && ((int)v1->gen[GEN_EXCLUSIVECLASS].val == 0);

        check(killed_same_space, "exclusive class cuts in same space (control)", (double)killed_same_space, 1);
        check(killed_cross_space, "exclusive class cuts across spaces", (double)killed_cross_space, 1);

        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
    }

    printf("\n== G. voice stealing still caps polyphony ==\n");
    {
        fluid_settings_t *settings = new_fluid_settings();
        fluid_synth_t *synth;
        int maxactive = 0, k;
        float b0[512], b1[512];
        float *o[2] = {b0, b1};

        fluid_settings_setint(settings, "synth.audio-groups", 4);
        fluid_settings_setint(settings, "synth.audio-channels", 4);
        fluid_settings_setint(settings, "synth.effects-groups", 4);
        fluid_settings_setint(settings, "synth.polyphony", 8);
        fluid_settings_setint(settings, "synth.threadsafe-api", 0);
        synth = new_fluid_synth(settings);
        fluid_synth_sfload(synth, SF2, 1);

        for(k = 0; k < 20; k++)
        {
            fluid_synth_noteon_space(synth, 0, 40 + (k % 30), VEL, k % 4); /* spread over spaces */
            fluid_synth_process(synth, 64, 0, NULL, 2, o);
            if(fluid_synth_get_active_voice_count(synth) > maxactive)
            {
                maxactive = fluid_synth_get_active_voice_count(synth);
            }
        }
        check(maxactive <= 8, "active voices never exceed polyphony", (double)maxactive, 8);

        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
    }

    /* ============ write report data ============ */
    printf("\nwriting report_data.js ...\n");
    write_report(&main512, &refHall, align_hall);

    printf("\n==== %d checks passed, %d failed ====\n", npass, nfail);

    bufs_free(&main512); bufs_free(&main64); bufs_free(&refCave); bufs_free(&refFoot);
    bufs_free(&refHall); bufs_free(&dripMove); bufs_free(&def1wf); bufs_free(&def1proc);
    bufs_free(&def4proc); bufs_free(&voltest); bufs_free(&volRefHall); bufs_free(&volRefFoot);
    bufs_free(&sendsOff);
    free(base_b1); free(base_b2);

    return nfail ? 1 : 0;
}
