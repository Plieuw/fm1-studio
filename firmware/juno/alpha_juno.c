#include "alpha_juno.h"
#include "aj_tables.h"

#define Q24 (1 << 24)
#define END 0xFFFFFFFFu
#define P(x) ((uint32_t)(((uint64_t)(x) << 32) / 100u))   /* compile-time constants only */

/* ---- waveforms read from the manual's drawings (pp.20-21); edges/heights approximate ---- */
static const aj_wave_t pulse_w[2] AJ_DATA = {
    {1, 0, {P(50)}, {END}}, {1, 0, {P(75)}, {END}} };
static const aj_wave_t saw_w[5] AJ_DATA = {
    {1, 1, {0}, {END}},
    {2, 1, {P(44), P(80)}, {P(62), END}},
    {0, 1, {0}, {0}},
    {8, 1, {P(32),P(41),P(50),P(60),P(69),P(78),P(87),P(96)}, {P(35),P(44),P(53),P(63),P(72),P(81),P(90),END}},
    {4, 1, {P(50),P(60),P(86),P(96)}, {P(53),P(63),P(89),END}} };
static const aj_wave_t sub_w[6] AJ_DATA = {
    {1, 0, {P(50)}, {END}}, {1, 0, {P(75)}, {END}},
    {2, 0, {P(64),P(84)}, {P(75),END}}, {3, 0, {P(65),P(78),P(93)}, {P(70),P(83),END}},
    {1, 0, {P(50)}, {END}}, {1, 0, {P(75)}, {END}} };
static const uint8_t pmax[AJ_NPARAM] AJ_DATA = {3,3,3,3,5,5,3,3,3,3,1,127,127,15,127,127,127,127,127,127,15,15,127,15,127,127,127,127,127,127,127,127,127,15,127,12};

/* ---- helpers (all inlined: no calls outside SRAM, no division, no libc) ---- */
AJ_INLINE int32_t sat32(int64_t v) { return v > INT32_MAX ? INT32_MAX : v < INT32_MIN ? INT32_MIN : (int32_t)v; }
AJ_INLINE int32_t mul24(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 24); }
AJ_INLINE uint32_t div127(uint32_t x) { return (uint32_t)(((uint64_t)x * 33818641u) >> 32); }   /* x/127 */
AJ_INLINE uint32_t div15(uint32_t x)  { return (uint32_t)(((uint64_t)x * 286331154u) >> 32); }  /* x/15  */
AJ_INLINE void zero(void *p, unsigned bytes) { uint32_t *w = (uint32_t *)p; for (unsigned i = 0; i < bytes / 4; ++i) w[i] = 0; }

