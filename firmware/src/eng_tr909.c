/* SPDX-License-Identifier: GPL-3.0-only */
/* TR909: a TR-909 drum part. The kit is X0X's drum909 (firmware/x0x/drum909.c: the 9W9 engine by Charles Vestal,
 * itself grown out of ER-99 by Matthew Cieplak, GPL-3.0; see LICENSING.md): circuit-modelled BD, SD, the three toms,
 * RS and CP, the hi-hats, ride and crash from the 909's sampled cymbals (6-bit, tools/gen_drum_samples.py).
 *
 * How it sits in Felucca: one drum909_t per part in the part's engine memory. It plays the DRUM grid's eight lanes
 * (seq.c: a step's hits are the lanes' GM notes, core.h DRUM_LANE_NOTE) and every GM drum note from the keys or MIDI:
 *   36 / 35 BD   38 / 40 SD   39 CP   42 / 44 CH   46 OH   41 / 43 LT   45 / 47 MT   48 / 50 HT   37 RS
 *   49 / 52 / 55 / 57 CR   51 / 53 / 56 / 59 RD
 * A note-on strikes the voice; a step's ACC (velocity above 110) is the 909's accent, at the ACC amount. The kit
 * renders as a whole (tr909_block, float) and costs one of the shared voices: the latest hit's voice stays while
 * the kit sounds, the earlier ones end at once (tr909_done).
 *
 * Parameters (EDIT 1 BD SD, EDIT 2 HAT TOM): BD TUNE, BD DEC, BD ATK (its click), SD SNAP, SD TONE, HAT (both hats'
 * decay, 0 = the 909's), TOM (the three toms' tune, 0 = the 909's), ACC. LANES / LANES 2 (P_LN0..P_LN7): each
 * lane's level: BD SD CP CH OH TOMS RS RIDE+CRASH. */
#define X0X_DRUM_TABLES_DEFINE
#include "../x0x/drum909.c"
#undef LIN
#undef EXP
#undef DRIVE
#undef DIST
#undef SENDS
#undef LEVEL
#undef PAN
#undef NP
#undef FITS

#ifndef TR909_OUT
#define TR909_OUT 40000.0f       /* float -> the part's scale: the 909 at its pots' defaults about as loud as DRUM */
#endif

typedef struct {
    drum909_t d;
    float f[CTL];                 /* this block, rendered by tr909_block */
    float scratch[2][CTL];        /* the kit's own reverb / delay sends (unused: the track's sends take the kit) */
    uint8_t have, init, rsv[2];
    int16_t pot[DR_NUM + 1][DR_MAX_PARAMS];   /* the pots last set (-1 never): only a change recomputes */
} tr909_mem_t;
ENG_MEM_FITS("TR909", sizeof(tr909_mem_t));
#define TR909_MEM(t) ((tr909_mem_t *)ENG_MEM((uint32_t)((t) - trk)))

static void tr909_init(tr909_mem_t *tm)
{
    uint32_t i, k;
    if (tm->init)
        return;
    drum909_init(&tm->d);
    for (i = 0; i <= DR_NUM; i++)
        for (k = 0; k < DR_MAX_PARAMS; k++)
            tm->pot[i][k] = -1;
    tm->init = 1;
}

static void tr909_pot(tr909_mem_t *tm, int voice, int i, int32_t v)
{
    v = clamp(v, 0, 127);
    if (tm->pot[voice][i] != v) {
        tm->pot[voice][i] = (int16_t)v;
        drum909_set(&tm->d, voice, i, (int)v);
    }
}

/* the 909 voice of a GM note, -1 = none */
static int tr909_voice(uint32_t note)
{
    switch (note) {
    case 35: case 36: return DR_BD;
    case 37: return DR_RS;
    case 38: case 40: return DR_SD;
    case 39: return DR_CP;
    case 42: case 44: return DR_CH;
    case 46: return DR_OH;
    case 41: case 43: return DR_LT;
    case 45: case 47: return DR_MT;
    case 48: case 50: return DR_HT;
    case 49: case 52: case 55: case 57: return DR_CR;
    case 51: case 53: case 56: case 59: return DR_RD;
    default: return -1;
    }
}

