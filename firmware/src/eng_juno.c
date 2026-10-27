/* SPDX-License-Identifier: GPL-3.0-only */
/* JUNO: an Alpha Juno-1 (JU-1) style part. The synth itself is firmware/juno/alpha_juno.c (integer, after the JU-1
 * owner's manual: one DCO per voice with its pulse / saw / sub waveforms, the 4-position HPF, the 24 dB resonant
 * low-pass, the 4-time / 3-level envelope, the PWM LFO and the stereo chorus), run whole, once per block, for the part.
 *
 * How it sits in Felucca:
 *   - The part's state is one aj_t (6 voices of its own, the chorus line) in the part's engine memory (JUNO_MEM).
 *   - Every Felucca voice of the part is one JUNO note: note_on starts it (aj_note_on), the key's release releases it
 *     (done(), once per control tick), a voice Felucca gives up (steal, budget) releases its note (juno_block). The
 *     voice stays active while its note sounds (ownenv: the JU-1's own envelope is the amplitude), and the last one
 *     until the chorus tail has run out.
 *   - Felucca's pitch (transpose, TUNE, bend, glide, LFO -> PIT, the matrix) reaches each note as an offset in octaves
 *     (aj_voice_t.xo16), its cutoff modulation (LFO -> FLT, the matrix's CUT) as a VCF CV offset (xcv); both are taken
 *     as the voices render and used by the next block's render (32 samples later).
 *   - juno_block renders the block into jm->l / jm->r; the first voice's render call adds it to the part's buffers.
 *     The part is stereo (the chorus): fx.c mix_part renders it as a SPREAD part at full width, the left channel in
 *     the SPREAD buffer and the right one in the part's (the SPREAD sum then holds both).
 *
 * The parameters (EDIT 1 DCO, EDIT 2 VCF):
 *   E0 PULS  OFF SQR NARW VAR          (the JU-1's PULSE 0..3; VAR follows PW/PWM)
 *   E1 SAW   OFF 1..5                   (SAWTOOTH 0..5; 3 follows PW/PWM)
 *   E2 SUB   1..6                       (SUB OSC waveform 0..5)
 *   E3 SUBL  0..3                       (SUB LEVEL)
 *   E4 CUT   E5 RES                     (VCF FREQ, VCF RES)
 *   E6 HPF   BOOST FLAT LOW HIGH        (HPF 0..3)
 *   E7 PWM   0..127                     (PW / PWM depth)
 * and the track's own pages, mapped onto the JU-1:
 *   ATK DEC SUS REL      ENV T1, T2 = T3, L3 (L2 half way from L1 = 127), T4
 *   ENV DEST FLT / PIT   VCF ENV / DCO ENV depth (|x| x 2), negative = the inverted mode
 *   LFO DEST SHP         PWM RATE: 0 = a medium rate, 1..63 = slow .. fast, negative = static (PW only)
 *   CHO                  the JU-1's chorus: 0 off, 1..127 on at that RATE (not the shared chorus send)
 *   LEVEL PAN TRN ..     as on every part; the JU-1's own LFO, bender and aftertouch are Felucca's (pitch, cutoff) */
#define AJ_PLAIN 1
/* alpha_juno.c's helpers by names of their own: Felucca has a blep, a softclip and an env_tick */
#define sat32 aj_sat32
#define mul24 aj_mul24
#define div127 aj_div127
#define div15 aj_div15
#define zero aj_zero
#define nlz aj_nlz
#define recip62 aj_recip62
#define scale_oct aj_scale_oct
#define blep aj_blep
#define wave_mean aj_wave_mean
#define wave_eval aj_wave_eval
#define softclip aj_softclip
#define lfo_tri aj_lfo_tri
#define rnd aj_rnd
#define env_stage aj_env_stage
#define env_tick aj_env_tick
#define control aj_control
#define voice_block aj_voice_block
#define cho_tap aj_cho_tap
#define chorus_block aj_chorus_block
#include "../juno/alpha_juno.c"
#undef sat32
#undef mul24
#undef div127
#undef div15
#undef zero
#undef nlz
#undef recip62
#undef scale_oct
#undef blep
#undef wave_mean
#undef wave_eval
#undef softclip
#undef lfo_tri
#undef rnd
#undef env_stage
#undef env_tick
#undef control
#undef voice_block
#undef cho_tap
#undef chorus_block
#undef Q24
#undef END
#undef P

