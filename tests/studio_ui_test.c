/* SPDX-License-Identifier: GPL-3.0-only */
/* STUDIO's UI items, on the real UI sources (tests/ui_test.c's harness, without its main: studio_ui_check.sh makes
 * build/host/ui_test_body.h):
 *   1. a held recorded note locks its step: REC, playing, a key held after its note was recorded: KNOB 1 turned is a
 *      lock on that step (not the sound, not automation); let go, KNOB 1 records automation as before.
 *   2. MENU > SYSTEM > 2ND CORE is ON on a new FM-1 (the settings' bit clear) and OFF when set. */
#include "ui_test_body.h"

static int rec_hold_locks(void)
{
    int bad = 0, ok;
    track_t *t;
    uint32_t id, i, step = 0, locks = 0;
    int16_t base, v;
    ui_power_on();
    track_select(1);                                   /* T2: JUNO (polyphonic) */
    t = TSEL;
    id = ENGINES[t->eng_req]->knob[0];                 /* HOME's KNOB 1 (JUNO: CUT) */
    base = t->p[id];
    song.rec = (uint8_t)(1u << song.sel);
    seq_start();
    go_home();
    frames(300);
    key_down(7);                                       /* C4 held */
    frames(48);
    ok = t->rh_n == 1u;
    step = t->rh_start;
    turn(EN_K1, 5);
    turn(EN_K1, 5);
    ok &= motion_lock_get(t, step, id, &v) && v != base;
    for (i = 0; i < NSTEP; i++) {
        int16_t w;
        locks += motion_lock_get(t, i, id, &w);
    }
    ok &= locks == 1u && t->p[id] == base;             /* the step alone; the sound as it was */
    bad += check("STUDIO: REC, a recorded note held: KNOB 1 locks its step only (the sound unchanged)", ok);
    key_up(7);
    frames(48);
    {
        uint32_t m0 = motion_count(t);
        turn(EN_K1, 3);
        ok = t->rh_n == 0u && motion_count(t) > m0;    /* let go: a knob records automation, as before */
        for (i = 0, locks = 0; i < NSTEP; i++) {
            int16_t w;
            locks += motion_lock_get(t, i, id, &w);
        }
        ok &= locks == 1u;
    }
    bad += check("STUDIO:   let go: KNOB 1 records automation as before, no other lock", ok);
    stop_transport();
    song.rec = 0;
    ui_power_on();
    go_home();
    turn(EN_K1, 3);
    {
        int16_t w;
        ok = !motion_lock_get(TSEL, 0, ENGINES[TSEL->eng_req]->knob[0], &w) && !motion_count(TSEL);
    }
    bad += check("STUDIO:   stopped, no REC: KNOB 1 is the sound, as always", ok);
    return bad;
}

static int cpu2_default(void)
{
    int bad = 0;
    ui_power_on();
    ui_prefs2 &= (uint8_t)~(PREF_CPU2_ON >> 16);
    bad += check("STUDIO: 2ND CORE ON on a new FM-1 (bit clear)", str_eq(menu_vname(MI_CPU2, menu_get(MI_CPU2)), "ON"));
    menu_put(MI_CPU2, menu_step(MI_CPU2, -1));           /* (ON is value 0: left is OFF, ui_menu.c) */
    bad += check("STUDIO:   turned: OFF (bit set), RESTART said", (ui_prefs2 & (PREF_CPU2_ON >> 16)) != 0u &&
                 str_eq(menu_vname(MI_CPU2, menu_get(MI_CPU2)), "OFF") && msg_is("2ND CORE OFF: RESTART"));
    return bad;
}

int main(void)
{
    int bad = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    bad += rec_hold_locks();
    bad += cpu2_default();
    printf(bad ? "studio ui: %d FAILED\n" : "studio ui: all passed\n", bad);
    return bad != 0;
}