AJ_INLINE unsigned nlz(uint32_t x)                       /* x != 0 */
{
    unsigned n = 0;
    if (!(x >> 16)) { n += 16; x <<= 16; }
    if (!(x >> 24)) { n += 8;  x <<= 8; }
    if (!(x >> 28)) { n += 4;  x <<= 4; }
    if (!(x >> 30)) { n += 2;  x <<= 2; }
    if (!(x >> 31)) { n += 1; }
    return n;
}
AJ_INLINE uint64_t recip62(uint32_t x)                   /* 2^62 / x, x >= 1, error ~1e-5 */
{
    unsigned n = nlz(x); uint32_t m = x << n, fr = m & 0xFFFFFFu; unsigned i = (m >> 24) - 128u;
    uint32_t t0 = aj_recip[i], t1 = aj_recip[i + 1];
    uint32_t q = t0 - (uint32_t)(((uint64_t)(t0 - t1) * fr) >> 24);
    return (uint64_t)q << n;
}
AJ_INLINE uint32_t scale_oct(uint32_t base, int32_t o16)         /* base * 2^(o16/65536) */
{
    int32_t o = o16 >> 16; unsigned f = (unsigned)(o16 >> 8) & 255, r = (unsigned)o16 & 255;
    uint32_t m = aj_exp2[f] + (uint32_t)(((uint64_t)(aj_exp2[f + 1] - aj_exp2[f]) * r) >> 8);
    uint64_t v = ((uint64_t)base * m) >> 30;
    if (o >= 0) { if (o > 31) return END; v <<= o; if (v > END) return END; }
    else { if (-o > 63) return 0; v >>= -o; }
    return (uint32_t)v;
}
AJ_INLINE int32_t blep(uint32_t phr, uint32_t inc, uint64_t rcp)  /* polyBLEP, Q31 */
{
    if (phr < inc) {
        int64_t x = (int64_t)(((uint64_t)phr * rcp) >> 31), xx = (x * x) >> 31;
        return (int32_t)(2 * x - xx - ((int64_t)1 << 31));
    }
    uint32_t d = 0u - phr;
    if (d < inc) {
        int64_t x = -(int64_t)(((uint64_t)d * rcp) >> 31), xx = (x * x) >> 31;
        return (int32_t)(xx + 2 * x + ((int64_t)1 << 31));
    }
    return 0;
}
AJ_INLINE int32_t wave_mean(const aj_wave_t *w)                    /* Q31 */
{
    int64_t m = 0;
    for (int i = 0; i < w->n; ++i) {
        if (w->ramp) { int64_t s = w->s[i] >> 1, e = w->e[i] >> 1; m += (e * e - s * s) >> 32; }
        else m += (int64_t)(w->e[i] - w->s[i]) >> 1;
    }
    return (int32_t)m;
}
AJ_INLINE int32_t wave_eval(const aj_wave_t *w, uint32_t ph, uint32_t inc, uint64_t rcp, int32_t mean)
{
    int64_t v = 0, c = 0;
    for (int i = 0; i < w->n; ++i) {
        uint32_t s = w->s[i], e = w->e[i];
        if (ph >= s && ph < e) v = w->ramp ? (int64_t)(ph >> 1) : 0x7FFFFFFF;
        int64_t hs = w->ramp ? (int64_t)(s >> 1) : 0x7FFFFFFF, he = w->ramp ? (int64_t)(e >> 1) : 0x7FFFFFFF;
        if (hs) c += (hs * blep(ph - s, inc, rcp)) >> 32;
        if (he) c -= (he * blep(ph - e, inc, rcp)) >> 32;
    }
    return sat32(v + c - mean);
}
/* STUDIO: wave_eval with only the edges next to ph (the neighbours of ph among the wave's edges, around the cycle):
 * the same sum as wave_eval whenever the nonzero edges are more than 2 x inc apart (minsp, wave_minsp: then no other
 * edge is within inc, where blep is 0 anyway), so the same samples; else wave_eval itself. A window's start of height
 * 0 (a ramp from 0) is no edge. Saves most of the loop on the many-window waves (SAW 4: 8 windows) */
