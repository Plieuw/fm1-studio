/* SPDX-License-Identifier: GPL-3.0-only */
/* ACID: a TB-303 bass part. The voice is X0X's bass303 (firmware/x0x/bass303.c: Open303 by Robin Schmidt, MIT,
 * through schwung-303 and X0X, GPL-3.0; see LICENSING.md): the 303's saw / square, the TeeBeeFilter with its
 * feedback high-pass, the decay and accent envelopes, the pitch slew of a slide, the soft / RAT drive.
 *
 * How it sits in Felucca: one bass303_t per part in the part's engine memory, monophonic (poly 1). A note-on of
 * the part's voice is a 303 note: velocity above 110 (a step's ACC, a hard key) is the accent; a note while the
 * previous one is still held (the sequencer's SLIDE keeps it held to the next step, LEGATO, a key played over a
 * held one) slides to it without a new attack, as on the 303. The key's release is the 303's gate off; the voice
 * ends when the amp envelope has died. Felucca's pitch (TUNE, bend, LFO -> PIT, the matrix) reaches it as a factor
 * on the oscillator (bass303_t.fmul), its cutoff modulation (LFO -> FLT, the matrix's CUT) as an offset on CUT.
 * acid_block renders each block (float, BASS303_OS x oversampled) into the part's memory; the voice's render adds it.
 *
 * Parameters (EDIT 1 VCF, EDIT 2 VCO): CUT RES ENV DEC, ACC WAVE DRV DTYP; GLD (the voice page) is the slide time
 * (0: the 303's 60 ms). */
#define params b303_params
#define wave_names b303_wave_names
#define drv_names b303_drv_names
#define rc_coeff b303_rc_coeff
#define powers b303_powers
#define env_tables b303_env_tables
#define calc_envmod b303_calc_envmod
#define bq_lowpass b303_bq_lowpass
#define bq_low_shelf b303_bq_low_shelf
#define bq_run b303_bq_run
#define rat_coeffs b303_rat_coeffs
#define clear_audio_state b303_clear_audio_state
#define set_accent_state b303_set_accent_state
#define blep b303_blep
#define hb b303_hb
#define decim_hb1 b303_decim_hb1
#define decim_hb2 b303_decim_hb2
#define idle_advance b303_idle_advance
#define drive_block b303_drive_block
#define recip b303_recip
#define run b303_run
#include "../x0x/bass303.c"
#undef params
#undef wave_names
#undef drv_names
#undef rc_coeff
#undef powers
#undef env_tables
#undef calc_envmod
#undef bq_lowpass
#undef bq_low_shelf
#undef bq_run
#undef rat_coeffs
#undef clear_audio_state
#undef set_accent_state
#undef blep
#undef hb
#undef decim_hb1
#undef decim_hb2
#undef idle_advance
#undef drive_block
#undef recip
#undef run

#ifndef ACID_OUT
#define ACID_OUT 70000.0f        /* float -> the part's scale: the 303 at its nominal peak (~0.5) as loud as ANALOG */
#endif

typedef struct {
    bass303_t b;
    float f[CTL];                 /* this block, rendered by acid_block */
    uint8_t have;                 /* f holds a sounding block not yet added */
    uint8_t init;                 /* bass303_init has run */
    uint8_t note;                 /* the 303's note now */
    uint8_t rsv;
    int16_t pot[BASS303_NPARAMS]; /* the pot values last set (-1: never), so only a change recomputes */
    int16_t cut_mod;              /* the cutoff modulation (pot steps) the voice saw last */
    float fmul;                   /* its pitch factor */
} acid_mem_t;
ENG_MEM_FITS("ACID", sizeof(acid_mem_t));
#define ACID_MEM(t) ((acid_mem_t *)ENG_MEM((uint32_t)((t) - trk)))

static void acid_init(acid_mem_t *am)
{
    uint32_t i;
    if (am->init)
        return;
    bass303_init(&am->b);
    for (i = 0; i < BASS303_NPARAMS; i++)
        am->pot[i] = -1;
    am->fmul = 1.0f;
    am->init = 1;
}

static void acid_pot(acid_mem_t *am, int i, int32_t v)
{
    const x0x_param_t *d = bass303_param(i);
    v = clamp(v, 0, d->max);
    if (am->pot[i] != v) {
        am->pot[i] = (int16_t)v;
        bass303_set(&am->b, i, (int)v);
    }
}

/* the voice's state: s[4] 1 once its release reached the 303 */
#define AV_REL 4

static void acid_note_on(track_t *t, voice_t *v)
{
    acid_mem_t *am = ACID_MEM(t);
    int32_t note = clamp((v->pitch16 + 8) >> 4, 0, 127);
    acid_init(am);
    bass303_note_on(&am->b, (int)note, v->vel > 110, am->b.gate);   /* held: a slide */
    am->note = (uint8_t)note;
    v->s[AV_REL] = 0;
}

static int acid_done(track_t *t, voice_t *v)
{
    acid_mem_t *am = ACID_MEM(t);
    if (v->gate)
        return 0;
    if (!v->s[AV_REL]) {
        bass303_note_off(&am->b);
        v->s[AV_REL] = 1;
    }
    return !am->b.gate && am->b.amp_y < 1e-4f;         /* died away (-80 dB) */
}

