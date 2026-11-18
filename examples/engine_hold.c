/*
 * Offline listen for one held note. Writes numbers and a small page.
 * No sound card.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fluidsynth.h>

#define SR 44100
#define STEP 64

static void die(const char *msg)
{
    fprintf(stderr, "%s\n", SR && msg ? msg : "fail");
    exit(1);
}

static fluid_synth_t *make_synth(void)
{
    fluid_settings_t *settings = new_fluid_settings();
    if (settings == NULL)
    {
        die("settings");
    }
    fluid_settings_setnum(settings, "synth.sample-rate", SR);
    fluid_settings_setnum(settings, "synth.gain", 0.5);
    fluid_settings_setint(settings, "synth.polyphony", 64);
    fluid_settings_setint(settings, "synth.cpu-cores", 1);
    fluid_settings_setint(settings, "synth.reverb.active", 0);
    fluid_settings_setint(settings, "synth.chorus.active", 0);
    fluid_synth_t *synth = new_fluid_synth(settings);
    if (synth == NULL)
    {
        die("synth");
    }
    return synth;
}

static void write_at(fluid_synth_t *synth, int n, float *left, float *right)
{
    int done = 0;
    while (done < n)
    {
        int chunk = n - done > 512 ? 512 : n - done;
        if (fluid_synth_write_float(synth, chunk,
                                    left + done, 0, 1,
                                    right + done, 0, 1) != FLUID_OK)
        {
            die("write");
        }
        done += chunk;
    }
}

static double rms(const float *left, const float *right, int a, int b)
{
    double acc = 0.0;
    int i;
    int n = b - a;
    if (n <= 0)
    {
        return 0.0;
    }
    for (i = a; i < b; i++)
    {
        double m = 0.5 * ((double)left[i] + (double)right[i]);
        acc += m * m;
    }
    return sqrt(acc / n);
}

static double goertzel(const float *left, const float *right, int a, int b, double freq)
{
    int n = b - a;
    double omega = 2.0 * 3.14159265358979323846 * freq / (double)SR;
    double coeff = 2.0 * cos(omega);
    double s0 = 0.0, s1 = 0.0, s2 = 0.0;
    int i;
    double real, imag;
    if (n <= 0)
    {
        return 0.0;
    }
    for (i = a; i < b; i++)
    {
        double x = 0.5 * ((double)left[i] + (double)right[i]);
        s0 = x + coeff * s1 - s2;
        s2 = s1;
        s1 = s0;
    }
    real = s1 - s2 * cos(omega);
    imag = s2 * sin(omega);
    return sqrt(real * real + imag * imag) / n;
}

static unsigned long hash_buf(const float *left, const float *right, int n)
{
    unsigned long h = 2166136261ul;
    int i;
    for (i = 0; i < n; i++)
    {
        int v = (int)lrint(0.5 * ((double)left[i] + (double)right[i]) * 1000000.0);
        h ^= (unsigned long)(v & 0xffff);
        h *= 16777619ul;
        h ^= (unsigned long)((v >> 16) & 0xffff);
        h *= 16777619ul;
    }
    return h;
}

static void render_hold(fluid_synth_t *synth, int program, float *left, float *right, int *voices_max)
{
    int i;
    int cc;
    if (fluid_synth_program_change(synth, 0, program) != FLUID_OK)
    {
        die("program");
    }
    if (fluid_synth_cc(synth, 0, 74, 0) != FLUID_OK)
    {
        die("cc");
    }
    if (fluid_synth_noteon(synth, 0, 60, 40) != FLUID_OK)
    {
        die("noteon");
    }
    write_at(synth, SR, left, right);
    *voices_max = fluid_synth_get_active_voice_count(synth);
    for (i = 0; i < SR; i += STEP)
    {
        int n = STEP;
        int v;
        if (i + n > SR)
        {
            n = SR - i;
        }
        cc = (int)((i * 127.0) / (double)(SR - 1) + 0.5);
        if (cc > 127)
        {
            cc = 127;
        }
        if (fluid_synth_cc(synth, 0, 74, cc) != FLUID_OK)
        {
            die("cc sweep");
        }
        write_at(synth, n, left + SR + i, right + SR + i);
        v = fluid_synth_get_active_voice_count(synth);
        if (v > *voices_max)
        {
            *voices_max = v;
        }
    }
    fluid_synth_noteoff(synth, 0, 60);
}

static void write_page(const char *path, const float *mono_src_left, const float *mono_src_right, int n,
                       double r0, double r1, double r2, int voices)
{
    FILE *fp = fopen(path, "w");
    int i;
    int bins = 240;
    if (fp == NULL)
    {
        die("page");
    }
    fprintf(fp, "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>engine hold</title></head><body>");
    fprintf(fp, "<p>voices %d</p><p>rms %.6f %.6f %.6f</p><canvas id=\"c\" width=\"960\" height=\"200\"></canvas><script>\n",
            voices, r0, r1, r2);
    fprintf(fp, "var a=[");
    for (i = 0; i < bins; i++)
    {
        int a0 = (i * n) / bins;
        int b0 = ((i + 1) * n) / bins;
        int j;
        double mn = 1e9, mx = -1e9;
        if (b0 <= a0)
        {
            b0 = a0 + 1;
        }
        for (j = a0; j < b0 && j < n; j++)
        {
            double m = 0.5 * ((double)mono_src_left[j] + (double)mono_src_right[j]);
            if (m < mn)
            {
                mn = m;
            }
            if (m > mx)
            {
                mx = m;
            }
        }
        fprintf(fp, "%s%.5f,%.5f", i ? "," : "", mn, mx);
    }
    fprintf(fp, "];var c=document.getElementById('c');var g=c.getContext('2d');g.fillStyle='#111';g.fillRect(0,0,960,200);g.strokeStyle='#ddd';g.beginPath();for(var i=0;i<a.length;i+=2){var x=i/2/ (a.length/2)*960;var y0=100-a[i]*400;var y1=100-a[i+1]*400;g.moveTo(x,y0);g.lineTo(x,y1);}g.stroke();</script></body></html>\n");
    fclose(fp);
}

static void listen_bank(const char *path, int program, const char *tag, const char *page)
{
    fluid_synth_t *synth = make_synth();
    float *left = calloc((size_t)SR * 2, sizeof(float));
    float *right = calloc((size_t)SR * 2, sizeof(float));
    int id, voices = 0;
    int w = 2205;
    if (left == NULL || right == NULL)
    {
        die("alloc");
    }
    id = fluid_synth_sfload(synth, path, 1);
    if (id == FLUID_FAILED)
    {
        die("sfload");
    }
    render_hold(synth, program, left, right, &voices);
    printf("METRIC %s voices %d\n", tag, voices);
    printf("METRIC %s attack_head %.6f\n", tag, rms(left, right, 0, w));
    printf("METRIC %s attack_tail %.6f\n", tag, rms(left, right, SR - w, SR));
    printf("METRIC %s sweep_head %.6f\n", tag, rms(left, right, SR, SR + w));
    printf("METRIC %s sweep_mid %.6f\n", tag, rms(left, right, SR + SR / 2 - w / 2, SR + SR / 2 + w / 2));
    printf("METRIC %s sweep_tail %.6f\n", tag, rms(left, right, SR * 2 - w, SR * 2));
    printf("METRIC %s low_head %.6f\n", tag, goertzel(left, right, SR, SR + w, 220.5));
    printf("METRIC %s low_mid %.6f\n", tag, goertzel(left, right, SR + SR / 2 - w / 2, SR + SR / 2 + w / 2, 220.5));
    printf("METRIC %s low_tail %.6f\n", tag, goertzel(left, right, SR * 2 - w, SR * 2, 220.5));
    printf("METRIC %s high_head %.6f\n", tag, goertzel(left, right, SR, SR + w, 882.0));
    printf("METRIC %s high_mid %.6f\n", tag, goertzel(left, right, SR + SR / 2 - w / 2, SR + SR / 2 + w / 2, 882.0));
    printf("METRIC %s high_tail %.6f\n", tag, goertzel(left, right, SR * 2 - w, SR * 2, 882.0));
    if (page != NULL)
    {
        double r0 = rms(left, right, SR, SR + w);
        double r1 = rms(left, right, SR + SR / 2 - w / 2, SR + SR / 2 + w / 2);
        double r2 = rms(left, right, SR * 2 - w, SR * 2);
        write_page(page, left + SR, right + SR, SR, r0, r1, r2, voices);
        printf("METRIC page %s\n", page);
    }
    free(left);
    free(right);
    delete_fluid_synth(synth);
}

static void listen_hash(const char *path)
{
    fluid_synth_t *synth = make_synth();
    float *left = calloc((size_t)SR / 2, sizeof(float));
    float *right = calloc((size_t)SR / 2, sizeof(float));
    int id;
    if (left == NULL || right == NULL)
    {
        die("alloc");
    }
    id = fluid_synth_sfload(synth, path, 1);
    if (id == FLUID_FAILED)
    {
        die("dreams");
    }
    if (fluid_synth_noteon(synth, 0, 60, 80) != FLUID_OK)
    {
        die("dream note");
    }
    write_at(synth, SR / 2, left, right);
    printf("METRIC dreams hash %08lx\n", hash_buf(left, right, SR / 2));
    printf("METRIC dreams rms %.6f\n", rms(left, right, 0, SR / 2));
    free(left);
    free(right);
    delete_fluid_synth(synth);
}

int main(int argc, char **argv)
{
    const char *bank = argc > 1 ? argv[1] : "sf2/engine-load.sf2";
    const char *dreams = argc > 2 ? argv[2] : "sf2/VintageDreamsWaves-v2.sf2";
    const char *page = argc > 3 ? argv[3] : "engine-hold.html";
    listen_bank(bank, 0, "layers", page);
    listen_bank(bank, 1, "single", NULL);
    listen_hash(dreams);
    return 0;
}