AJ_INLINE uint32_t wave_minsp(const aj_wave_t *w)
{
    uint32_t pos[16], m = 0xFFFFFFFFu, k = 0;
    for (int i = 0; i < w->n; ++i) {
        if (!w->ramp || (w->s[i] >> 1)) pos[k++] = w->s[i];
        pos[k++] = w->e[i];
    }
    if (k < 2) return 0;                                         /* (one edge: wave_eval is as cheap) */
    for (uint32_t i = 0; i < k; ++i) {
        uint32_t d = pos[(i + 1) % k] - pos[i];                  /* around the cycle (unsigned) */
        if (d < m) m = d;
    }
    return m;
}
AJ_INLINE int32_t wave_edge(const aj_wave_t *w, uint32_t at, int64_t h, uint32_t ph, uint32_t inc, uint64_t rcp)
{
    return h ? (int32_t)((h * blep(ph - at, inc, rcp)) >> 32) : 0;
}
AJ_INLINE int32_t wave_eval_n(const aj_wave_t *w, uint32_t ph, uint32_t inc, uint64_t rcp, int32_t mean, uint32_t minsp)
{
    int n = w->n, i = 0, r = w->ramp;
    int64_t v = 0, c = 0;
    if (n < 2 || minsp <= 2u * inc || inc > 0x7FFFFFFFu)
        return wave_eval(w, ph, inc, rcp, mean);
    while (i < n && ph >= w->e[i]) ++i;                          /* the first window ending after ph */
    if (i < n) {
        int64_t hs = r ? (int64_t)(w->s[i] >> 1) : 0x7FFFFFFF, he = r ? (int64_t)(w->e[i] >> 1) : 0x7FFFFFFF;
        int ip = i ? i - 1 : n - 1, in = i + 1 < n ? i + 1 : 0;
        int64_t hp = r ? (int64_t)(w->e[ip] >> 1) : 0x7FFFFFFF, hn = r ? (int64_t)(w->s[in] >> 1) : 0x7FFFFFFF;
        if (ph >= w->s[i]) {                                     /* inside window i: its start behind, its end ahead */
            v = r ? (int64_t)(ph >> 1) : 0x7FFFFFFF;
            c += wave_edge(w, w->s[i], hs, ph, inc, rcp);
            c -= wave_edge(w, w->e[i], he, ph, inc, rcp);
        } else {                                                 /* before it: the previous end behind, its start ahead */
            c -= wave_edge(w, w->e[ip], hp, ph, inc, rcp);
            c += wave_edge(w, w->s[i], hs, ph, inc, rcp);
        }
        (void)hn;
    } else {                                                     /* after the last: its end behind, the first start ahead */
        int64_t hl = r ? (int64_t)(w->e[n - 1] >> 1) : 0x7FFFFFFF, hf = r ? (int64_t)(w->s[0] >> 1) : 0x7FFFFFFF;
        c -= wave_edge(w, w->e[n - 1], hl, ph, inc, rcp);
        c += wave_edge(w, w->s[0], hf, ph, inc, rcp);
    }
    return sat32(v + c - mean);
}
AJ_INLINE int32_t softclip(int32_t x)                              /* x - x^3/6.75 */
{
    const int32_t lim = 12582912;
    if (x > lim) x = lim; else if (x < -lim) x = -lim;
    int64_t x2 = ((int64_t)x * x) >> 23, x3 = (x2 * x) >> 23;
    return (int32_t)(x - ((x3 * 4855) >> 15));
}
AJ_INLINE int32_t lfo_tri(uint32_t ph)                             /* Q24 +-1 */
{
    int64_t d = (int64_t)(int32_t)(ph ^ 0x80000000u); if (d < 0) d = -d;
    return (int32_t)((d >> 6) - Q24);
}
AJ_INLINE uint32_t rnd(aj_t *a) { a->rng ^= a->rng << 13; a->rng ^= a->rng >> 17; a->rng ^= a->rng << 5; return a->rng; }

