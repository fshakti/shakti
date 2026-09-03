/* synth_audio_wasm.c — no ALSA; JS pulls synth_core_render. */
#include "synth_platform.h"

#include <stddef.h>
#include <stdio.h>
#include <string.h>

int synth_platform_init(char *err, size_t cap) {
    (void)err;
    (void)cap;
    if (synth_core_fb_design_init() != 0) {
        if (err && cap) snprintf(err, cap, "synth_open: fb init failed");
        return -1;
    }
    synth_core_set_alive(1);
    return 0;
}

void synth_platform_shutdown(void) {}

int synth_platform_poll(int *cfg_w, int *cfg_h, int *cfg_count) {
    if (cfg_w) *cfg_w = 0;
    if (cfg_h) *cfg_h = 0;
    if (cfg_count) *cfg_count = 0;
    return synth_core_is_alive();
}

void synth_platform_present(void) {}

void synth_platform_request_maximize(void) {}

int synth_audio_start(char *err, size_t cap) {
    (void)err;
    (void)cap;
    synth_core_set_audio_run(1);
    return 0;
}

void synth_audio_stop(void) { synth_core_set_audio_run(0); }
