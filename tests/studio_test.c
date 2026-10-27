/* SPDX-License-Identifier: GPL-3.0-only */
/* Host test of the STUDIO firmware's own parts, the same sources as the firmware (hostsim.c):
 *   build/host/studio_test [DEMO_DIR]      (run_host_tests.sh builds and runs it; demos in build/studio_demo/)
 * 1. STRIP (strip.c): at rest it changes nothing (bit for bit); each band's response measured with sines through a
 *    whole part (HIGH at 12 kHz, LOW at 80 Hz, MID at its FREQ: +-15 dB within 1.5 dB, flat far away), LOW CUT
 *    (75 Hz -3 dB, 18 dB / octave below), GAIN (+-20 dB on a quiet signal; a hot one rounded by the preamp: bounded,
 *    no DC), a stereo part (JUNO) left and right alike, values kept by a sound load and recorded by motion.
 * 2. the engines: JUNO, ACID, TR909 sound, stay bounded, end their voices; ACID's slide (no new attack), accent
 *    (louder, brighter); TR909's lanes (each GM lane note strikes its voice), one voice of the budget for the kit.
 * 3. old stores: a retired engine's sound loads as JUNO, DRUM's number is TR909's.
 * 4. demos: juno_presets.wav, acid_line.wav, tr909_beat.wav, strip_sweep.wav, studio_mix.wav. */
#define main hostsim_main
#include "hostsim.c"
#undef main

static int bad;
static void check(const char *what, int ok)
{
    printf("studio: %-78s %s\n", what, ok ? "ok" : "FAIL");
    bad += !ok;
}

static void silence_all(void)
{
    uint32_t k, i;
    for (k = 0; k < NTRK; k++) {
        trk_all_off(&trk[k]);
        for (i = 0; i < NVOICE; i++)
            voice_kill(&trk[k].v[i]);
    }
    for (i = 0; i < 400u; i++) {
        int32_t o[2 * CTL];
        mix_block(o, CTL);
    }
}

/* ------------------------------------------------------------------------------------------- STRIP --- */
/* a sine of f Hz through the strip of track 0 as a part block would carry it (amplitude a), the strip alone: the
 * level of its last half in dB against the input's */
static double strip_gain_db(const int16_t *pv, double f, double a, int stereo)
{
    track_t *t = &trk[0];
    int32_t b[CTL], d[CTL];
    double in2 = 0, out2 = 0, ph = 0;
    uint32_t blk, i, nblk = (uint32_t)(0.6 * FS / CTL);
    memset(&mx_strip[0], 0, sizeof mx_strip[0]);
    memcpy(t->p, pv, sizeof t->p);
    for (blk = 0; blk < nblk; blk++) {
        for (i = 0; i < CTL; i++) {
            double x = a * sin(ph);
            ph += 2 * M_PI * f / FS;
            b[i] = (int32_t)(stereo ? 2 * x : x);
            d[i] = 0;
            if (blk >= nblk / 2u)
                in2 += x * x;
        }
        track_chstrip(t, b, stereo ? d : 0, CTL, 0);
        track_chstrip(t, b, stereo ? d : 0, CTL, 1);
        for (i = 0; i < CTL; i++) {
            double y = stereo ? (b[i] + d[i]) * 0.5 : b[i];   /* the left */
            if (blk >= nblk / 2u)
                out2 += y * y;
        }
    }
    return 10 * log10((out2 + 1e-9) / (in2 + 1e-9));
}

