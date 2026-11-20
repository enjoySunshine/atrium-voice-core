/*
 * Scene (space) routing scenario harness for the modified FluidSynth.
 *
 * Renders the "dripping water / footsteps" scenario into four spaces
 * (cave, wooden hall, yard, well head), each with its own stereo dry
 * pair, reverb and chorus. Writes all buffers plus the numeric
 * comparison results to scene_report_data.json for the local HTML page.
 *
 * The harness only uses the public FluidSynth API.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "fluidsynth.h"

#define SR 44100
#define NSPACES 4
#define MOVE_TICK 22050u
#define LEN_MAIN (2 * SR)
#define LEN_REF (1 * SR)
#define TOL 0.0001
#define SF2_PATH "sf2/VintageDreamsWaves-v2.sf2"

/* indices of the four spaces */
enum { SPACE_CAVE = 0, SPACE_WOOD = 1, SPACE_YARD = 2, SPACE_WELL = 3 };

typedef struct
{
    int n;                       /* number of samples per channel */
    float *dry[NSPACES][2];      /* dry stereo pairs */
    float *rev[NSPACES][2];      /* reverb outputs */
    float *cho[NSPACES][2];      /* chorus outputs */
} render_t;

typedef struct
{
    int tick;
    int chan;
    int key;
    int vel;   /* >0 noteon, ==0 noteoff */
} note_evt_t;

typedef struct
{
    int tick;
    int chan;
    int key;
    int space;
} move_evt_t;

typedef struct
{
    int fx_on;            /* enable reverb/chorus sends processing */
    int audio_groups;     /* synth.audio-groups / effects-groups */
    int polyphony;
} run_opts_t;

static float *xalloc_floats(int n)
{
    float *p = (float *)calloc((size_t)n, sizeof(float));

    if(p == NULL)
    {
        fprintf(stderr, "out of memory\n");
        exit(2);
    }

    return p;
}

static void render_free(render_t *r)
{
    int s, c;

    for(s = 0; s < NSPACES; s++)
        for(c = 0; c < 2; c++)
        {
            free(r->dry[s][c]);
            free(r->rev[s][c]);
            free(r->cho[s][c]);
        }
}

static void alloc_render(render_t *r, int n, int groups)
{
    int s, c;
    memset(r, 0, sizeof(*r));
    r->n = n;

    for(s = 0; s < groups; s++)
        for(c = 0; c < 2; c++)
        {
            r->dry[s][c] = xalloc_floats(n);
            r->rev[s][c] = xalloc_floats(n);
            r->cho[s][c] = xalloc_floats(n);
        }
}

static void new_synth_with_groups(fluid_synth_t **out_synth, fluid_settings_t **out_set,
                                  int groups, int fx_on, int polyphony)
{
    fluid_settings_t *settings = new_fluid_settings();
    fluid_synth_t *synth;

    fluid_settings_setnum(settings, "synth.sample-rate", (double)SR);
    fluid_settings_setint(settings, "synth.audio-channels", groups);
    fluid_settings_setint(settings, "synth.audio-groups", groups);
    fluid_settings_setint(settings, "synth.effects-groups", groups);
    fluid_settings_setint(settings, "synth.polyphony", polyphony);
    fluid_settings_setint(settings, "synth.reverb.active", fx_on);
    fluid_settings_setint(settings, "synth.chorus.active", fx_on);
    /* deterministic, single threaded rendering */
    fluid_settings_setint(settings, "synth.cpu-cores", 1);
    fluid_settings_setint(settings, "synth.threadsafe-api", 0);

    synth = new_fluid_synth(settings);

    if(synth == NULL)
    {
        fprintf(stderr, "failed to create synth\n");
        exit(2);
    }

    if(fluid_synth_sfload(synth, "/workspace/sf2/VintageDreamsWaves-v2.sf2", 1) == FLUID_FAILED
            && fluid_synth_sfload(synth, SF2_PATH, 1) == FLUID_FAILED)
    {
        fprintf(stderr, "failed to load soundfont %s\n", SF2_PATH);
        exit(2);
    }

    *out_synth = synth;
    *out_set = settings;
}

/*
 * Generic render. groups is the number of stereo groups allocated in r.
 * note events and move events are scheduled at absolute ticks; the
 * renderer may be driven with arbitrary chunk sizes (used to prove
 * 512 vs 8x64 sample-identical output).
 */