/* ---- shared envelope: T1->L1 (lin), T2->L2 (lin), T3->L3 (exp), sustain, release T4->0 (exp) ---- */
AJ_INLINE void env_stage(const aj_patch_t *p, aj_env_t *e, unsigned stage)
{
    unsigned ti, li;
    switch (stage) {
    case 1:  ti = AJ_ENV_T1; li = AJ_ENV_L1; break;
    case 2:  ti = AJ_ENV_T2; li = AJ_ENV_L2; break;
    case 3:  ti = AJ_ENV_T3; li = AJ_ENV_L3; break;
    default: ti = AJ_ENV_T4; li = 255; break;
    }
    int32_t tgt = li == 255 ? 0 : (int32_t)aj_lvl[p->v[li] & 127];
    uint32_t n = scale_oct(aj_ticks[p->v[ti] & 127], e->key_o16); if (n < 1) n = 1;
    e->stage = (uint8_t)stage; e->target = tgt; e->left = n; e->start = e->cur; e->diff = tgt - e->cur;
    if (stage == 3 || stage == 5) {
        uint64_t c = recip62(n + 2) >> 30;                       /* 2^32/(n+2) */
        e->c = c > (1u << 30) ? (1u << 30) : (uint32_t)c; e->pos = 0; e->step = 0;
    } else { e->c = 0; e->pos = 0; e->step = recip62(n) >> 30; }  /* 2^32/n */
}
AJ_INLINE int32_t env_tick(const aj_patch_t *p, aj_env_t *e)
{
    if (e->stage == 0 || e->stage == 4) return e->cur;
    if (e->c) e->cur += (int32_t)(((int64_t)(e->target - e->cur) * e->c) >> 30);
    else { e->pos += e->step; if (e->pos > 0x100000000ull) e->pos = 0x100000000ull;
           e->cur = e->start + (int32_t)(((int64_t)e->diff * (int64_t)e->pos) >> 32); }
    if (--e->left == 0) {
        e->cur = e->target;
        if (e->stage == 5) e->stage = 0; else if (e->stage == 3) e->stage = 4; else env_stage(p, e, e->stage + 1u);
    }
    return e->cur;
}

/* ---- API ---- */
unsigned aj_param_max(unsigned idx) { return idx < AJ_NPARAM ? pmax[idx] : 0; }

void aj_patch_default(aj_patch_t *p)
{
    for (unsigned i = 0; i < AJ_NPARAM; ++i) p->v[i] = 0;
    p->v[AJ_PULSE] = 1; p->v[AJ_SAW] = 1; p->v[AJ_SUB_LEVEL] = 1; p->v[AJ_RANGE] = 1; p->v[AJ_HPF] = 1;
    p->v[AJ_VCF_FREQ] = 80; p->v[AJ_VCF_RES] = 20; p->v[AJ_VCF_ENV] = 60; p->v[AJ_VCF_KEY] = 8;
    p->v[AJ_VCA_LEVEL] = 100; p->v[AJ_LFO_RATE] = 50; p->v[AJ_BENDER_RANGE] = 2; p->v[AJ_CHORUS_RATE] = 30;
    p->v[AJ_ENV_T1] = 40; p->v[AJ_ENV_L1] = 127; p->v[AJ_ENV_T2] = 60; p->v[AJ_ENV_L2] = 90;
    p->v[AJ_ENV_T3] = 60; p->v[AJ_ENV_L3] = 70; p->v[AJ_ENV_T4] = 60;
}
void aj_init(aj_t *a)
{
    zero(a, sizeof *a); aj_patch_default(&a->p);
    a->rng = 0x9E3779B9u; a->mod_sens = 32; a->gain = 32768;
}
AJ_CODE void aj_set_param(aj_t *a, unsigned idx, unsigned value)
{ if (idx < AJ_NPARAM) a->p.v[idx] = (uint8_t)(value > pmax[idx] ? pmax[idx] : value); }
AJ_CODE void aj_load(aj_t *a, const uint8_t *vals)
{ for (unsigned i = 0; i < AJ_NPARAM; ++i) aj_set_param(a, i, vals[i]); }