static void strip_tests(void)
{
    int16_t p[P_COUNT];
    uint32_t i;
    double g;
    host_tracks_init();
    memcpy(p, trk[0].p, sizeof p);
    check("STRIP: every store's 0 is the strip at rest (GAIN LOCUT LOW MID HIGH)",
          TP[P_MX_GAIN].def == 0 && TP[P_MX_LCUT].def == 0 && TP[P_MX_LOW].def == 0 && TP[P_MX_MID].def == 0 &&
          TP[P_MX_HIGH].def == 0);
    {   /* at rest: nothing changes, bit for bit */
        int32_t b[CTL], b0[CTL];
        for (i = 0; i < CTL; i++)
            b[i] = b0[i] = (int32_t)(90000 * sin(i * 0.3));
        memcpy(trk[0].p, p, sizeof p);
        track_chstrip(&trk[0], b, 0, CTL, 0);
        track_chstrip(&trk[0], b, 0, CTL, 1);
        check("STRIP at rest: the part's samples bit for bit", !memcmp(b, b0, sizeof b));
    }
    p[P_MX_HIGH] = 15;
    g = strip_gain_db(p, 12000, 4000, 0);
    check("HIGH +15: the shelf's corner at 12 kHz (+7.5 dB, within 1.5)", fabs(g - 7.5) < 1.5);
    g = strip_gain_db(p, 20000, 4000, 0);
    check("HIGH +15: +15 dB (within 1) at 20 kHz", fabs(g - 15) < 1.0);
    g = strip_gain_db(p, 200, 4000, 0);
    check("HIGH +15: flat (within 0.5 dB) at 200 Hz", fabs(g) < 0.5);
    p[P_MX_HIGH] = -15;
    g = strip_gain_db(p, 20000, 4000, 0);
    check("HIGH -15: -15 dB (within 1) at 20 kHz", fabs(g + 15) < 1.0);
    p[P_MX_HIGH] = 0;
    p[P_MX_LOW] = 15;
    g = strip_gain_db(p, 30, 4000, 0);
    check("LOW +15: +15 dB (within 1.5) at 30 Hz", fabs(g - 15) < 1.5);
    g = strip_gain_db(p, 2000, 4000, 0);
    check("LOW +15: flat (within 0.5 dB) at 2 kHz", fabs(g) < 0.5);
    p[P_MX_LOW] = -15;
    g = strip_gain_db(p, 30, 4000, 0);
    check("LOW -15: -15 dB (within 1.5) at 30 Hz", fabs(g + 15) < 1.5);
    p[P_MX_LOW] = 0;
    {
        static const int32_t FV[3] = {0, 64, 127};
        uint32_t k;
        int ok = 1;
        for (k = 0; k < 3u; k++) {
            double f = mx_mid_hz(FV[k]);
            p[P_MX_FREQ] = (int16_t)FV[k];
            p[P_MX_MID] = 15;
            ok &= fabs(strip_gain_db(p, f, 4000, 0) - 15) < 1.0;
            p[P_MX_MID] = -15;
            ok &= fabs(strip_gain_db(p, f, 4000, 0) + 15) < 1.0;
            ok &= fabs(strip_gain_db(p, f * 16 < 20000 ? f * 16 : f / 16, 4000, 0)) < 1.5;
        }
        check("MID +-15 at FREQ 100 Hz, ~900 Hz, 8 kHz: +-15 dB there, flat 4 octaves away", ok);
        check("MID FREQ: 100 Hz .. 8 kHz", fabs(mx_mid_hz(0) - 100) < 1 && fabs(mx_mid_hz(127) - 8000) < 40);
    }
    p[P_MX_MID] = 0;
    p[P_MX_LCUT] = 1;
    {
        double g75 = strip_gain_db(p, 75, 4000, 0), g37 = strip_gain_db(p, 37.5, 4000, 0);
        double g1k = strip_gain_db(p, 1000, 4000, 0);
        check("LOW CUT: -3 dB (within 1) at 75 Hz", fabs(g75 + 3) < 1.0);
        check("LOW CUT: 18 dB / octave: -18 dB (within 3) an octave below", fabs(g37 - g75 + 15) < 3.0);
        check("LOW CUT: flat (within 0.3 dB) at 1 kHz", fabs(g1k) < 0.3);
    }
    p[P_MX_LCUT] = 0;
    p[P_MX_GAIN] = 20;
    check("GAIN +20: +20 dB (within 0.3) on a quiet signal", fabs(strip_gain_db(p, 440, 1000, 0) - 20) < 0.3);
    p[P_MX_GAIN] = -20;
    check("GAIN -20: -20 dB (within 0.3)", fabs(strip_gain_db(p, 440, 30000, 0) + 20) < 0.3);
    p[P_MX_GAIN] = 20;
    {   /* driven: bounded by the preamp's knee, symmetric (no DC) */
        track_t *t = &trk[0];
        int32_t b[CTL], mx = 0;
        double dc = 0, ph = 0;
        uint32_t blk;
        memset(&mx_strip[0], 0, sizeof mx_strip[0]);
        memcpy(t->p, p, sizeof t->p);
        for (blk = 0; blk < 2000u; blk++) {
            for (i = 0; i < CTL; i++) {
                b[i] = (int32_t)(60000 * sin(ph));
                ph += 2 * M_PI / 400.0;               /* 110.25 Hz: the 32000 samples measured are 80 cycles */
            }
            track_chstrip(t, b, 0, CTL, 0);
            for (i = 0; i < CTL; i++) {
                int32_t a = b[i] < 0 ? -b[i] : b[i];
                mx = a > mx ? a : mx;
                if (blk >= 1000u)
                    dc += b[i];
            }
        }
        dc /= 1000.0 * CTL;
        check("GAIN +20 into the preamp: rounded off, peak below the knee + ceiling", mx <= (int32_t)(MX_KNEE + MX_CEIL));
        check("GAIN +20 into the preamp: no DC (|mean| < 0.1 % of the peak)", fabs(dc) < 0.001 * mx);
    }
    p[P_MX_GAIN] = 0;
    p[P_MX_HIGH] = 12;
    p[P_MX_LOW] = -9;
    {
        double gm = strip_gain_db(p, 15000, 4000, 0), gs = strip_gain_db(p, 15000, 4000, 1);
        check("a stereo part (SPREAD / JUNO): its left as a mono part's (within 0.1 dB)", fabs(gm - gs) < 0.1);
    }
    {   /* kept by a sound load; motion takes them */
        track_t *t = &trk[0];
        host_tracks_init();
        t->p[P_MX_HIGH] = 7;
        t->p[P_MX_GAIN] = -5;
        check("STRIP values can be automated / locked (motion_param), are not engine ids",
              motion_param(P_MX_MID) && motion_param(P_MX_GAIN) && !motion_engine_id(P_MX_LOW));
    }
}