typedef struct {
    aj_t aj;
    int32_t l[CTL], r[CTL];       /* this block, rendered by juno_block */
    uint8_t have;                 /* l / r hold a sounding block not yet added to the part */
    uint8_t init;                 /* aj_init has run (the region is zero after a claim) */
    int8_t pwm_last;              /* (unused: keeps the size a multiple of 4) */
    uint8_t rsv;
} juno_mem_t;
ENG_MEM_FITS("JUNO", sizeof(juno_mem_t));
#ifndef JUNO_OUT
#define JUNO_OUT 7                /* Q23 -> the part: >> 7 (tests/juno_test.c sets the level against ANALOG) */
#endif
#define JUNO_MEM(t) ((juno_mem_t *)ENG_MEM((uint32_t)((t) - trk)))

/* the part's buffers this block (voice.c track_render sets them before its voices): the part's own and, while it
 * renders as SPREAD, the left one; JUNO writes both itself, whichever its voice's side */
static int32_t *part_out[NTRK], *part_left[NTRK];

/* the voice <-> note link, in the Felucca voice's state: s[4] the JUNO voice + 1 (0 none), s[5] its age (a JUNO voice
 * reused for another note has another), s[6] 1 once released, s[7] the note it plays */
#define JV_IDX 4
#define JV_AGE 5
#define JV_REL 6
#define JV_NOTE 7

static int juno_mine(const aj_t *a, const voice_t *v)   /* the JUNO voice is still this voice's note */
{
    int32_t i = v->s[JV_IDX] - 1;
    return i >= 0 && i < AJ_VOICES && a->v[i].active && a->v[i].age == (uint32_t)v->s[JV_AGE];
}

static void juno_init_mem(juno_mem_t *jm)
{
    if (!jm->init) {
        aj_init(&jm->aj);
        jm->init = 1;
    }
}

static void juno_note_on(track_t *t, voice_t *v)
{
    juno_mem_t *jm = JUNO_MEM(t);
    aj_t *a = &jm->aj;
    int32_t note = clamp((v->pitch16 + 8) >> 4, 0, 127), i;
    juno_init_mem(jm);
    if (juno_mine(a, v) && !v->s[JV_REL])
        aj_voice_off(a, (unsigned)(v->s[JV_IDX] - 1));   /* a retrigger (MONO, a steal): the old note releases */
    aj_note_on(a, (unsigned)note, v->vel ? v->vel : 100u);
    i = aj_last_voice(a);
    v->s[JV_IDX] = i + 1;
    v->s[JV_AGE] = i >= 0 ? (int32_t)a->v[i].age : 0;
    v->s[JV_REL] = 0;
    v->s[JV_NOTE] = note;
    if (i >= 0) {
        a->v[i].xo16 = 0;
        a->v[i].xcv = 0;
    }
}

/* once per control tick, before the render: the key's release reaches the note; the voice ends with its note (the
 * last one of the part when the chorus has rung out too) */
static int juno_done(track_t *t, voice_t *v)
{
    juno_mem_t *jm = JUNO_MEM(t);
    aj_t *a = &jm->aj;
    uint32_t k;
    if (juno_mine(a, v)) {
        if (!v->gate && !v->s[JV_REL]) {
            aj_voice_off(a, (unsigned)(v->s[JV_IDX] - 1));
            v->s[JV_REL] = 1;
        }
        return 0;
    }
    for (k = 0; k < NVOICE; k++)
        if (&t->v[k] != v && t->v[k].active)
            return 1;                                  /* another voice keeps the part rendering */
    return !jm->have;                                  /* the last one: until the block is silent */
}

static uint8_t juno_u7(int32_t x) { return (uint8_t)(x < 0 ? 0 : x > 127 ? 127 : x); }

