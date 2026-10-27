/* SPDX-License-Identifier: GPL-3.0-only */
/* STRIP: a Mackie-style channel strip on every track (fx.c mix_part / mix_spread), after the engine and before the
 * track's LEVEL and PAN, in the order of a compact Mackie mixer's channel (1202 / 1402-VLZ):
 *
 *   GAIN      the trim, -20 .. +20 dB, then the preamp: clean up to about 6 dB over a full voice, then a soft
 *             saturation that rounds the peaks off (a hot trim drives it, as a Mackie channel pushed into the red)
 *   LOW CUT   75 Hz, 18 dB / octave (a third-order Butterworth high-pass), on / off
 *   [the track's DIST and INSERT: the channel's insert point]
 *   HIGH      12 kHz shelving, +-15 dB
 *   MID       peaking, +-15 dB, swept 100 Hz .. 8 kHz (MID FREQ), wide (Q 0.9)
 *   LOW       80 Hz shelving, +-15 dB
 *
 * The parameters take the ids of DIGITAL's inert operator envelope (core.h P_MX_*): projects and user presets store
 * them, motion records them (automation, locks), a sound load keeps them (they are the mixer's, ui.c param_kept).
 * Every older store holds 0 there: GAIN 0 dB, LOW CUT off, the bands flat (MID FREQ 100 Hz) - the strip at rest.
 *
 * At rest (all of that) the strip does nothing: the part's samples are as without it, bit for bit. Working, it is
 * float (the FPU, as ACID's and TR909's DSP): the coefficients when a value changes (RBJ's cookbook), the filters as
 * transposed direct form II, per channel: a mono part one, a stereo part (SPREAD, JUNO's chorus) left and right. */

#define MX_FS 44100.0f
#define MX_KNEE 65536.0f          /* the preamp: linear up to here (a full voice x 2, the sum of a few) */
#define MX_CEIL 65536.0f          /* .. then a soft knee of at most this much more */

typedef struct { float b0, b1, b2, a1, a2; } mx_bq_t;
typedef struct { float z1, z2; } mx_bz_t;
typedef struct {
    int16_t key[6];               /* the values the coefficients were made with (GAIN LCUT LOW MID FREQ HIGH) */
    uint8_t on, pre, eq;          /* any stage working; GAIN / LOW CUT working; the bands working */
    uint8_t rsv;
    float g, g_prev;              /* the trim (linear), and where it was a block ago (a ramp: no zipper) */
    float lc1_b, lc1_a;           /* LOW CUT: the first-order high-pass (y = b (x - x1) + a y1) */
    mx_bq_t lc2, lo, mid, hi;     /* .. its second-order section; the three bands */
    struct {
        float lc1_x, lc1_y;
        mx_bz_t lc2, lo, mid, hi;
    } ch[2];                      /* per channel: 0 mono / left, 1 right */
} mx_strip_t;
static mx_strip_t mx_strip[NTRK] __attribute__((section(".pool")));

/* MID FREQ: 0..127 -> 100 Hz .. 8 kHz, log */
static float mx_mid_hz(int32_t v) { return 100.0f * fm_exp2f((float)clamp(v, 0, 127) * (6.321928f / 127.0f)); }

/* RBJ: shelves (S = slope), peak (Q); g_db the gain */
static void mx_shelf(mx_bq_t *q, float f0, float g_db, int high)
{
    float A = fm_exp2f(g_db * (0.083048202f)), w = 6.2831853f * f0 / MX_FS, cs = fm_cosf(w), sn = fm_sinf(w);
    float S = 0.75f, al = sn * 0.5f * fm_sqrtf((A + 1.0f / A) * (1.0f / S - 1.0f) + 2.0f), sa = 2.0f * fm_sqrtf(A) * al;
    float b0, b1, b2, a0, a1, a2;
    if (!high) {
        b0 = A * ((A + 1.0f) - (A - 1.0f) * cs + sa);
        b1 = 2.0f * A * ((A - 1.0f) - (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) - (A - 1.0f) * cs - sa);
        a0 = (A + 1.0f) + (A - 1.0f) * cs + sa;
        a1 = -2.0f * ((A - 1.0f) + (A + 1.0f) * cs);
        a2 = (A + 1.0f) + (A - 1.0f) * cs - sa;
    } else {
        b0 = A * ((A + 1.0f) + (A - 1.0f) * cs + sa);
        b1 = -2.0f * A * ((A - 1.0f) + (A + 1.0f) * cs);
        b2 = A * ((A + 1.0f) + (A - 1.0f) * cs - sa);
        a0 = (A + 1.0f) - (A - 1.0f) * cs + sa;
        a1 = 2.0f * ((A - 1.0f) - (A + 1.0f) * cs);
        a2 = (A + 1.0f) - (A - 1.0f) * cs - sa;
    }
    q->b0 = b0 / a0; q->b1 = b1 / a0; q->b2 = b2 / a0; q->a1 = a1 / a0; q->a2 = a2 / a0;
}
static void mx_peak(mx_bq_t *q, float f0, float g_db, float Q)
{
    float A = fm_exp2f(g_db * (0.083048202f)), w = 6.2831853f * f0 / MX_FS, cs = fm_cosf(w), al = fm_sinf(w) / (2.0f * Q);
    float a0 = 1.0f + al / A;
    q->b0 = (1.0f + al * A) / a0;
    q->b1 = -2.0f * cs / a0;
    q->b2 = (1.0f - al * A) / a0;
    q->a1 = q->b1;
    q->a2 = (1.0f - al / A) / a0;
}
static void mx_hp2(mx_bq_t *q, float f0, float Q)
{
    float w = 6.2831853f * f0 / MX_FS, cs = fm_cosf(w), al = fm_sinf(w) / (2.0f * Q), a0 = 1.0f + al;
    q->b0 = (1.0f + cs) * 0.5f / a0;
    q->b1 = -(1.0f + cs) / a0;
    q->b2 = q->b0;
    q->a1 = -2.0f * cs / a0;
    q->a2 = (1.0f - al) / a0;
}