static void acid_block(track_t *t)
{
    acid_mem_t *am = ACID_MEM(t);
    const int16_t *p = t->p;
    uint32_t i, k;
    acid_init(am);
    for (i = 0, k = 0; i < NVOICE; i++)
        k |= t->v[i].active;
    am->have = 0;
    if (!k)                                            /* no voice: nothing renders (the 303's states wait, warm, as
                                                        * Open303's do under a closed amp) */
        return;
    acid_pot(am, BASS303_CUTOFF, p[P_E0] + am->cut_mod);
    acid_pot(am, BASS303_RESO, p[P_E1]);
    acid_pot(am, BASS303_ENVMOD, p[P_E2]);
    acid_pot(am, BASS303_DECAY, p[P_E3]);
    acid_pot(am, BASS303_ACCENT, p[P_E4]);
    acid_pot(am, BASS303_WAVE, p[P_E5]);
    acid_pot(am, BASS303_DRIVE, p[P_E6]);
    acid_pot(am, BASS303_DRVTYPE, p[P_E7]);
    acid_pot(am, BASS303_SLIDE, p[P_GLIDE] ? p[P_GLIDE] : 21);
    am->b.fmul = am->fmul;
    /* STUDIO: X0X's overload guard: the audio ISR busy above ~80 % (song.cpu_q8, audio.c), the 303 renders without its
     * oversampling (about half its cost; a little aliasing on bright, resonant notes) until it is below ~65 % again */
    if (song.cpu_q8 > 205u && !am->b.lite)
        bass303_set_lite(&am->b, 1);
    else if (song.cpu_q8 < 166u && am->b.lite)
        bass303_set_lite(&am->b, 0);
    bass303_render(&am->b, am->f, CTL);
    for (i = 0; i < CTL; i++)
        am->f[i] *= ACID_OUT;
    am->have = 1;
}

static void acid_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    acid_mem_t *am = ACID_MEM(t);
    int32_t note = clamp((v->pitch16 + 8) >> 4, 0, 127);
    uint32_t i;
    if (v->gate && note != am->note) {                 /* LEGATO: a new note without a note-on: the 303 slides */
        bass303_note_on(&am->b, (int)note, v->vel > 110, 1);
        am->note = (uint8_t)note;
    }
    /* for the next block: pitch as a factor (1/16 semitone: 2^(d / 192)), cutoff in pot steps */
    am->fmul = fm_exp2f((float)(m->pitch16 - (int32_t)am->note * 16) * (1.0f / 192.0f)) *
               (1.0f + (float)m->fine * (1.0f / 4096.0f));
    am->cut_mod = (int16_t)(m->cutoff >> 8);
    if (!am->have)
        return;
    am->have = 0;
    for (i = 0; i < n; i++) {
        float x = am->f[i] * (float)amp_at(m, i) * (1.0f / 32768.0f);
        out[i] += (int32_t)(x < -8.0e6f ? -8.0e6f : x > 8.0e6f ? 8.0e6f : x);
    }
}

static const char *const N_ACID_WAVE[] = {"SAW", "SQR"};
static const char *const N_ACID_DTYP[] = {"OFF", "SOFT", "RAT"};

/* E0..E7: CUT RES ENV DEC ACC WAVE DRV DTYP; the ADSR is the 303's own (ownenv), the track's ATK .. REL do nothing */
static const preset_t ACID_PRESETS[] = {
    {"TB CLASSIC", {64, 90, 70, 50, 80, 0, 0, 1}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 25, 10), PAT(1)},
    {"SQUELCH", {40, 115, 100, 70, 110, 0, 40, 1}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 35, 10), PAT(1)},
    {"SQR ACID", {55, 100, 80, 45, 90, 1, 0, 1}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 25, 15), PAT(1)},
    {"RAT BASS", {50, 80, 60, 40, 70, 0, 70, 2}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 15, 5), PAT(2)},
    {"DEEP SUB", {20, 40, 30, 80, 40, 1, 0, 1}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 0, 5), PAT(8)},
    {"SCREAM", {75, 124, 120, 90, 127, 0, 90, 2}, {0, 64, 0, 10}, 0, 1, FX(0, 0, 40, 20), PAT(1)},
};

static const engine_t ENG_ACID = {
    .name = "ACID",
    .page_title = {"VCF", "VCO"},
    .edit = {
        {"CUT", F_PCT, 0, 127, 64, 0, 0},
        {"RES", F_PCT, 0, 127, 64, 0, 0},
        {"ENV", F_PCT, 0, 127, 64, 0, 0},
        {"DEC", F_PCT, 0, 127, 64, 0, 0},
        {"ACC", F_PCT, 0, 127, 64, 0, 0},
        {"WAVE", F_ENUM, 0, 1, 0, N_ACID_WAVE, 0},
        {"DRV", F_PCT, 0, 127, 0, 0, 0},
        {"DTYP", F_ENUM, 0, 2, 1, N_ACID_DTYP, 0},
    },
    .presets = ACID_PRESETS,
    .npresets = NELEM(ACID_PRESETS),
    .note_on = acid_note_on,
    .render = acid_render,
    .block = acid_block,
    .knob = {P_E0, P_E1, P_E2, P_E3},
    .poly = 1,
    .ownenv = 1,
    .done = acid_done,
    .mem = sizeof(acid_mem_t),
};