/* the track's parameters -> the JU-1's 36 (every block: a knob turned is heard at the next block) */
static void juno_patch(const track_t *t, aj_patch_t *pt)
{
    const int16_t *p = t->p;
    uint8_t *q = pt->v;
    int32_t ef = p[P_ED_FLT], ep = p[P_ED_PIT], ls = p[P_LD_SHP];
    q[AJ_PULSE] = (uint8_t)clamp(p[P_E0], 0, 3);
    q[AJ_SAW] = (uint8_t)clamp(p[P_E1], 0, 5);
    q[AJ_SUB] = (uint8_t)clamp(p[P_E2], 0, 5);
    q[AJ_SUB_LEVEL] = (uint8_t)clamp(p[P_E3], 0, 3);
    q[AJ_VCF_FREQ] = juno_u7(p[P_E4]);
    q[AJ_VCF_RES] = juno_u7(p[P_E5]);
    q[AJ_HPF] = (uint8_t)clamp(p[P_E6], 0, 3);
    q[AJ_PW_PWM_DEPTH] = juno_u7(p[P_E7]);
    q[AJ_PWM_RATE] = ls == 0 ? 40u : ls > 0 ? juno_u7(ls * 2) : 0u;
    q[AJ_RANGE] = 1;                                   /* 8': Felucca's octave and TRN move it */
    q[AJ_NOISE_LEVEL] = 0;
    q[AJ_ENV_T1] = juno_u7(p[P_ATK]);
    q[AJ_ENV_L1] = 127;
    q[AJ_ENV_T2] = juno_u7(p[P_DEC]);
    q[AJ_ENV_L2] = juno_u7((127 + p[P_SUS]) >> 1);
    q[AJ_ENV_T3] = juno_u7(p[P_DEC]);
    q[AJ_ENV_L3] = juno_u7(p[P_SUS]);
    q[AJ_ENV_T4] = juno_u7(p[P_REL]);
    q[AJ_ENV_KEY] = 0;
    q[AJ_VCF_ENV] = juno_u7((ef < 0 ? -ef : ef) * 2);
    q[AJ_VCF_ENV_MODE] = ef < 0 ? 1u : 2u;             /* inverted, or normal with the key's dynamics */
    q[AJ_DCO_ENV] = juno_u7((ep < 0 ? -ep : ep) * 2);
    q[AJ_DCO_ENV_MODE] = ep < 0 ? 1u : 0u;
    q[AJ_VCA_ENV_MODE] = 2;                            /* ENV with dynamics: velocity, as on every Felucca part */
    q[AJ_VCA_LEVEL] = 127;
    q[AJ_VCF_KEY] = 8;
    q[AJ_DCO_LFO] = q[AJ_VCF_LFO] = 0;                 /* Felucca's LFO: through xo16 / xcv */
    q[AJ_LFO_RATE] = 0;
    q[AJ_LFO_DELAY] = 0;
    q[AJ_DCO_AFTER] = q[AJ_VCF_AFTER] = q[AJ_VCA_AFTER] = 0;
    q[AJ_BENDER_RANGE] = 0;                            /* Felucca's bend: through xo16 */
    q[AJ_CHORUS] = p[P_CHOR] > 0;
    q[AJ_CHORUS_RATE] = juno_u7(p[P_CHOR]);
}

/* once per block and part (also with no voice sounding): release the notes whose voices Felucca gave up, take the
 * track's parameters, render the block */
static void juno_block(track_t *t)
{
    juno_mem_t *jm = JUNO_MEM(t);
    aj_t *a = &jm->aj;
    int32_t st[2 * CTL];
    uint32_t i, k;
    juno_init_mem(jm);
    for (i = 0; i < AJ_VOICES; i++) {
        aj_voice_t *av = &a->v[i];
        int found = 0;
        if (!av->active || !(av->held || av->sus))
            continue;
        for (k = 0; k < NVOICE && !found; k++) {
            const voice_t *v = &t->v[k];
            found = v->active && v->s[JV_IDX] == (int32_t)i + 1 && v->s[JV_AGE] == (int32_t)av->age;
        }
        if (!found)
            aj_voice_off(a, i);
    }
    juno_patch(t, &a->p);
    for (i = 0, k = 0; i < AJ_VOICES; i++)
        k |= a->v[i].active;
    if (!k && !a->tail) {                              /* silent, the chorus rung out: nothing to render */
        jm->have = 0;
        return;
    }
    for (i = 0; i < 2u * CTL; i++)
        st[i] = 0;
    aj_render(a, st, CTL);
    for (i = 0; i < CTL; i++) {                        /* Q23 -> the part's scale (JUNO_OUT: one note as loud as an
                                                        * ANALOG note, tests/juno_test.c) */
        jm->l[i] = st[2u * i] >> JUNO_OUT;
        jm->r[i] = st[2u * i + 1u] >> JUNO_OUT;
    }
    jm->have = 1;
}