static void render_scenario(int groups, int len, int chunk, int fx_on, int polyphony,
                            const note_evt_t *notes, int nnotes,
                            const move_evt_t *moves, int nmoves,
                            render_t *r)
{
    fluid_synth_t *synth;
    fluid_settings_t *settings;
    float *out[8];   /* up to 4 stereo dry groups */
    float *fx[16];    /* 2 stereo channels (rev, cho) per group */
    int s, pos, ni = 0, mi = 0;

    alloc_render(r, len, groups);
    new_synth_with_groups(&synth, &settings, groups, fx_on, polyphony);

    for(s = 0; s < groups; s++)
    {
        /* user fx layout per unit: [revL, revR, choL, choR] = fx[4s..4s+3].
         * Passing groups*4 buffers avoids the documented modulo wrap. */
        out[2 * s] = r->dry[s][0];
        out[2 * s + 1] = r->dry[s][1];
        fx[4 * s] = r->rev[s][0];
        fx[4 * s + 1] = r->rev[s][1];
        fx[4 * s + 2] = r->cho[s][0];
        fx[4 * s + 3] = r->cho[s][1];
    }

    pos = 0;

    while(pos < len)
    {
        int this_chunk = len - pos;

        if(this_chunk > chunk)
        {
            this_chunk = chunk;
        }

        /* events whose tick falls inside [pos, pos+this_chunk) */
        while(ni < nnotes && notes[ni].tick < pos + this_chunk)
        {
            if(notes[ni].vel > 0)
            {
                fluid_synth_noteon(synth, notes[ni].chan, notes[ni].key, notes[ni].vel);
            }
            else
            {
                fluid_synth_noteoff(synth, notes[ni].chan, notes[ni].key);
            }

            ni++;
        }

        while(mi < nmoves && moves[mi].tick < pos + this_chunk)
        {
            fluid_voice_t *voices[64];
            int k, moved = 0;

            fluid_synth_get_voicelist(synth, voices, 64, -1);

            /* A note may produce several voices (e.g. a hard-panned
             * stereo pair): move ALL voices of that note together so
             * the whole sound changes space, not just one of them. */
            for(k = 0; voices[k] != NULL; k++)
            {
                if(fluid_voice_get_channel(voices[k]) == moves[mi].chan
                        && fluid_voice_get_key(voices[k]) == moves[mi].key)
                {
                    fluid_synth_voice_set_space_at(synth, voices[k], moves[mi].space,
                                                   (unsigned int)moves[mi].tick);
                    moved++;
                }
            }

            if(moved == 0)
            {
                fprintf(stderr, "warn: no voice for move at tick %d (chan %d key %d)\n",
                        moves[mi].tick, moves[mi].chan, moves[mi].key);
            }

            mi++;
        }

        /* output pointers must advance per chunk */
        for(s = 0; s < groups; s++)
        {
            out[2 * s] = r->dry[s][0] + pos;
            out[2 * s + 1] = r->dry[s][1] + pos;
            fx[4 * s] = r->rev[s][0] + pos;
            fx[4 * s + 1] = r->rev[s][1] + pos;
            fx[4 * s + 2] = r->cho[s][0] + pos;
            fx[4 * s + 3] = r->cho[s][1] + pos;
        }

        if(fluid_synth_process(synth, this_chunk, groups * 4, fx,
                               groups * 2, out) != FLUID_OK)
        {
            fprintf(stderr, "fluid_synth_process failed at %d\n", pos);
            exit(2);
        }

        pos += this_chunk;
    }

    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
}

/* ---------- numeric helpers ---------- */

static double peak_abs(const float *a, int from, int n)
{
    double m = 0.0;
    int i;

    for(i = from; i < n; i++)
    {
        double v = fabs((double)a[i]);

        if(v > m)
        {
            m = v;
        }
    }

    return m;
}

static double max_abs_diff(const float *a, const float *b, int from, int n)
{
    double m = 0.0;
    int i;

    for(i = from; i < n; i++)
    {
        double v = fabs((double)a[i] - (double)b[i]);

        if(v > m)
        {
            m = v;
        }
    }

    return m;
}

/* first sample at which |a-b| stays within tolerance, searching [from,n) */
static int first_match_sample(const float *a, const float *b, int from, int n)
{
    int i;

    for(i = from; i < n; i++)
    {
        if(fabs((double)a[i] - (double)b[i]) <= TOL)
        {
            return i;
        }
    }

    return -1;
}

static int check_count = 0;
static int fail_count = 0;

static void check(int ok, const char *name, double value)
{
    check_count++;

    if(!ok)
    {
        fail_count++;
    }

    printf("[%s] %-58s %g\n", ok ? "PASS" : "FAIL", name, value);
}

/* Render one middle-C note into a freshly created N-space synth and
 * return the reverb/chorus output of unit 0. Used to prove the effect
 * units themselves are unaffected by how many units exist (comparing
 * equal nfx layouts). */