AJ_CODE void aj_note_on(aj_t *a, unsigned note, unsigned vel)
{
    note &= 127; if (!vel) vel = 1; if (vel > 127) vel = 127;
    int idx = -1; uint32_t oldest = 0xFFFFFFFFu;
    for (int i = 0; i < AJ_VOICES; ++i) if (!a->v[i].active) { idx = i; break; }
    if (idx < 0) for (int i = 0; i < AJ_VOICES; ++i)          /* steal: the oldest released voice first */
        if (!a->v[i].held && !a->v[i].sus && a->v[i].age < oldest) { oldest = a->v[i].age; idx = i; }
    if (idx < 0) for (int i = 0; i < AJ_VOICES; ++i) if (a->v[i].age < oldest) { oldest = a->v[i].age; idx = i; }
    aj_voice_t *v = &a->v[idx];
    if (!v->active) zero(v, sizeof *v);
    v->active = 1; v->held = 1; v->sus = 0; v->note = (uint8_t)note; v->vel = (uint8_t)vel; v->age = ++a->age;
    v->dyn = (int32_t)aj_lvl[vel];
    v->lfo_fade = 0;
    v->env.key_o16 = -(((int32_t)note - 60) * (int32_t)a->p.v[AJ_ENV_KEY] * 182);   /* approx */
    env_stage(&a->p, &v->env, 1);
}
AJ_CODE void aj_note_off(aj_t *a, unsigned note)
{
    for (int i = 0; i < AJ_VOICES; ++i) {
        aj_voice_t *v = &a->v[i];
        if (v->active && v->held && v->note == note) {
            v->held = 0;
            if (a->hold) v->sus = 1; else env_stage(&a->p, &v->env, 5);
        }
    }
}
AJ_CODE void aj_hold(aj_t *a, unsigned on)
{
    a->hold = on ? 1 : 0;
    if (!on) for (int i = 0; i < AJ_VOICES; ++i) {
        aj_voice_t *v = &a->v[i];
        if (v->active && v->sus) { v->sus = 0; env_stage(&a->p, &v->env, 5); }
    }
}
AJ_CODE void aj_all_off(aj_t *a)
{
    a->hold = 0;
    for (int i = 0; i < AJ_VOICES; ++i) {
        aj_voice_t *v = &a->v[i];
        if (v->active && (v->held || v->sus)) { v->held = 0; v->sus = 0; env_stage(&a->p, &v->env, 5); }
    }
}
AJ_CODE void aj_voice_off(aj_t *a, unsigned idx)
{
    aj_voice_t *v;
    if (idx >= AJ_VOICES) return;
    v = &a->v[idx];
    if (v->active && (v->held || v->sus)) { v->held = 0; v->sus = 0; env_stage(&a->p, &v->env, 5); }
}
AJ_CODE int aj_last_voice(const aj_t *a)
{
    for (int i = 0; i < AJ_VOICES; ++i) if (a->v[i].active && a->v[i].age == a->age) return i;
    return -1;
}
AJ_CODE void aj_bend(aj_t *a, int v) { a->bend = v; }
AJ_CODE void aj_aftertouch(aj_t *a, unsigned v) { a->after = (int32_t)(v > 127 ? 127 : v); }
AJ_CODE void aj_modlever(aj_t *a, unsigned v) { a->modlever = (int32_t)(v > 127 ? 127 : v); }
AJ_CODE void aj_volume(aj_t *a, unsigned v) { a->gain = (int32_t)(aj_lvl[v > 127 ? 127 : v] >> 9); }   /* Q15 */