/* lane l's level (P_LN0..7, 0..127) on the 909's level pot of default d; 0 while the lane is muted (the EDIT layer's
 * black keys 1..8, eng_drum.c drum_mute: a channel mute, the lane strikes on silently) */
static int32_t tr909_lvl(const track_t *t, uint32_t l, int32_t d)
{
    return (drum_mute[(uint32_t)(t - trk) % NPART] >> l) & 1u ? 0 : d * t->p[P_LN0 + l] / 127;
}

/* the track's parameters -> the kit's pots (every block; a change costs its voice's recompute only) */
static void tr909_pots(const track_t *t, tr909_mem_t *tm)
{
    const int16_t *p = t->p;
    int32_t hat = p[P_E5], tom = p[P_E6];
    tr909_pot(tm, DR_BD, 0, p[P_E0]);                 /* Tune */
    tr909_pot(tm, DR_BD, 2, p[P_E1]);                 /* Decay */
    tr909_pot(tm, DR_BD, 1, p[P_E2]);                 /* Attack: the click */
    tr909_pot(tm, DR_BD, 3, tr909_lvl(t, 0, 94));
    tr909_pot(tm, DR_SD, 2, p[P_E3]);                 /* Snappy */
    tr909_pot(tm, DR_SD, 1, p[P_E4]);                 /* Tone */
    tr909_pot(tm, DR_SD, 4, tr909_lvl(t, 1, 58));
    tr909_pot(tm, DR_CP, 0, tr909_lvl(t, 2, 122));
    tr909_pot(tm, DR_CH, 0, 84 + hat);                /* Decay */
    tr909_pot(tm, DR_CH, 1, tr909_lvl(t, 3, 89));
    tr909_pot(tm, DR_OH, 0, 97 + hat);
    tr909_pot(tm, DR_OH, 1, tr909_lvl(t, 4, 80));
    tr909_pot(tm, DR_LT, 0, 66 + tom);                /* Tune */
    tr909_pot(tm, DR_MT, 0, 69 + tom);
    tr909_pot(tm, DR_HT, 0, 53 + tom);
    tr909_pot(tm, DR_LT, 2, tr909_lvl(t, 5, 52));
    tr909_pot(tm, DR_MT, 2, tr909_lvl(t, 5, 52));
    tr909_pot(tm, DR_HT, 2, tr909_lvl(t, 5, 52));
    tr909_pot(tm, DR_RS, 0, tr909_lvl(t, 6, 103));
    tr909_pot(tm, DR_RD, 1, tr909_lvl(t, 7, 42));
    tr909_pot(tm, DR_CR, 1, tr909_lvl(t, 7, 47));
    tr909_pot(tm, DR_KIT, 1, 127);                    /* Veloc: the hit's velocity at full depth */
}

static void tr909_note_on(track_t *t, voice_t *v)
{
    tr909_mem_t *tm = TR909_MEM(t);
    static const int8_t LANE_V[NLANE] = {DR_BD, DR_SD, DR_CP, DR_CH, DR_OH, DR_MT, DR_RS, DR_RD};
    int voice = tr909_voice(v->note);
    if (voice < 0)                                    /* any other note: its GM lane (eng_drum.c drum_lane folds the
                                                       * notes outside 35..81 into 36..47, as DRUM did) */
        voice = LANE_V[drum_lane(v->note) & (NLANE - 1u)];
    float vel = (float)v->vel * (1.0f / 96.0f);       /* a step's VEL 96: the 909 at its level */
    tr909_init(tm);
    if (voice < 0)
        return;
    tr909_pots(t, tm);
    tm->d.accent = v->vel > 110 ? 1.0f + 3.0f * (float)t->p[P_E7] * (1.0f / 127.0f) : 1.0f;   /* ACC: x1 .. x4 */
    drum909_trigger(&tm->d, voice, vel > 1.0f ? 1.0f : vel);
}

