/* alpha_juno.h - Roland Alpha Juno-1 (JU-1) style synth engine. Integer only, division-free
 * in the audio path. Structure and parameters follow the JU-1 owner's manual (1985):
 *   6 voices, 1 DCO/voice (pulse 0-3, sawtooth 0-5, sub 0-5, noise), 4-position HPF,
 *   24 dB/oct resonant LPF, one 4-time/3-level ENV shared by DCO/VCF/VCA, LFO with delay,
 *   separate PWM LFO, stereo chorus.  Parameter numbers 0..35 are the manual's SysEx IPR numbers.
 * Output: signed Q23 stereo (interleaved L,R), ADDED into the caller's buffer. 44.1 kHz.
 * All audio-path code/data is placed in internal SRAM on the FM-1 (AJ_CODE / AJ_DATA). */
#ifndef ALPHA_JUNO_H
#define ALPHA_JUNO_H
#include <stdint.h>
#define AJ_VOICES 6
#define AJ_CTRL   8
#define AJ_CHO_LEN 512            /* chorus delay line (power of two) */

#if (defined(__PI32V2__) || defined(__PI32__)) && !defined(FM1_HOST_TEST) && !defined(AJ_PLAIN)   /* AJ_PLAIN: Felucca runs it from XIP */
#define AJ_CODE   __attribute__((section(".volatile_ram_code"), noinline, used))
#define AJ_DATA   __attribute__((section(".volatile_ram.fm1"), aligned(4)))
#define AJ_INLINE static inline __attribute__((always_inline))
#else
#define AJ_CODE
#define AJ_DATA
#define AJ_INLINE static inline
#endif

enum {  /* == manual's individual-parameter (IPR) numbers */
    AJ_DCO_ENV_MODE = 0,  /* 0 normal 1 inverted 2 normal+dynamics 3 inverted+dynamics */
    AJ_VCF_ENV_MODE,      /* 0 normal 1 inverted 2 normal+dynamics 3 dynamics only      */
    AJ_VCA_ENV_MODE,      /* 0 ENV 1 GATE 2 ENV+dynamics 3 GATE+dynamics                */
    AJ_PULSE,             /* 0 off 1 square 2 narrow 3 variable (PW/PWM)                */
    AJ_SAW,               /* 0 off 1..5 (3 variable)                                    */
    AJ_SUB,               /* 0..5 (0-3: 1 oct lower, 4-5: 2 oct lower)                  */
    AJ_RANGE,             /* 0=4' 1=8' 2=16' 3=32'                                      */
    AJ_SUB_LEVEL, AJ_NOISE_LEVEL,   /* 0..3 */
    AJ_HPF,               /* 0 bass boost 1 off 2 low cutoff 3 higher cutoff            */
    AJ_CHORUS,            /* 0 off 1 on */
    AJ_DCO_LFO, AJ_DCO_ENV, AJ_DCO_AFTER /*0..15*/, AJ_PW_PWM_DEPTH, AJ_PWM_RATE,
    AJ_VCF_FREQ, AJ_VCF_RES, AJ_VCF_LFO, AJ_VCF_ENV, AJ_VCF_KEY /*0..15*/, AJ_VCF_AFTER /*0..15*/,
    AJ_VCA_LEVEL, AJ_VCA_AFTER /*0..15*/,
    AJ_LFO_RATE, AJ_LFO_DELAY,
    AJ_ENV_T1, AJ_ENV_L1, AJ_ENV_T2, AJ_ENV_L2, AJ_ENV_T3, AJ_ENV_L3, AJ_ENV_T4,
    AJ_ENV_KEY /*0..15*/, AJ_CHORUS_RATE, AJ_BENDER_RANGE /*0..12*/,
    AJ_NPARAM
};

typedef struct { uint8_t v[AJ_NPARAM]; } aj_patch_t;
typedef struct { uint8_t n, ramp; uint32_t s[8], e[8]; } aj_wave_t;   /* gate windows over one cycle */
typedef struct {
    uint8_t stage; int32_t cur, start, diff, target, key_o16;
    uint32_t left, c; uint64_t pos, step;
} aj_env_t;
typedef struct {
    uint8_t active, held, sus, note, vel; uint32_t age;
    uint32_t ph, sph, inc, sinc; uint64_t rcp, srcp;
    int32_t hp, s[4], g, k, amp, amp_step, amp_tgt, dyn, lfo_fade;
    int32_t xo16, xcv;           /* host offsets: pitch (1/65536 octave), VCF CV (FREQ x 21 units); 0 = none */
    aj_env_t env;
} aj_voice_t;
typedef struct {
    aj_patch_t p; aj_voice_t v[AJ_VOICES];
    uint32_t lfo_ph, pwm_ph, rng, age, tail;
    int32_t lfo, pwm01, bend, after, modlever, mod_sens, fade_step, gain, hold;
    aj_wave_t dyn_p, dyn_s;
    const aj_wave_t *wp, *ws, *wsub; int32_t mp, ms, msub;
    uint32_t sp_p, sp_s, sp_sub;   /* STUDIO: the waves' smallest edge spacing (wave_minsp: the fast wave_eval_n) */
    int32_t cho_buf[AJ_CHO_LEN]; uint32_t cho_w, cho_ph; int32_t cho_lpL, cho_lpR, cho_mix;
} aj_t;

unsigned aj_param_max(unsigned idx);
void aj_init(aj_t *a);                                   /* control side, before audio runs */
void aj_patch_default(aj_patch_t *p);
AJ_CODE void aj_set_param(aj_t *a, unsigned idx, unsigned value);   /* clamps to the manual's range */
AJ_CODE void aj_load(aj_t *a, const uint8_t *values36);             /* parameters 0..35 */
AJ_CODE void aj_note_on(aj_t *a, unsigned note, unsigned velocity);
AJ_CODE void aj_note_off(aj_t *a, unsigned note);
AJ_CODE void aj_all_off(aj_t *a);
AJ_CODE void aj_voice_off(aj_t *a, unsigned idx);   /* release one voice (the host lost its note) */
AJ_CODE int aj_last_voice(const aj_t *a);            /* the voice the latest aj_note_on took */
AJ_CODE void aj_hold(aj_t *a, unsigned on);              /* sustain pedal */
AJ_CODE void aj_bend(aj_t *a, int value14);              /* -8192..8191  */
AJ_CODE void aj_aftertouch(aj_t *a, unsigned v);         /* 0..127       */
AJ_CODE void aj_modlever(aj_t *a, unsigned v);           /* 0..127       */
AJ_CODE void aj_volume(aj_t *a, unsigned v);             /* 0..127       */
AJ_CODE void aj_render(aj_t *a, int32_t *stereo_out, unsigned frames);   /* frames multiple of AJ_CTRL; ADDS */
#endif