static void render_unit0_fx(int groups, float *revL, float *revR,
                            float *choL, float *choR)
{
    fluid_synth_t *synth;
    fluid_settings_t *settings;
    float *outp[8], *fxp[16];
    int s;

    new_synth_with_groups(&synth, &settings, groups, 1, 256);

    for(s = 0; s < groups; s++)
    {
        outp[2 * s] = calloc(LEN_REF, sizeof(float));
        outp[2 * s + 1] = calloc(LEN_REF, sizeof(float));
        fxp[4 * s] = calloc(LEN_REF, sizeof(float));
        fxp[4 * s + 1] = calloc(LEN_REF, sizeof(float));
        fxp[4 * s + 2] = calloc(LEN_REF, sizeof(float));
        fxp[4 * s + 3] = calloc(LEN_REF, sizeof(float));
    }

    fluid_synth_noteon(synth, 0, 60, 100);
    fluid_synth_process(synth, LEN_REF, groups * 4, fxp, groups * 2, outp);

    /* unit 0 occupies fx[0..3] = revL, revR, choL, choR */
    for(s = 0; s < LEN_REF; s++)
    {
        revL[s] = fxp[0][s];
        revR[s] = fxp[1][s];
        choL[s] = fxp[2][s];
        choR[s] = fxp[3][s];
    }

    for(s = 0; s < groups * 4; s++)
    {
        free(fxp[s]);
    }

    for(s = 0; s < groups * 2; s++)
    {
        free(outp[s]);
    }

    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
}

/* Render the two-voice main scenario with an optional CC7 change right
 * after the 22050 move. Returns post-move dry peaks for cave and wood. */
static void render_vol_scenario(int cc7, double *cave_peak, double *wood_peak)
{
    fluid_synth_t *synth;
    fluid_settings_t *settings;
    float *outp[8], *fxp[16];
    int s;
    render_t r;

    alloc_render(&r, LEN_REF, NSPACES);
    new_synth_with_groups(&synth, &settings, NSPACES, 1, 256);
    fluid_synth_noteon(synth, 0, 60, 100);
    fluid_synth_noteon(synth, 0, 48, 100);

    for(s = 0; s < NSPACES; s++)
    {
        outp[2 * s] = r.dry[s][0];
        outp[2 * s + 1] = r.dry[s][1];
        fxp[4 * s] = r.rev[s][0];
        fxp[4 * s + 1] = r.rev[s][1];
        fxp[4 * s + 2] = r.cho[s][0];
        fxp[4 * s + 3] = r.cho[s][1];
    }

    fluid_synth_process(synth, MOVE_TICK, 16, fxp, 8, outp);
    {
        fluid_voice_t *vv[64];
        int k;
        fluid_synth_get_voicelist(synth, vv, 64, -1);

        for(k = 0; vv[k] != NULL; k++)
        {
            if(fluid_voice_get_key(vv[k]) == 60)
            {
                fluid_synth_voice_set_space(synth, vv[k], SPACE_WOOD);
            }
        }
    }

    if(cc7 >= 0)
    {
        fluid_synth_cc(synth, 0, 7, cc7);
    }

    for(s = 0; s < NSPACES; s++)
    {
        outp[2 * s] = r.dry[s][0] + MOVE_TICK;
        outp[2 * s + 1] = r.dry[s][1] + MOVE_TICK;
        fxp[4 * s] = r.rev[s][0] + MOVE_TICK;
        fxp[4 * s + 1] = r.rev[s][1] + MOVE_TICK;
        fxp[4 * s + 2] = r.cho[s][0] + MOVE_TICK;
        fxp[4 * s + 3] = r.cho[s][1] + MOVE_TICK;
    }

    fluid_synth_process(synth, LEN_REF - MOVE_TICK, 16, fxp, 8, outp);

    *cave_peak = peak_abs(r.dry[SPACE_CAVE][0], MOVE_TICK + 2000, LEN_REF);
    *wood_peak = peak_abs(r.dry[SPACE_WOOD][0], MOVE_TICK + 2000, LEN_REF);

    render_free(&r);
    delete_fluid_synth(synth);
    delete_fluid_settings(settings);
}

/* ---------- scenario definitions ---------- */