/* the latest hit's voice stays while the kit sounds; any other ends now (one voice of the budget for the kit) */
static int tr909_done(track_t *t, voice_t *v)
{
    tr909_mem_t *tm = TR909_MEM(t);
    uint32_t k;
    for (k = 0; k < NVOICE; k++)
        if (&t->v[k] != v && t->v[k].active && (int32_t)(t->v[k].age - v->age) > 0)
            return 1;
    return !drum909_active(&tm->d);
}

static void tr909_block(track_t *t)
{
    tr909_mem_t *tm = TR909_MEM(t);
    uint32_t i, k;
    tr909_init(tm);
    for (i = 0, k = 0; i < NVOICE; i++)
        k |= t->v[i].active;
    tm->have = 0;
    if (!k || !drum909_active(&tm->d))
        return;
    tr909_pots(t, tm);
    for (i = 0; i < CTL; i++)
        tm->f[i] = tm->scratch[0][i] = tm->scratch[1][i] = 0.0f;
    drum909_render(&tm->d, tm->f, tm->scratch[0], tm->scratch[1], CTL);
    tm->have = 1;
}

static void tr909_render(track_t *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    tr909_mem_t *tm = TR909_MEM(t);
    uint32_t i;
    (void)v;
    if (!tm->have)
        return;
    tm->have = 0;
    for (i = 0; i < n; i++) {
        float x = tm->f[i] * TR909_OUT * (float)amp_at(m, i) * (1.0f / 32768.0f);
        out[i] += (int32_t)(x < -8.0e6f ? -8.0e6f : x > 8.0e6f ? 8.0e6f : x);
    }
}

/* (the grid's lane names on a TR909 track: eng_drum.c drum_lane_name / drum_lane_abbr) */

/* E0..E7: BD TUNE, BD DEC, BD ATK, SD SNAP, SD TONE, HAT, TOM, ACC (the 909's pots: 9W9's defaults) */
static const preset_t TR909_PRESETS[] = {
    {"909 KIT", {34, 90, 13, 64, 68, 0, 0, 42}, {0, 64, 127, 10}, 0, 0, FX(0, 0, 0, 10), PAT(12)},
    {"HARD 909", {28, 110, 60, 90, 90, -8, -4, 80}, {0, 64, 127, 10}, 0, 0, FX(15, 0, 0, 8), PAT(12)},
    {"DEEP 909", {20, 120, 5, 50, 45, 8, -14, 30}, {0, 64, 127, 10}, 0, 0, FX(0, 0, 0, 20), PAT(12)},
    {"TIGHT 909", {40, 60, 30, 70, 80, -24, 6, 50}, {0, 64, 127, 10}, 0, 0, FX(0, 0, 0, 5), PAT(12)},
};

static const engine_t ENG_TR909 = {
    .name = "TR909",
    .page_title = {"BD SD", "HAT TOM"},
    .edit = {
        {"BD TN", F_PCT, 0, 127, 34, 0, 0},
        {"BD DC", F_PCT, 0, 127, 90, 0, 0},
        {"BD AT", F_PCT, 0, 127, 13, 0, 0},
        {"SNAP", F_PCT, 0, 127, 64, 0, 0},
        {"TONE", F_PCT, 0, 127, 68, 0, 0},
        {"HAT", F_BIPCT, -64, 63, 0, 0, 0},
        {"TOM", F_BIPCT, -64, 63, 0, 0, 0},
        {"ACC", F_PCT, 0, 127, 42, 0, 0},
    },
    .presets = TR909_PRESETS,
    .npresets = NELEM(TR909_PRESETS),
    .note_on = tr909_note_on,
    .render = tr909_render,
    .block = tr909_block,
    .knob = {P_E0, P_E1, P_E3, P_E5},
    .keys = drum_keys,            /* the keys: the GM drum map from C (eng_drum.c) */
    .ownenv = 1,
    .oneshot = 1,                 /* hits: always POLY, no glide */
    .done = tr909_done,
    .mem = sizeof(tr909_mem_t),
};
