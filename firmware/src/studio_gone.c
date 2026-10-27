/* SPDX-License-Identifier: GPL-3.0-only */
/* STUDIO: the Felucca engines this firmware does not build (core.h). Each keeps its name in the source as a silent,
 * never offered placeholder (as ENG_FM4_GONE stands for DIGITAL): the code that names one (a page shown for ANALOG
 * only, WHEEL's drawbar graph, the conversions of PHYS sounds in old stores) compiles and finds nothing. A sound of one
 * that arrives anyway (an old project, a user preset, the editor) plays nothing; set_engine and the loaders give such a
 * track JUNO (engines.c eng_fix). */
static void gone_note_on(struct track *t, voice_t *v) { (void)t; (void)v; }
static void gone_render(struct track *t, voice_t *v, int32_t *out, uint32_t n, const vmod_t *m)
{
    (void)t; (void)out; (void)n; (void)m;
    v->active = 0;
}
#define GONE_PD {"-", F_INT, 0, 0, 0, 0, 0}
#define GONE_ENGINE(sym, nm) static const engine_t sym = {                                                             \
    .name = nm, .page_title = {"-", "-"},                                                                             \
    .edit = {GONE_PD, GONE_PD, GONE_PD, GONE_PD, GONE_PD, GONE_PD, GONE_PD, GONE_PD},                                 \
    .note_on = gone_note_on, .render = gone_render, .knob = {P_E0, P_E1, P_E2, P_E3}}
#ifndef FELUCCA_HOST_CLASSIC
GONE_ENGINE(ENG_ANALOG, "ANALOG");
#endif
GONE_ENGINE(ENG_PHASE, "PHASE");
GONE_ENGINE(ENG_LOFI, "LOFI");
#ifndef FELUCCA_HOST_CLASSIC
GONE_ENGINE(ENG_FORMANT, "VOICE");
#endif
GONE_ENGINE(ENG_TRIO, "TRIO");
GONE_ENGINE(ENG_WHEEL, "WHEEL");
GONE_ENGINE(ENG_GRAIN, "GRAIN");
GONE_ENGINE(ENG_PHYS, "PHYS");
GONE_ENGINE(ENG_NOISE, "NOISE");
#if !FELUCCA_SLICE
GONE_ENGINE(ENG_SLICE_GONE, "SLICE");
#endif
#if !FELUCCA_FM6
GONE_ENGINE(ENG_FM6_GONE, "FM6");
#endif
#undef GONE_ENGINE
#undef GONE_PD

/* what the rest of the firmware asked of them */
static const char *const N_TRIO_MODE[] = {"-"};     /* (icons.c: TRIO's MODE icons) */
static const char *const N_NOISE_MODE[] = {"-"};    /* (icons.c: NOISE's MODE icons) */
static const param_desc_t NOISE_CLK = {"-", F_INT, 0, 0, 0, 0, 0};   /* (icons.c: NOISE's CLK descriptor) */
static int32_t drw_level(const int16_t *p, uint32_t k) { (void)p; (void)k; return 0; }   /* (ui_graph.c: WHEEL's bars) */
static void phys_legacy(int16_t *e) { (void)e; }    /* (project.c, upreset.c: PHYS's retired DUST model) */