/* the strip of values p (a track's): coefficients into s; 1 = it works (some stage is not at rest) */
static int mx_design(mx_strip_t *s, const int16_t *p)
{
    int32_t gain = p[P_MX_GAIN], lcut = p[P_MX_LCUT], lo = p[P_MX_LOW], mid = p[P_MX_MID], hi = p[P_MX_HIGH];
    s->pre = gain != 0 || lcut != 0;
    s->eq = lo != 0 || mid != 0 || hi != 0;
    s->on = s->pre || s->eq;
    s->g = fm_exp2f((float)gain * 0.16609640f);       /* dB / 6.0206 */
    if (lcut) {                                       /* 75 Hz Butterworth, third order: a pole, then Q 1 */
        float k = fm_expf(-6.2831853f * 75.0f / MX_FS);
        s->lc1_b = (1.0f + k) * 0.5f;
        s->lc1_a = k;
        mx_hp2(&s->lc2, 75.0f, 1.0f);
    }
    if (lo)
        mx_shelf(&s->lo, 80.0f, (float)lo, 0);
    if (mid)
        mx_peak(&s->mid, mx_mid_hz(p[P_MX_FREQ]), (float)mid, 0.9f);
    if (hi)
        mx_shelf(&s->hi, 12000.0f, (float)hi, 1);
    return s->on;
}

static inline float mx_run(const mx_bq_t *q, mx_bz_t *z, float x)
{
    float y = q->b0 * x + z->z1;
    z->z1 = q->b1 * x - q->a1 * y + z->z2;
    z->z2 = q->b2 * x - q->a2 * y;
    return y;
}

/* the preamp's curve: linear to MX_KNEE, then a soft knee to MX_KNEE + MX_CEIL ((27 + u^2) / (27 + 9 u^2): tanh) */
static inline float mx_sat(float x)
{
    float a = x < 0.0f ? -x : x, u, t;
    if (a <= MX_KNEE)
        return x;
    u = (a - MX_KNEE) * (1.0f / MX_CEIL);
    if (u > 3.0f)
        u = 3.0f;
    t = u * (27.0f + u * u) / (27.0f + 9.0f * u * u);
    a = MX_KNEE + MX_CEIL * t;
    return x < 0.0f ? -a : a;
}

/* track t's strip state, its coefficients made again if its values changed; 0 = at rest (do nothing) */
static mx_strip_t *mx_of(const track_t *t)
{
    mx_strip_t *s = &mx_strip[(uint32_t)(t - trk) % NTRK];
    const int16_t *p = t->p;
    int16_t key[6] = {p[P_MX_GAIN], p[P_MX_LCUT], p[P_MX_LOW], p[P_MX_MID], p[P_MX_FREQ], p[P_MX_HIGH]};
    uint32_t i, same = 1;
    for (i = 0; i < 6u; i++)
        same &= s->key[i] == key[i];
    if (!same) {
        int was_pre = s->pre, was_eq = s->eq;
        for (i = 0; i < 6u; i++)
            s->key[i] = key[i];
        mx_design(s, p);
        if (s->pre && !was_pre) {                     /* switched on: the trim ramps from unity, the states clear */
            s->g_prev = 1.0f;
            for (i = 0; i < 2u; i++) {
                s->ch[i].lc1_x = s->ch[i].lc1_y = 0.0f;
                s->ch[i].lc2.z1 = s->ch[i].lc2.z2 = 0.0f;
            }
        }
        if (s->eq && !was_eq)
            for (i = 0; i < 2u; i++) {
                s->ch[i].lo.z1 = s->ch[i].lo.z2 = s->ch[i].mid.z1 = s->ch[i].mid.z2 = 0.0f;
                s->ch[i].hi.z1 = s->ch[i].hi.z2 = 0.0f;
            }
    }
    return s->on ? s : 0;
}