/* ----------------------------------------------------------------------------------------- engines --- */
static int32_t run_peak(uint32_t blocks, int64_t *dcsum)
{
    int32_t o[2 * CTL], pk = 0;
    uint32_t b, i;
    for (b = 0; b < blocks; b++) {
        mix_block(o, CTL);
        for (i = 0; i < 2u * CTL; i++) {
            int32_t a = o[i] < 0 ? -o[i] : o[i];
            pk = a > pk ? a : pk;
            if (dcsum)
                *dcsum += o[i];
        }
    }
    return pk;
}
static int track_free(const track_t *t)
{
    uint32_t i;
    for (i = 0; i < NVOICE; i++)
        if (t->v[i].active)
            return 0;
    return 1;
}

static void engine_tests(void)
{
    track_t *t = &trk[0];
    uint32_t pi, k;
    int ok, ended;
    host_tracks_init();
    check("engines offered: JUNO ACID TR909 (eng_ok), the rest reserved",
          eng_ok(ENGI_JUNO) && eng_ok(ENGI_ACID) && eng_ok(ENGI_TR909) && !eng_ok(5) && !eng_ok(ENGI_FM6) &&
          !eng_ok(ENGI_DIGITAL) && NENG_SHOWN == 3);
    for (k = 0; k < 3u; k++) {
        static const uint32_t E[3] = {ENGI_JUNO, ENGI_ACID, ENGI_TR909};
        static const char *const N[3] = {"JUNO", "ACID", "TR909"};
        const engine_t *e = ENGINES[E[k]];
        char msg[96];
        ok = 1;
        ended = 1;
        for (pi = 0; pi < e->npresets; pi++) {
            int32_t pk;
            silence_all();
            host_tracks_init();
            host_preset(t, E[k], pi);
            if (E[k] == ENGI_TR909) {
                trk_note_on(t, 36, 100);
                trk_note_on(t, 42, 100);
            } else {
                trk_note_on(t, 48, 100);
                if (!e->poly || e->poly > 1)
                    trk_note_on(t, 55, 100);
            }
            pk = run_peak(600, 0);
            trk_all_off(t);
            run_peak((uint32_t)(4 * FS / CTL), 0);
            ok &= pk > 1500 && pk < 32767;
            ended &= track_free(t);
            if (!(pk > 1500 && pk < 32767))
                printf("studio:   %s %s peak %d\n", N[k], e->presets[pi].name, pk);
        }
        snprintf(msg, sizeof msg, "%s: every preset sounds and stays below full scale (%u presets)", N[k], e->npresets);
        check(msg, ok);
        snprintf(msg, sizeof msg, "%s: every preset's voices end after the release", N[k]);
        check(msg, ended);
    }
    {   /* ACID: a slide does not attack again; an accent is louder */
        int32_t p0, p1;
        silence_all();
        host_tracks_init();
        host_preset(t, ENGI_ACID, 0);
        trk_note_on(t, 45, 90);
        run_peak(200, 0);
        trk_note_on(t, 57, 90);                       /* held over: a slide */
        run_peak(1, 0);
        check("ACID: a note played over a held one slides (the pitch glides, the 303 not retriggered)",
              ACID_MEM(t)->b.gate && ACID_MEM(t)->note == 57 &&
              ACID_MEM(t)->b.osc_freq - ACID_MEM(t)->b.slew_y > 20.0f);
        trk_all_off(t);
        silence_all();
        host_preset(t, ENGI_ACID, 0);
        trk_note_on(t, 45, 90);
        p0 = run_peak(100, 0);
        trk_all_off(t);
        silence_all();
        host_preset(t, ENGI_ACID, 0);
        trk_note_on(t, 45, 127);
        p1 = run_peak(100, 0);
        trk_all_off(t);
        check("ACID: an accented note (velocity > 110) is louder", p1 > p0 + p0 / 8);
    }
    {   /* TR909: each grid lane strikes its voice; one voice of the budget */
        static const int V[NLANE] = {DR_BD, DR_SD, DR_CP, DR_CH, DR_OH, DR_MT, DR_RS, DR_RD};
        uint32_t l, most = 0;
        ok = 1;
        for (l = 0; l < NLANE; l++)
            ok &= tr909_voice(DRUM_LANE_NOTE[l]) == V[l];
        check("TR909: the grid's 8 lanes strike BD SD CP CH OH MT RS RD", ok);
        silence_all();
        host_tracks_init();
        host_preset(t, ENGI_TR909, 0);
        for (k = 0; k < 64u; k++) {
            trk_note_on(t, DRUM_LANE_NOTE[k % NLANE], 100);
            run_peak(4, 0);
            {
                uint32_t i, n = 0;
                for (i = 0; i < NVOICE; i++)
                    n += t->v[i].active && t->v[i].stage != 4u;
                most = n > most ? n : most;
            }
        }
        run_peak((uint32_t)(5 * FS / CTL), 0);
        check("TR909: a hit at a time keeps at most 2 voices sounding (the kit renders as one)", most <= 2u);
        check("TR909: the kit's voice ends when it has rung out", track_free(t));
        check("TR909 has DRUM's number (old drum sounds and patterns: the 909 on the grid)",
              ENGI_TR909 == ENGI_DRUM && drum_track(t));
    }
    {   /* old stores */
        check("a retired engine's sound (ANALOG, FM6, DIGITAL, VOICE) loads as JUNO",
              eng_fix(5) == ENGI_JUNO && eng_fix(ENGI_FM6) == ENGI_JUNO && eng_fix(ENGI_DIGITAL) == ENGI_JUNO &&
              eng_fix(6) == ENGI_JUNO && eng_fix(ENGI_TR909) == ENGI_TR909 && eng_fix(ENGI_SAMPLE) == ENGI_SAMPLE);
    }
}