int main(void)
{
    render_t main512, main64, ref_both, ref_drip, ref_wood;
    render_t one_group, four_default, fx_off, overflow_run;
    note_evt_t main_notes[2];
    move_evt_t main_move[1];
    note_evt_t single_note[1];
    int i, s, c;
    FILE *jf;
    int wood_align[2] = {-1, -1};
    double wood_align_diff[2] = {0.0, 0.0};

    /* Main scenario:
     * - water drip: middle C (60), vel 100, starts at sample 0 in the
     *   cave, moves to the wooden hall at sample 22050.
     * - footsteps: one octave lower (48), vel 100, same MIDI channel,
     *   starts at sample 0 and stays in the cave the whole time. */
    main_notes[0] = (note_evt_t) {0, 0, 60, 100};
    main_notes[1] = (note_evt_t) {0, 0, 48, 100};
    main_move[0] = (move_evt_t) {MOVE_TICK, 0, 60, SPACE_WOOD};

    /* Reference renders, all one second long: */
    note_evt_t ref_both_notes[2] = {
        {0, 0, 60, 100}, {0, 0, 48, 100}
    };
    note_evt_t ref_wood_notes[1] = {{0, 0, 60, 100}};
    move_evt_t ref_wood_move[1] = {{0, 0, 60, SPACE_WOOD}};

    printf("== rendering main scenario (512-sample chunks) ==\n");
    render_scenario(NSPACES, LEN_MAIN, 512, 1, 256,
                    main_notes, 2, main_move, 1, &main512);

    printf("== rendering main scenario (8 x 64-sample chunks) ==\n");
    render_scenario(NSPACES, LEN_MAIN, 64, 1, 256,
                    main_notes, 2, main_move, 1, &main64);

    printf("== reference: drip + footsteps, both in cave ==\n");
    render_scenario(NSPACES, LEN_REF, 512, 1, 256,
                    ref_both_notes, 2, NULL, 0, &ref_both);

    printf("== reference: footsteps only, cave ==\n");
    render_scenario(NSPACES, LEN_REF, 512, 1, 256,
                    ((note_evt_t[]) {{0, 0, 48, 100}}), 1, NULL, 0, &ref_drip);

    printf("== reference: drip in wooden hall from sample 0 ==\n");
    render_scenario(NSPACES, LEN_REF, 512, 1, 256,
                    ref_wood_notes, 1, ref_wood_move, 1, &ref_wood);

    /* ---- checks ---- */
    printf("\n== checks ==\n");

    /* 1. yard / well dry silent, full length */
    for(s = SPACE_YARD; s <= SPACE_WELL; s++)
        for(c = 0; c < 2; c++)
        {
            char nm[96];
            snprintf(nm, sizeof(nm), "space %d dry %s silent (peak<1e-4)", s, c ? "R" : "L");
            check(peak_abs(main512.dry[s][c], 0, LEN_MAIN) < TOL, nm,
                  peak_abs(main512.dry[s][c], 0, LEN_MAIN));
        }

    /* 2. yard / well reverb & chorus silent */
    for(s = SPACE_YARD; s <= SPACE_WELL; s++)
        for(c = 0; c < 2; c++)
        {
            char nm[96];
            snprintf(nm, sizeof(nm), "space %d reverb %s silent", s, c ? "R" : "L");
            check(peak_abs(main512.rev[s][c], 0, LEN_MAIN) < TOL, nm,
                  peak_abs(main512.rev[s][c], 0, LEN_MAIN));
            snprintf(nm, sizeof(nm), "space %d chorus %s silent", s, c ? "R" : "L");
            check(peak_abs(main512.cho[s][c], 0, LEN_MAIN) < TOL, nm,
                  peak_abs(main512.cho[s][c], 0, LEN_MAIN));
        }

    /* 3. cave dry matches drip+footsteps reference before the move */
    for(c = 0; c < 2; c++)
    {
        double d = max_abs_diff(main512.dry[SPACE_CAVE][c], ref_both.dry[SPACE_CAVE][c],
                                0, MOVE_TICK);
        char nm[96];
        snprintf(nm, sizeof(nm), "cave dry %s == drip+steps ref [0,22050)", c ? "R" : "L");
        check(d <= TOL, nm, d);
    }

    /* 4. cave dry matches footsteps-only reference from the move on */
    for(c = 0; c < 2; c++)
    {
        double d = max_abs_diff(main512.dry[SPACE_CAVE][c], ref_drip.dry[SPACE_CAVE][c],
                                MOVE_TICK, LEN_REF);
        char nm[96];
        snprintf(nm, sizeof(nm), "cave dry %s == steps-only ref [22050,44100)", c ? "R" : "L");
        check(d <= TOL, nm, d);
    }

    /* 5. wood dry silent before the move */
    for(c = 0; c < 2; c++)
    {
        double p = peak_abs(main512.dry[SPACE_WOOD][c], 0, MOVE_TICK);
        char nm[96];
        snprintf(nm, sizeof(nm), "wood dry %s silent before 22050", c ? "R" : "L");
        check(p < TOL, nm, p);
    }

    /* 6. wood dry aligns with the always-in-wood drip from the move on */
    for(c = 0; c < 2; c++)
    {
        double d = max_abs_diff(main512.dry[SPACE_WOOD][c], ref_wood.dry[SPACE_WOOD][c],
                                MOVE_TICK, LEN_REF);
        char nm[96];
        snprintf(nm, sizeof(nm), "wood dry %s == always-wood drip [22050,44100)", c ? "R" : "L");
        check(d <= TOL, nm, d);
        wood_align[c] = first_match_sample(main512.dry[SPACE_WOOD][c],
                                           ref_wood.dry[SPACE_WOOD][c],
                                           MOVE_TICK, LEN_REF);
        wood_align_diff[c] = d;
    }

    /* 7. no re-trigger: the cave waveform stays continuous across the
     *    cut (the drip simply stops contributing; footsteps continue) */
    for(c = 0; c < 2; c++)
    {
        double before = peak_abs(main512.dry[SPACE_CAVE][c], MOVE_TICK - 64, MOVE_TICK);
        double at = (double)main512.dry[SPACE_CAVE][c][MOVE_TICK];
        char nm[96];
        snprintf(nm, sizeof(nm), "cave dry %s continuous across cut (no new attack)", c ? "R" : "L");
        check(at <= before + TOL, nm, at);
    }

    /* 8. wood reverb/chorus silent before the move */
    for(c = 0; c < 2; c++)
    {
        double pr = peak_abs(main512.rev[SPACE_WOOD][c], 0, MOVE_TICK);
        double pc = peak_abs(main512.cho[SPACE_WOOD][c], 0, MOVE_TICK);
        char nm[96];
        snprintf(nm, sizeof(nm), "wood reverb %s silent before 22050", c ? "R" : "L");
        check(pr < TOL, nm, pr);
        snprintf(nm, sizeof(nm), "wood chorus %s silent before 22050", c ? "R" : "L");
        check(pc < TOL, nm, pc);
    }

    /* 9. cave reverb still carries a tail after the drip left */
    for(c = 0; c < 2; c++)
    {
        double p = peak_abs(main512.rev[SPACE_CAVE][c], MOVE_TICK, LEN_MAIN);
        char nm[96];
        snprintf(nm, sizeof(nm), "cave reverb %s tail present after 22050", c ? "R" : "L");
        check(p > TOL, nm, p);
    }

    /* 10. 512 vs 8x64 sample-identical: every dry and effect buffer */
    for(s = 0; s < NSPACES; s++)
    {
        const char *sn[4] = {"cave", "wood", "yard", "well"};

        for(c = 0; c < 2; c++)
        {
            char nm[110];
            double dd = max_abs_diff(main512.dry[s][c], main64.dry[s][c], 0, LEN_MAIN);
            double dr = max_abs_diff(main512.rev[s][c], main64.rev[s][c], 0, LEN_MAIN);
            double dc = max_abs_diff(main512.cho[s][c], main64.cho[s][c], 0, LEN_MAIN);
            snprintf(nm, sizeof(nm), "512 vs 8x64 %s dry %s", sn[s], c ? "R" : "L");
            check(dd <= TOL, nm, dd);
            snprintf(nm, sizeof(nm), "512 vs 8x64 %s reverb %s", sn[s], c ? "R" : "L");
            check(dr <= TOL, nm, dr);
            snprintf(nm, sizeof(nm), "512 vs 8x64 %s chorus %s", sn[s], c ? "R" : "L");
            check(dc <= TOL, nm, dc);
        }
    }

    /* 11. effects off: all eight effect buffers silent */
    printf("== rendering with reverb/chorus disabled ==\n");
    render_scenario(NSPACES, LEN_REF, 512, 0, 256,
                    ref_both_notes, 2, NULL, 0, &fx_off);

    for(s = 0; s < NSPACES; s++)
        for(c = 0; c < 2; c++)
        {
            char nm[96];
            double pr = peak_abs(fx_off.rev[s][c], 0, LEN_REF);
            double pc = peak_abs(fx_off.cho[s][c], 0, LEN_REF);
            snprintf(nm, sizeof(nm), "fx-off space %d reverb %s silent", s, c ? "R" : "L");
            check(pr < TOL, nm, pr);
            snprintf(nm, sizeof(nm), "fx-off space %d chorus %s silent", s, c ? "R" : "L");
            check(pc < TOL, nm, pc);
        }

    /* 12. compatibility: default routing identical with 1 or 4 spaces.
     * The dry requirement ("同一条中央 C 的干声逐采样一致") is checked
     * directly. For the effect units we compare equal nfx layouts,
     * because FluidSynth's documented fx-buffer modulo wrapping means a
     * 1-unit nfx=2 readback sums reverb+chorus while an 8-unit readback
     * keeps them separate (pre-existing, unrelated to spaces). */
    printf("== compatibility renders (1 space vs 4 spaces, default routing) ==\n");
    single_note[0] = (note_evt_t) {0, 0, 60, 100};
    render_scenario(1, LEN_REF, 512, 1, 256, single_note, 1, NULL, 0, &one_group);
    render_scenario(NSPACES, LEN_REF, 512, 1, 256, single_note, 1, NULL, 0, &four_default);

    for(c = 0; c < 2; c++)
    {
        char nm[96];
        double dd = max_abs_diff(one_group.dry[0][c], four_default.dry[0][c], 0, LEN_REF);
        snprintf(nm, sizeof(nm), "compat dry %s: 1 space == 4 spaces default route", c ? "R" : "L");
        check(dd <= TOL, nm, dd);
    }

    {
        /* effect unit 0 output with identical (multi-unit) layouts */
        float *r1L = calloc(LEN_REF, 4), *r1R = calloc(LEN_REF, 4);
        float *c1L = calloc(LEN_REF, 4), *c1R = calloc(LEN_REF, 4);
        float *r4L = calloc(LEN_REF, 4), *r4R = calloc(LEN_REF, 4);
        float *c4L = calloc(LEN_REF, 4), *c4R = calloc(LEN_REF, 4);
        render_unit0_fx(1, r1L, r1R, c1L, c1R);
        render_unit0_fx(NSPACES, r4L, r4R, c4L, c4R);

        for(c = 0; c < 2; c++)
        {
            char nm[96];
            double dr = max_abs_diff(c ? r1R : r1L, c ? r4R : r4L, 0, LEN_REF);
            double dc = max_abs_diff(c ? c1R : c1L, c ? c4R : c4L, 0, LEN_REF);
            snprintf(nm, sizeof(nm), "compat reverb unit0 %s (same nfx layout)", c ? "R" : "L");
            check(dr <= TOL, nm, dr);
            snprintf(nm, sizeof(nm), "compat chorus unit0 %s (same nfx layout)", c ? "R" : "L");
            check(dc <= TOL, nm, dc);
        }

        free(r1L); free(r1R); free(c1L); free(c1R);
        free(r4L); free(r4R); free(c4L); free(c4R);
    }

    /* 13. channel volume scales voices in BOTH spaces together.
     * Compare a run with CC7 lowered after the move against an otherwise
     * identical run without the volume change, over the same post-move
     * window. Both peaks must drop by the same factor. */
    printf("== channel volume check ==\n");
    {
        double cave_hi, wood_hi, cave_lo, wood_lo;
        double r_cave, r_wood;
        render_vol_scenario(-1, &cave_hi, &wood_hi);   /* no CC7 change */
        render_vol_scenario(40, &cave_lo, &wood_lo);   /* CC7 -> 40 */
        r_cave = cave_lo / cave_hi;
        r_wood = wood_lo / wood_hi;
        check(r_cave < 0.5 && r_wood < 0.5,
              "CC7 lowers cave footsteps and wood drip together (both <0.5x)",
              r_cave + r_wood);
        check(fabs(r_cave - r_wood) < 0.02,
              "CC7 attenuates both spaces by the same factor",
              fabs(r_cave - r_wood));
    }

    /* 14. channel pan rotates L/R of both voices, each inside its own
     * space. Uses the "Simple Square Floot" preset (2), which answers
     * CC10 exactly (hard left => right channel silent). The drip is
     * moved to the wooden hall; both voices must end up on the LEFT of
     * their respective space, and nothing must appear on the right. */
    printf("== channel pan check (Simple Square Floot, CC10 hard-left) ==\n");
    {
        fluid_settings_t *settings;
        fluid_synth_t *synth;
        float *outp[8], *fxp[16];
        render_t pan_left;
        double wood_l, wood_r, cave_l, cave_r;
        int s2;

        alloc_render(&pan_left, LEN_REF, NSPACES);
        new_synth_with_groups(&synth, &settings, NSPACES, 1, 256);
        fluid_synth_program_change(synth, 0, 2); /* Simple Square Floot */
        fluid_synth_cc(synth, 0, 10, 0);         /* hard left */
        fluid_synth_noteon(synth, 0, 60, 100);
        fluid_synth_noteon(synth, 0, 48, 100);

        for(s2 = 0; s2 < NSPACES; s2++)
        {
            outp[2 * s2] = pan_left.dry[s2][0];
            outp[2 * s2 + 1] = pan_left.dry[s2][1];
            fxp[4 * s2] = pan_left.rev[s2][0];
            fxp[4 * s2 + 1] = pan_left.rev[s2][1];
            fxp[4 * s2 + 2] = pan_left.cho[s2][0];
            fxp[4 * s2 + 3] = pan_left.cho[s2][1];
        }

        fluid_synth_process(synth, MOVE_TICK, 16, fxp, 8, outp);
        {
            fluid_voice_t *vv[64];
            int k;
            fluid_synth_get_voicelist(synth, vv, 64, -1);

            for(k = 0; vv[k] != NULL; k++)
            {
                if(fluid_voice_get_key(vv[k]) == 60)
                {
                    fluid_synth_voice_set_space(synth, vv[k], SPACE_WOOD);
                }
            }
        }
        for(s2 = 0; s2 < NSPACES; s2++)
        {
            outp[2 * s2] = pan_left.dry[s2][0] + MOVE_TICK;
            outp[2 * s2 + 1] = pan_left.dry[s2][1] + MOVE_TICK;
            fxp[4 * s2] = pan_left.rev[s2][0] + MOVE_TICK;
            fxp[4 * s2 + 1] = pan_left.rev[s2][1] + MOVE_TICK;
            fxp[4 * s2 + 2] = pan_left.cho[s2][0] + MOVE_TICK;
            fxp[4 * s2 + 3] = pan_left.cho[s2][1] + MOVE_TICK;
        }
        fluid_synth_process(synth, LEN_REF - MOVE_TICK, 16, fxp, 8, outp);

        wood_l = peak_abs(pan_left.dry[SPACE_WOOD][0], MOVE_TICK, LEN_REF);
        wood_r = peak_abs(pan_left.dry[SPACE_WOOD][1], MOVE_TICK, LEN_REF);
        cave_l = peak_abs(pan_left.dry[SPACE_CAVE][0], MOVE_TICK, LEN_REF);
        cave_r = peak_abs(pan_left.dry[SPACE_CAVE][1], MOVE_TICK, LEN_REF);
        check(wood_l > TOL && wood_r < TOL, "pan-left: wood drip on wood LEFT only", wood_r);
        check(cave_l > TOL && cave_r < TOL, "pan-left: cave footsteps on cave LEFT only", cave_r);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        render_free(&pan_left);
    }

    /* 15. polyphony overflow: the number of surviving voices is the
     * polyphony limit regardless of how spaces are used. */
    printf("== polyphony overflow check ==\n");
    {
        note_evt_t many[8];

        for(i = 0; i < 8; i++)
        {
            many[i] = (note_evt_t) {i * 100, 0, 48 + i, 100};
        }

        render_scenario(NSPACES, LEN_REF, 512, 1, 4, many, 8, NULL, 0, &overflow_run);

        {
            fluid_voice_t *vv[64];
            fluid_settings_t *settings;
            fluid_synth_t *synth;
            int playing_at_end = 0, k;
            new_synth_with_groups(&synth, &settings, NSPACES, 1, 4);

            for(i = 0; i < 8; i++)
            {
                fluid_synth_noteon(synth, 0, 48 + i, 100);
                fluid_synth_process(synth, 100, 0, NULL, 0, NULL);
            }

            fluid_synth_process(synth, 64, 0, NULL, 0, NULL);
            fluid_synth_get_voicelist(synth, vv, 64, -1);

            for(k = 0; vv[k] != NULL; k++)
            {
                playing_at_end++;
            }

            check(playing_at_end == 4, "overflow leaves exactly polyphony voices (4)",
                  (double)playing_at_end);
            (void)overflow_run;
            delete_fluid_synth(synth);
            delete_fluid_settings(settings);
        }
    }

    /* 16. soundfont exclusive class: a new note cuts the old one even
     * when they live in different spaces. Analog Drumset (bank 128,
     * program 132), key 67, exclusive class 2, produces a stereo pair. */
    printf("== exclusive-class cross-space check (Analog Drumset key 67) ==\n");
    {
        fluid_settings_t *settings;
        fluid_synth_t *synth;
        fluid_voice_t *vv[64];
        unsigned int first_id = 0;
        int k, old_on = -1, new_present = 0;

        new_synth_with_groups(&synth, &settings, NSPACES, 1, 64);
        fluid_synth_bank_select(synth, 0, 128);
        fluid_synth_program_change(synth, 0, 132);

        fluid_synth_noteon(synth, 0, 67, 100);
        fluid_synth_process(synth, 128, 0, NULL, 0, NULL);

        /* move the first note's voices to the wooden hall */
        fluid_synth_get_voicelist(synth, vv, 64, -1);

        for(k = 0; vv[k] != NULL; k++)
        {
            if(fluid_voice_get_key(vv[k]) == 67)
            {
                first_id = fluid_voice_get_id(vv[k]);
                fluid_synth_voice_set_space(synth, vv[k], SPACE_WOOD);
            }
        }

        fluid_synth_process(synth, 128, 0, NULL, 0, NULL);

        /* second note stays in the default (cave) space */
        fluid_synth_noteon(synth, 0, 67, 100);
        fluid_synth_process(synth, 128, 0, NULL, 0, NULL);
        fluid_synth_get_voicelist(synth, vv, 64, -1);

        for(k = 0; vv[k] != NULL; k++)
        {
            if(fluid_voice_get_key(vv[k]) != 67)
            {
                continue;
            }

            if(fluid_voice_get_id(vv[k]) == first_id)
            {
                /* old voice: must have been forced off, and still in wood */
                old_on = fluid_voice_is_on(vv[k]);
                check(fluid_synth_voice_get_space(synth, vv[k]) == SPACE_WOOD,
                      "exclusive cut keeps the dying voice in its own space",
                      (double)fluid_synth_voice_get_space(synth, vv[k]));
            }
            else
            {
                /* new voice: alive in the default space */
                if(fluid_voice_is_on(vv[k]))
                {
                    new_present++;
                }
            }
        }

        check(old_on == 0, "exclusive class: old clave cut (forced off) across spaces",
              (double)(old_on == 0 ? 0 : 1));
        check(new_present >= 1, "exclusive class: new clave sounds in default space",
              (double)new_present);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
    }

    printf("\n== %d checks, %d failures ==\n", check_count, fail_count);

    /* ---------- write JSON report data ---------- */
    jf = fopen("scene_report_data.json", "w");

    if(jf == NULL)
    {
        fprintf(stderr, "cannot open scene_report_data.json for writing\n");
        return 2;
    }

    fprintf(jf, "{\n");
    fprintf(jf, "  \"sample_rate\": %d,\n", SR);
    fprintf(jf, "  \"length\": %d,\n", LEN_MAIN);
    fprintf(jf, "  \"move_tick\": %u,\n", MOVE_TICK);
    fprintf(jf, "  \"wood_align_start\": [%d, %d],\n", wood_align[0], wood_align[1]);
    fprintf(jf, "  \"wood_align_maxdiff\": [%.8g, %.8g],\n",
            wood_align_diff[0], wood_align_diff[1]);
    fprintf(jf, "  \"checks_total\": %d,\n", check_count);
    fprintf(jf, "  \"checks_failed\": %d,\n", fail_count);
    fprintf(jf, "  \"spaces\": [\n");

    for(s = 0; s < NSPACES; s++)
    {
        const char *names[4] = {"cave", "wooden_hall", "yard", "well_head"};
        fprintf(jf, "    {\"name\": \"%s\", \"traces\": [\n", names[s]);

        for(i = 0; i < 6; i++)
        {
            const char *tn[6] = {"dry_L", "dry_R", "reverb_L", "reverb_R", "chorus_L", "chorus_R"};
            float *src;

            switch(i)
            {
                case 0:
                    src = main512.dry[s][0];
                    break;

                case 1:
                    src = main512.dry[s][1];
                    break;

                case 2:
                    src = main512.rev[s][0];
                    break;

                case 3:
                    src = main512.rev[s][1];
                    break;

                case 4:
                    src = main512.cho[s][0];
                    break;

                default:
                    src = main512.cho[s][1];
                    break;
            }

            fprintf(jf, "      {\"name\": \"%s\", \"peak\": %.8g, \"buckets\": [",
                    tn[i], peak_abs(src, 0, LEN_MAIN));

            /* min/max decimation into ~900 buckets, no external deps */
            {
                int nbuck = 900;
                int bsize = (LEN_MAIN + nbuck - 1) / nbuck;
                int b;

                for(b = 0; b < nbuck; b++)
                {
                    int lo = b * bsize;
                    int hi = lo + bsize;
                    double mn = 0.0, mx = 0.0;
                    int k2;

                    if(hi > LEN_MAIN)
                    {
                        hi = LEN_MAIN;
                    }

                    for(k2 = lo; k2 < hi; k2++)
                    {
                        double v = (double)src[k2];

                        if(v < mn)
                        {
                            mn = v;
                        }

                        if(v > mx)
                        {
                            mx = v;
                        }
                    }

                    if(b)
                    {
                        fprintf(jf, ",");
                    }

                    fprintf(jf, "[%.6g,%.6g]", mn, mx);
                }
            }
            fprintf(jf, "]}%s\n", i == 5 ? "" : ",");
        }

        /* close the "traces" array, then the space object */
        fprintf(jf, "    ]}%s\n", s == NSPACES - 1 ? "" : ",");
    }

    fprintf(jf, "  ]\n}\n");
    fclose(jf);
    printf("wrote scene_report_data.json\n");

    /* Also emit a JavaScript wrapper of the same data so the report page
     * can be opened directly via file:// without a server (fetch() is
     * blocked on local pages in most browsers). */
    {
        FILE *js = fopen("scene_report_data.js", "w");

        if(js != NULL)
        {
            FILE *in = fopen("scene_report_data.json", "r");

            if(in != NULL)
            {
                int ch;
                fputs("window.SCENE_REPORT_DATA = ", js);

                while((ch = fgetc(in)) != EOF)
                {
                    fputc(ch, js);
                }

                fputs(";\n", js);
                fclose(in);
            }

            fclose(js);
            printf("wrote scene_report_data.js\n");
        }
    }

    render_free(&main512);
    render_free(&main64);
    render_free(&ref_both);
    render_free(&ref_drip);
    render_free(&ref_wood);
    render_free(&one_group);
    render_free(&four_default);
    render_free(&fx_off);
    render_free(&overflow_run);

    return fail_count ? 1 : 0;
}