/* GAIN and LOW CUT on channel c of n samples (in place); g0 -> g1 the trim's ramp */
static void mx_pre_ch(mx_strip_t *s, uint32_t c, float *x, uint32_t n)
{
    uint32_t i;
    float g = s->g_prev, dg = (s->g - s->g_prev) / (float)n;
    int lc = s->key[1] != 0;
    for (i = 0; i < n; i++) {
        float v = mx_sat(x[i] * g);
        g += dg;
        if (lc) {
            float y = s->lc1_b * (v - s->ch[c].lc1_x) + s->lc1_a * s->ch[c].lc1_y;
            s->ch[c].lc1_x = v;
            s->ch[c].lc1_y = y;
            v = mx_run(&s->lc2, &s->ch[c].lc2, y);
        }
        x[i] = v;
    }
}
static void mx_eq_ch(mx_strip_t *s, uint32_t c, float *x, uint32_t n)
{
    uint32_t i;
    int lo = s->key[2] != 0, mid = s->key[3] != 0, hi = s->key[5] != 0;
    for (i = 0; i < n; i++) {
        float v = x[i];
        if (lo)
            v = mx_run(&s->lo, &s->ch[c].lo, v);
        if (mid)
            v = mx_run(&s->mid, &s->ch[c].mid, v);
        if (hi)
            v = mx_run(&s->hi, &s->ch[c].hi, v);
        x[i] = v;
    }
}
static inline int32_t mx_i(float v) { return (int32_t)(v < -1.0e8f ? -1.0e8f : v > 1.0e8f ? 1.0e8f : v); }

/* the strip's stage `eq` (0: GAIN + LOW CUT, before the insert; 1: the bands, after it) on part t's block: b mono, or
 * with d (SPREAD: b = L + R, d = L - R) the two channels */
static __attribute__((noinline)) void track_chstrip(track_t *t, int32_t *b, int32_t *d, uint32_t n, int eq)
{
    mx_strip_t *s = mx_of(t);
    float l[CTL], r[CTL];
    uint32_t i;
    if (!s || !(eq ? s->eq : s->pre) || n > CTL)
        return;
    if (!d) {
        for (i = 0; i < n; i++)
            l[i] = (float)b[i];
        if (eq)
            mx_eq_ch(s, 0, l, n);
        else
            mx_pre_ch(s, 0, l, n);
        for (i = 0; i < n; i++)
            b[i] = mx_i(l[i]);
    } else {
        for (i = 0; i < n; i++) {
            l[i] = (float)(b[i] + d[i]) * 0.5f;
            r[i] = (float)(b[i] - d[i]) * 0.5f;
        }
        if (eq) {
            mx_eq_ch(s, 0, l, n);
            mx_eq_ch(s, 1, r, n);
        } else {
            mx_pre_ch(s, 0, l, n);
            mx_pre_ch(s, 1, r, n);
        }
        for (i = 0; i < n; i++) {
            b[i] = mx_i(l[i] + r[i]);
            d[i] = mx_i(l[i] - r[i]);
        }
    }
    if (!eq)
        s->g_prev = s->g;
}

/* the UI (ui_graph.c graph_strip): the bands' and LOW CUT's response at f Hz, in dB, for values p */
static float mx_bq_db(const mx_bq_t *q, float f)
{
    float w = 6.2831853f * f / MX_FS, c1 = fm_cosf(w), s1 = fm_sinf(w), c2 = c1 * c1 - s1 * s1, s2 = 2.0f * s1 * c1;
    float nr = q->b0 + q->b1 * c1 + q->b2 * c2, ni = -(q->b1 * s1 + q->b2 * s2);
    float dr = 1.0f + q->a1 * c1 + q->a2 * c2, di = -(q->a1 * s1 + q->a2 * s2);
    float num = nr * nr + ni * ni, den = dr * dr + di * di;
    return den > 0.0f && num > 0.0f ? 3.0103f * fm_log2f(num / den) : -96.0f;   /* 10 log10 = 3.0103 log2 */
}
static float mx_resp_db(const mx_strip_t *s, float f)
{
    float db = 0.0f;
    if (s->key[1]) {
        float w = 6.2831853f * f / MX_FS, c1 = fm_cosf(w), s1 = fm_sinf(w);   /* the pole: b (1 - z^-1) / (1 - a z^-1) */
        float nr = s->lc1_b * (1.0f - c1), ni = s->lc1_b * s1, dr = 1.0f - s->lc1_a * c1, di = s->lc1_a * s1;
        db += 3.0103f * fm_log2f((nr * nr + ni * ni) / (dr * dr + di * di) + 1e-12f);
        db += mx_bq_db(&s->lc2, f);
    }
    if (s->key[2])
        db += mx_bq_db(&s->lo, f);
    if (s->key[3])
        db += mx_bq_db(&s->mid, f);
    if (s->key[5])
        db += mx_bq_db(&s->hi, f);
    return db;
}