static void juno_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    juno_mem_t *jm = JUNO_MEM(t);
    aj_t *a = &jm->aj;
    uint32_t ti = (uint32_t)(t - trk) % NTRK, i;
    (void)out;
    if (juno_mine(a, v)) {                             /* this note's pitch and cutoff offsets, for the next block */
        aj_voice_t *av = &a->v[v->s[JV_IDX] - 1];
        int32_t d16 = m->pitch16 - v->s[JV_NOTE] * 16;
        av->xo16 = ((d16 * 1365) >> 2) + m->fine * 23;   /* 1/16 semitone = 65536 / 192; fine: 1/4096 of the rate */
        av->xcv = (m->cutoff * 21) >> 8;
    }
    if (!jm->have)
        return;
    jm->have = 0;
    if (part_left[ti]) {                               /* stereo: left in the SPREAD buffer, right in the part's */
        int32_t *l = part_left[ti], *r = part_out[ti];
        for (i = 0; i < n; i++) {
            int32_t g = amp_at(m, i);
            l[i] += (int32_t)(((int64_t)jm->l[i] * g) >> 15);
            r[i] += (int32_t)(((int64_t)jm->r[i] * g) >> 15);
        }
    } else {
        int32_t *o = part_out[ti];
        for (i = 0; i < n; i++)
            o[i] += (int32_t)(((int64_t)((jm->l[i] + jm->r[i]) >> 1) * amp_at(m, i)) >> 15);
    }
}

static const char *const N_JUNO_PULS[] = {"OFF", "SQR", "NARW", "VAR"};
static const char *const N_JUNO_SAW[] = {"OFF", "SAW1", "SAW2", "SAW3", "SAW4", "SAW5"};
static const char *const N_JUNO_SUB[] = {"SUB1", "SUB2", "SUB3", "SUB4", "SUB5", "SUB6"};
static const char *const N_JUNO_HPF[] = {"BOOST", "FLAT", "LOW", "HIGH"};

/* the 8 presets of the JU-1 patch set (firmware/juno/aj_presets.h, the APR values) in Felucca's terms: E0..E7, the
 * envelope (T1, T3, L3, T4), VCF ENV as the filter envelope, MONO, the JU-1's CHORUS (rate) as CHO, then DLY REV */
static const preset_t JUNO_PRESETS[] = {
    {"INIT SAW", {0, 1, 0, 0, 90, 10, 1, 0}, {20, 50, 80, 50}, 25, 0, FX(0, 0, 10, 15), PAT(4)},
    {"BRASS", {1, 1, 0, 1, 50, 15, 1, 0}, {45, 60, 95, 30}, 37, 0, FX(0, 30, 10, 25), PAT(6)},
    {"WARM PAD", {3, 1, 0, 0, 55, 12, 1, 60}, {85, 70, 100, 90}, 17, 0, FX(0, 20, 20, 50), PAT(5)},
    {"FAT BASS", {1, 0, 1, 3, 30, 35, 1, 0}, {5, 40, 40, 25}, 40, 1, FX(0, 0, 0, 5), PAT(2)},
    {"STRINGS", {0, 4, 0, 0, 75, 5, 2, 0}, {75, 60, 110, 80}, 10, 0, FX(0, 45, 15, 55), PAT(5)},
    {"PLUCK", {2, 1, 0, 1, 25, 30, 1, 0}, {0, 50, 0, 40}, 47, 0, FX(0, 0, 30, 25), PAT(3)},
    {"PWM LEAD", {3, 0, 0, 1, 70, 25, 1, 85}, {10, 50, 100, 35}, 20, 1, FX(0, 60, 30, 20), PAT(4)},
    {"HOOVER", {3, 3, 4, 3, 85, 20, 1, 90}, {0, 60, 110, 45}, 30, 1, FX(10, 70, 20, 30), PAT(1)},
};

static const engine_t ENG_JUNO = {
    .name = "JUNO",
    .page_title = {"DCO", "VCF"},
    .edit = {
        {"PULS", F_ENUM, 0, 3, 1, N_JUNO_PULS, 0},
        {"SAW", F_ENUM, 0, 5, 1, N_JUNO_SAW, 0},
        {"SUB", F_ENUM, 0, 5, 0, N_JUNO_SUB, 0},
        {"SUBL", F_INT, 0, 3, 1, 0, 0},
        {"CUT", F_PCT, 0, 127, 80, 0, 0},
        {"RES", F_PCT, 0, 127, 20, 0, 0},
        {"HPF", F_ENUM, 0, 3, 1, N_JUNO_HPF, 0},
        {"PWM", F_PCT, 0, 127, 0, 0, 0},
    },
    .presets = JUNO_PRESETS,
    .npresets = NELEM(JUNO_PRESETS),
    .note_on = juno_note_on,
    .render = juno_render,
    .block = juno_block,
    .knob = {P_E4, P_E5, P_ATK, P_REL},
    .poly = AJ_VOICES,
    .ownenv = 1,
    .done = juno_done,
    .mem = sizeof(juno_mem_t),
};

/* fx.c: the part renders as a stereo SPREAD part (its chorus); its CHO is the JU-1's chorus, not the shared send */
static int juno_part(const track_t *t) { return t->engine == ENGI_JUNO; }