/* ---- control tick (every AJ_CTRL samples) ---- */
AJ_CODE static void control(aj_t *a)
{
    const uint8_t *p = a->p.v;
    a->lfo_ph += aj_lfo_inc[p[AJ_LFO_RATE] & 127]; a->lfo = lfo_tri(a->lfo_ph);
    a->pwm_ph += aj_pwm_inc[p[AJ_PWM_RATE] & 127]; a->pwm01 = (lfo_tri(a->pwm_ph) + Q24) >> 9;   /* Q16 0..1 */
    a->fade_step = (int32_t)aj_dly_step[p[AJ_LFO_DELAY] & 127];

    /* PW/PWM depth: 0 = 50% square, 127 = zero width. With PWM rate != 0 it sweeps 50% <-> depth setting. */
    uint64_t dq = p[AJ_PWM_RATE] ? (uint64_t)p[AJ_PW_PWM_DEPTH] * (uint32_t)a->pwm01 : (uint64_t)p[AJ_PW_PWM_DEPTH] << 16;
    uint32_t f = 0x80000000u - (uint32_t)((dq * 16909320u) >> 16);                 /* 1.0 = 2^31 */
    a->wp = p[AJ_PULSE] == 0 ? 0 : p[AJ_PULSE] < 3 ? &pulse_w[p[AJ_PULSE] - 1] : &a->dyn_p;
    a->ws = p[AJ_SAW] == 0 ? 0 : p[AJ_SAW] != 3 ? &saw_w[p[AJ_SAW] - 1] : &a->dyn_s;
    a->wsub = &sub_w[p[AJ_SUB]];
    if (p[AJ_PULSE] == 3) { a->dyn_p.ramp = 0; a->dyn_p.n = f ? 1 : 0; a->dyn_p.s[0] = 0u - f; a->dyn_p.e[0] = END; }
    if (p[AJ_SAW] == 3) {
        uint64_t f2 = ((uint64_t)f * f) >> 31, f4 = (f2 * f2) >> 31, f8 = (f4 * f4) >> 31;
        uint64_t w = (((uint64_t)f * 3435973837ull) >> 32) + ((f8 * 5153960755ull) >> 32); if (w > END) w = END;
        uint32_t e1 = P(60), s1 = e1 > w ? e1 - (uint32_t)w : 0, s2 = END - (uint32_t)w + 1u; if (s2 < e1) s2 = e1;
        a->dyn_s.ramp = 1; a->dyn_s.n = f ? 2 : 0; a->dyn_s.s[0] = s1; a->dyn_s.e[0] = e1; a->dyn_s.s[1] = s2; a->dyn_s.e[1] = END;
    }
    a->mp = a->wp ? wave_mean(a->wp) : 0; a->ms = a->ws ? wave_mean(a->ws) : 0; a->msub = wave_mean(a->wsub);
    a->sp_p = a->wp ? wave_minsp(a->wp) : 0; a->sp_s = a->ws ? wave_minsp(a->ws) : 0; a->sp_sub = wave_minsp(a->wsub);

    int32_t bend_o16 = (int32_t)(((int64_t)a->bend * p[AJ_BENDER_RANGE] * 683) >> 10);
    int32_t lfo_dco = (int32_t)p[AJ_DCO_LFO] + (int32_t)div127((uint32_t)(a->modlever * a->mod_sens)) + (int32_t)div15((uint32_t)(a->after * p[AJ_DCO_AFTER]));
    if (lfo_dco > 127) lfo_dco = 127;
    const unsigned rng = p[AJ_RANGE], sub_oct = p[AJ_SUB] >= 4 ? 2 : 1;
    const int32_t aft_vcf = (int32_t)(((uint32_t)(a->after * p[AJ_VCF_AFTER]) * 35229u) >> 16);
    const int32_t aft_vca = (int32_t)(((uint32_t)(a->after * p[AJ_VCA_AFTER]) * 1101u) >> 6);

    for (int i = 0; i < AJ_VOICES; ++i) {
        aj_voice_t *v = &a->v[i];
        if (!v->active) continue;
        int32_t env = env_tick(&a->p, &v->env);
        v->lfo_fade += a->fade_step; if (v->lfo_fade > 65536) v->lfo_fade = 65536;
        int32_t lfo = mul24(a->lfo, v->lfo_fade << 8);

        int32_t e_dco = (p[AJ_DCO_ENV_MODE] & 1) ? Q24 - env : env; if (p[AJ_DCO_ENV_MODE] >= 2) e_dco = mul24(e_dco, v->dyn);
        int32_t e_vcf; unsigned vm = p[AJ_VCF_ENV_MODE];
        if (vm == 3) e_vcf = v->dyn; else { e_vcf = (vm & 1) ? Q24 - env : env; if (vm == 2) e_vcf = mul24(e_vcf, v->dyn); }

        int32_t o16 = v->xo16 + bend_o16 + (int32_t)(((int64_t)lfo * lfo_dco * 10923) >> 31) + (int32_t)(((int64_t)e_dco * p[AJ_DCO_ENV] * 131072) >> 31);
        uint32_t inc = aj_inc[v->note];
        inc = rng == 0 ? (inc > 0x3FFFFFFFu ? 0x7FFFFFFFu : inc << 1) : rng == 2 ? inc >> 1 : rng == 3 ? inc >> 2 : inc;
        inc = scale_oct(inc, o16); if (inc > 0x7FFFFFFFu) inc = 0x7FFFFFFFu; if (inc < 4) inc = 4;
        v->inc = inc; v->rcp = recip62(inc);
        v->sinc = inc >> sub_oct; if (v->sinc < 2) v->sinc = 2; v->srcp = recip62(v->sinc);

        int32_t cv = (int32_t)p[AJ_VCF_FREQ] * 21
            + (int32_t)(((int64_t)e_vcf * p[AJ_VCF_ENV] * 2048) >> 31)
            + (int32_t)(((int64_t)lfo * p[AJ_VCF_LFO] * 512) >> 31)
            + ((((int32_t)v->note - 60) * (int32_t)p[AJ_VCF_KEY] * 364) >> 8)
            + aft_vcf + v->xcv;
        if (cv < 0) cv = 0;
        unsigned idx = (unsigned)cv >> 3, fr = (unsigned)cv & 7;
        if (idx >= AJ_LPG_N - 1) { idx = AJ_LPG_N - 2; fr = 7; }
        v->g = (int32_t)(aj_lpg[idx] + (((int64_t)aj_lpg[idx + 1] - aj_lpg[idx]) * (int)fr >> 3));
        v->k = (int32_t)((int64_t)p[AJ_VCF_RES] * 13950189);          /* up to ~3.3 in Q29 */

        unsigned am = p[AJ_VCA_ENV_MODE];
        int32_t base = (am & 1) ? (v->held ? Q24 : 0) : env; if (am >= 2) base = mul24(base, v->dyn);
        int32_t amp = (int32_t)div127((uint32_t)(base >> 9) * p[AJ_VCA_LEVEL]);
        amp = (int32_t)(((int64_t)amp * (32768 + aft_vca)) >> 15);
        v->amp_tgt = amp; v->amp_step = (amp - v->amp) >> 3;           /* AJ_CTRL == 8 */
    }
}

