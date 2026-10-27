#include "ui_test_body.h"   /* ui_test.c without its main (made by tests/studio_ui_check.sh into build/host) */
#include <sys/wait.h>
#include <unistd.h>
typedef int (*tf_t)(void);
static const struct { const char *n; tf_t f; } T[] = {
    {"test_large_face", test_large_face},
    {"test_sound_loads", test_sound_loads},
    {"test_patterns", test_patterns},
    {"test_rec", test_rec},
    {"test_step_rec", test_step_rec},
    {"test_step_preview_chord_entry", test_step_preview_chord_entry},
    {"test_presets_knob", test_presets_knob},
    {"test_midi", test_midi},
    {"test_save", test_save},
    {"test_actions", test_actions},
    {"test_tracks", test_tracks},
    {"test_grid", test_grid},
    {"test_screen", test_screen},
    {"test_favorites", test_favorites},
    {"test_categories", test_categories},
    {"test_display_preferences", test_display_preferences},
    {"test_menu_tabs", test_menu_tabs},
    {"test_menu_project_rows", test_menu_project_rows},
    {"test_information", test_information},
    {"test_chain", test_chain},
    {"test_song_timeline", test_song_timeline},
    {"test_song_lanes", test_song_lanes},
    {"test_song_lanes_drawn", test_song_lanes_drawn},
    {"test_song_pictures", test_song_pictures},
    {"test_head_slot", test_head_slot},
    {"test_product_ux", test_product_ux},
    {"test_page_cycles", test_page_cycles},
    {"test_mono_screens", test_mono_screens},
    {"test_roll", test_roll},
    {"test_soak_cards", test_soak_cards},
    {"test_panel", test_panel},
    {"test_layer", test_layer},
    {"test_layer_knob_leak", test_layer_knob_leak},
    {"test_layer_knob_race", test_layer_knob_race},
    {"test_fx_latch", test_fx_latch},
    {"test_fx_map", test_fx_map},
    {"test_layer_lock", test_layer_lock},
    {"test_rec_layer", test_rec_layer},
    {"test_seq_tools", test_seq_tools},
    {"test_edit_quick_actions", test_edit_quick_actions},
    {"test_menu_prefs", test_menu_prefs},
    {"test_style", test_style},
    {"test_head_centres", test_head_centres},
    {"test_head_metronome", test_head_metronome},
    {"test_head_group", test_head_group},
    {"test_div_order", test_div_order},
    {"test_knob_accel", test_knob_accel},
    {"test_usb_level", test_usb_level},
    {"test_click_menu", test_click_menu},
    {"test_usb_serial", test_usb_serial},
    {"test_bpm_lock", test_bpm_lock},
    {"test_large", test_large},
    {"test_browse_no_pattern", test_browse_no_pattern},
    {"test_name", test_name},
    {"test_edit_cycle", test_edit_cycle},
    {"test_sample_alert", test_sample_alert},
    {"test_quick_layers", test_quick_layers},
    {"test_chord_page", test_chord_page},
    {"test_bughunt_ui", test_bughunt_ui},
    {"test_bughunt_ui2", test_bughunt_ui2},
    {"test_piano_roll", test_piano_roll},
    {"test_play_leds", test_play_leds},
    {"test_cursor_step_leds", test_cursor_step_leds},
    {"test_midi_leds", test_midi_leds},
    {"test_seq_quant", test_seq_quant},
    {"test_idle_glow", test_idle_glow},
    {"test_breath", test_breath},
    {"test_step_leds", test_step_leds},
    {"test_scale_leds", test_scale_leds},
    {"test_screen_off", test_screen_off},
    {"test_screen_wake", test_screen_wake},
    {"test_idle_leds", test_idle_leds},
    {"test_momentary", test_momentary},
    {"test_fm6_charts", test_fm6_charts},
    {"test_fm6_ops", test_fm6_ops},
    {"test_home_tracks", test_home_tracks},
    {"test_fx_map_columns", test_fx_map_columns},
    {"test_help", test_help},
    {"test_no_flash", test_no_flash},
    {"test_fm4_retired", test_fm4_retired},
};
int main(int argc, char **argv)
{
    uint32_t i; int failed = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    if (getenv("UI_SEQ")) {                     /* in one process, in order (the state carried, as ui_test.c) */
        const char *skip = getenv("UI_SKIP");
        for (i = 0; i < sizeof T / sizeof T[0]; i++) {
            int r;
            if (skip && strstr(skip, T[i].n)) { printf("UIFN skip %s\n", T[i].n); continue; }
            r = T[i].f();
            printf("UIFN %s %s\n", r ? "FAIL" : "ok  ", T[i].n);
            failed += r != 0;
        }
        printf("UIFN %d failed\n", failed);
        return failed != 0;
    }
    for (i = 0; i < sizeof T / sizeof T[0]; i++) {
        pid_t p; int st = 0;
        if (argc > 1 && strcmp(argv[1], T[i].n)) continue;
        p = fork();
        if (!p) { int r; if (!getenv("UI_EACH_VERBOSE")) freopen("/dev/null", "w", stdout); r = T[i].f(); _exit(r ? 1 : 0); }
        waitpid(p, &st, 0);
        if (!WIFEXITED(st) || WEXITSTATUS(st)) { failed++; printf("UIFN FAIL %s%s\n", T[i].n, WIFEXITED(st) ? "" : " (crash)"); }
        else printf("UIFN ok   %s\n", T[i].n);
    }
    printf("UIFN %d of %u failed\n", failed, (unsigned)(sizeof T / sizeof T[0]));
    return failed != 0;
}