/* ------------------------------------------------------------------------------------------- demos --- */
static void demo(const char *dir, const char *name, void (*setup)(void), uint32_t seconds)
{
    char path[512];
    FILE *f;
    uint32_t b, i, nb = (uint32_t)(seconds * FS / CTL);
    int32_t o[2 * CTL];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    f = fopen(path, "wb");
    if (!f)
        return;
    silence_all();
    host_tracks_init();
    setup();
    wav_hdr(f, nb * CTL);
    for (b = 0; b < nb; b++) {
        mix_block(o, CTL);
        for (i = 0; i < CTL; i++)
            wav_put(f, o[2 * i], o[2 * i + 1]);
    }
    fclose(f);
    song.playing = 0;
    printf("studio: demo %s\n", path);
}
static void pat16(track_t *t, const uint8_t *n, const uint8_t *fl)
{
    uint32_t i;
    for (i = 0; i < 16u; i++)
        if (n[i])
            put_step(t, i, 1, &n[i], (fl[i] & 4u) ? ST_TIE : ST_NOTE, fl[i] & 3u);
        else if (fl[i] & 4u)
            put_step(t, i, 0, 0, ST_TIE, 0);
    t->p[P_SLEN] = 16;
}
static const uint8_t ACIDN[16] = {33, 33, 45, 33, 0, 36, 33, 43, 33, 0, 45, 40, 33, 36, 0, 38};
static const uint8_t ACIDF[16] = {1, 0, 2, 0, 0, 0, 1, 2, 0, 0, 1, 0, 0, 2, 0, 1};
static void beat909(track_t *t)
{
    uint32_t i;
    for (i = 0; i < 16u; i++) {
        step_t *s = &t->step[i];
        memset(s, 0, sizeof *s);
        s->time = ST_NOTE;
        s->vel = 96;
        if (i % 4u == 0u) s->hit |= 1u;               /* BD */
        if (i % 8u == 4u) s->hit |= 2u | 4u;          /* SD + CP */
        if (i % 2u == 1u) s->hit |= 1u << 3;          /* CH */
        if (i % 4u == 2u) s->hit |= 1u << 4;          /* OH */
        if (i == 14u) s->hit |= 1u << 6;              /* RS */
        if (i == 0u) s->acc |= 1u;
    }
    t->p[P_SLEN] = 16;
}
static void setup_acid(void)
{
    host_preset(&trk[0], ENGI_ACID, 1);
    pat16(&trk[0], ACIDN, ACIDF);
    song.g[G_BPM] = 128;
    song.playing = 1;
}
static void setup_909(void)
{
    host_preset(&trk[3], ENGI_TR909, 0);
    beat909(&trk[3]);
    song.g[G_BPM] = 128;
    song.playing = 1;
}
static void setup_juno(void)
{
    static const uint8_t PADN[16] = {57, 0, 0, 0, 0, 0, 0, 0, 53, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t PADF[16] = {0, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 4, 0};
    host_preset(&trk[1], ENGI_JUNO, 7);               /* HOOVER */
    pat16(&trk[1], PADN, PADF);
    song.g[G_BPM] = 128;
    song.playing = 1;
}
static void setup_mix(void)
{
    static const uint8_t PADN[16] = {57, 0, 0, 0, 0, 0, 0, 0, 53, 0, 0, 0, 0, 0, 0, 0};
    static const uint8_t PADF[16] = {0, 4, 4, 4, 4, 4, 4, 0, 0, 4, 4, 4, 4, 4, 4, 0};
    host_preset(&trk[0], ENGI_ACID, 0);
    pat16(&trk[0], ACIDN, ACIDF);
    host_preset(&trk[1], ENGI_JUNO, 2);
    pat16(&trk[1], PADN, PADF);
    trk[1].p[P_LEVEL] = 88;
    trk[1].p[P_MX_LCUT] = 1;                          /* the pad out of the bass's way */
    trk[1].p[P_MX_LOW] = -6;
    trk[1].p[P_MX_HIGH] = 4;
    host_preset(&trk[3], ENGI_TR909, 0);
    beat909(&trk[3]);
    trk[3].p[P_MX_GAIN] = 6;                          /* the drums a little hot into the preamp */
    trk[3].p[P_MX_MID] = 3;
    trk[3].p[P_MX_FREQ] = 90;
    song.g[G_BPM] = 128;
    song.playing = 1;
}
static void setup_sweep(void)
{
    setup_acid();
    trk[0].p[P_MX_MID] = 12;                          /* the MID swept by automation would be nicer; a fixed boost */
    trk[0].p[P_MX_FREQ] = 70;
}

int main(int argc, char **argv)
{
    strip_tests();
    engine_tests();
    if (argc > 1) {
        demo(argv[1], "acid_line.wav", setup_acid, 8);
        demo(argv[1], "tr909_beat.wav", setup_909, 8);
        demo(argv[1], "juno_hoover.wav", setup_juno, 8);
        demo(argv[1], "strip_mid_boost.wav", setup_sweep, 8);
        demo(argv[1], "studio_mix.wav", setup_mix, 15);
    }
    printf(bad ? "studio: %d FAILED\n" : "studio: all ok\n", bad);
    return bad != 0;
}