AJ_CODE static void voice_block(aj_t *a, aj_voice_t *v, int32_t *out)
{
    const uint8_t *p = a->p.v;
    const int32_t G = 11469;                                         /* ~0.35 mix headroom */
    const int32_t gsub = (int32_t)p[AJ_SUB_LEVEL] * 10923, gnoi = (int32_t)p[AJ_NOISE_LEVEL] * 10923;
    const unsigned hp = p[AJ_HPF] & 3;
    for (int n = 0; n < AJ_CTRL; ++n) {
        v->ph += v->inc; v->sph += v->sinc;
        int64_t m = 0;
        if (a->wp) m += wave_eval_n(a->wp, v->ph, v->inc, v->rcp, a->mp, a->sp_p);
        if (a->ws) m += wave_eval_n(a->ws, v->ph, v->inc, v->rcp, a->ms, a->sp_s);
        if (gsub)  m += ((int64_t)wave_eval_n(a->wsub, v->sph, v->sinc, v->srcp, a->msub, a->sp_sub) * gsub) >> 15;
        if (gnoi)  m += ((int64_t)(int32_t)rnd(a) * gnoi) >> 15;
        int32_t x = (int32_t)((m * G) >> 23);
        if (hp != 1) {
            v->hp += (int32_t)(((int64_t)aj_hpf_g[hp] * (x - v->hp)) >> 30);
            x = hp == 0 ? x + v->hp : x - v->hp;
        }
        int32_t in = softclip(x - (int32_t)(((int64_t)v->k * v->s[3]) >> 29)), prev = in;
        for (int j = 0; j < 4; ++j) { v->s[j] += (int32_t)(((int64_t)v->g * (prev - v->s[j])) >> 30); prev = v->s[j]; }
        v->amp += v->amp_step;
        out[n] += (int32_t)(((int64_t)prev * v->amp) >> 15);
    }
    v->amp = v->amp_tgt;
}

