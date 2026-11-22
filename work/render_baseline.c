/* Baseline renderer: renders reference buffers with the CURRENT (unmodified)
 * FluidSynth API. These buffers are the "现在渲染" reference that the modified
 * synth must reproduce sample-for-sample in the default configuration.
 *
 * B1: one stereo group, fluid_synth_write_float (reverb+chorus folded into the
 *     stereo pair, the classic single-stereo render).
 * B2: one stereo group + one fx group, fluid_synth_process with separate
 *     dry / reverb / chorus buffers.
 *
 * Both: central C (key 60), velocity 100, channel 0, note on at sample 0,
 * sample rate 44100, VintageDreamsWaves-v2.sf2, default preset.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fluidsynth.h>

#define N 66048  /* total samples: multiple of 512 and 64 (~1.5 s at 44100) */
#define SF2 "/workspace/sf2/VintageDreamsWaves-v2.sf2"

static fluid_synth_t *make_synth(fluid_settings_t **settings_out, int audio_groups, int fx_groups)
{
    fluid_settings_t *settings = new_fluid_settings();
    fluid_settings_setint(settings, "synth.audio-groups", audio_groups);
    fluid_settings_setint(settings, "synth.audio-channels", audio_groups);
    fluid_settings_setint(settings, "synth.effects-groups", fx_groups);
    fluid_settings_setnum(settings, "synth.sample-rate", 44100.0);
    fluid_settings_setint(settings, "synth.reverb.active", 1);
    fluid_settings_setint(settings, "synth.chorus.active", 1);
    fluid_settings_setint(settings, "synth.threadsafe-api", 0);
    fluid_synth_t *synth = new_fluid_synth(settings);
    *settings_out = settings;
    return synth;
}

int main(void)
{
    /* ---- B1: write_float, fx folded into the stereo pair ---- */
    {
        fluid_settings_t *settings;
        fluid_synth_t *synth = make_synth(&settings, 1, 1);
        float *interleaved = calloc(2 * N, sizeof(float));
        FILE *f;

        if(fluid_synth_sfload(synth, SF2, 1) == FLUID_FAILED)
        {
            fprintf(stderr, "sfload failed\n");
            return 1;
        }
        fluid_synth_noteon(synth, 0, 60, 100);
        fluid_synth_write_float(synth, N, interleaved, 0, 2, interleaved, 1, 2);

        f = fopen("/workspace/work/baseline_b1.bin", "wb");
        fwrite(interleaved, sizeof(float), 2 * N, f);
        fclose(f);
        printf("B1 written: %d frames stereo (write_float)\n", N);

        free(interleaved);
        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
    }

    /* ---- B2: process, dry/reverb/chorus in separate buffers ---- */
    {
        fluid_settings_t *settings;
        fluid_synth_t *synth = make_synth(&settings, 1, 1);
        float *dryL = calloc(N, sizeof(float));
        float *dryR = calloc(N, sizeof(float));
        float *revL = calloc(N, sizeof(float));
        float *revR = calloc(N, sizeof(float));
        float *choL = calloc(N, sizeof(float));
        float *choR = calloc(N, sizeof(float));
        float *out[2] = { dryL, dryR };
        float *fx[4] = { revL, revR, choL, choR };
        float *all[6] = { dryL, dryR, revL, revR, choL, choR };
        FILE *f;
        int i;

        if(fluid_synth_sfload(synth, SF2, 1) == FLUID_FAILED)
        {
            fprintf(stderr, "sfload failed\n");
            return 1;
        }
        fluid_synth_noteon(synth, 0, 60, 100);
        fluid_synth_process(synth, N, 4, fx, 2, out);

        f = fopen("/workspace/work/baseline_b2.bin", "wb");
        for(i = 0; i < 6; i++)
        {
            fwrite(all[i], sizeof(float), N, f);
        }
        fclose(f);
        printf("B2 written: %d frames x 6 (dryL dryR revL revR choL choR)\n", N);

        delete_fluid_synth(synth);
        delete_fluid_settings(settings);
        free(dryL); free(dryR); free(revL); free(revR); free(choL); free(choR);
    }

    return 0;
}