/* ---- stereo chorus: two BBD-style modulated taps, opposite LFO phase, light lowpass on wet ---- */
AJ_INLINE int32_t cho_tap(const aj_t *a, int32_t tri)
{
    uint32_t d = (148u << 16) + (uint32_t)((tri * 79) >> 8);          /* 3.4 ms +- 1.8 ms, Q16 samples */
    uint32_t rp = (a->cho_w << 16) - d;
    unsigned i = (rp >> 16) & (AJ_CHO_LEN - 1), j = (i + 1) & (AJ_CHO_LEN - 1);
    int32_t s0 = a->cho_buf[i], s1 = a->cho_buf[j];
    return s0 + (int32_t)(((int64_t)(s1 - s0) * (int32_t)(rp & 0xFFFFu)) >> 16);
}
AJ_CODE static void chorus_block(aj_t *a, const int32_t *x, int32_t *out)
{
    const int32_t target = a->p.v[AJ_CHORUS] ? 65536 : 0;
    const uint32_t inc = aj_cho_inc[a->p.v[AJ_CHORUS_RATE] & 127];
    for (int n = 0; n < AJ_CTRL; ++n) {
        int32_t s = (int32_t)(((int64_t)x[n] * a->gain) >> 15);
        a->cho_buf[a->cho_w & (AJ_CHO_LEN - 1)] = s;
        a->cho_ph += inc;
        if (a->cho_mix < target) { a->cho_mix += 128; if (a->cho_mix > target) a->cho_mix = target; }
        else if (a->cho_mix > target) { a->cho_mix -= 128; if (a->cho_mix < target) a->cho_mix = target; }
        int32_t l = s, r = s;
        if (a->cho_mix) {
            int32_t wl = cho_tap(a, lfo_tri(a->cho_ph)), wr = cho_tap(a, lfo_tri(a->cho_ph + 0x80000000u));
            a->cho_lpL += (int32_t)(((int64_t)(wl - a->cho_lpL) * 20000) >> 15);
            a->cho_lpR += (int32_t)(((int64_t)(wr - a->cho_lpR) * 20000) >> 15);
            l = s + (int32_t)(((int64_t)(a->cho_lpL - s) * a->cho_mix) >> 17);      /* full mix = 50/50 */
            r = s + (int32_t)(((int64_t)(a->cho_lpR - s) * a->cho_mix) >> 17);
        }
        a->cho_w++;
        out[2 * n] += l; out[2 * n + 1] += r;
    }
}

AJ_CODE void aj_render(aj_t *a, int32_t *out, unsigned frames)
{
    int32_t mono[AJ_CTRL];
    for (unsigned done = 0; done + AJ_CTRL <= frames; done += AJ_CTRL) {
        unsigned live = 0;
        for (int i = 0; i < AJ_VOICES; ++i) live |= a->v[i].active;
        if (live) a->tail = 1536; else if (a->tail) a->tail = a->tail > AJ_CTRL ? a->tail - AJ_CTRL : 0; else continue;
        for (int n = 0; n < AJ_CTRL; ++n) mono[n] = 0;
        control(a);
        for (int i = 0; i < AJ_VOICES; ++i) {
            aj_voice_t *v = &a->v[i];
            if (!v->active) continue;
            voice_block(a, v, mono);
            if ((a->p.v[AJ_VCA_ENV_MODE] & 1) ? (!v->held && !v->sus && v->amp == 0) : (v->env.stage == 0)) v->active = 0;
        }
        chorus_block(a, mono, out + 2 * done);
    }
}
